#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"


/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void) {
    pagetable_t kpgtbl;

    kpgtbl = (pagetable_t) kalloc();
    memset(kpgtbl, 0, PGSIZE);

    // uart registers
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PLIC
    kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // map kernel text executable and read-only.
    kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64) etext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    kvmmap(kpgtbl, (uint64) etext, (uint64) etext, PHYSTOP - (uint64) etext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpgtbl, TRAMPOLINE, (uint64) trampoline, PGSIZE, PTE_R | PTE_X);

    // map kernel stacks
    proc_mapstacks(kpgtbl);

    return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void) {
    kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart() {
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t) PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (pde_t *) kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va) {
    pte_t *pte;
    uint64 pa;
    if (va >= MAXVA)
        return 0;
    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_PG) != 0)
        printf("walkaddr(): pte PTE_PG is on\n");
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    if (pa == 0)
        printf(" walkaddr(): pa == 0\n");
    return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    if (mappages(kpgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
    uint64 a, last;
    pte_t *pte;
    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
            panic("remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

int
mappages2(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
    uint64 a, last;
    pte_t *pte;
    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            return -1;
//        if (*pte & PTE_V)
//            panic("remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    uint64 a;
    pte_t *pte;
    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");
    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        // check PTE_PG to see if the pte is in file
        if ((*pte & PTE_V) == 0 && (*pte & PTE_PG) == 0)
            panic("uvmunmap: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        // free only if page is in memory
        if (do_free && (*pte & PTE_V) == 0) {
            uint64 pa = PTE2PA(*pte);
            if (pa != 0)
                kfree((void *) pa);
        }
        if (!is_none_policy() && (*pte & PTE_V) != 0) {
            // page is in memory
            remove_from_memory_meta_data(a, pagetable);
        }
//        else if (!is_none_policy() && (*pte & PTE_PG) != 0) {
//            // page is in file
//            remove_from_file_meta_data(a, pagetable);
//        }
        *pte = 0;
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate() {
    pagetable_t pagetable;
    pagetable = (pagetable_t) kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz) {
    char *mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64) mem, PTE_W | PTE_R | PTE_X | PTE_U);
    memmove(mem, src, sz);
}

int is_none_policy() {
#ifdef NONE
    return 1;
#endif
    return 0;
}

void swap(pagetable_t pagetable, uint64 user_page_va) {
    struct proc *p = myproc();
    // move selected page from memory to swapFile
    int out_index = get_swap_out_page_index();
    pte_t *pte = walk(p->pagetable, p->memory_pages[out_index].user_page_VA, 0);
    uint64 out_page_pa = PTE2PA(*pte);
    write_page_to_file(p, p->memory_pages[out_index].user_page_VA, p->pagetable);
    // clear the page from memory
    kfree((void *) out_page_pa); //free swapped page
    p->memory_pages[out_index].state = P_UNUSED;
    update_page_out_pte(p->pagetable, p->memory_pages[out_index].user_page_VA);
    // move the requested page to memory
    add_to_memory_page_metadata(pagetable, user_page_va);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    struct proc *p = myproc();
    char *mem;
    uint64 a;
    if (newsz < oldsz)
        return oldsz;
    oldsz = PGROUNDUP(oldsz);
    int num_of_new_page = 0;
    int old_num_of_pages_in_mem = p->pages_in_memory_counter;
    for (a = oldsz; a < newsz; a += PGSIZE) {
        num_of_new_page++;
        mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64) mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        if (p->pid > 2 && !is_none_policy()) {
            // more then 32 pages -> kill proc
            if (p->pages_in_memory_counter + p->pages_in_file_counter == MAX_TOTAL_PAGES) {
                printf("PID: %d inisde uvmalloc(): try to kalloc more then 32 pages\n",p->pid);
                kfree(mem);
                uvmdealloc(pagetable, a, oldsz);
                panic("uvmalloc(): Proc is too big\n");
            }
            else if (old_num_of_pages_in_mem + num_of_new_page > MAX_PYSC_PAGES) {
                // no more space in memory need to swap
                swap(pagetable, a);
            }
                // have space in memory
            else {
                add_to_memory_page_metadata(pagetable, a);
            }
        }
    }
    return newsz;
}


// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t) child);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    kfree((void *) pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz) {
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;
    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");

        // if the pte is in file just update the pte without kalloc
        if (*pte & PTE_PG) {
            update_page_out_pte(new, i);
            continue;
        }

        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");

        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if ((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char *) pa, PGSIZE);
        if (mappages(new, i, PGSIZE, (uint64) mem, flags) != 0) {
            kfree(mem);
            goto err;
        }
    }
    return 0;

    err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void *) (pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void *) (pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *) (pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}


int get_free_memory_page_index() {
    struct proc *p = myproc();
    if (p == 0)
        return -1;
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_UNUSED)
            return i;
    }
    return -1; // memory is full
}


void update_page_out_pte(pagetable_t pagetable, uint64 user_page_va) {
    uint64 * pte = walk(pagetable, user_page_va, 0);
    if (!pte)
        panic("PTE of swapped out page is missing\n");
    *pte &= PTE_FLAGS(*pte); // clear junk physical address
    *pte |= PTE_PG; // turn on Paged out to storage bit
    *pte &= ~PTE_V; // turn off valid bit
    sfence_vma(); //flush the TLB
}

void update_page_in_pte(pagetable_t pagetable, uint64 user_page_va, uint64 page_pa, int index) {
    uint64 * pte = walk(pagetable, user_page_va, 0);
    if (!pte)
        panic("PTE of swapped in page is missing\n");
    if (*pte & PTE_V)
        panic("in update_paged_in_flags page is Valid!\n");
    *pte |= PA2PTE(page_pa); // Map PTE to the new_page
    *pte |= PTE_W | PTE_X | PTE_R | PTE_U | PTE_V; // Turn on needed flags
    *pte &= ~PTE_PG; // page is back in memory turn off Paged out bit
#ifdef NFUA
    struct proc *p = myproc();
    p->memory_pages[index].access_count = 0;
#endif
#ifdef LAPA
    struct proc *p = myproc();
    p->memory_pages[index].access_count = 0xFFFFFFFF;
#endif
    sfence_vma(); // flush the TLB

}

void add_to_memory_page_metadata(pagetable_t pagetable, uint64 user_page_va) {
    struct proc *p = myproc();
    int free_index = get_free_memory_page_index();
    p->memory_pages[free_index].state = P_USED;
    p->memory_pages[free_index].user_page_VA = user_page_va;
    p->memory_pages[free_index].page_order = p->page_order_counter++;
    p->pages_in_memory_counter++;
#ifdef NFUA
    p->memory_pages[free_index].access_count = 0;
#endif
#ifdef LAPA
    p->memory_pages[free_index].access_count = 0xFFFFFFFF; // -1
#endif
}

int get_page_from_file(uint64 r_stval) {
    struct proc *p = myproc();
    p->page_fault_counter++;
    uint64 user_page_va = PGROUNDDOWN(r_stval);
    char *new_page = kalloc();
    if (!new_page)
        return 0;
    // clean the new page
    memset(new_page, 0, PGSIZE);
    int free_index = get_free_memory_page_index();
    // have free space in the memory
    if (free_index >= 0) {
        update_page_in_pte(p->pagetable, user_page_va, (uint64) new_page, free_index);
        read_page_from_file(p, free_index, user_page_va, new_page);
        return 1;
    }
    // else memory is full & swapping is needed
    else {
        int out_index = get_swap_out_page_index(); // select page to swap to file
        struct page_metadata_struct out_page = p->memory_pages[out_index];
        // insert new page into memory
        update_page_in_pte(p->pagetable, user_page_va, (uint64) new_page, out_index);
        read_page_from_file(p, out_index, user_page_va, new_page);
        // write page to file
        write_page_to_file(p, out_page.user_page_VA, p->pagetable);
        update_page_out_pte(p->pagetable, out_page.user_page_VA);
        // free physical memory
        pte_t *pte = walk(p->pagetable, out_page.user_page_VA, 0);
        uint64 out_page_pa = PTE2PA(*pte);
        if (out_page_pa != 0)
            kfree((void *) out_page_pa); // free swapped page
        return 1;
    }
}

int page_in_file(uint64 user_page_va, pagetable_t pagetable) {

    pte_t *pte = walk(pagetable, user_page_va, 0);
    int found = (*pte & PTE_PG); // if return 1 page is in file
    return found;
}

// This must use user_page_va + pagetable addresses!
// The proc has identical user_page_va on different page directories until exec finish executing
void remove_from_memory_meta_data(uint64 user_page_va, pagetable_t pagetable) {
    struct proc *p = myproc();
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_USED && p->memory_pages[i].user_page_VA == user_page_va &&
            p->pagetable == pagetable) {
            p->memory_pages[i].access_count = 0;
            p->memory_pages[i].page_order = 0;
            p->pages_in_memory_counter--;
            p->memory_pages[i].state = P_UNUSED;
            return;
        }
    }
}

void remove_from_file_meta_data(uint64 user_page_va, pagetable_t pagetable) {
    struct proc *p = myproc();
    for (int i = 0; i < MAX_TOTAL_PAGES - MAX_PYSC_PAGES; i++) {
        if (p->file_pages[i].state == P_USED
            && p->file_pages[i].user_page_VA == user_page_va && p->pagetable == pagetable) {
            p->file_pages[i].access_count = 0;
            p->file_pages[i].page_order = 0;
            p->pages_in_file_counter--;
            p->file_pages[i].state = P_UNUSED;
            return;
        }
    }
}


//#if defined(NFUA) || defined(LAPA)
// Updates the access counter in NFUA and LAPA paging policies
void update_access_counter(struct proc *p) {
    uint addr = 0x80000000; // 10000000000000000000000000000000 in binary
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_USED) {
            p->memory_pages[i].access_count >>= 1; // Shift-right
            pte_t *pte = walk(p->pagetable, p->memory_pages[i].user_page_VA, 0);
            if (*pte & PTE_A) {
                p->memory_pages[i].access_count |= addr; // add 1 to the most significant bit
                *pte &= ~PTE_A; // turn off PTE_A flag
            }
        }
    }
}
//#endif

// Counts the number of turned on bits
uint num_of_ones(uint access_count) {
    int num_of_ones = 0;
    while (access_count) {
        if (access_count % 2 != 0)
            num_of_ones++;
        access_count /= 2;
    }
    return num_of_ones;
}

// Second Chance FIFO - Page Replacement Algorithm
int SCFIFO_algorithm() {
    struct proc *p = myproc();
    int page_index;
    uint64 page_order;
    recheck:
    page_index = -1;
    page_order = 0xffffffff;
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_USED && p->memory_pages[i].page_order <= page_order) {
            page_index = i;
            page_order = p->memory_pages[i].page_order;
        }
    }
    pte_t *pte = walk(p->pagetable, p->memory_pages[page_index].user_page_VA, 0);
    if (*pte & PTE_A) {
        *pte &= ~PTE_A; // turn off PTE_A flag
        p->memory_pages[page_index].page_order = p->page_order_counter++; // put this page to the end of the queue
        goto recheck;
    }
    return page_index;
}

// Not Frequently Used With Aging Page Replacement Algorithm
int NFUA_algorithm() {
    struct proc *p = myproc();
    int page_index = -1;
    uint best = 0xFFFFFFFF;
    uint curr = 0xFFFFFFFF;
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_USED) {
            curr = p->memory_pages[i].access_count;
            if (curr < best) {
                best = curr;
                page_index = i;
            }
        }
    }
    return page_index;
}

// Least Accessed Page With Aging Page Replacement Algorithm
int LAPA_algorithm() {
    struct proc *p = myproc();
    int page_index = -1;
    uint best = 0xFFFFFFFF;
    uint curr = 0xFFFFFFFF;
    for (int i = 0; i < MAX_PYSC_PAGES; i++) {
        if (p->memory_pages[i].state == P_USED) {
            curr = num_of_ones(p->memory_pages[i].access_count);
            if (curr < best ||
                (curr == best && p->memory_pages[i].access_count < p->memory_pages[page_index].access_count)) {
                best = curr;
                page_index = i;
            }
        }
    }
    return page_index;
}

// for debug always try to swap the first page
int first_only_algorithm() {
    return 0;
}

int get_swap_out_page_index() {
    // update the access counter before using swap algorithm in order to update AGING data
#if defined(NFUA) || defined(LAPA)
    update_access_counter(myproc());
#endif
#ifdef SCFIFO
    return SCFIFO_algorithm();
#endif
#ifdef LAPA
    return LAPA_algorithm();
#endif
#ifdef NFUA
    return NFUA_algorithm();
#endif
#ifdef DEBUG
    return first_only_algorithm();
#endif
    panic("Unrecognized paging machanism");
}

void print_memory_metadata_state(struct proc *p) {
    if (p->pid > 2) {
        printf("PID: %d num of pages in MEM: %d\n", p->pid, p->pages_in_memory_counter);
        printf("PID: %d num of pages in FILE: %d\n", p->pid, p->pages_in_file_counter);
        printf("########### memory PAGES ###########\n");
        for (int i = 0; i < 16; i++) {
            if (p->memory_pages[i].state == P_USED) {
                printf("memory page num: %d, state is P_USED\n", i);
                printf("user_page_VA: %p, access_count: %u page_order: %d \n",
                       p->memory_pages[i].user_page_VA, p->memory_pages[i].access_count, p->memory_pages[i].page_order);

            }
//            else if (p->memory_pages[i].state == P_UNUSED){
//                printf("memory page num: %d, state is P_UNUSED\n",i);
//                printf("user_page_VA: %p, access_count: %u page_order: %d \n",
//                       p->memory_pages[i].user_page_VA,p->memory_pages[i].access_count,p->memory_pages[i].page_order);
//            }
        }
        printf("########### file PAGES ###########\n");
        for (int i = 0; i < 16; i++) {
            if (p->file_pages[i].state == P_USED) {
                printf("FILE page num: %d, state is P_USED\n", i);
                printf("user_page_VA: %p, access_count: %u, page_order: %d \n",
                       p->file_pages[i].user_page_VA, p->file_pages[i].access_count, p->file_pages[i].page_order);
            }
//            else if (p->file_pages[i].state == P_UNUSED){
//                printf("FILE page num: %d, state is P_UNUSED\n",i);
//                printf("user_page_VA: %p, access_count: %u, page_order: %d \n",
//                       p->file_pages[i].user_page_VA,p->file_pages[i].access_count,p->file_pages[i].page_order);
//            }
        }
    }
}






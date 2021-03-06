#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define PGSIZE 4096

void fork_test() {

    printf("--------- fork_test starting ---------\n");
    if (fork() == 0) {
        for (int i = 0; i < 25; i++) {
            printf("doing sbrk number %d\n", i);
            sbrk(PGSIZE);
        }
        exit(0);
    }
    wait(0);
    printf("--------- fork_test finished ---------\n");
}

void alloc_dealloc_test() {
    printf("--------- alloc_dealloc_test starting ---------\n");
    char *alloc = malloc(25 * PGSIZE);
    for (int i = 0; i < 25; i++) {
        alloc[i * PGSIZE] = 'a' + i;
        printf("added i : %d\n", i);
    }
    for (int i = 0; i < 25; i++)
        printf("alloc[%d] = %c\n", i * PGSIZE, alloc[i * PGSIZE]);
    free(alloc);
    printf("--------- alloc_dealloc_test finished ---------\n");
}


void exec_test() {
    printf("--------- exec_test starting ---------\n");
    if (fork() == 0) {
        printf("allocating pages\n");
        int *array = (int *) (malloc(sizeof(int) * 6 * PGSIZE));
        for (int i = 0; i < 6 * PGSIZE; i = i + PGSIZE) {
            array[i] = i / PGSIZE;
        }
        printf("forking\n");
        int pid = fork();
        if (pid == 0) {
            char *argv[] = {"sanity", "exec_child_test", 0};
            // child wil also run sanity test form main with only exec_child_test
            exec(argv[0], argv);
        } else { // father
            wait(0);
        }
        exit(0);
    } else {
        wait(0);
    }
    printf("--------- exec_test finished ---------\n");
}

void exec_child_test() {
    printf("child allocating pages\n");
    int *array = (int *) (malloc(sizeof(int) * 6 * PGSIZE));
    for (int i = 0; i < 6 * PGSIZE; i = i + PGSIZE) {
        array[i] = i / PGSIZE;
    }
    printf("Num of page faults: %d \n", page_fault_num());
    printf("child exiting\n");
}



// ++++With malloc:++++
// with only 2 loop:
// SCFIFO: 18
// LAPA: 10
// NFUA: 9

// with only 1 loop:
// SCFIFO: 18
// LAPA: 8
// NFUA: 7

// ++++With sbrk of 14 pages total is: 17:++++
// with only 1 loop:
// SCFIFO: 12
// LAPA: 1
// NFUA: 1

// ++++With sbrk of 16 pages total is: 20:++++
// with only 1 loop:
// SCFIFO: 16
// LAPA: 5
// NFUA: 4 - the minimum possible
void page_faults_test() {
    printf("--------- page_faults_test starting ---------\n");
//    char * arr = malloc(PGSIZE*16); // allocates 13 pages
    char * arr = sbrk(PGSIZE*16); // allocates 14 pages - total 17
    // after exec we already have: text, data, guard page, stack = 4, we malloc 13, total is: 17 pages - to allow swapping)
    // note that malloc will save 16 pages  so total is 20 but we will only access to 13 of them.
    for (int i = 0; i < PGSIZE*16; i++) {
        arr[i] = '1';        // write to memory
    }
    // write to memory for the second time to cause more page faults so we can see differences between NFUA & LAPA
//    for (int i = 0; i < PGSIZE*13; i++) {
//        arr[i] = '2';        // write to memory
//    }
//    free(arr);
    printf("Num of page faults: %d \n", page_fault_num());
    printf("--------- page_faults_test finished ---------\n");
}

void exec_page_faults_test() {
    int pid = fork();
    if (pid == 0) {
        char *argv[] = {"sanity", "page_faults_test", 0};
        // child will also run sanity test form main with only page_faults_test
        exec(argv[0], argv);
    } else { // father
        wait(0);
    }
}


int main(int argc, char *argv[]) {
    if (argc >= 1 && strcmp(argv[1], "exec_child_test") == 0) {
        exec_child_test();
        exit(0);
    }
    if (argc >= 1 && strcmp(argv[1], "page_faults_test") == 0) {
        page_faults_test();
        exit(0);
    }
    exec_test();
    fork_test();
    alloc_dealloc_test();
    exec_page_faults_test();  // should be run with exec on a "clean" process
    exit(0);
}

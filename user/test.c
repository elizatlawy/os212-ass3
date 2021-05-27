#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define ARR_SIZE 55000 // 14 pages
#define PGSIZE 4096

void fork_test(){

    printf("--------- fork_test starting ---------\n");
    if (fork() == 0){
        for (int i = 0; i < 25; i++){
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
    for (int i = 0; i < 25; i++)
    {
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
        int *arr = (int *) (malloc(sizeof(int) * 5 * PGSIZE));
        for (int i = 0; i < 5 * PGSIZE; i = i + PGSIZE) {
            arr[i] = i / PGSIZE;
        }
        printf("forking\n");
        int pid = fork();
        if (pid == 0) {
            char *argv[] = {"test", "exectest", 0};
            exec(argv[0], argv);
        } else {
            wait(0);
        }
        exit(0);
    } else {
        wait(0);
    }
    printf("--------- exec_test finished ---------\n");
}

void exec_test_child() {
    printf("child allocating pages\n");
    int *arr = (int *) (malloc(sizeof(int) * 5 * PGSIZE));
    for (int i = 0; i < 5 * PGSIZE; i = i + PGSIZE) {
        arr[i] = i / PGSIZE;
    }
    printf("child exiting\n");
}


// SCFIFO: 21
// LAPA: 11
// NFUA: 5


void page_faults_test() {
    printf("--------- page_faults_test starting ---------\n");
    char *arr;
    int i;
    arr = malloc(ARR_SIZE); // allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
    for (i = 0; i <= 100; i++) {
        arr[1] = 'X';        // write to memory
        arr[ARR_SIZE - 1] = 'Y';
    }

    for (i = 0; i < ARR_SIZE-1; i++) {
        arr[i] = 'Y';        // write to memory
    }
        free(arr);
        printf("Num of page faults: %d \n", page_fault_num());
        printf("--------- page_faults_test finished ---------\n");
}

int main(int argc, char *argv[]) {
    if (argc >= 1) {
        if (strcmp(argv[1], "exectest") == 0) {
            exec_test_child();
            exit(0);
        }
    }
    exec_test();
    fork_test();
    alloc_dealloc_test();
//    page_faults_test();

    exit(0);
}

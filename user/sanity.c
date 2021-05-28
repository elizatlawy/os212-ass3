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
        int *array = (int *) (malloc(sizeof(int) * 7 * PGSIZE));
        for (int i = 0; i < 7 * PGSIZE; i = i + PGSIZE) {
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
    int *array = (int *) (malloc(sizeof(int) * 7 * PGSIZE));
    for (int i = 0; i < 7 * PGSIZE; i = i + PGSIZE) {
        array[i] = i / PGSIZE;
    }
    printf("Num of page faults: %d \n", page_fault_num());
    printf("child exiting\n");
}



// when updating only in scheduler:
// SCFIFO: 17
// LAPA: 11
// NFUA: 10

// when updating only in scheduler & before calling page_out algo:
// SCFIFO: 17
// LAPA: 8
// NFUA: 6

// with only 1 loop:
// SCFIFO: 18
// LAPA: 7
// NFUA: 7
void page_faults_test() {
    printf("--------- page_faults_test starting ---------\n");
    char *arr;
    int i;
    arr = malloc(ARR_SIZE); // allocates 14 pages (sums to 17 pages - to allow  swapping)
    for (i = 0; i < ARR_SIZE; i++) {
        arr[i] = 'X';        // write to memory
    }
    // write to memory for the second time to cause more page faults
    for (i = 0; i < ARR_SIZE; i++) {
        arr[i] = 'Y';        // write to memory
    }
    free(arr);
    printf("Num of page faults: %d \n", page_fault_num());
    printf("--------- page_faults_test finished ---------\n");
}

void exec_page_faults_test(){
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
    // the page_faults_test() should be run with exec on a "clean" process
    exec_page_faults_test();
    exit(0);
}

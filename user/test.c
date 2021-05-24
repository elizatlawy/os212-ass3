#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/param.h"

void page_fault_test();

void fork_test(){
    page_fault_test();
    int child_pid = fork();
    if (child_pid < 0) {
        printf("fork failed\n");
    }
    else if (child_pid > 0) { // father
        printf("new child PID is: %d\n", child_pid);
        int status;
        wait(&status);
        printf("Child PID: %d exit with status: %d\n",child_pid, status);
    } else { // child
        printf("new child created\n");
//        page_fault_test();
    }
}
//#define ARR_SIZE 85000
// num of page fault with ARR_SIZE 85000 is: 41
#define ARR_SIZE 70000 // 18 pages
// num of page fault with ARR_SIZE 75000 is: 37


void page_fault_test(){
    char * arr;
    int i;
    arr = malloc(ARR_SIZE); //allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
    for(i = 0; i <= 100; i++){
        arr[1] = 'X';		// write to memory
        arr[ARR_SIZE-1] = 'Y';
    }
    printf("after first loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
//    for(i = 0; i < ARR_SIZE; i++){
//        arr[i] = 'Y';		// write to memory
//
//    }
//    printf("after second loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
    free(arr);
    printf("Num of page faults: %d \n",page_fault_num());
}

#define PGSIZE 4096
#define pages_num 17

void simple_test(){
    char * memory_arr[pages_num];
    for(int i = 0; i < 17; i++){
        memory_arr[i] = malloc(PGSIZE);
    }
    for (int i = 100; i < 120; i++)
    {
        *memory_arr[(i % 3)] = i;
        printf("*memory_arr[%d] was accessed in tick %d\n", i % 3, uptime());
    }
    free(memory_arr);
    printf("Num of page faults: %d \n",page_fault_num());
}


int main(int argc, char *argv[]) {
//    fork_test();
//    page_fault_test();
    simple_test();
    exit(0);
}
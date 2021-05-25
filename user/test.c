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
//#define ARR_SIZE 70000 // 18 pages
// num of page fault with ARR_SIZE 75000 is: 37
//#define ARR_SIZE 70000 // 18 pages
#define ARR_SIZE 57344 // 14 pages

// SCFIFO:
//
void page_fault_test(){
    char * arr;
    int i;
    arr = malloc(ARR_SIZE); // allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
//    for(i = 0; i <= 100; i++){
//        arr[1] = 'X';		// write to memory
//        arr[ARR_SIZE-1] = 'Y';
//    }
    for(i = 0; i < ARR_SIZE; i++){
        arr[i] = 'W';		// write to memory
    }

//    printf("after first loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
//    for(i = 0; i < ARR_SIZE; i++){
//        arr[i] = 'Y';		// write to memory
//    }
    printf("after second loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
    free(arr);
    printf("Num of page faults: %d \n",page_fault_num());
}

#define PGSIZE 4096
#define pages_num 17

void func(){
    int a = 10;
    printf("Hello Word %d\n",a);
    exit(0);
}

void scfifo_test() {
    fprintf(2, "SCFIFO test\n");
    int i;
    char *pages[31];
    char input[10];

    for (i = 0; i < 12; ++i)
        pages[i] = sbrk(PGSIZE);
    fprintf(2, "\nThe physical memory should be full - use Ctrl + P followned by Enter\n");
    gets(input, 10);

    fprintf(2, "\ncreating 3 pages... \n");
    pages[13] = sbrk(PGSIZE);
    pages[14] = sbrk(PGSIZE);
    pages[15] = sbrk(PGSIZE);

    fprintf(2, "\n\n3 pages are taken out - use Ctrl + P followned by Enter\n\n");
    gets(input, 10);

    fprintf(2, "\n\nWe accessed 2 pages and then accessed 2 pages we moved to Swapfile. should be 2 PGFLT - use Ctrl + P followned by Enter");
    int j;
    for (j = 3; j < 6; j = j + 2){
        pages[j][1]='T';
    }

    pages[0][1] = 'T';
    pages[1][1] = 'E';
    fprintf(2, "\n\nwe wrote th the first 2 pages - use Ctrl + P followned by Enter\n\n");
    gets(input, 10);

    fprintf(2,"\n\nTesting the fork :\n\n");

    if (fork() == 0) {
        fprintf(2, "\nthis is the code of the child%d\n",getpid());
        fprintf(2, "Child pages are identical to parent - use Ctrl + P followned by Enter\n");
        gets(input, 10);

        pages[10][0] = 'F';
        fprintf(2, "\n\n a page fault should occur here - use Ctrl + P followned by Enter\n\n");
        gets(input, 10);
        char *command = "/ls";
        char *args[4];
        args[0] = "/ls";
        args[1] = 0;
        args[2] = 0;
        if (exec(command,args) < 0) {
            fprintf(2, "exec failed\n");
        }
        exit(0);
    }
    else {
        //father waits until child finishes.
        wait(0);
    }
}


int main(int argc, char *argv[]) {
//    fork_test();
    page_fault_test();
//    scfifo_test();
    exit(0);
}
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define BUFSZ  ((MAXOPBLOCKS+2)*BSIZE)

char buf[BUFSZ];


#define PGSIZE 4096
#define ARR_SIZE 55000
//#define ARR_SIZE 74096 // arr with size of 19 pages
//#define ARR_SIZE 59096 // arr with 15 pages
/*
	Test used to check the swapping machanism in fork.
	Best tested when LIFO is used (for more swaps)
*/
void forkTest(char *s) {
    int i;
    char *arr;
//    arr = malloc (50000); //allocates 13 pages (sums to 16), in lifo, OS puts page #15 in file.
    arr = malloc(60000); //allocates 14 pages (sums to 17), in lifo, OS puts page #16 in file.


    for (i = 0; i < 50; i++) {
        arr[59100 + i] = 'A'; //last six A's stored in page #17, the rest in #16
        arr[55200 + i] = 'B'; //all B's are stored in page #16.
    }
    arr[59100 + i] = 0; //for null terminating string...
    arr[55200 + i] = 0;

    if (fork() == 0) { //is son
        for (i = 40; i < 50; i++) {
            arr[59100 + i] = 'C'; //changes last ten A's to C
            arr[55200 + i] = 'D'; //changes last ten B's to D
        }
        printf("SON: %s\n", &arr[59100]); // should print AAAAA..CCC...
        printf("SON: %s\n", &arr[55200]); // should print BBBBB..DDD...
        printf("\n");
        free(arr);
        exit(0);
    } else { //is parent
        wait(0);
        printf("PARENT: %s\n", &arr[59100]); // should print AAAAA...
        printf("PARENT: %s\n", &arr[55200]); // should print BBBBB...
        free(arr);
    }
}


static unsigned long int next = 1;

int getRandNum() {
    next = next * 1103515245 + 12341;
    return (unsigned int) (next / 65536) % ARR_SIZE;
}

#define PAGE_NUM(addr) ((uint)(addr) & ~0xFFF)
#define TEST_POOL 500

/*
Global Test:
Allocates 17 pages (1 code, 1 space, 1 stack, 14 malloc)
Using pseudoRNG to access a single cell in the array and put a number in it.
Idea behind the algorithm:
	Space page will be swapped out sooner or later with scfifo or lap.
	Since no one calls the space page, an extra page is needed to play with swapping (hence the #17).
	We selected a single page and reduced its page calls to see if scfifo and lap will become more efficient.

Results (for TEST_POOL = 500):
LIFO: 42 Page faults
LAP: 18 Page faults
SCFIFO: 35 Page faults

 // our results: with ARR_SIZE 55000 - arr with 14 pages
 SCFIFO: 20
 NFUA: 8
 LAPA: 11

*/
void globalTest(char *s) {
    char *arr;
    int i;
    int randNum;
    arr = malloc(ARR_SIZE); //allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
    for (i = 0; i < TEST_POOL; i++) {
        randNum = getRandNum();    //generates a pseudo random number between 0 and ARR_SIZE
        while (PGSIZE * 10 - 8 < randNum && randNum < PGSIZE * 10 + PGSIZE / 2 - 8)
            randNum = getRandNum(); // gives page #13 50% less chance of being selected
        //(redraw number if randNum is in the first half of page #13)
        arr[randNum] = 'X';                // write to memory
    }
    free(arr);
    printf("Num of page faults: %d \n", page_fault_num());
}

//
//
// use sbrk() to count how many free physical memory pages there are.
// touches the pages to force allocation.
// because out of memory with lazy allocation results in the process
// taking a fault and being killed, fork and report back.
//
int
countfree()
{
    int fds[2];

    if(pipe(fds) < 0){
        printf("pipe() failed in countfree()\n");
        exit(1);
    }

    int pid = fork();

    if(pid < 0){
        printf("fork failed in countfree()\n");
        exit(1);
    }

    if(pid == 0){
        close(fds[0]);

        while(1){
            uint64 a = (uint64) sbrk(4096);
            if(a == 0xffffffffffffffff){
                break;
            }

            // modify the memory to make sure it's really allocated.
            *(char *)(a + 4096 - 1) = 1;

            // report back one more page.
            if(write(fds[1], "x", 1) != 1){
                printf("write() failed in countfree()\n");
                exit(1);
            }
        }

        exit(0);
    }

    close(fds[1]);

    int n = 0;
    while(1){
        char c;
        int cc = read(fds[0], &c, 1);
        if(cc < 0){
            printf("read() failed in countfree()\n");
            exit(1);
        }
        if(cc == 0)
            break;
        n += 1;
    }

    close(fds[0]);
    wait((int*)0);

    return n;
}

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s) {
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if((pid = fork()) < 0) {
        printf("runtest: fork error\n");
        exit(1);
    }
    if(pid == 0) {
        f(s);
        exit(0);
    } else {
        wait(&xstatus);
        if(xstatus != 0)
            printf("FAILED\n");
        else
            printf("OK\n");
        return xstatus == 0;
    }
}

int
main(int argc, char *argv[])
{
    int continuous = 0;
    char *justone = 0;

    if(argc == 2 && strcmp(argv[1], "-c") == 0){
        continuous = 1;
    } else if(argc == 2 && strcmp(argv[1], "-C") == 0){
        continuous = 2;
    } else if(argc == 2 && argv[1][0] != '-'){
        justone = argv[1];
    } else if(argc > 1){
        printf("Usage: usertests [-c] [testname]\n");
        exit(1);
    }

    struct test {
        void (*f)(char *);
        char *s;
    } tests[] = {
    {forkTest, "forkTest\n"},
    {globalTest, "globalTest\n"},
            { 0, 0},
    };

    if(continuous){
        printf("continuous usertests starting\n");
        while(1){
            int fail = 0;
            int free0 = countfree();
            for (struct test *t = tests; t->s != 0; t++) {
                if(!run(t->f, t->s)){
                    fail = 1;
                    break;
                }
            }
            if(fail){
                printf("SOME TESTS FAILED\n");
                if(continuous != 2)
                    exit(1);
            }
            int free1 = countfree();
            if(free1 < free0){
                printf("FAILED -- lost %d free pages\n", free0 - free1);
                if(continuous != 2)
                    exit(1);
            }
        }
    }

    printf("usertests starting\n");
    int free0 = countfree();
    int free1 = 0;
    int fail = 0;
    for (struct test *t = tests; t->s != 0; t++) {
        if((justone == 0) || strcmp(t->s, justone) == 0) {
            if(!run(t->f, t->s))
                fail = 1;
        }
    }

    if(fail){
        printf("SOME TESTS FAILED\n");
        exit(1);
    } else if((free1 = countfree()) < free0){
        printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0);
        exit(1);
    } else {
        printf("ALL TESTS PASSED\n");
        exit(0);
    }
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// TESTS FILE
#define PGSIZE		4096

#define PAGE_SIZE 4096
#define H_SIZE 8
#define NUM_PAGES 13
//int success=0, fail=0,ans=0;

int testnum = 20;
int success=0, fail=0,ans=0, fibNum=10,mid=-1;
void * pages[NUM_PAGES] ; int pnum=0;
int loopnum=1;

int num_threads=1;

int fib(uint64 num){
    if (num == 0)
        return 0;
    if(num == 1)
        return 1;
    return fib(num-1) + fib(num-2);
}

//sanity to check that replaced malloc works properly
void test_malloc(){
    void * addr1 = malloc(500);
    if(addr1 == 0) //malloc failed
        return;
    free(addr1);

    void * addr2 = malloc(500);
    if(addr2 == 0 )
        return;
    if((uint64)addr1 == (uint64) addr2){
        ans++;
    }
    else printf("adrr1:%d, addr2%d\n",(uint64)addr1, (uint64)addr2);


    free(addr2);
    addr1 = malloc(300);
    if(addr1 == 0) //malloc failed
        return;

    free(addr1);
    addr2 = malloc(300);
    if(addr2 == 0 )
        return;
    if((uint64)addr1 == (uint64) addr2){
        ans++;
    }
    else printf("adrr1:%d, addr2%d\n",(uint64)addr1, (uint64)addr2);


}





void test_z(){
    uint64 sz = (uint64) sbrk(0);
    printf("proc size %d\n", (sz)/PAGE_SIZE+ (sz%PAGE_SIZE !=0));
    sbrk(18*4096-sz);
    sz = (uint64) sbrk(0);
    printf("proc size %d\n", (sz)/PAGE_SIZE+ (sz%PAGE_SIZE !=0));
}


int randomGenrator(int seed, int limit){
    return (seed ^ (seed * 7)) % limit;
}


void test2(void){
    char * bla = sbrk(20 * PGSIZE);

    for(int i = 0; i < 10; ++i){
        bla[i] = i;
        bla[20 - i] = i;
    }

    //reading
    for(int i = 0; i < 10; ++i){
        if(bla[i] != i || bla[20-i] != i){
            printf("failed\n");
            ans = -1;
        }
    }
}

void test1(int flag){
    // request 20 pages.
    char * bla = sbrk(20*PGSIZE);

    // write i to page i
    for(int i = 0; i < 20; i++)
        bla[i*PGSIZE] = i;

    //read
    for(int i = 0; i < 20 ; i++){
        if(bla[i*PGSIZE] != i){
            printf("Simple Test failed");
            ans = -1;
        }
    }
    if (flag) exit(0);
}

void mySimpleTets(void){
    int pid;

    if((pid = fork()) == -1) {
        printf("fork failed exiting...\n");
        ans = -1;
        goto bad;
    }

    if(pid == 0)
        test1(1);
    else{
        wait(0);
        printf("done\n");
    }
    return;

    bad:
    exit(0);
}

void doubleProcess(){
    int pid = fork();
    if(!pid){
        test2();
        exit(0);
    }
    int pid2 = fork();
    if(!pid2){
        test2();
        exit(0);
    }
    wait(0);
    wait(0);
    printf("done\n");
}

//checking segemntion fault
void segTest(){
    int pid = fork();
    int* seg = 0;

    if(!pid){
        *seg = 1;
        printf("failed\n");
        ans = -1;
    }
    wait(0);
    printf("done\n");
}

void test3(void){
    sbrk(10*PGSIZE);
    exit(0);
}

void forktests(void){
    ans = -1;
    for (int i = 0; i < 5 ; i++){
        int pid = fork();
        if(pid)
            continue;
        test3();
    }
    for(int i = 0 ; i < 5; i++){
        wait(0);
    }
    ans = 1;
    printf("done\n");
}

void multiplewritesOneProcess(void){
    int pid = fork();
    if(pid)
        goto waitf;

    char buf[100];
    char* brk = sbrk(20*PGSIZE);
    printf("choose seed for random genrator: ");
    gets(buf, 100);
    int random = randomGenrator(atoi(buf),20);

    for(int i = 0; i < PGSIZE; i++)
    {
        brk[random*i + i] = i;
        random = randomGenrator(random, 20);
    }

    printf("done\n");
    exit(0);

    waitf:
    wait(0);
}

void overLoadPage(void){

    int pid = fork();
    if(!pid)
        sbrk(33*PGSIZE);
    wait(0);
    printf("done\n");
}

void memtest(char* brk){
    for(int i = 0; i < 10; i++){
        for(int j = 0; j < 20; j++){
            if(brk[PGSIZE*i + j] == j )
                continue;
            printf("mem test failed\n");
            ans = -1;
        }
        exit(0);
    }
}

void copyMemTest(void){
    char* brk = sbrk(10*PGSIZE);

    for(int i = 0; i < 10; i++){
        for(int j = 0; j < 20; j++){
            brk[PGSIZE*i + j] = j;
        }
    }

    int pid = fork();
    if(!pid)
        memtest(brk);
    wait(0);
    printf("done\n");
}

void make_test(void (*f)(void) , int expected ,char * test_name){

    printf("_______________________starting test %s______________________\n",test_name);
    ans = 0;
    f();
    if(ans == expected)
        success++;
    else {
        fail++;
        printf("%s failed!!\n",test_name);
    }

}
int main(){


//     make_test(test_malloc, 2, "test_malloc");



    //first-> allocate 20 pages, write in lineric, meaning write each
    // page i the number i, and read it.
    //Sanity Test...

    make_test(mySimpleTets, 0,"mySimpleTets");
    make_test(doubleProcess, 0,"doubleProcess");
    make_test(segTest, 0,"segTest");
    make_test(forktests, 1,"forktests");
    make_test(multiplewritesOneProcess, 0,"multiplewritesOneProcess");
//    make_test(overLoadPage, 0,"overLoadPage");
    make_test(copyMemTest, 0,"copyMemTest");
    printf("__________________SUMMERY________________________________\n");
    printf( "num of success:%d num of failures: %d\n", success, fail);

    if (fail == 0)
        printf("All tests passed!! Yay!\n");
//    test1(1);
    exit(0);
}
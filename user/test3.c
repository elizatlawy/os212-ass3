#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/param.h"


int main(int argc, char *argv[]) {
    int a = 10;
    printf("Hello Word %d\n",a);
    exit(0);
}
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

// Define the syscall number for sys_hello.
// This number should match the one added in syscall_64.tbl (468).
#define SYS_hello 468

int main() {
    long ret;
    printf("Attempting to call syscall SYS_hello (%d)\n", SYS_hello);
    ret = syscall(SYS_hello);
    if (ret == 0) {
        printf("syscall(SYS_hello) successful!\n");
    } else {
        perror("syscall(SYS_hello) failed");
    }
    return ret;
}

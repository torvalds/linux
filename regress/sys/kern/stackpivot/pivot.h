#ifndef REGRESS_PIVOT_H
#define REGRESS_PIVOT_H

static void pivot(void *newstack) {
#if defined(__aarch64__)
    asm("mov sp, %0; ldr lr, [sp]; ret;" ::"r"(newstack));
#elif defined(__amd64__)
    asm("mov %0, %%rsp; retq;" ::"r"(newstack));
#elif defined(__i386__)
    asm("mov %0, %%esp; retl;" ::"r"(newstack));
#elif defined(__mips64__)
    asm("move $sp, %0; ld $ra, 0($sp); jr $ra;" ::"r"(newstack));
#elif defined(__powerpc64__)
    asm("mr %%r1, %0; ld %%r3, 0(%%r1); mtlr %%r3; blr;" ::"r"(newstack));
#elif defined(__powerpc__)
    asm("mr %%r1, %0; lwz %%r3, 0(%%r1); mtlr %%r3; blr;" ::"r"(newstack));
#elif defined(__riscv)
    asm("mv sp, %0; ld ra, 0(sp); jr ra" ::"r"(newstack));
#endif
}

#endif

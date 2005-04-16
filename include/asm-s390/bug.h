#ifndef _S390_BUG_H
#define _S390_BUG_H

#include <linux/kernel.h>

#define BUG() do { \
        printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
        __asm__ __volatile__(".long 0"); \
} while (0)

#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif

#ifndef _S390_BUG_H
#define _S390_BUG_H

#include <linux/kernel.h>

#ifdef CONFIG_BUG

static inline __attribute__((noreturn)) void __do_illegal_op(void)
{
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
	__builtin_trap();
#else
	asm volatile(".long 0");
#endif
}

#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__do_illegal_op(); \
} while (0)

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif

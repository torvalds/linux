#ifndef __ASM_SH64_BUG_H
#define __ASM_SH64_BUG_H

#ifdef CONFIG_BUG
/*
 * Tell the user there is some problem, then force a segfault (in process
 * context) or a panic (interrupt context).
 */
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	*(volatile int *)0 = 0; \
} while (0)

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif /* __ASM_SH64_BUG_H */

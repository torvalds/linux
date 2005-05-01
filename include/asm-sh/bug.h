#ifndef __ASM_SH_BUG_H
#define __ASM_SH_BUG_H

#include <linux/config.h>

#ifdef CONFIG_BUG
/*
 * Tell the user there is some problem.
 */
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	*(volatile int *)0 = 0; \
} while (0)

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif

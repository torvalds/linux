#ifndef _PARISC_BUG_H
#define _PARISC_BUG_H

#define HAVE_ARCH_BUG
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	dump_stack(); \
	panic("BUG!"); \
} while (0)

#include <asm-generic/bug.h>
#endif

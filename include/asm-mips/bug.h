#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_BUG

#include <asm/break.h>

#define BUG()								\
do {									\
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));		\
} while (0)

#define HAVE_ARCH_BUG

#endif

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */

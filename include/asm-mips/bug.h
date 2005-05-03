#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <asm/break.h>

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG
#define BUG()								\
do {									\
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));		\
} while (0)
#endif

#include <asm-generic/bug.h>

#endif

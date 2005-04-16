#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <asm/break.h>

#define BUG()								\
do {									\
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));		\
} while (0)

#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif

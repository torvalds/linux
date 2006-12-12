#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <asm/sgidefs.h>

#ifdef CONFIG_BUG

#include <asm/break.h>

#define BUG()								\
do {									\
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));		\
} while (0)

#define HAVE_ARCH_BUG

#if (_MIPS_ISA > _MIPS_ISA_MIPS1)

#define BUG_ON(condition)						\
do {									\
	__asm__ __volatile__("tne $0, %0" : : "r" (condition));		\
} while (0)

#define HAVE_ARCH_BUG_ON

#endif /* _MIPS_ISA > _MIPS_ISA_MIPS1 */

#endif

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */

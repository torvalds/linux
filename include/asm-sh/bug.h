#ifndef __ASM_SH_BUG_H
#define __ASM_SH_BUG_H

#ifdef CONFIG_BUG

struct bug_frame {
	unsigned short	opcode;
	unsigned short	line;
	const char	*file;
	const char	*func;
};

struct pt_regs;

extern void handle_BUG(struct pt_regs *);

#define TRAPA_BUG_OPCODE	0xc33e	/* trapa #0x3e */

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define BUG()						\
do {							\
	__asm__ __volatile__ (				\
		".align	2\n\t"				\
		".short	%O0\n\t"			\
		".short	%O1\n\t"			\
		".long	%O2\n\t"			\
		".long	%O3\n\t"			\
		:					\
		: "n" (TRAPA_BUG_OPCODE),		\
		  "i" (__LINE__), "X" (__FILE__),	\
		  "X" (__FUNCTION__));			\
} while (0)

#else

#define BUG()					\
do {						\
	__asm__ __volatile__ (			\
		".align	2\n\t"			\
		".short	%O0\n\t"		\
		:				\
		: "n" (TRAPA_BUG_OPCODE));	\
} while (0)

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#define HAVE_ARCH_BUG

#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* __ASM_SH_BUG_H */

#ifndef _ASM_POWERPC_BUG_H
#define _ASM_POWERPC_BUG_H

#include <asm/asm-compat.h>
/*
 * Define an illegal instr to trap on the bug.
 * We don't use 0 because that marks the end of a function
 * in the ELF ABI.  That's "Boo Boo" in case you wonder...
 */
#define BUG_OPCODE .long 0x00b00b00  /* For asm */
#define BUG_ILLEGAL_INSTR "0x00b00b00" /* For BUG macro */

#ifndef __ASSEMBLY__

struct bug_entry {
	unsigned long	bug_addr;
	long		line;
	const char	*file;
	const char	*function;
};

struct bug_entry *find_bug(unsigned long bugaddr);

/*
 * If this bit is set in the line number it means that the trap
 * is for WARN_ON rather than BUG or BUG_ON.
 */
#define BUG_WARNING_TRAP	0x1000000

#ifdef CONFIG_BUG

#define BUG() do {							 \
	__asm__ __volatile__(						 \
		"1:	twi 31,0,0\n"					 \
		".section __bug_table,\"a\"\n"				 \
		"\t"PPC_LONG"	1b,%0,%1,%2\n"			 \
		".previous"						 \
		: : "i" (__LINE__), "i" (__FILE__), "i" (__FUNCTION__)); \
} while (0)

#define BUG_ON(x) do {						\
	__asm__ __volatile__(					\
		"1:	"PPC_TLNEI"	%0,0\n"			\
		".section __bug_table,\"a\"\n"			\
		"\t"PPC_LONG"	1b,%1,%2,%3\n"		\
		".previous"					\
		: : "r" ((long)(x)), "i" (__LINE__),		\
		    "i" (__FILE__), "i" (__FUNCTION__));	\
} while (0)

#define WARN_ON(x) do {						\
	__asm__ __volatile__(					\
		"1:	"PPC_TLNEI"	%0,0\n"			\
		".section __bug_table,\"a\"\n"			\
		"\t"PPC_LONG"	1b,%1,%2,%3\n"		\
		".previous"					\
		: : "r" ((long)(x)),				\
		    "i" (__LINE__ + BUG_WARNING_TRAP),		\
		    "i" (__FILE__), "i" (__FUNCTION__));	\
} while (0)

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON
#endif /* CONFIG_BUG */
#endif /* __ASSEMBLY __ */

#include <asm-generic/bug.h>

#endif /* _ASM_POWERPC_BUG_H */

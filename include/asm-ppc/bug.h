#ifndef _PPC_BUG_H
#define _PPC_BUG_H

struct bug_entry {
	unsigned long	bug_addr;
	int		line;
	const char	*file;
	const char	*function;
};

/*
 * If this bit is set in the line number it means that the trap
 * is for WARN_ON rather than BUG or BUG_ON.
 */
#define BUG_WARNING_TRAP	0x1000000

#define BUG() do {							 \
	__asm__ __volatile__(						 \
		"1:	twi 31,0,0\n"					 \
		".section __bug_table,\"a\"\n\t"			 \
		"	.long 1b,%0,%1,%2\n"				 \
		".previous"						 \
		: : "i" (__LINE__), "i" (__FILE__), "i" (__FUNCTION__)); \
} while (0)

#define BUG_ON(x) do {							\
	if (!__builtin_constant_p(x) || (x)) {				\
		__asm__ __volatile__(					\
			"1:	twnei %0,0\n"				\
			".section __bug_table,\"a\"\n\t"		\
			"	.long 1b,%1,%2,%3\n"			\
			".previous"					\
			: : "r" (x), "i" (__LINE__), "i" (__FILE__),	\
			    "i" (__FUNCTION__));			\
	}								\
} while (0)

#define WARN_ON(x) do {							\
	if (!__builtin_constant_p(x) || (x)) {				\
		__asm__ __volatile__(					\
			"1:	twnei %0,0\n"				\
			".section __bug_table,\"a\"\n\t"		\
			"	.long 1b,%1,%2,%3\n"			\
			".previous"					\
			: : "r" (x), "i" (__LINE__ + BUG_WARNING_TRAP),	\
			    "i" (__FILE__), "i" (__FUNCTION__));	\
	}								\
} while (0)

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON
#include <asm-generic/bug.h>

#endif

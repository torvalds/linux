#ifndef _ASM_POWERPC_BUG_H
#define _ASM_POWERPC_BUG_H
#ifdef __KERNEL__

#include <asm/asm-compat.h>
/*
 * Define an illegal instr to trap on the bug.
 * We don't use 0 because that marks the end of a function
 * in the ELF ABI.  That's "Boo Boo" in case you wonder...
 */
#define BUG_OPCODE .long 0x00b00b00  /* For asm */
#define BUG_ILLEGAL_INSTR "0x00b00b00" /* For BUG macro */

#ifndef __ASSEMBLY__

#ifdef CONFIG_BUG

/* _EMIT_BUG_ENTRY expects args %0,%1,%2,%3 to be FILE, LINE, flags and
   sizeof(struct bug_entry), respectively */
#ifdef CONFIG_DEBUG_BUGVERBOSE
#define _EMIT_BUG_ENTRY				\
	".section __bug_table,\"a\"\n"		\
	"2:\t" PPC_LONG "1b, %0\n"		\
	"\t.short %1, %2\n"			\
	".org 2b+%3\n"				\
	".previous\n"
#else
#define _EMIT_BUG_ENTRY				\
	".section __bug_table,\"a\"\n"		\
	"2:\t" PPC_LONG "1b\n"			\
	"\t.short %2\n"				\
	".org 2b+%3\n"				\
	".previous\n"
#endif

/*
 * BUG_ON() and WARN_ON() do their best to cooperate with compile-time
 * optimisations. However depending on the complexity of the condition
 * some compiler versions may not produce optimal results.
 */

#define BUG() do {						\
	__asm__ __volatile__(					\
		"1:	twi 31,0,0\n"				\
		_EMIT_BUG_ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		    "i" (0), "i"  (sizeof(struct bug_entry)));	\
	for(;;) ;						\
} while (0)

#define BUG_ON(x) do {						\
	if (__builtin_constant_p(x)) {				\
		if (x)						\
			BUG();					\
	} else {						\
		__asm__ __volatile__(				\
		"1:	"PPC_TLNEI"	%4,0\n"			\
		_EMIT_BUG_ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__), "i" (0),	\
		  "i" (sizeof(struct bug_entry)),		\
		  "r" ((long)(x)));				\
	}							\
} while (0)

#define __WARN() do {						\
	__asm__ __volatile__(					\
		"1:	twi 31,0,0\n"				\
		_EMIT_BUG_ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		  "i" (BUGFLAG_WARNING),			\
		  "i" (sizeof(struct bug_entry)));		\
} while (0)

#define WARN_ON(x) ({						\
	typeof(x) __ret_warn_on = (x);				\
	if (__builtin_constant_p(__ret_warn_on)) {		\
		if (__ret_warn_on)				\
			__WARN();				\
	} else {						\
		__asm__ __volatile__(				\
		"1:	"PPC_TLNEI"	%4,0\n"			\
		_EMIT_BUG_ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		  "i" (BUGFLAG_WARNING),			\
		  "i" (sizeof(struct bug_entry)),		\
		  "r" (__ret_warn_on));				\
	}							\
	unlikely(__ret_warn_on);				\
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON
#endif /* CONFIG_BUG */
#endif /* __ASSEMBLY __ */

#include <asm-generic/bug.h>

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_BUG_H */

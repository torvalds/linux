#ifndef _ASM_S390_BUG_H
#define _ASM_S390_BUG_H

#include <linux/kernel.h>

#ifdef CONFIG_BUG

#ifdef CONFIG_64BIT
#define S390_LONG ".quad"
#else
#define S390_LONG ".long"
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define __EMIT_BUG(x) do {					\
	asm volatile(						\
		"0:	j	0b+2\n"				\
		"1:\n"						\
		".section .rodata.str,\"aMS\",@progbits,1\n"	\
		"2:	.asciz	\""__FILE__"\"\n"		\
		".previous\n"					\
		".section __bug_table,\"a\"\n"			\
		"3:\t"	S390_LONG "\t1b,2b\n"			\
		"	.short	%0,%1\n"			\
		"	.org	3b+%2\n"			\
		".previous\n"					\
		: : "i" (__LINE__),				\
		    "i" (x),					\
		    "i" (sizeof(struct bug_entry)));		\
} while (0)

#else /* CONFIG_DEBUG_BUGVERBOSE */

#define __EMIT_BUG(x) do {				\
	asm volatile(					\
		"0:	j	0b+2\n"			\
		"1:\n"					\
		".section __bug_table,\"a\"\n"		\
		"2:\t"	S390_LONG "\t1b\n"		\
		"	.short	%0\n"			\
		"	.org	2b+%1\n"		\
		".previous\n"				\
		: : "i" (x),				\
		    "i" (sizeof(struct bug_entry)));	\
} while (0)

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#define BUG()	__EMIT_BUG(0)

#define WARN_ON(x) ({					\
	typeof(x) __ret_warn_on = (x);			\
	if (__builtin_constant_p(__ret_warn_on)) {	\
		if (__ret_warn_on)			\
			__EMIT_BUG(BUGFLAG_WARNING);	\
	} else {					\
		if (unlikely(__ret_warn_on))		\
			__EMIT_BUG(BUGFLAG_WARNING);	\
	}						\
	unlikely(__ret_warn_on);			\
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_WARN_ON
#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* _ASM_S390_BUG_H */

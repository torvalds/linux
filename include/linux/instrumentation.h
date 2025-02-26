/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_INSTRUMENTATION_H
#define __LINUX_INSTRUMENTATION_H

#ifdef CONFIG_NOINSTR_VALIDATION

#include <linux/objtool.h>
#include <linux/stringify.h>

/* Begin/end of an instrumentation safe region */
#define __instrumentation_begin(c) ({					\
	asm volatile(__stringify(c) ": nop\n\t"				\
		     ANNOTATE_INSTR_BEGIN(__ASM_BREF(c))		\
		     : : "i" (c));					\
})
#define instrumentation_begin() __instrumentation_begin(__COUNTER__)

/*
 * Because instrumentation_{begin,end}() can nest, objtool validation considers
 * _begin() a +1 and _end() a -1 and computes a sum over the instructions.
 * When the value is greater than 0, we consider instrumentation allowed.
 *
 * There is a problem with code like:
 *
 * noinstr void foo()
 * {
 *	instrumentation_begin();
 *	...
 *	if (cond) {
 *		instrumentation_begin();
 *		...
 *		instrumentation_end();
 *	}
 *	bar();
 *	instrumentation_end();
 * }
 *
 * If instrumentation_end() would be an empty label, like all the other
 * annotations, the inner _end(), which is at the end of a conditional block,
 * would land on the instruction after the block.
 *
 * If we then consider the sum of the !cond path, we'll see that the call to
 * bar() is with a 0-value, even though, we meant it to happen with a positive
 * value.
 *
 * To avoid this, have _end() be a NOP instruction, this ensures it will be
 * part of the condition block and does not escape.
 */
#define __instrumentation_end(c) ({					\
	asm volatile(__stringify(c) ": nop\n\t"				\
		     ANNOTATE_INSTR_END(__ASM_BREF(c))			\
		     : : "i" (c));					\
})
#define instrumentation_end() __instrumentation_end(__COUNTER__)
#else /* !CONFIG_NOINSTR_VALIDATION */
# define instrumentation_begin()	do { } while(0)
# define instrumentation_end()		do { } while(0)
#endif /* CONFIG_NOINSTR_VALIDATION */

#endif /* __LINUX_INSTRUMENTATION_H */

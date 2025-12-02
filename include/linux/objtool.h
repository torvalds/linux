/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OBJTOOL_H
#define _LINUX_OBJTOOL_H

#include <linux/objtool_types.h>
#include <linux/annotate.h>

#ifdef CONFIG_OBJTOOL

#ifndef __ASSEMBLY__

#define UNWIND_HINT(type, sp_reg, sp_offset, signal)		\
	"987: \n\t"						\
	".pushsection .discard.unwind_hints\n\t"		\
	ANNOTATE_DATA_SPECIAL "\n\t"				\
	/* struct unwind_hint */				\
	".long 987b - .\n\t"					\
	".short " __stringify(sp_offset) "\n\t"			\
	".byte " __stringify(sp_reg) "\n\t"			\
	".byte " __stringify(type) "\n\t"			\
	".byte " __stringify(signal) "\n\t"			\
	".balign 4 \n\t"					\
	".popsection\n\t"

/*
 * This macro marks the given function's stack frame as "non-standard", which
 * tells objtool to ignore the function when doing stack metadata validation.
 * It should only be used in special cases where you're 100% sure it won't
 * affect the reliability of frame pointers and kernel stack traces.
 *
 * For more information, see tools/objtool/Documentation/objtool.txt.
 */
#define STACK_FRAME_NON_STANDARD(func) \
	static void __used __section(".discard.func_stack_frame_non_standard") \
		*__func_stack_frame_non_standard_##func = func

/*
 * STACK_FRAME_NON_STANDARD_FP() is a frame-pointer-specific function ignore
 * for the case where a function is intentionally missing frame pointer setup,
 * but otherwise needs objtool/ORC coverage when frame pointers are disabled.
 */
#ifdef CONFIG_FRAME_POINTER
#define STACK_FRAME_NON_STANDARD_FP(func) STACK_FRAME_NON_STANDARD(func)
#else
#define STACK_FRAME_NON_STANDARD_FP(func)
#endif

#define ASM_REACHABLE							\
	"998:\n\t"							\
	".pushsection .discard.reachable\n\t"				\
	".long 998b\n\t"						\
	".popsection\n\t"

#define __ASM_BREF(label)	label ## b

#else /* __ASSEMBLY__ */

/*
 * In asm, there are two kinds of code: normal C-type callable functions and
 * the rest.  The normal callable functions can be called by other code, and
 * don't do anything unusual with the stack.  Such normal callable functions
 * are annotated with SYM_FUNC_{START,END}.  Most asm code falls in this
 * category.  In this case, no special debugging annotations are needed because
 * objtool can automatically generate the ORC data for the ORC unwinder to read
 * at runtime.
 *
 * Anything which doesn't fall into the above category, such as syscall and
 * interrupt handlers, tends to not be called directly by other functions, and
 * often does unusual non-C-function-type things with the stack pointer.  Such
 * code needs to be annotated such that objtool can understand it.  The
 * following CFI hint macros are for this type of code.
 *
 * These macros provide hints to objtool about the state of the stack at each
 * instruction.  Objtool starts from the hints and follows the code flow,
 * making automatic CFI adjustments when it sees pushes and pops, filling out
 * the debuginfo as necessary.  It will also warn if it sees any
 * inconsistencies.
 */
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 signal=0
.Lhere_\@:
	.pushsection .discard.unwind_hints
		ANNOTATE_DATA_SPECIAL
		/* struct unwind_hint */
		.long .Lhere_\@ - .
		.short \sp_offset
		.byte \sp_reg
		.byte \type
		.byte \signal
		.balign 4
	.popsection
.endm

.macro STACK_FRAME_NON_STANDARD func:req
	.pushsection .discard.func_stack_frame_non_standard, "aw"
	.quad \func
	.popsection
.endm

.macro STACK_FRAME_NON_STANDARD_FP func:req
#ifdef CONFIG_FRAME_POINTER
	STACK_FRAME_NON_STANDARD \func
#endif
.endm

#endif /* __ASSEMBLY__ */

#else /* !CONFIG_OBJTOOL */

#ifndef __ASSEMBLY__

#define UNWIND_HINT(type, sp_reg, sp_offset, signal) "\n\t"
#define STACK_FRAME_NON_STANDARD(func)
#define STACK_FRAME_NON_STANDARD_FP(func)
#else
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 signal=0
.endm
.macro STACK_FRAME_NON_STANDARD func:req
.endm
#endif

#endif /* CONFIG_OBJTOOL */

#if defined(CONFIG_NOINSTR_VALIDATION) && \
	(defined(CONFIG_MITIGATION_UNRET_ENTRY) || defined(CONFIG_MITIGATION_SRSO))
#define VALIDATE_UNRET_BEGIN	ANNOTATE_UNRET_BEGIN
#else
#define VALIDATE_UNRET_BEGIN
#endif

#endif /* _LINUX_OBJTOOL_H */

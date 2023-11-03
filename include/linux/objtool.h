/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OBJTOOL_H
#define _LINUX_OBJTOOL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

/*
 * This struct is used by asm and inline asm code to manually annotate the
 * location of registers on the stack.
 */
struct unwind_hint {
	u32		ip;
	s16		sp_offset;
	u8		sp_reg;
	u8		type;
	u8		end;
};
#endif

/*
 * UNWIND_HINT_TYPE_CALL: Indicates that sp_reg+sp_offset resolves to PREV_SP
 * (the caller's SP right before it made the call).  Used for all callable
 * functions, i.e. all C code and all callable asm functions.
 *
 * UNWIND_HINT_TYPE_REGS: Used in entry code to indicate that sp_reg+sp_offset
 * points to a fully populated pt_regs from a syscall, interrupt, or exception.
 *
 * UNWIND_HINT_TYPE_REGS_PARTIAL: Used in entry code to indicate that
 * sp_reg+sp_offset points to the iret return frame.
 *
 * UNWIND_HINT_FUNC: Generate the unwind metadata of a callable function.
 * Useful for code which doesn't have an ELF function annotation.
 *
 * UNWIND_HINT_ENTRY: machine entry without stack, SYSCALL/SYSENTER etc.
 */
#define UNWIND_HINT_TYPE_CALL		0
#define UNWIND_HINT_TYPE_REGS		1
#define UNWIND_HINT_TYPE_REGS_PARTIAL	2
#define UNWIND_HINT_TYPE_FUNC		3
#define UNWIND_HINT_TYPE_ENTRY		4
#define UNWIND_HINT_TYPE_SAVE		5
#define UNWIND_HINT_TYPE_RESTORE	6

#ifdef CONFIG_STACK_VALIDATION

#ifndef __ASSEMBLY__

#define UNWIND_HINT(sp_reg, sp_offset, type, end)		\
	"987: \n\t"						\
	".pushsection .discard.unwind_hints\n\t"		\
	/* struct unwind_hint */				\
	".long 987b - .\n\t"					\
	".short " __stringify(sp_offset) "\n\t"			\
	".byte " __stringify(sp_reg) "\n\t"			\
	".byte " __stringify(type) "\n\t"			\
	".byte " __stringify(end) "\n\t"			\
	".balign 4 \n\t"					\
	".popsection\n\t"

/*
 * This macro marks the given function's stack frame as "non-standard", which
 * tells objtool to ignore the function when doing stack metadata validation.
 * It should only be used in special cases where you're 100% sure it won't
 * affect the reliability of frame pointers and kernel stack traces.
 *
 * For more information, see tools/objtool/Documentation/stack-validation.txt.
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

#define ANNOTATE_NOENDBR					\
	"986: \n\t"						\
	".pushsection .discard.noendbr\n\t"			\
	_ASM_PTR " 986b\n\t"					\
	".popsection\n\t"

#else /* __ASSEMBLY__ */

/*
 * This macro indicates that the following intra-function call is valid.
 * Any non-annotated intra-function call will cause objtool to issue a warning.
 */
#define ANNOTATE_INTRA_FUNCTION_CALL				\
	999:							\
	.pushsection .discard.intra_function_calls;		\
	.long 999b;						\
	.popsection;

/*
 * In asm, there are two kinds of code: normal C-type callable functions and
 * the rest.  The normal callable functions can be called by other code, and
 * don't do anything unusual with the stack.  Such normal callable functions
 * are annotated with the ENTRY/ENDPROC macros.  Most asm code falls in this
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
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 end=0
.Lunwind_hint_ip_\@:
	.pushsection .discard.unwind_hints
		/* struct unwind_hint */
		.long .Lunwind_hint_ip_\@ - .
		.short \sp_offset
		.byte \sp_reg
		.byte \type
		.byte \end
		.balign 4
	.popsection
.endm

.macro ANNOTATE_NOENDBR
.Lhere_\@:
	.pushsection .discard.noendbr
	.quad	.Lhere_\@
	.popsection
.endm

#endif /* __ASSEMBLY__ */

#else /* !CONFIG_STACK_VALIDATION */

#ifndef __ASSEMBLY__

#define UNWIND_HINT(sp_reg, sp_offset, type, end)	\
	"\n\t"
#define STACK_FRAME_NON_STANDARD(func)
#define STACK_FRAME_NON_STANDARD_FP(func)
#define ANNOTATE_NOENDBR
#else
#define ANNOTATE_INTRA_FUNCTION_CALL
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 end=0
.endm
.macro ANNOTATE_NOENDBR
.endm
#endif

#endif /* CONFIG_STACK_VALIDATION */

#endif /* _LINUX_OBJTOOL_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OBJTOOL_H
#define _LINUX_OBJTOOL_H

#include <linux/objtool_types.h>

#ifdef CONFIG_OBJTOOL

#include <asm/asm.h>

#ifndef __ASSEMBLY__

#define UNWIND_HINT(type, sp_reg, sp_offset, signal)	\
	"987: \n\t"						\
	".pushsection .discard.unwind_hints\n\t"		\
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

#define __ASM_ANNOTATE(label, type)					\
	".pushsection .discard.annotate_insn,\"M\",@progbits,8\n\t"	\
	".long " __stringify(label) " - .\n\t"			\
	".long " __stringify(type) "\n\t"				\
	".popsection\n\t"

#define ASM_ANNOTATE(type)						\
	"911:\n\t"						\
	__ASM_ANNOTATE(911b, type)

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
	.long \func - .
	.popsection
.endm

.macro STACK_FRAME_NON_STANDARD_FP func:req
#ifdef CONFIG_FRAME_POINTER
	STACK_FRAME_NON_STANDARD \func
#endif
.endm

.macro ANNOTATE type:req
.Lhere_\@:
	.pushsection .discard.annotate_insn,"M",@progbits,8
	.long	.Lhere_\@ - .
	.long	\type
	.popsection
.endm

#endif /* __ASSEMBLY__ */

#else /* !CONFIG_OBJTOOL */

#ifndef __ASSEMBLY__

#define UNWIND_HINT(type, sp_reg, sp_offset, signal) "\n\t"
#define STACK_FRAME_NON_STANDARD(func)
#define STACK_FRAME_NON_STANDARD_FP(func)
#define __ASM_ANNOTATE(label, type) ""
#define ASM_ANNOTATE(type)
#else
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 signal=0
.endm
.macro STACK_FRAME_NON_STANDARD func:req
.endm
.macro ANNOTATE type:req
.endm
#endif

#endif /* CONFIG_OBJTOOL */

#ifndef __ASSEMBLY__
/*
 * Annotate away the various 'relocation to !ENDBR` complaints; knowing that
 * these relocations will never be used for indirect calls.
 */
#define ANNOTATE_NOENDBR		ASM_ANNOTATE(ANNOTYPE_NOENDBR)
#define ANNOTATE_NOENDBR_SYM(sym)	asm(__ASM_ANNOTATE(sym, ANNOTYPE_NOENDBR))

/*
 * This should be used immediately before an indirect jump/call. It tells
 * objtool the subsequent indirect jump/call is vouched safe for retpoline
 * builds.
 */
#define ANNOTATE_RETPOLINE_SAFE		ASM_ANNOTATE(ANNOTYPE_RETPOLINE_SAFE)
/*
 * See linux/instrumentation.h
 */
#define ANNOTATE_INSTR_BEGIN(label)	__ASM_ANNOTATE(label, ANNOTYPE_INSTR_BEGIN)
#define ANNOTATE_INSTR_END(label)	__ASM_ANNOTATE(label, ANNOTYPE_INSTR_END)
/*
 * objtool annotation to ignore the alternatives and only consider the original
 * instruction(s).
 */
#define ANNOTATE_IGNORE_ALTERNATIVE	ASM_ANNOTATE(ANNOTYPE_IGNORE_ALTS)
/*
 * This macro indicates that the following intra-function call is valid.
 * Any non-annotated intra-function call will cause objtool to issue a warning.
 */
#define ANNOTATE_INTRA_FUNCTION_CALL	ASM_ANNOTATE(ANNOTYPE_INTRA_FUNCTION_CALL)
/*
 * Use objtool to validate the entry requirement that all code paths do
 * VALIDATE_UNRET_END before RET.
 *
 * NOTE: The macro must be used at the beginning of a global symbol, otherwise
 * it will be ignored.
 */
#define ANNOTATE_UNRET_BEGIN		ASM_ANNOTATE(ANNOTYPE_UNRET_BEGIN)
/*
 * This should be used to refer to an instruction that is considered
 * terminating, like a noreturn CALL or UD2 when we know they are not -- eg
 * WARN using UD2.
 */
#define ANNOTATE_REACHABLE(label)	__ASM_ANNOTATE(label, ANNOTYPE_REACHABLE)
/*
 * This should not be used; it annotates away CFI violations. There are a few
 * valid use cases like kexec handover to the next kernel image, and there is
 * no security concern there.
 *
 * There are also a few real issues annotated away, like EFI because we can't
 * control the EFI code.
 */
#define ANNOTATE_NOCFI_SYM(sym)		asm(__ASM_ANNOTATE(sym, ANNOTYPE_NOCFI))

#else
#define ANNOTATE_NOENDBR		ANNOTATE type=ANNOTYPE_NOENDBR
#define ANNOTATE_RETPOLINE_SAFE		ANNOTATE type=ANNOTYPE_RETPOLINE_SAFE
/*	ANNOTATE_INSTR_BEGIN		ANNOTATE type=ANNOTYPE_INSTR_BEGIN */
/*	ANNOTATE_INSTR_END		ANNOTATE type=ANNOTYPE_INSTR_END */
#define ANNOTATE_IGNORE_ALTERNATIVE	ANNOTATE type=ANNOTYPE_IGNORE_ALTS
#define ANNOTATE_INTRA_FUNCTION_CALL	ANNOTATE type=ANNOTYPE_INTRA_FUNCTION_CALL
#define ANNOTATE_UNRET_BEGIN		ANNOTATE type=ANNOTYPE_UNRET_BEGIN
#define ANNOTATE_REACHABLE		ANNOTATE type=ANNOTYPE_REACHABLE
#define ANNOTATE_NOCFI_SYM		ANNOTATE type=ANNOTYPE_NOCFI
#endif

#if defined(CONFIG_NOINSTR_VALIDATION) && \
	(defined(CONFIG_MITIGATION_UNRET_ENTRY) || defined(CONFIG_MITIGATION_SRSO))
#define VALIDATE_UNRET_BEGIN	ANNOTATE_UNRET_BEGIN
#else
#define VALIDATE_UNRET_BEGIN
#endif

#endif /* _LINUX_OBJTOOL_H */

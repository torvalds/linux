/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ANNOTATE_H
#define _LINUX_ANNOTATE_H

#include <linux/objtool_types.h>

#ifdef CONFIG_OBJTOOL

#ifndef __ASSEMBLY__

#define __ASM_ANNOTATE(label, type)					\
	".pushsection .discard.annotate_insn,\"M\",@progbits,8\n\t"	\
	".long " __stringify(label) " - .\n\t"				\
	".long " __stringify(type) "\n\t"				\
	".popsection\n\t"

#define ASM_ANNOTATE(type)						\
	"911:\n\t"							\
	__ASM_ANNOTATE(911b, type)

#else /* __ASSEMBLY__ */

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
#define __ASM_ANNOTATE(label, type) ""
#define ASM_ANNOTATE(type)
#else /* __ASSEMBLY__ */
.macro ANNOTATE type:req
.endm
#endif /* __ASSEMBLY__ */
#endif /* !CONFIG_OBJTOOL */

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

#else /* __ASSEMBLY__ */
#define ANNOTATE_NOENDBR		ANNOTATE type=ANNOTYPE_NOENDBR
#define ANNOTATE_RETPOLINE_SAFE		ANNOTATE type=ANNOTYPE_RETPOLINE_SAFE
/*	ANNOTATE_INSTR_BEGIN		ANNOTATE type=ANNOTYPE_INSTR_BEGIN */
/*	ANNOTATE_INSTR_END		ANNOTATE type=ANNOTYPE_INSTR_END */
#define ANNOTATE_IGNORE_ALTERNATIVE	ANNOTATE type=ANNOTYPE_IGNORE_ALTS
#define ANNOTATE_INTRA_FUNCTION_CALL	ANNOTATE type=ANNOTYPE_INTRA_FUNCTION_CALL
#define ANNOTATE_UNRET_BEGIN		ANNOTATE type=ANNOTYPE_UNRET_BEGIN
#define ANNOTATE_REACHABLE		ANNOTATE type=ANNOTYPE_REACHABLE
#define ANNOTATE_NOCFI_SYM		ANNOTATE type=ANNOTYPE_NOCFI
#endif /* __ASSEMBLY__ */

#endif /* _LINUX_ANNOTATE_H */

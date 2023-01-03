/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <linux/compiler_types.h>
#include <linux/stringify.h>
#include <linux/export.h>
#include <asm/linkage.h>

/* Some toolchains use other characters (e.g. '`') to mark new line in macro */
#ifndef ASM_NL
#define ASM_NL		 ;
#endif

#ifdef __cplusplus
#define CPP_ASMLINKAGE extern "C"
#else
#define CPP_ASMLINKAGE
#endif

#ifndef asmlinkage
#define asmlinkage CPP_ASMLINKAGE
#endif

#ifndef cond_syscall
#define cond_syscall(x)	asm(				\
	".weak " __stringify(x) "\n\t"			\
	".set  " __stringify(x) ","			\
		 __stringify(sys_ni_syscall))
#endif

#ifndef SYSCALL_ALIAS
#define SYSCALL_ALIAS(alias, name) asm(			\
	".globl " __stringify(alias) "\n\t"		\
	".set   " __stringify(alias) ","		\
		  __stringify(name))
#endif

#define __page_aligned_data	__section(".data..page_aligned") __aligned(PAGE_SIZE)
#define __page_aligned_bss	__section(".bss..page_aligned") __aligned(PAGE_SIZE)

/*
 * For assembly routines.
 *
 * Note when using these that you must specify the appropriate
 * alignment directives yourself
 */
#define __PAGE_ALIGNED_DATA	.section ".data..page_aligned", "aw"
#define __PAGE_ALIGNED_BSS	.section ".bss..page_aligned", "aw"

/*
 * This is used by architectures to keep arguments on the stack
 * untouched by the compiler by keeping them live until the end.
 * The argument stack may be owned by the assembly-language
 * caller, not the callee, and gcc doesn't always understand
 * that.
 *
 * We have the return value, and a maximum of six arguments.
 *
 * This should always be followed by a "return ret" for the
 * protection to work (ie no more work that the compiler might
 * end up needing stack temporaries for).
 */
/* Assembly files may be compiled with -traditional .. */
#ifndef __ASSEMBLY__
#ifndef asmlinkage_protect
# define asmlinkage_protect(n, ret, args...)	do { } while (0)
#endif
#endif

#ifndef __ALIGN
#define __ALIGN			.balign CONFIG_FUNCTION_ALIGNMENT
#define __ALIGN_STR		__stringify(__ALIGN)
#endif

#ifdef __ASSEMBLY__

/* SYM_T_FUNC -- type used by assembler to mark functions */
#ifndef SYM_T_FUNC
#define SYM_T_FUNC				STT_FUNC
#endif

/* SYM_T_OBJECT -- type used by assembler to mark data */
#ifndef SYM_T_OBJECT
#define SYM_T_OBJECT				STT_OBJECT
#endif

/* SYM_T_NONE -- type used by assembler to mark entries of unknown type */
#ifndef SYM_T_NONE
#define SYM_T_NONE				STT_NOTYPE
#endif

/* SYM_A_* -- align the symbol? */
#define SYM_A_ALIGN				ALIGN
#define SYM_A_NONE				/* nothing */

/* SYM_L_* -- linkage of symbols */
#define SYM_L_GLOBAL(name)			.globl name
#define SYM_L_WEAK(name)			.weak name
#define SYM_L_LOCAL(name)			/* nothing */

#ifndef LINKER_SCRIPT
#define ALIGN __ALIGN
#define ALIGN_STR __ALIGN_STR

/* === DEPRECATED annotations === */

#ifndef CONFIG_ARCH_USE_SYM_ANNOTATIONS
#ifndef GLOBAL
/* deprecated, use SYM_DATA*, SYM_ENTRY, or similar */
#define GLOBAL(name) \
	.globl name ASM_NL \
	name:
#endif

#ifndef ENTRY
/* deprecated, use SYM_FUNC_START */
#define ENTRY(name) \
	SYM_FUNC_START(name)
#endif
#endif /* CONFIG_ARCH_USE_SYM_ANNOTATIONS */
#endif /* LINKER_SCRIPT */

#ifndef CONFIG_ARCH_USE_SYM_ANNOTATIONS
#ifndef WEAK
/* deprecated, use SYM_FUNC_START_WEAK* */
#define WEAK(name)	   \
	SYM_FUNC_START_WEAK(name)
#endif

#ifndef END
/* deprecated, use SYM_FUNC_END, SYM_DATA_END, or SYM_END */
#define END(name) \
	.size name, .-name
#endif

/* If symbol 'name' is treated as a subroutine (gets called, and returns)
 * then please use ENDPROC to mark 'name' as STT_FUNC for the benefit of
 * static analysis tools such as stack depth analyzer.
 */
#ifndef ENDPROC
/* deprecated, use SYM_FUNC_END */
#define ENDPROC(name) \
	SYM_FUNC_END(name)
#endif
#endif /* CONFIG_ARCH_USE_SYM_ANNOTATIONS */

/* === generic annotations === */

/* SYM_ENTRY -- use only if you have to for non-paired symbols */
#ifndef SYM_ENTRY
#define SYM_ENTRY(name, linkage, align...)		\
	linkage(name) ASM_NL				\
	align ASM_NL					\
	name:
#endif

/* SYM_START -- use only if you have to */
#ifndef SYM_START
#define SYM_START(name, linkage, align...)		\
	SYM_ENTRY(name, linkage, align)
#endif

/* SYM_END -- use only if you have to */
#ifndef SYM_END
#define SYM_END(name, sym_type)				\
	.type name sym_type ASM_NL			\
	.set .L__sym_size_##name, .-name ASM_NL		\
	.size name, .L__sym_size_##name
#endif

/* SYM_ALIAS -- use only if you have to */
#ifndef SYM_ALIAS
#define SYM_ALIAS(alias, name, linkage)			\
	linkage(alias) ASM_NL				\
	.set alias, name ASM_NL
#endif

/* === code annotations === */

/*
 * FUNC -- C-like functions (proper stack frame etc.)
 * CODE -- non-C code (e.g. irq handlers with different, special stack etc.)
 *
 * Objtool validates stack for FUNC, but not for CODE.
 * Objtool generates debug info for both FUNC & CODE, but needs special
 * annotations for each CODE's start (to describe the actual stack frame).
 *
 * Objtool requires that all code must be contained in an ELF symbol. Symbol
 * names that have a  .L prefix do not emit symbol table entries. .L
 * prefixed symbols can be used within a code region, but should be avoided for
 * denoting a range of code via ``SYM_*_START/END`` annotations.
 *
 * ALIAS -- does not generate debug info -- the aliased function will
 */

/* SYM_INNER_LABEL_ALIGN -- only for labels in the middle of code */
#ifndef SYM_INNER_LABEL_ALIGN
#define SYM_INNER_LABEL_ALIGN(name, linkage)	\
	.type name SYM_T_NONE ASM_NL			\
	SYM_ENTRY(name, linkage, SYM_A_ALIGN)
#endif

/* SYM_INNER_LABEL -- only for labels in the middle of code */
#ifndef SYM_INNER_LABEL
#define SYM_INNER_LABEL(name, linkage)		\
	.type name SYM_T_NONE ASM_NL			\
	SYM_ENTRY(name, linkage, SYM_A_NONE)
#endif

/* SYM_FUNC_START -- use for global functions */
#ifndef SYM_FUNC_START
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_NOALIGN -- use for global functions, w/o alignment */
#ifndef SYM_FUNC_START_NOALIGN
#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)
#endif

/* SYM_FUNC_START_LOCAL -- use for local functions */
#ifndef SYM_FUNC_START_LOCAL
#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_LOCAL_NOALIGN -- use for local functions, w/o alignment */
#ifndef SYM_FUNC_START_LOCAL_NOALIGN
#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)
#endif

/* SYM_FUNC_START_WEAK -- use for weak functions */
#ifndef SYM_FUNC_START_WEAK
#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_WEAK_NOALIGN -- use for weak functions, w/o alignment */
#ifndef SYM_FUNC_START_WEAK_NOALIGN
#define SYM_FUNC_START_WEAK_NOALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_NONE)
#endif

/*
 * SYM_FUNC_END -- the end of SYM_FUNC_START_LOCAL, SYM_FUNC_START,
 * SYM_FUNC_START_WEAK, ...
 */
#ifndef SYM_FUNC_END
#define SYM_FUNC_END(name)				\
	SYM_END(name, SYM_T_FUNC)
#endif

/*
 * SYM_FUNC_ALIAS -- define a global alias for an existing function
 */
#ifndef SYM_FUNC_ALIAS
#define SYM_FUNC_ALIAS(alias, name)					\
	SYM_ALIAS(alias, name, SYM_L_GLOBAL)
#endif

/*
 * SYM_FUNC_ALIAS_LOCAL -- define a local alias for an existing function
 */
#ifndef SYM_FUNC_ALIAS_LOCAL
#define SYM_FUNC_ALIAS_LOCAL(alias, name)				\
	SYM_ALIAS(alias, name, SYM_L_LOCAL)
#endif

/*
 * SYM_FUNC_ALIAS_WEAK -- define a weak global alias for an existing function
 */
#ifndef SYM_FUNC_ALIAS_WEAK
#define SYM_FUNC_ALIAS_WEAK(alias, name)				\
	SYM_ALIAS(alias, name, SYM_L_WEAK)
#endif

/* SYM_CODE_START -- use for non-C (special) functions */
#ifndef SYM_CODE_START
#define SYM_CODE_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_CODE_START_NOALIGN -- use for non-C (special) functions, w/o alignment */
#ifndef SYM_CODE_START_NOALIGN
#define SYM_CODE_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)
#endif

/* SYM_CODE_START_LOCAL -- use for local non-C (special) functions */
#ifndef SYM_CODE_START_LOCAL
#define SYM_CODE_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)
#endif

/*
 * SYM_CODE_START_LOCAL_NOALIGN -- use for local non-C (special) functions,
 * w/o alignment
 */
#ifndef SYM_CODE_START_LOCAL_NOALIGN
#define SYM_CODE_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)
#endif

/* SYM_CODE_END -- the end of SYM_CODE_START_LOCAL, SYM_CODE_START, ... */
#ifndef SYM_CODE_END
#define SYM_CODE_END(name)				\
	SYM_END(name, SYM_T_NONE)
#endif

/* === data annotations === */

/* SYM_DATA_START -- global data symbol */
#ifndef SYM_DATA_START
#define SYM_DATA_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)
#endif

/* SYM_DATA_START -- local data symbol */
#ifndef SYM_DATA_START_LOCAL
#define SYM_DATA_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)
#endif

/* SYM_DATA_END -- the end of SYM_DATA_START symbol */
#ifndef SYM_DATA_END
#define SYM_DATA_END(name)				\
	SYM_END(name, SYM_T_OBJECT)
#endif

/* SYM_DATA_END_LABEL -- the labeled end of SYM_DATA_START symbol */
#ifndef SYM_DATA_END_LABEL
#define SYM_DATA_END_LABEL(name, linkage, label)	\
	linkage(label) ASM_NL				\
	.type label SYM_T_OBJECT ASM_NL			\
	label:						\
	SYM_END(name, SYM_T_OBJECT)
#endif

/* SYM_DATA -- start+end wrapper around simple global data */
#ifndef SYM_DATA
#define SYM_DATA(name, data...)				\
	SYM_DATA_START(name) ASM_NL				\
	data ASM_NL						\
	SYM_DATA_END(name)
#endif

/* SYM_DATA_LOCAL -- start+end wrapper around simple local data */
#ifndef SYM_DATA_LOCAL
#define SYM_DATA_LOCAL(name, data...)			\
	SYM_DATA_START_LOCAL(name) ASM_NL			\
	data ASM_NL						\
	SYM_DATA_END(name)
#endif

#endif /* __ASSEMBLY__ */

#endif /* _LINUX_LINKAGE_H */

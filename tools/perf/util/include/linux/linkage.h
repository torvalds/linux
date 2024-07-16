/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PERF_LINUX_LINKAGE_H_
#define PERF_LINUX_LINKAGE_H_

/* linkage.h ... for including arch/x86/lib/memcpy_64.S */

/* Some toolchains use other characters (e.g. '`') to mark new line in macro */
#ifndef ASM_NL
#define ASM_NL		 ;
#endif

#ifndef __ALIGN
#define __ALIGN		.align 4,0x90
#define __ALIGN_STR	".align 4,0x90"
#endif

/* SYM_T_FUNC -- type used by assembler to mark functions */
#ifndef SYM_T_FUNC
#define SYM_T_FUNC				STT_FUNC
#endif

/* SYM_A_* -- align the symbol? */
#define SYM_A_ALIGN				ALIGN

/* SYM_L_* -- linkage of symbols */
#define SYM_L_GLOBAL(name)			.globl name
#define SYM_L_WEAK(name)			.weak name
#define SYM_L_LOCAL(name)			/* nothing */

#define ALIGN __ALIGN

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
	.size name, .-name
#endif

/* SYM_ALIAS -- use only if you have to */
#ifndef SYM_ALIAS
#define SYM_ALIAS(alias, name, sym_type, linkage)			\
	linkage(alias) ASM_NL						\
	.set alias, name ASM_NL						\
	.type alias sym_type ASM_NL					\
	.set .L__sym_size_##alias, .L__sym_size_##name ASM_NL		\
	.size alias, .L__sym_size_##alias
#endif

/* SYM_FUNC_START -- use for global functions */
#ifndef SYM_FUNC_START
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_LOCAL -- use for local functions */
#ifndef SYM_FUNC_START_LOCAL
#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_WEAK -- use for weak functions */
#ifndef SYM_FUNC_START_WEAK
#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)
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
	SYM_ALIAS(alias, name, SYM_T_FUNC, SYM_L_GLOBAL)
#endif

/*
 * SYM_FUNC_ALIAS_LOCAL -- define a local alias for an existing function
 */
#ifndef SYM_FUNC_ALIAS_LOCAL
#define SYM_FUNC_ALIAS_LOCAL(alias, name)				\
	SYM_ALIAS(alias, name, SYM_T_FUNC, SYM_L_LOCAL)
#endif

/*
 * SYM_FUNC_ALIAS_WEAK -- define a weak global alias for an existing function
 */
#ifndef SYM_FUNC_ALIAS_WEAK
#define SYM_FUNC_ALIAS_WEAK(alias, name)				\
	SYM_ALIAS(alias, name, SYM_T_FUNC, SYM_L_WEAK)
#endif

// In the kernel sources (include/linux/cfi_types.h), this has a different
// definition when CONFIG_CFI_CLANG is used, for tools/ just use the !clang
// definition:
#ifndef SYM_TYPED_START
#define SYM_TYPED_START(name, linkage, align...)        \
        SYM_START(name, linkage, align)
#endif

#ifndef SYM_TYPED_FUNC_START
#define SYM_TYPED_FUNC_START(name)                      \
        SYM_TYPED_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

#endif	/* PERF_LINUX_LINKAGE_H_ */

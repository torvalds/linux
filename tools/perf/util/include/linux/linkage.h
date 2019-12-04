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
	.size name, .-name
#endif

/*
 * SYM_FUNC_START_ALIAS -- use where there are two global names for one
 * function
 */
#ifndef SYM_FUNC_START_ALIAS
#define SYM_FUNC_START_ALIAS(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START -- use for global functions */
#ifndef SYM_FUNC_START
/*
 * The same as SYM_FUNC_START_ALIAS, but we will need to distinguish these two
 * later.
 */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_LOCAL -- use for local functions */
#ifndef SYM_FUNC_START_LOCAL
/* the same as SYM_FUNC_START_LOCAL_ALIAS, see comment near SYM_FUNC_START */
#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_END_ALIAS -- the end of LOCAL_ALIASed or ALIASed function */
#ifndef SYM_FUNC_END_ALIAS
#define SYM_FUNC_END_ALIAS(name)			\
	SYM_END(name, SYM_T_FUNC)
#endif

/*
 * SYM_FUNC_END -- the end of SYM_FUNC_START_LOCAL, SYM_FUNC_START,
 * SYM_FUNC_START_WEAK, ...
 */
#ifndef SYM_FUNC_END
/* the same as SYM_FUNC_END_ALIAS, see comment near SYM_FUNC_START */
#define SYM_FUNC_END(name)				\
	SYM_END(name, SYM_T_FUNC)
#endif

#endif	/* PERF_LINUX_LINKAGE_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Clang Control Flow Integrity (CFI) type definitions.
 */
#ifndef _LINUX_CFI_TYPES_H
#define _LINUX_CFI_TYPES_H

#ifdef __ASSEMBLY__
#include <linux/linkage.h>

#ifdef CONFIG_CFI
/*
 * Use the __kcfi_typeid_<function> type identifier symbol to
 * annotate indirectly called assembly functions. The compiler emits
 * these symbols for all address-taken function declarations in C
 * code.
 */
#ifndef __CFI_TYPE
#define __CFI_TYPE(name)				\
	.4byte __kcfi_typeid_##name
#endif

#define SYM_TYPED_ENTRY(name, linkage, align...)	\
	linkage(name) ASM_NL				\
	align ASM_NL					\
	__CFI_TYPE(name) ASM_NL				\
	name:

#define SYM_TYPED_START(name, linkage, align...)	\
	SYM_TYPED_ENTRY(name, linkage, align)

#else /* CONFIG_CFI */

#define SYM_TYPED_START(name, linkage, align...)	\
	SYM_START(name, linkage, align)

#endif /* CONFIG_CFI */

#ifndef SYM_TYPED_FUNC_START
#define SYM_TYPED_FUNC_START(name) 			\
	SYM_TYPED_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)
#endif

#else /* __ASSEMBLY__ */

#ifdef CONFIG_CFI
#define DEFINE_CFI_TYPE(name, func)						\
	/*									\
	 * Force a reference to the function so the compiler generates		\
	 * __kcfi_typeid_<func>.						\
	 */									\
	__ADDRESSABLE(func);							\
	/* u32 name __ro_after_init = __kcfi_typeid_<func> */			\
	extern u32 name;							\
	asm (									\
	"	.pushsection	.data..ro_after_init,\"aw\",\%progbits	\n"	\
	"	.type	" #name ",\%object				\n"	\
	"	.globl	" #name "					\n"	\
	"	.p2align	2, 0x0					\n"	\
	#name ":							\n"	\
	"	.4byte	__kcfi_typeid_" #func "				\n"	\
	"	.size	" #name ", 4					\n"	\
	"	.popsection						\n"	\
	);
#endif

#endif /* __ASSEMBLY__ */
#endif /* _LINUX_CFI_TYPES_H */

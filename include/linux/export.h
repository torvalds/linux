/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/stringify.h>

/*
 * This comment block is used by fixdep. Please do not remove.
 *
 * When CONFIG_MODVERSIONS is changed from n to y, all source files having
 * EXPORT_SYMBOL variants must be re-compiled because genksyms is run as a
 * side effect of the *.o build rule.
 */

#ifdef CONFIG_64BIT
#define __EXPORT_SYMBOL_REF(sym)			\
	.balign 8				ASM_NL	\
	.quad sym
#else
#define __EXPORT_SYMBOL_REF(sym)			\
	.balign 4				ASM_NL	\
	.long sym
#endif

/*
 * LLVM integrated assembler cam merge adjacent string literals (like
 * C and GNU-as) passed to '.ascii', but not to '.asciz' and chokes on:
 *
 *   .asciz "MODULE_" "kvm" ;
 */
#define ___EXPORT_SYMBOL(sym, license, ns...)		\
	.section ".export_symbol","a"		ASM_NL	\
	__export_symbol_##sym:			ASM_NL	\
		.asciz license			ASM_NL	\
		.ascii ns "\0"			ASM_NL	\
		__EXPORT_SYMBOL_REF(sym)	ASM_NL	\
	.previous

#if defined(__DISABLE_EXPORTS)

/*
 * Allow symbol exports to be disabled completely so that C code may
 * be reused in other execution contexts such as the UEFI stub or the
 * decompressor.
 */
#define __EXPORT_SYMBOL(sym, license, ns)

#elif defined(__GENKSYMS__)

#define __EXPORT_SYMBOL(sym, license, ns)	__GENKSYMS_EXPORT_SYMBOL(sym)

#elif defined(__ASSEMBLY__)

#define __EXPORT_SYMBOL(sym, license, ns) \
	___EXPORT_SYMBOL(sym, license, ns)

#else

#ifdef CONFIG_GENDWARFKSYMS
/*
 * With CONFIG_GENDWARFKSYMS, ensure the compiler emits debugging
 * information for all exported symbols, including those defined in
 * different TUs, by adding a __gendwarfksyms_ptr_<symbol> pointer
 * that's discarded during the final link.
 */
#define __GENDWARFKSYMS_EXPORT(sym)				\
	static typeof(sym) *__gendwarfksyms_ptr_##sym __used	\
		__section(".discard.gendwarfksyms") = &sym;
#else
#define __GENDWARFKSYMS_EXPORT(sym)
#endif

#define __EXPORT_SYMBOL(sym, license, ns)			\
	extern typeof(sym) sym;					\
	__ADDRESSABLE(sym)					\
	__GENDWARFKSYMS_EXPORT(sym)				\
	asm(__stringify(___EXPORT_SYMBOL(sym, license, ns)))

#endif

#ifdef DEFAULT_SYMBOL_NAMESPACE
#define _EXPORT_SYMBOL(sym, license)	__EXPORT_SYMBOL(sym, license, DEFAULT_SYMBOL_NAMESPACE)
#else
#define _EXPORT_SYMBOL(sym, license)	__EXPORT_SYMBOL(sym, license, "")
#endif

#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "GPL")
#define EXPORT_SYMBOL_NS(sym, ns)	__EXPORT_SYMBOL(sym, "", ns)
#define EXPORT_SYMBOL_NS_GPL(sym, ns)	__EXPORT_SYMBOL(sym, "GPL", ns)

#define EXPORT_SYMBOL_GPL_FOR_MODULES(sym, mods) __EXPORT_SYMBOL(sym, "GPL", "module:" mods)

#endif /* _LINUX_EXPORT_H */

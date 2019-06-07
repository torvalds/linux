#ifndef __ASM_GENERIC_EXPORT_H
#define __ASM_GENERIC_EXPORT_H

#ifndef KSYM_FUNC
#define KSYM_FUNC(x) x
#endif
#ifdef CONFIG_64BIT
#ifndef KSYM_ALIGN
#define KSYM_ALIGN 8
#endif
#else
#ifndef KSYM_ALIGN
#define KSYM_ALIGN 4
#endif
#endif
#ifndef KCRC_ALIGN
#define KCRC_ALIGN 4
#endif

.macro __put, val, name
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	.long	\val - ., \name - .
#elif defined(CONFIG_64BIT)
	.quad	\val, \name
#else
	.long	\val, \name
#endif
.endm

/*
 * note on .section use: @progbits vs %progbits nastiness doesn't matter,
 * since we immediately emit into those sections anyway.
 */
.macro ___EXPORT_SYMBOL name,val,sec
#ifdef CONFIG_MODULES
	.globl __ksymtab_\name
	.section ___ksymtab\sec+\name,"a"
	.balign KSYM_ALIGN
__ksymtab_\name:
	__put \val, __kstrtab_\name
	.previous
	.section __ksymtab_strings,"a"
__kstrtab_\name:
	.asciz "\name"
	.previous
#ifdef CONFIG_MODVERSIONS
	.section ___kcrctab\sec+\name,"a"
	.balign KCRC_ALIGN
__kcrctab_\name:
#if defined(CONFIG_MODULE_REL_CRCS)
	.long __crc_\name - .
#else
	.long __crc_\name
#endif
	.weak __crc_\name
	.previous
#endif
#endif
.endm
#undef __put

#if defined(CONFIG_TRIM_UNUSED_KSYMS)

#include <linux/kconfig.h>
#include <generated/autoksyms.h>

.macro __ksym_marker sym
	.section ".discard.ksym","a"
__ksym_marker_\sym:
	 .previous
.endm

#define __EXPORT_SYMBOL(sym, val, sec)				\
	__ksym_marker sym;					\
	__cond_export_sym(sym, val, sec, __is_defined(__KSYM_##sym))
#define __cond_export_sym(sym, val, sec, conf)			\
	___cond_export_sym(sym, val, sec, conf)
#define ___cond_export_sym(sym, val, sec, enabled)		\
	__cond_export_sym_##enabled(sym, val, sec)
#define __cond_export_sym_1(sym, val, sec) ___EXPORT_SYMBOL sym, val, sec
#define __cond_export_sym_0(sym, val, sec) /* nothing */

#else
#define __EXPORT_SYMBOL(sym, val, sec) ___EXPORT_SYMBOL sym, val, sec
#endif

#define EXPORT_SYMBOL(name)					\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name),)
#define EXPORT_SYMBOL_GPL(name) 				\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name), _gpl)
#define EXPORT_DATA_SYMBOL(name)				\
	__EXPORT_SYMBOL(name, name,)
#define EXPORT_DATA_SYMBOL_GPL(name)				\
	__EXPORT_SYMBOL(name, name,_gpl)

#endif

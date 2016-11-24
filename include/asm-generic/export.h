#ifndef __ASM_GENERIC_EXPORT_H
#define __ASM_GENERIC_EXPORT_H

#ifndef KSYM_FUNC
#define KSYM_FUNC(x) x
#endif
#ifdef CONFIG_64BIT
#define __put .quad
#ifndef KSYM_ALIGN
#define KSYM_ALIGN 8
#endif
#ifndef KCRC_ALIGN
#define KCRC_ALIGN 8
#endif
#else
#define __put .long
#ifndef KSYM_ALIGN
#define KSYM_ALIGN 4
#endif
#ifndef KCRC_ALIGN
#define KCRC_ALIGN 4
#endif
#endif

#ifdef CONFIG_HAVE_UNDERSCORE_SYMBOL_PREFIX
#define KSYM(name) _##name
#else
#define KSYM(name) name
#endif

/*
 * note on .section use: @progbits vs %progbits nastiness doesn't matter,
 * since we immediately emit into those sections anyway.
 */
.macro ___EXPORT_SYMBOL name,val,sec
#ifdef CONFIG_MODULES
	.globl KSYM(__ksymtab_\name)
	.section ___ksymtab\sec+\name,"a"
	.balign KSYM_ALIGN
KSYM(__ksymtab_\name):
	__put \val, KSYM(__kstrtab_\name)
	.previous
	.section __ksymtab_strings,"a"
KSYM(__kstrtab_\name):
#ifdef CONFIG_HAVE_UNDERSCORE_SYMBOL_PREFIX
	.asciz "_\name"
#else
	.asciz "\name"
#endif
	.previous
#ifdef CONFIG_MODVERSIONS
	.section ___kcrctab\sec+\name,"a"
	.balign KCRC_ALIGN
KSYM(__kcrctab_\name):
	__put KSYM(__crc_\name)
	.weak KSYM(__crc_\name)
	.previous
#endif
#endif
.endm
#undef __put

#if defined(__KSYM_DEPS__)

#define __EXPORT_SYMBOL(sym, val, sec)	=== __KSYM_##sym ===

#elif defined(CONFIG_TRIM_UNUSED_KSYMS)

#include <linux/kconfig.h>
#include <generated/autoksyms.h>

#define __EXPORT_SYMBOL(sym, val, sec)				\
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
	__EXPORT_SYMBOL(name, KSYM_FUNC(KSYM(name)),)
#define EXPORT_SYMBOL_GPL(name) 				\
	__EXPORT_SYMBOL(name, KSYM_FUNC(KSYM(name)), _gpl)
#define EXPORT_DATA_SYMBOL(name)				\
	__EXPORT_SYMBOL(name, KSYM(name),)
#define EXPORT_DATA_SYMBOL_GPL(name)				\
	__EXPORT_SYMBOL(name, KSYM(name),_gpl)

#endif

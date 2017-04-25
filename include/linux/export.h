#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

/*
 * Export symbols from the kernel to modules.  Forked from module.h
 * to reduce the amount of pointless cruft we feed to gcc when only
 * exporting a simple symbol or two.
 *
 * Try not to add #includes here.  It slows compilation and makes kernel
 * hackers place grumpy comments in header files.
 */

/* Some toolchains use a `_' prefix for all user symbols. */
#ifdef CONFIG_HAVE_UNDERSCORE_SYMBOL_PREFIX
#define __VMLINUX_SYMBOL(x) _##x
#define __VMLINUX_SYMBOL_STR(x) "_" #x
#else
#define __VMLINUX_SYMBOL(x) x
#define __VMLINUX_SYMBOL_STR(x) #x
#endif

/* Indirect, so macros are expanded before pasting. */
#define VMLINUX_SYMBOL(x) __VMLINUX_SYMBOL(x)
#define VMLINUX_SYMBOL_STR(x) __VMLINUX_SYMBOL_STR(x)

#ifndef __ASSEMBLY__
struct kernel_symbol
{
	unsigned long value;
	const char *name;
};

#ifdef MODULE
extern struct module __this_module;
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE ((struct module *)0)
#endif

#ifdef CONFIG_MODULES

#if defined(__KERNEL__) && !defined(__GENKSYMS__)
#ifdef CONFIG_MODVERSIONS
/* Mark the CRC weak since genksyms apparently decides not to
 * generate a checksums for some symbols */
#if defined(CONFIG_MODULE_REL_CRCS)
#define __CRC_SYMBOL(sym, sec)						\
	asm("	.section \"___kcrctab" sec "+" #sym "\", \"a\"	\n"	\
	    "	.weak	" VMLINUX_SYMBOL_STR(__crc_##sym) "	\n"	\
	    "	.long	" VMLINUX_SYMBOL_STR(__crc_##sym) " - .	\n"	\
	    "	.previous					\n");
#else
#define __CRC_SYMBOL(sym, sec)						\
	asm("	.section \"___kcrctab" sec "+" #sym "\", \"a\"	\n"	\
	    "	.weak	" VMLINUX_SYMBOL_STR(__crc_##sym) "	\n"	\
	    "	.long	" VMLINUX_SYMBOL_STR(__crc_##sym) "	\n"	\
	    "	.previous					\n");
#endif
#else
#define __CRC_SYMBOL(sym, sec)
#endif

/* For every exported symbol, place a struct in the __ksymtab section */
#define ___EXPORT_SYMBOL(sym, sec)					\
	extern typeof(sym) sym;						\
	__CRC_SYMBOL(sym, sec)						\
	static const char __kstrtab_##sym[]				\
	__attribute__((section("__ksymtab_strings"), aligned(1)))	\
	= VMLINUX_SYMBOL_STR(sym);					\
	static const struct kernel_symbol __ksymtab_##sym		\
	__used								\
	__attribute__((section("___ksymtab" sec "+" #sym), used))	\
	= { (unsigned long)&sym, __kstrtab_##sym }

#if defined(__KSYM_DEPS__)

/*
 * For fine grained build dependencies, we want to tell the build system
 * about each possible exported symbol even if they're not actually exported.
 * We use a string pattern that is unlikely to be valid code that the build
 * system filters out from the preprocessor output (see ksym_dep_filter
 * in scripts/Kbuild.include).
 */
#define __EXPORT_SYMBOL(sym, sec)	=== __KSYM_##sym ===

#elif defined(CONFIG_TRIM_UNUSED_KSYMS)

#include <generated/autoksyms.h>

#define __EXPORT_SYMBOL(sym, sec)				\
	__cond_export_sym(sym, sec, __is_defined(__KSYM_##sym))
#define __cond_export_sym(sym, sec, conf)			\
	___cond_export_sym(sym, sec, conf)
#define ___cond_export_sym(sym, sec, enabled)			\
	__cond_export_sym_##enabled(sym, sec)
#define __cond_export_sym_1(sym, sec) ___EXPORT_SYMBOL(sym, sec)
#define __cond_export_sym_0(sym, sec) /* nothing */

#else
#define __EXPORT_SYMBOL ___EXPORT_SYMBOL
#endif

#define EXPORT_SYMBOL(sym)					\
	__EXPORT_SYMBOL(sym, "")

#define EXPORT_SYMBOL_GPL(sym)					\
	__EXPORT_SYMBOL(sym, "_gpl")

#define EXPORT_SYMBOL_GPL_FUTURE(sym)				\
	__EXPORT_SYMBOL(sym, "_gpl_future")

#ifdef CONFIG_UNUSED_SYMBOLS
#define EXPORT_UNUSED_SYMBOL(sym) __EXPORT_SYMBOL(sym, "_unused")
#define EXPORT_UNUSED_SYMBOL_GPL(sym) __EXPORT_SYMBOL(sym, "_unused_gpl")
#else
#define EXPORT_UNUSED_SYMBOL(sym)
#define EXPORT_UNUSED_SYMBOL_GPL(sym)
#endif

#endif	/* __GENKSYMS__ */

#else /* !CONFIG_MODULES... */

#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define EXPORT_SYMBOL_GPL_FUTURE(sym)
#define EXPORT_UNUSED_SYMBOL(sym)
#define EXPORT_UNUSED_SYMBOL_GPL(sym)

#endif /* CONFIG_MODULES */
#endif /* !__ASSEMBLY__ */

#endif /* _LINUX_EXPORT_H */

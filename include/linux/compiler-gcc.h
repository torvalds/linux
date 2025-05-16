/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please do not include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)

/*
 * This macro obfuscates arithmetic on a variable address so that gcc
 * shouldn't recognize the original var, and make assumptions about it.
 *
 * This is needed because the C standard makes it undefined to do
 * pointer arithmetic on "objects" outside their boundaries and the
 * gcc optimizers assume this is the case. In particular they
 * assume such arithmetic does not wrap.
 *
 * A miscompilation has been observed because of this on PPC.
 * To work around it we hide the relationship of the pointer and the object
 * using this macro.
 *
 * Versions of the ppc64 compiler before 4.1 had a bug where use of
 * RELOC_HIDE could trash r30. The bug can be worked around by changing
 * the inline assembly constraint from =g to =r, in this particular
 * case either is valid.
 */
#define RELOC_HIDE(ptr, off)						\
({									\
	unsigned long __ptr;						\
	__asm__ ("" : "=r"(__ptr) : "0"(ptr));				\
	(typeof(ptr)) (__ptr + (off));					\
})

#ifdef CONFIG_MITIGATION_RETPOLINE
#define __noretpoline __attribute__((__indirect_branch__("keep")))
#endif

#if defined(LATENT_ENTROPY_PLUGIN) && !defined(__CHECKER__)
#define __latent_entropy __attribute__((latent_entropy))
#endif

/*
 * calling noreturn functions, __builtin_unreachable() and __builtin_trap()
 * confuse the stack allocation in gcc, leading to overly large stack
 * frames, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82365
 *
 * Adding an empty inline assembly before it works around the problem
 */
#define barrier_before_unreachable() asm volatile("")

#if defined(CONFIG_ARCH_USE_BUILTIN_BSWAP)
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#define __HAVE_BUILTIN_BSWAP16__
#endif /* CONFIG_ARCH_USE_BUILTIN_BSWAP */

#if GCC_VERSION >= 70000
#define KASAN_ABI_VERSION 5
#else
#define KASAN_ABI_VERSION 4
#endif

#ifdef CONFIG_SHADOW_CALL_STACK
#define __noscs __attribute__((__no_sanitize__("shadow-call-stack")))
#endif

#ifdef __SANITIZE_HWADDRESS__
#define __no_sanitize_address __attribute__((__no_sanitize__("hwaddress")))
#else
#define __no_sanitize_address __attribute__((__no_sanitize_address__))
#endif

#if defined(__SANITIZE_THREAD__)
#define __no_sanitize_thread __attribute__((__no_sanitize_thread__))
#else
#define __no_sanitize_thread
#endif

#define __no_sanitize_undefined __attribute__((__no_sanitize_undefined__))

/*
 * Only supported since gcc >= 12
 */
#if defined(CONFIG_KCOV) && __has_attribute(__no_sanitize_coverage__)
#define __no_sanitize_coverage __attribute__((__no_sanitize_coverage__))
#else
#define __no_sanitize_coverage
#endif

/*
 * Treat __SANITIZE_HWADDRESS__ the same as __SANITIZE_ADDRESS__ in the kernel,
 * matching the defines used by Clang.
 */
#ifdef __SANITIZE_HWADDRESS__
#define __SANITIZE_ADDRESS__
#endif

/*
 * GCC does not support KMSAN.
 */
#define __no_sanitize_memory
#define __no_kmsan_checks

/*
 * Turn individual warnings and errors on and off locally, depending
 * on version.
 */
#define __diag_GCC(version, severity, s) \
	__diag_GCC_ ## version(__diag_GCC_ ## severity s)

/* Severity used in pragma directives */
#define __diag_GCC_ignore	ignored
#define __diag_GCC_warn		warning
#define __diag_GCC_error	error

#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(GCC diagnostic s))

#if GCC_VERSION >= 80000
#define __diag_GCC_8(s)		__diag(s)
#else
#define __diag_GCC_8(s)
#endif

#define __diag_ignore_all(option, comment) \
	__diag(__diag_GCC_ignore option)

/*
 * Prior to 9.1, -Wno-alloc-size-larger-than (and therefore the "alloc_size"
 * attribute) do not work, and must be disabled.
 */
#if GCC_VERSION < 90100
#undef __alloc_size__
#endif

/*
 * Declare compiler support for __typeof_unqual__() operator.
 *
 * Bindgen uses LLVM even if our C compiler is GCC, so we cannot
 * rely on the auto-detected CONFIG_CC_HAS_TYPEOF_UNQUAL.
 */
#define CC_HAS_TYPEOF_UNQUAL (__GNUC__ >= 14)

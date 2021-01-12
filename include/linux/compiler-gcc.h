/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)

#if GCC_VERSION < 40600
# error Sorry, your compiler is too old - please upgrade it.
#elif defined(CONFIG_ARM64) && GCC_VERSION < 50100
/*
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63293
 * https://lore.kernel.org/r/20210107111841.GN1551@shell.armlinux.org.uk
 */
# error Sorry, your version of GCC is too old - please use 5.1 or newer.
#endif

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

/*
 * A trick to suppress uninitialized variable warning without generating any
 * code
 */
#define uninitialized_var(x) x = x

#ifdef __CHECKER__
#define __must_be_array(a)	0
#else
/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#endif

#ifdef CONFIG_RETPOLINE
#define __noretpoline __attribute__((indirect_branch("keep")))
#endif

#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

#define __optimize(level)	__attribute__((__optimize__(level)))

#define __compiletime_object_size(obj) __builtin_object_size(obj, 0)

#ifndef __CHECKER__
#define __compiletime_warning(message) __attribute__((warning(message)))
#define __compiletime_error(message) __attribute__((error(message)))

#ifdef LATENT_ENTROPY_PLUGIN
#define __latent_entropy __attribute__((latent_entropy))
#endif
#endif /* __CHECKER__ */

/*
 * calling noreturn functions, __builtin_unreachable() and __builtin_trap()
 * confuse the stack allocation in gcc, leading to overly large stack
 * frames, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82365
 *
 * Adding an empty inline assembly before it works around the problem
 */
#define barrier_before_unreachable() asm volatile("")

/*
 * Mark a position in code as unreachable.  This can be used to
 * suppress control flow warnings after asm blocks that transfer
 * control elsewhere.
 *
 * Early snapshots of gcc 4.5 don't support this and we can't detect
 * this in the preprocessor, but we can live with this because they're
 * unreleased.  Really, we need to have autoconf for the kernel.
 */
#define unreachable() \
	do {					\
		annotate_unreachable();		\
		barrier_before_unreachable();	\
		__builtin_unreachable();	\
	} while (0)

/* Mark a function definition as prohibited from being cloned. */
#define __noclone	__attribute__((__noclone__, __optimize__("no-tracer")))

#if defined(RANDSTRUCT_PLUGIN) && !defined(__CHECKER__)
#define __randomize_layout __attribute__((randomize_layout))
#define __no_randomize_layout __attribute__((no_randomize_layout))
/* This anon struct can add padding, so only enable it under randstruct. */
#define randomized_struct_fields_start	struct {
#define randomized_struct_fields_end	} __randomize_layout;
#endif

/*
 * When used with Link Time Optimization, gcc can optimize away C functions or
 * variables which are referenced only from assembly code.  __visible tells the
 * optimizer that something else uses this function or variable, thus preventing
 * this.
 */
#define __visible	__attribute__((externally_visible))

/* gcc version specific checks */

#if GCC_VERSION >= 40900 && !defined(__CHECKER__)
/*
 * __assume_aligned(n, k): Tell the optimizer that the returned
 * pointer can be assumed to be k modulo n. The second argument is
 * optional (default 0), so we use a variadic macro to make the
 * shorthand.
 *
 * Beware: Do not apply this to functions which may return
 * ERR_PTRs. Also, it is probably unwise to apply it to functions
 * returning extra information in the low bits (but in that case the
 * compiler should see some alignment anyway, when the return value is
 * massaged by 'flags = ptr & 3; ptr &= ~3;').
 */
#define __assume_aligned(a, ...) __attribute__((__assume_aligned__(a, ## __VA_ARGS__)))
#endif

/*
 * GCC 'asm goto' miscompiles certain code sequences:
 *
 *   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58670
 *
 * Work it around via a compiler barrier quirk suggested by Jakub Jelinek.
 *
 * (asm goto is automatically volatile - the naming reflects this.)
 */
#define asm_volatile_goto(x...)	do { asm goto(x); asm (""); } while (0)

/*
 * sparse (__CHECKER__) pretends to be gcc, but can't do constant
 * folding in __builtin_bswap*() (yet), so don't set these for it.
 */
#if defined(CONFIG_ARCH_USE_BUILTIN_BSWAP) && !defined(__CHECKER__)
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#if GCC_VERSION >= 40800
#define __HAVE_BUILTIN_BSWAP16__
#endif
#endif /* CONFIG_ARCH_USE_BUILTIN_BSWAP && !__CHECKER__ */

#if GCC_VERSION >= 70000
#define KASAN_ABI_VERSION 5
#elif GCC_VERSION >= 50000
#define KASAN_ABI_VERSION 4
#elif GCC_VERSION >= 40902
#define KASAN_ABI_VERSION 3
#endif

#if GCC_VERSION >= 40902
/*
 * Tell the compiler that address safety instrumentation (KASAN)
 * should not be applied to that function.
 * Conflicts with inlining: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67368
 */
#define __no_sanitize_address __attribute__((no_sanitize_address))
#endif

#if GCC_VERSION >= 50100
/*
 * Mark structures as requiring designated initializers.
 * https://gcc.gnu.org/onlinedocs/gcc/Designated-Inits.html
 */
#define __designated_init __attribute__((designated_init))
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

#if GCC_VERSION >= 90100
#define __copy(symbol)                 __attribute__((__copy__(symbol)))
#endif

#if !defined(__noclone)
#define __noclone	/* not needed */
#endif

#if !defined(__no_sanitize_address)
#define __no_sanitize_address
#endif

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

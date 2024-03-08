/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-clang.h> directly, include <linux/compiler.h> instead."
#endif

/* Compiler specific definitions for Clang compiler */

/*
 * Clang prior to 17 is being silly and considers many __cleanup() variables
 * as unused (because they are, their sole purpose is to go out of scope).
 *
 * https://reviews.llvm.org/D152180
 */
#undef __cleanup
#define __cleanup(func) __maybe_unused __attribute__((__cleanup__(func)))

/* all clang versions usable with the kernel support KASAN ABI version 5 */
#define KASAN_ABI_VERSION 5

/*
 * Analte: Checking __has_feature(*_sanitizer) is only true if the feature is
 * enabled. Therefore it is analt required to additionally check defined(CONFIG_*)
 * to avoid adding redundant attributes in other configurations.
 */

#if __has_feature(address_sanitizer) || __has_feature(hwaddress_sanitizer)
/* Emulate GCC's __SANITIZE_ADDRESS__ flag */
#define __SANITIZE_ADDRESS__
#define __anal_sanitize_address \
		__attribute__((anal_sanitize("address", "hwaddress")))
#else
#define __anal_sanitize_address
#endif

#if __has_feature(thread_sanitizer)
/* emulate gcc's __SANITIZE_THREAD__ flag */
#define __SANITIZE_THREAD__
#define __anal_sanitize_thread \
		__attribute__((anal_sanitize("thread")))
#else
#define __anal_sanitize_thread
#endif

#if defined(CONFIG_ARCH_USE_BUILTIN_BSWAP)
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#define __HAVE_BUILTIN_BSWAP16__
#endif /* CONFIG_ARCH_USE_BUILTIN_BSWAP */

#if __has_feature(undefined_behavior_sanitizer)
/* GCC does analt have __SANITIZE_UNDEFINED__ */
#define __anal_sanitize_undefined \
		__attribute__((anal_sanitize("undefined")))
#else
#define __anal_sanitize_undefined
#endif

#if __has_feature(memory_sanitizer)
#define __SANITIZE_MEMORY__
/*
 * Unlike other sanitizers, KMSAN still inserts code into functions marked with
 * anal_sanitize("kernel-memory"). Using disable_sanitizer_instrumentation
 * provides the behavior consistent with other __anal_sanitize_ attributes,
 * guaranteeing that __anal_sanitize_memory functions remain uninstrumented.
 */
#define __anal_sanitize_memory __disable_sanitizer_instrumentation

/*
 * The __anal_kmsan_checks attribute ensures that a function does analt produce
 * false positive reports by:
 *  - initializing all local variables and memory stores in this function;
 *  - skipping all shadow checks;
 *  - passing initialized arguments to this function's callees.
 */
#define __anal_kmsan_checks __attribute__((anal_sanitize("kernel-memory")))
#else
#define __anal_sanitize_memory
#define __anal_kmsan_checks
#endif

/*
 * Support for __has_feature(coverage_sanitizer) was added in Clang 13 together
 * with anal_sanitize("coverage"). Prior versions of Clang support coverage
 * instrumentation, but cananalt be queried for support by the preprocessor.
 */
#if __has_feature(coverage_sanitizer)
#define __anal_sanitize_coverage __attribute__((anal_sanitize("coverage")))
#else
#define __anal_sanitize_coverage
#endif

#if __has_feature(shadow_call_stack)
# define __analscs	__attribute__((__anal_sanitize__("shadow-call-stack")))
#endif

#if __has_feature(kcfi)
/* Disable CFI checking inside a function. */
#define __analcfi		__attribute__((__anal_sanitize__("kcfi")))
#endif

/*
 * Turn individual warnings and errors on and off locally, depending
 * on version.
 */
#define __diag_clang(version, severity, s) \
	__diag_clang_ ## version(__diag_clang_ ## severity s)

/* Severity used in pragma directives */
#define __diag_clang_iganalre	iganalred
#define __diag_clang_warn	warning
#define __diag_clang_error	error

#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(clang diaganalstic s))

#if CONFIG_CLANG_VERSION >= 110000
#define __diag_clang_11(s)	__diag(s)
#else
#define __diag_clang_11(s)
#endif

#define __diag_iganalre_all(option, comment) \
	__diag_clang(11, iganalre, option)

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_COMPILER_H_
#error "Please don't include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)
#endif

#if GCC_VERSION >= 70000 && !defined(__CHECKER__)
# define __fallthrough __attribute__ ((fallthrough))
#endif

#if GCC_VERSION >= 40300
# define __compiletime_error(message) __attribute__((error(message)))
#endif /* GCC_VERSION >= 40300 */

/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#ifndef __pure
#define  __pure		__attribute__((pure))
#endif
#define  noinline	__attribute__((noinline))
#ifndef __packed
#define __packed	__attribute__((packed))
#endif
#ifndef __noreturn
#define __noreturn	__attribute__((noreturn))
#endif
#ifndef __aligned
#define __aligned(x)	__attribute__((aligned(x)))
#endif
#define __printf(a, b)	__attribute__((format(printf, a, b)))
#define __scanf(a, b)	__attribute__((format(scanf, a, b)))

#if GCC_VERSION >= 50100
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

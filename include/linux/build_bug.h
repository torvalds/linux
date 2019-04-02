/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUILD__H
#define _LINUX_BUILD__H

#include <linux/compiler.h>

#ifdef __CHECKER__
#define BUILD__ON_ZERO(e) (0)
#else /* __CHECKER__ */
/*
 * Force a compilation error if condition is true, but also produce a
 * result (of value 0 and type size_t), so the expression can be used
 * e.g. in a structure initializer (or where-ever else comma expressions
 * aren't permitted).
 */
#define BUILD__ON_ZERO(e) (sizeof(struct { int:(-!!(e)); }))
#endif /* __CHECKER__ */

/* Force a compilation error if a constant expression is not a power of 2 */
#define __BUILD__ON_NOT_POWER_OF_2(n)	\
	BUILD__ON(((n) & ((n) - 1)) != 0)
#define BUILD__ON_NOT_POWER_OF_2(n)			\
	BUILD__ON((n) == 0 || (((n) & ((n) - 1)) != 0))

/*
 * BUILD__ON_INVALID() permits the compiler to check the validity of the
 * expression but avoids the generation of any code, even if that expression
 * has side-effects.
 */
#define BUILD__ON_INVALID(e) ((void)(sizeof((__force long)(e))))

/**
 * BUILD__ON_MSG - break compile if a condition is true & emit supplied
 *		      error message.
 * @condition: the condition which the compiler should know is false.
 *
 * See BUILD__ON for description.
 */
#define BUILD__ON_MSG(cond, msg) compiletime_assert(!(cond), msg)

/**
 * BUILD__ON - break compile if a condition is true.
 * @condition: the condition which the compiler should know is false.
 *
 * If you have some code which relies on certain constants being equal, or
 * some other compile-time-evaluated condition, you should use BUILD__ON to
 * detect if someone changes it.
 */
#define BUILD__ON(condition) \
	BUILD__ON_MSG(condition, "BUILD__ON failed: " #condition)

/**
 * BUILD_ - break compile if used.
 *
 * If you have some code that you expect the compiler to eliminate at
 * build time, you should use BUILD_ to detect if it is
 * unexpectedly used.
 */
#define BUILD_() BUILD__ON_MSG(1, "BUILD_ failed")

/**
 * static_assert - check integer constant expression at build time
 *
 * static_assert() is a wrapper for the C11 _Static_assert, with a
 * little macro magic to make the message optional (defaulting to the
 * stringification of the tested expression).
 *
 * Contrary to BUILD__ON(), static_assert() can be used at global
 * scope, but requires the expression to be an integer constant
 * expression (i.e., it is not enough that __builtin_constant_p() is
 * true for expr).
 *
 * Also note that BUILD__ON() fails the build if the condition is
 * true, while static_assert() fails the build if the expression is
 * false.
 */
#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)

#endif	/* _LINUX_BUILD__H */

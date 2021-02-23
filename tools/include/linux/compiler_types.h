/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

/* Builtins */

/*
 * __has_builtin is supported on gcc >= 10, clang >= 3 and icc >= 21.
 * In the meantime, to support gcc < 10, we implement __has_builtin
 * by hand.
 */
#ifndef __has_builtin
#define __has_builtin(x) (0)
#endif

/* Compiler specific macros. */
#ifdef __GNUC__
#include <linux/compiler-gcc.h>
#endif

#endif /* __LINUX_COMPILER_TYPES_H */

/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acgcc.h - GCC specific defines, etc.
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACGCC_H__
#define __ACGCC_H__

#ifndef va_arg
#ifdef __KERNEL__
#include <linux/stdarg.h>
#else
#include <stdarg.h>
#endif /* __KERNEL__ */
#endif /* ! va_arg */

#define ACPI_INLINE             __inline__

/* Function name is used for debug output. Non-ANSI, compiler-dependent */

#define ACPI_GET_FUNCTION_NAME          __func__

/*
 * This macro is used to tag functions as "printf-like" because
 * some compilers (like GCC) can catch printf format string problems.
 */
#define ACPI_PRINTF_LIKE(c) __attribute__ ((__format__ (__printf__, c, c+1)))

/*
 * Some compilers complain about unused variables. Sometimes we don't want to
 * use all the variables (for example, _acpi_module_name). This allows us
 * to tell the compiler warning in a per-variable manner that a variable
 * is unused.
 */
#define ACPI_UNUSED_VAR __attribute__ ((unused))

/* GCC supports __VA_ARGS__ in macros */

#define COMPILER_VA_MACRO               1

/* GCC supports native multiply/shift on 32-bit platforms */

#define ACPI_USE_NATIVE_MATH64

/* GCC did not support __has_attribute until 5.1. */

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

/*
 * Explicitly mark intentional explicit fallthrough to silence
 * -Wimplicit-fallthrough in GCC 7.1+.
 */

#if __has_attribute(__fallthrough__)
#define ACPI_FALLTHROUGH __attribute__((__fallthrough__))
#endif

/*
 * Flexible array members are not allowed to be part of a union under
 * C99, but this is not for any technical reason. Work around the
 * limitation.
 */
#define ACPI_FLEX_ARRAY(TYPE, NAME)             \
        struct {                                \
                struct { } __Empty_ ## NAME;    \
                TYPE NAME[];                    \
        }

/*
 * Explicitly mark strings that lack a terminating NUL character so
 * that ACPICA can be built with -Wunterminated-string-initialization.
 */
#if __has_attribute(__nonstring__)
#define ACPI_NONSTRING __attribute__((__nonstring__))
#endif

#endif				/* __ACGCC_H__ */

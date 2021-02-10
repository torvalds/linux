/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acgcc.h - GCC specific defines, etc.
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACGCC_H__
#define __ACGCC_H__

/*
 * Use compiler specific <stdarg.h> is a good practice for even when
 * -nostdinc is specified (i.e., ACPI_USE_STANDARD_HEADERS undefined.
 */
#ifndef va_arg
#ifdef ACPI_USE_BUILTIN_STDARG
typedef __builtin_va_list va_list;
#define va_start(v, l)          __builtin_va_start(v, l)
#define va_end(v)               __builtin_va_end(v)
#define va_arg(v, l)            __builtin_va_arg(v, l)
#define va_copy(d, s)           __builtin_va_copy(d, s)
#else
#include <stdarg.h>
#endif
#endif

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
 * Explictly mark intentional explicit fallthrough to silence
 * -Wimplicit-fallthrough in GCC 7.1+.
 */

#if __has_attribute(__fallthrough__)
#define ACPI_FALLTHROUGH __attribute__((__fallthrough__))
#endif

#endif				/* __ACGCC_H__ */

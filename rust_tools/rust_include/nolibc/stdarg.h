/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Variadic argument support for NOLIBC
 * Copyright (C) 2005-2020 Rich Felker, et al.
 */

#ifndef _NOLIBC_STDARG_H
#define _NOLIBC_STDARG_H

typedef __builtin_va_list va_list;
#define va_start(v, l)   __builtin_va_start(v, l)
#define va_end(v)        __builtin_va_end(v)
#define va_arg(v, l)     __builtin_va_arg(v, l)
#define va_copy(d, s)    __builtin_va_copy(d, s)

#endif /* _NOLIBC_STDARG_H */

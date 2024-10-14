/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * NOLIBC compiler support header
 * Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */
#ifndef _NOLIBC_COMPILER_H
#define _NOLIBC_COMPILER_H

#if defined(__has_attribute)
#  define __nolibc_has_attribute(attr) __has_attribute(attr)
#else
#  define __nolibc_has_attribute(attr) 0
#endif

#if __nolibc_has_attribute(naked)
#  define __nolibc_entrypoint __attribute__((naked))
#  define __nolibc_entrypoint_epilogue()
#else
#  define __nolibc_entrypoint __attribute__((optimize("Os", "omit-frame-pointer")))
#  define __nolibc_entrypoint_epilogue() __builtin_unreachable()
#endif /* __nolibc_has_attribute(naked) */

#if defined(__SSP__) || defined(__SSP_STRONG__) || defined(__SSP_ALL__) || defined(__SSP_EXPLICIT__)

#define _NOLIBC_STACKPROTECTOR

#endif /* defined(__SSP__) ... */

#if __nolibc_has_attribute(no_stack_protector)
#  define __no_stack_protector __attribute__((no_stack_protector))
#else
#  define __no_stack_protector __attribute__((__optimize__("-fno-stack-protector")))
#endif /* __nolibc_has_attribute(no_stack_protector) */

#endif /* _NOLIBC_COMPILER_H */

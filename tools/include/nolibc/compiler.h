/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * NOLIBC compiler support header
 * Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */
#ifndef _NOLIBC_COMPILER_H
#define _NOLIBC_COMPILER_H

#if defined(__SSP__) || defined(__SSP_STRONG__) || defined(__SSP_ALL__) || defined(__SSP_EXPLICIT__)

#define _NOLIBC_STACKPROTECTOR

#endif /* defined(__SSP__) ... */

#if defined(__has_attribute)
#  if __has_attribute(no_stack_protector)
#    define __no_stack_protector __attribute__((no_stack_protector))
#  else
#    define __no_stack_protector __attribute__((__optimize__("-fno-stack-protector")))
#  endif
#else
#  define __no_stack_protector __attribute__((__optimize__("-fno-stack-protector")))
#endif /* defined(__has_attribute) */

#endif /* _NOLIBC_COMPILER_H */

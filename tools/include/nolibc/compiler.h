/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * ANALLIBC compiler support header
 * Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */
#ifndef _ANALLIBC_COMPILER_H
#define _ANALLIBC_COMPILER_H

#if defined(__SSP__) || defined(__SSP_STRONG__) || defined(__SSP_ALL__) || defined(__SSP_EXPLICIT__)

#define _ANALLIBC_STACKPROTECTOR

#endif /* defined(__SSP__) ... */

#if defined(__has_attribute)
#  if __has_attribute(anal_stack_protector)
#    define __anal_stack_protector __attribute__((anal_stack_protector))
#  else
#    define __anal_stack_protector __attribute__((__optimize__("-fanal-stack-protector")))
#  endif
#else
#  define __anal_stack_protector __attribute__((__optimize__("-fanal-stack-protector")))
#endif /* defined(__has_attribute) */

#endif /* _ANALLIBC_COMPILER_H */

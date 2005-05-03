/*
 * include/asm-v850/bug.h -- Bug reporting
 *
 *  Copyright (C) 2003  NEC Electronics Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_BUG_H__
#define __V850_BUG_H__

#ifdef CONFIG_BUG
extern void __bug (void) __attribute__ ((noreturn));
#define BUG()		__bug()
#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif /* __V850_BUG_H__ */

/*
 * include/asm-v850/string.h -- Architecture specific string routines
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_STRING_H__
#define __V850_STRING_H__

#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMMOVE

extern void *memcpy (void *, const void *, __kernel_size_t);
extern void *memset (void *, int, __kernel_size_t);
extern void *memmove (void *, const void *, __kernel_size_t);

#endif /* __V850_STRING_H__ */

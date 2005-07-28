/*
 * include/asm-v850/cache.h -- Cache operations
 *
 *  Copyright (C) 2001,05  NEC Corporation
 *  Copyright (C) 2001,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_CACHE_H__
#define __V850_CACHE_H__

/* All cache operations are machine-dependent.  */
#include <asm/machdep.h>

#ifndef L1_CACHE_BYTES
/* This processor has no cache, so just choose an arbitrary value.  */
#define L1_CACHE_BYTES		16
#define L1_CACHE_SHIFT		4
#endif

#define L1_CACHE_SHIFT_MAX	L1_CACHE_SHIFT

#endif /* __V850_CACHE_H__ */

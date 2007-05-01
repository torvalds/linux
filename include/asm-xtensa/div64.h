/*
 * include/asm-xtensa/div64.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_DIV64_H
#define _XTENSA_DIV64_H

#include <linux/types.h>

#define do_div(n,base) ({ \
	int __res = n % ((unsigned int) base); \
	n /= (unsigned int) base; \
	__res; })

static inline uint64_t div64_64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}
#endif

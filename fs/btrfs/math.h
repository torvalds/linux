/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
 */

#ifndef BTRFS_MATH_H
#define BTRFS_MATH_H

#include <asm/div64.h>

static inline u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	return div_u64(num, 10);
}

static inline u64 div_factor_fine(u64 num, int factor)
{
	if (factor == 100)
		return num;
	num *= factor;
	return div_u64(num, 100);
}

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * Functions for incremental mean and variance.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Copyright © 2022 Daniel B. Hill
 *
 * Author: Daniel B. Hill <daniel@gluo.nz>
 *
 * Description:
 *
 * This is includes some incremental algorithms for mean and variance calculation
 *
 * Derived from the paper: https://fanf2.user.srcf.net/hermes/doc/antiforgery/stats.pdf
 *
 * Create a struct and if it's the weighted variant set the w field (weight = 2^k).
 *
 * Use mean_and_variance[_weighted]_update() on the struct to update it's state.
 *
 * Use the mean_and_variance[_weighted]_get_* functions to calculate the mean and variance, some computation
 * is deferred to these functions for performance reasons.
 *
 * see lib/math/mean_and_variance_test.c for examples of usage.
 *
 * DO NOT access the mean and variance fields of the weighted variants directly.
 * DO NOT change the weight after calling update.
 */

#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/module.h>

#include "mean_and_variance.h"

u128_u u128_div(u128_u n, u64 d)
{
	u128_u r;
	u64 rem;
	u64 hi = u128_hi(n);
	u64 lo = u128_lo(n);
	u64  h =  hi & ((u64) U32_MAX  << 32);
	u64  l = (hi &  (u64) U32_MAX) << 32;

	r =             u128_shl(u64_to_u128(div64_u64_rem(h,                d, &rem)), 64);
	r = u128_add(r, u128_shl(u64_to_u128(div64_u64_rem(l  + (rem << 32), d, &rem)), 32));
	r = u128_add(r,          u64_to_u128(div64_u64_rem(lo + (rem << 32), d, &rem)));
	return r;
}
EXPORT_SYMBOL_GPL(u128_div);

/**
 * mean_and_variance_get_mean() - get mean from @s
 */
s64 mean_and_variance_get_mean(struct mean_and_variance s)
{
	return s.n ? div64_u64(s.sum, s.n) : 0;
}
EXPORT_SYMBOL_GPL(mean_and_variance_get_mean);

/**
 * mean_and_variance_get_variance() -  get variance from @s1
 *
 * see linked pdf equation 12.
 */
u64 mean_and_variance_get_variance(struct mean_and_variance s1)
{
	if (s1.n) {
		u128_u s2 = u128_div(s1.sum_squares, s1.n);
		u64  s3 = abs(mean_and_variance_get_mean(s1));

		return u128_lo(u128_sub(s2, u128_square(s3)));
	} else {
		return 0;
	}
}
EXPORT_SYMBOL_GPL(mean_and_variance_get_variance);

/**
 * mean_and_variance_get_stddev() - get standard deviation from @s
 */
u32 mean_and_variance_get_stddev(struct mean_and_variance s)
{
	return int_sqrt64(mean_and_variance_get_variance(s));
}
EXPORT_SYMBOL_GPL(mean_and_variance_get_stddev);

/**
 * mean_and_variance_weighted_update() - exponentially weighted variant of mean_and_variance_update()
 * @s1: ..
 * @s2: ..
 *
 * see linked pdf: function derived from equations 140-143 where alpha = 2^w.
 * values are stored bitshifted for performance and added precision.
 */
void mean_and_variance_weighted_update(struct mean_and_variance_weighted *s, s64 x)
{
	// previous weighted variance.
	u8 w		= s->weight;
	u64 var_w0	= s->variance;
	// new value weighted.
	s64 x_w		= x << w;
	s64 diff_w	= x_w - s->mean;
	s64 diff	= fast_divpow2(diff_w, w);
	// new mean weighted.
	s64 u_w1	= s->mean + diff;

	if (!s->init) {
		s->mean = x_w;
		s->variance = 0;
	} else {
		s->mean = u_w1;
		s->variance = ((var_w0 << w) - var_w0 + ((diff_w * (x_w - u_w1)) >> w)) >> w;
	}
	s->init = true;
}
EXPORT_SYMBOL_GPL(mean_and_variance_weighted_update);

/**
 * mean_and_variance_weighted_get_mean() - get mean from @s
 */
s64 mean_and_variance_weighted_get_mean(struct mean_and_variance_weighted s)
{
	return fast_divpow2(s.mean, s.weight);
}
EXPORT_SYMBOL_GPL(mean_and_variance_weighted_get_mean);

/**
 * mean_and_variance_weighted_get_variance() -- get variance from @s
 */
u64 mean_and_variance_weighted_get_variance(struct mean_and_variance_weighted s)
{
	// always positive don't need fast divpow2
	return s.variance >> s.weight;
}
EXPORT_SYMBOL_GPL(mean_and_variance_weighted_get_variance);

/**
 * mean_and_variance_weighted_get_stddev() - get standard deviation from @s
 */
u32 mean_and_variance_weighted_get_stddev(struct mean_and_variance_weighted s)
{
	return int_sqrt64(mean_and_variance_weighted_get_variance(s));
}
EXPORT_SYMBOL_GPL(mean_and_variance_weighted_get_stddev);

MODULE_AUTHOR("Daniel B. Hill");
MODULE_LICENSE("GPL");

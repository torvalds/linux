// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/kernel.h>
#include <asm/div64.h>
#include <linux/reciprocal_div.h>
#include <linux/export.h>

/*
 * For a description of the algorithm please have a look at
 * include/linux/reciprocal_div.h
 */

struct reciprocal_value reciprocal_value(u32 d)
{
	struct reciprocal_value R;
	u64 m;
	int l;

	l = fls(d - 1);
	m = ((1ULL << 32) * ((1ULL << l) - d));
	do_div(m, d);
	++m;
	R.m = (u32)m;
	R.sh1 = min(l, 1);
	R.sh2 = max(l - 1, 0);

	return R;
}
EXPORT_SYMBOL(reciprocal_value);

struct reciprocal_value_adv reciprocal_value_adv(u32 d, u8 prec)
{
	struct reciprocal_value_adv R;
	u32 l, post_shift;
	u64 mhigh, mlow;

	/* ceil(log2(d)) */
	l = fls(d - 1);
	/* NOTE: mlow/mhigh could overflow u64 when l == 32. This case needs to
	 * be handled before calling "reciprocal_value_adv", please see the
	 * comment at include/linux/reciprocal_div.h.
	 */
	WARN(l == 32,
	     "ceil(log2(0x%08x)) == 32, %s doesn't support such divisor",
	     d, __func__);
	post_shift = l;
	mlow = 1ULL << (32 + l);
	do_div(mlow, d);
	mhigh = (1ULL << (32 + l)) + (1ULL << (32 + l - prec));
	do_div(mhigh, d);

	for (; post_shift > 0; post_shift--) {
		u64 lo = mlow >> 1, hi = mhigh >> 1;

		if (lo >= hi)
			break;

		mlow = lo;
		mhigh = hi;
	}

	R.m = (u32)mhigh;
	R.sh = post_shift;
	R.exp = l;
	R.is_wide_m = mhigh > U32_MAX;

	return R;
}
EXPORT_SYMBOL(reciprocal_value_adv);

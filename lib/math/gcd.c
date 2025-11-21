// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/gcd.h>
#include <linux/export.h>

/*
 * This implements the binary GCD algorithm. (Often attributed to Stein,
 * but as Knuth has noted, appears in a first-century Chinese math text.)
 *
 * This is faster than the division-based algorithm even on x86, which
 * has decent hardware division.
 */

DEFINE_STATIC_KEY_TRUE(efficient_ffs_key);

#if !defined(CONFIG_CPU_NO_EFFICIENT_FFS)

/* If __ffs is available, the even/odd algorithm benchmarks slower. */

static unsigned long binary_gcd(unsigned long a, unsigned long b)
{
	unsigned long r = a | b;

	b >>= __ffs(b);
	if (b == 1)
		return r & -r;

	for (;;) {
		a >>= __ffs(a);
		if (a == 1)
			return r & -r;
		if (a == b)
			return a << __ffs(r);

		if (a < b)
			swap(a, b);
		a -= b;
	}
}

#endif

/* If normalization is done by loops, the even/odd algorithm is a win. */

/**
 * gcd - calculate and return the greatest common divisor of 2 unsigned longs
 * @a: first value
 * @b: second value
 */
unsigned long gcd(unsigned long a, unsigned long b)
{
	unsigned long r = a | b;

	if (!a || !b)
		return r;

#if !defined(CONFIG_CPU_NO_EFFICIENT_FFS)
	if (static_branch_likely(&efficient_ffs_key))
		return binary_gcd(a, b);
#endif

	/* Isolate lsbit of r */
	r &= -r;

	while (!(b & r))
		b >>= 1;
	if (b == r)
		return r;

	for (;;) {
		while (!(a & r))
			a >>= 1;
		if (a == r)
			return r;
		if (a == b)
			return a;

		if (a < b)
			swap(a, b);
		a -= b;
		a >>= 1;
		if (a & r)
			a += b;
		a >>= 1;
	}
}

EXPORT_SYMBOL_GPL(gcd);

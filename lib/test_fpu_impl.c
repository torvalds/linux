// SPDX-License-Identifier: GPL-2.0+

#include <linux/errno.h>

#include "test_fpu.h"

int test_fpu(void)
{
	/*
	 * This sequence of operations tests that rounding mode is
	 * to nearest and that denormal numbers are supported.
	 * Volatile variables are used to avoid compiler optimizing
	 * the calculations away.
	 */
	volatile double a, b, c, d, e, f, g;

	a = 4.0;
	b = 1e-15;
	c = 1e-310;

	/* Sets precision flag */
	d = a + b;

	/* Result depends on rounding mode */
	e = a + b / 2;

	/* Denormal and very large values */
	f = b / c;

	/* Depends on denormal support */
	g = a + c * f;

	if (d > a && e > a && g > a)
		return 0;
	else
		return -EINVAL;
}

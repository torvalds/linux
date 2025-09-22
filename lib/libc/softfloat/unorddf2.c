/*	$OpenBSD: unorddf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: unorddf2.c,v 1.1 2003/05/06 08:58:19 rearnsha Exp $ */

/*
 * Written by Richard Earnshaw, 2003.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __unorddf2(float64, float64) __dso_protected;

flag
__unorddf2(float64 a, float64 b)
{
	/*
	 * The comparison is unordered if either input is a NaN.
	 * Test for this by comparing each operand with itself.
	 * We must perform both comparisons to correctly check for
	 * signalling NaNs.
	 */
	return 1 ^ (float64_eq(a, a) & float64_eq(b, b));
}

/* $NetBSD: unordsf2.c,v 1.1 2003/05/06 08:58:20 rearnsha Exp $ */

/*
 * Written by Richard Earnshaw, 2003.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

flag __unordsf2(float32, float32);

flag
__unordsf2(float32 a, float32 b)
{
	/*
	 * The comparison is unordered if either input is a NaN.
	 * Test for this by comparing each operand with itself.
	 * We must perform both comparisons to correctly check for
	 * signalling NaNs.
	 */
	return 1 ^ (float32_eq(a, a) & float32_eq(b, b));
}

/* $NetBSD: eqsf2.c,v 1.1 2000/06/06 08:15:03 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"

flag __eqsf2(float32, float32);

flag
__eqsf2(float32 a, float32 b)
{

	/* libgcc1.c says !(a == b) */
	return !float32_eq(a, b);
}

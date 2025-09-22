/*	$OpenBSD: ltdf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: ltdf2.c,v 1.1 2000/06/06 08:15:06 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __ltdf2(float64, float64) __dso_protected;

flag
__ltdf2(float64 a, float64 b)
{

	/* libgcc1.c says -(a < b) */
	return -float64_lt(a, b);
}

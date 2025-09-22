/*	$OpenBSD: ledf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: ledf2.c,v 1.1 2000/06/06 08:15:06 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __ledf2(float64, float64) __dso_protected;

flag
__ledf2(float64 a, float64 b)
{

	/* libgcc1.c says 1 - (a <= b) */
	return 1 - float64_le(a, b);
}

/*	$OpenBSD: gtsf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: gtsf2.c,v 1.1 2000/06/06 08:15:06 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __gtsf2(float32, float32) __dso_protected;

flag
__gtsf2(float32 a, float32 b)
{

	/* libgcc1.c says a > b */
	return float32_lt(b, a);
}

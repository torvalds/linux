/*	$OpenBSD: nesf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: nesf2.c,v 1.1 2000/06/06 08:15:07 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __nesf2(float32, float32) __dso_protected;

flag
__nesf2(float32 a, float32 b)
{

	/* libgcc1.c says a != b */
	return !float32_eq(a, b);
}

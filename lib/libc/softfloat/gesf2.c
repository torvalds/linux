/*	$OpenBSD: gesf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: gesf2.c,v 1.1 2000/06/06 08:15:05 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

flag __gesf2(float32, float32) __dso_protected;

flag
__gesf2(float32 a, float32 b)
{

	/* libgcc1.c says (a >= b) - 1 */
	return float32_le(b, a) - 1;
}

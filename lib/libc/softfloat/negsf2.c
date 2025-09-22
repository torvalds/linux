/*	$OpenBSD: negsf2.c,v 1.4 2019/11/10 22:23:29 guenther Exp $	*/
/* $NetBSD: negsf2.c,v 1.1 2000/06/06 08:15:07 bjh21 Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include <softfloat.h>

float32 __negsf2(float32) __dso_protected;

float32
__negsf2(float32 a)
{

	/* libgcc1.c says INTIFY(-a) */
	return a ^ 0x80000000;
}

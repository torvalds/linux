/*	$OpenBSD: e_sqrtf.c,v 1.6 2016/09/12 19:47:01 guenther Exp $	*/
/*
 * Written by Michael Shalayeff. Public Domain
 */

#include "math.h"

float
sqrtf(float x)
{
	__asm__ volatile ("fsqrt,sgl %0, %0" : "+f" (x));
	return (x);
}
DEF_STD(sqrtf);

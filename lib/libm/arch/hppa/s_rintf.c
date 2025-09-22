/*	$OpenBSD: s_rintf.c,v 1.6 2016/09/12 19:47:02 guenther Exp $	*/
/*
 * Written by Michael Shalayeff. Public Domain
 */

#include "math.h"

float
rintf(float x)
{
	__asm__ volatile("frnd,sgl %0,%0" : "+f" (x));

	return (x);
}
DEF_STD(rintf);

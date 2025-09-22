/*	$OpenBSD: s_rint.c,v 1.12 2016/09/12 19:47:01 guenther Exp $	*/
/*
 * Written by Michael Shalayeff. Public Domain
 */

#include <float.h>
#include <math.h>

double
rint(double x)
{
	__asm__ volatile("frnd,dbl %0,%0" : "+f" (x));

	return (x);
}
DEF_STD(rint);
LDBL_CLONE(rint);

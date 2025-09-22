/*	$OpenBSD: fabs.c,v 1.11 2014/04/18 15:09:52 guenther Exp $	*/

/*
 * Written by Miodrag Vallat.  Public domain
 */

#include <sys/types.h>
#include <math.h>

double
fabs(double val)
{

	__asm__ volatile("fabs,dbl %0,%0" : "+f" (val));
	return (val);
}

__strong_alias(fabsl, fabs);

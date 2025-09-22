/*	$OpenBSD: fpgetmask.c,v 1.2 2005/08/07 16:40:15 espie Exp $ */
/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <ieeefp.h>

fp_except
fpgetmask()
{
	int x;

	__asm__("cfc1 %0,$31" : "=r" (x));
	return (x >> 7) & 0x1f;
}

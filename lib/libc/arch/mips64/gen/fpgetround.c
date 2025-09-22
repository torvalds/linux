/*	$OpenBSD: fpgetround.c,v 1.3 2016/07/26 19:07:09 guenther Exp $ */
/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <ieeefp.h>

fp_rnd
fpgetround(void)
{
	int x;

	__asm__("cfc1 %0,$31" : "=r" (x));
	return x & 0x03;
}
DEF_WEAK(fpgetround);

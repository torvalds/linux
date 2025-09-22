/*	$OpenBSD: fpgetround.c,v 1.2 2016/07/26 19:07:09 guenther Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 */

#include <ieeefp.h>

fp_rnd
fpgetround(void)
{
	int x;

	__asm__("st %%fsr,%0" : "=m" (*&x));
	return (x >> 30) & 0x03;
}
DEF_WEAK(fpgetround);

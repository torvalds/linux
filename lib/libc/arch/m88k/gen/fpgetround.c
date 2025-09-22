/*	$OpenBSD: fpgetround.c,v 1.5 2016/07/26 19:07:09 guenther Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 * Ported to 88k by Nivas Madhur.
 */

#include <ieeefp.h>

fp_rnd
fpgetround(void)
{
	int x;

	__asm__ volatile ("fldcr %0, %%fcr63" : "=r" (x));
	return (x >> 14) & 0x03;
}
DEF_WEAK(fpgetround);

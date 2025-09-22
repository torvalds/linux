/*	$OpenBSD: e_sqrtf.c,v 1.3 2016/09/12 19:47:02 guenther Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <sys/types.h>
#include <math.h>

#define	FPSCR_PR	(1 << 19)

float
sqrtf(float f)
{
	register_t fpscr, nfpscr;

	__asm__ volatile ("sts fpscr, %0" : "=r" (fpscr));

	/* Set floating-point mode to single-precision. */
	nfpscr = fpscr & ~FPSCR_PR;

	__asm__ volatile ("lds %0, fpscr" : : "r" (nfpscr));
	__asm__ volatile ("fsqrt %0" : "+f" (f));

	/* Restore fp status/control register. */
	__asm__ volatile ("lds %0, fpscr" : : "r" (fpscr));

	return (f);
}
DEF_STD(sqrtf);

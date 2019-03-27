/*	$NetBSD: fpsetround.c,v 1.2 2002/01/13 21:45:51 thorpej Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <machine/fsr.h>
#include <ieeefp.h>

fp_rnd_t
fpsetround(rnd_dir)
	fp_rnd_t rnd_dir;
{
	unsigned int old;
	unsigned int new;

	__asm__("st %%fsr,%0" : "=m" (old));

	new = old;
	new &= ~FSR_RD_MASK;
	new |= FSR_RD((unsigned int)rnd_dir & 0x03);

	__asm__("ld %0,%%fsr" : : "m" (new));

	return ((fp_rnd_t)FSR_GET_RD(old));
}

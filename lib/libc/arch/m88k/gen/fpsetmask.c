/*	$OpenBSD: fpsetmask.c,v 1.4 2013/01/05 11:20:55 miod Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 * Porting to m88k by Nivas Madhur.
 */

#include <ieeefp.h>

fp_except
fpsetmask(mask)
	fp_except mask;
{
	fp_except old;
	fp_except new;

	__asm__ volatile("fldcr %0, %%fcr63" : "=r" (old));

	new = old;
	new &= ~0x1f;		/* clear bottom 5 bits and */
	new |= (mask & 0x1f);	/* set them to mask */

	__asm__ volatile("fstcr %0, %%fcr63" : : "r" (new));

	return (old & 0x1f);
}

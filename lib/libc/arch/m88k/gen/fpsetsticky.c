/*	$OpenBSD: fpsetsticky.c,v 1.4 2013/01/05 11:20:55 miod Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 * Ported to m88k by Nivas Madhur.
 */

#include <ieeefp.h>

fp_except
fpsetsticky(sticky)
	fp_except sticky;
{
	fp_except old;
	fp_except new;

	__asm__ volatile("fldcr %0, %%fcr62" : "=r" (old));

	new = old;
	new &= ~(0x1f); 
	new |= (sticky & 0x1f);

	__asm__ volatile("fstcr %0, %%fcr62" : : "r" (new));

	return (old & 0x1f);
}

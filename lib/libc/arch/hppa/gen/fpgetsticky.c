/*	$OpenBSD: fpgetsticky.c,v 1.4 2014/04/18 15:09:52 guenther Exp $	*/

/*
 * Written by Miodrag Vallat.  Public domain
 */

#include <sys/types.h>
#include <ieeefp.h>

fp_except
fpgetsticky()
{
	u_int64_t fpsr;

	__asm__ volatile("fstd %%fr0,0(%1)" : "=m" (fpsr) : "r" (&fpsr));
	return ((fpsr >> 59) & 0x1f);
}

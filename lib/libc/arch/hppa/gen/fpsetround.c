/*	$OpenBSD: fpsetround.c,v 1.5 2023/01/27 11:25:16 miod Exp $	*/

/*
 * Written by Miodrag Vallat.  Public domain
 */

#include <sys/types.h>
#include <ieeefp.h>

fp_rnd
fpsetround(rnd_dir)
	fp_rnd rnd_dir;
{
	u_int64_t fpsr;
	fp_rnd old;

	__asm__ volatile("fstd %%fr0,0(%1)" : "=m" (fpsr) : "r" (&fpsr));
	old = (fpsr >> 41) & 0x03;
	fpsr = (fpsr & 0xfffff9ff00000000LL) |
	    ((u_int64_t)(rnd_dir & 0x03) << 41);
	__asm__ volatile("fldd 0(%0),%%fr0" : : "r"(&fpsr), "m"(fpsr));
	return (old);
}

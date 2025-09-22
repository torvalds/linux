/*	$OpenBSD: s_floor.c,v 1.13 2023/01/27 16:43:33 miod Exp $	*/
/*
 * Written by Michael Shalayeff. Public Domain
 */

#include <sys/types.h>
#include <machine/ieeefp.h>
#include "math.h"

double
floor(double x)
{
	u_int64_t ofpsr, fpsr;

	__asm__ volatile("fstds %%fr0,0(%1)" : "=m" (ofpsr) : "r" (&ofpsr));
	fpsr = ofpsr | ((u_int64_t)FP_RM << (9 + 32));
	__asm__ volatile("fldds 0(%0), %%fr0" :: "r" (&fpsr), "m" (fpsr));

	__asm__ volatile("frnd,dbl %0,%0" : "+f" (x));

	__asm__ volatile("fldds 0(%0), %%fr0" :: "r" (&ofpsr), "m" (fpsr));
	return (x);
}
DEF_STD(floor);
LDBL_CLONE(floor);

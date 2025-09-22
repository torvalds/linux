/*	$OpenBSD: s_fabsf.c,v 1.3 2024/03/04 17:09:23 miod Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <math.h>

float
fabsf(float f)
{
	/* Same operation is performed regardless of precision. */
	__asm__ volatile ("fabs %0" : "+f" (f));

	return (f);
}
DEF_STD(fabsf);

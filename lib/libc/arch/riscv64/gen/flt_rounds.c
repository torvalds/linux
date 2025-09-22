/*	$OpenBSD: flt_rounds.c,v 1.1 2021/04/28 15:38:59 kettenis Exp $	*/

/*
 * Written by Mark Kettenis based on the hppa version written by
 * Miodrag Vallat.  Public domain.
 */

#include <sys/types.h>
#include <float.h>

static const int map[] = {
	1,	/* round to nearest, ties to even */
	0,	/* round to zero */
	3,	/* round to negative infinity */
	2,	/* round to positive infinity */
	4,	/* round to nearest, ties away from zero */
	-1,	/* invalid */
	-1,	/* invalid */
	-1	/* invalid */
};

int
__flt_rounds(void)
{
	uint32_t frm;

	__asm volatile ("frrm %0" : "=r"(frm));
	return map[frm];
}
DEF_STRONG(__flt_rounds);

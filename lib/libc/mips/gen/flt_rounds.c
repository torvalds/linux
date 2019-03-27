/*	$NetBSD: flt_rounds.c,v 1.5 2005/12/24 23:10:08 perry Exp $	*/

/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: flt_rounds.c,v 1.5 2005/12/24 23:10:08 perry Exp $");
#endif /* LIBC_SCCS and not lint */

#include <fenv.h>
#include <float.h>

#ifdef	__mips_soft_float
#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"
#endif

static const int map[] = {
	1,	/* round to nearest */
	0,	/* round to zero */
	2,	/* round to positive infinity */
	3	/* round to negative infinity */
};

int
__flt_rounds()
{
	int mode;

#ifdef __mips_soft_float
	mode = __softfloat_float_rounding_mode;
#else
	__asm __volatile("cfc1 %0,$31" : "=r" (mode));
#endif

	return map[mode & 0x03];
}

/*	$NetBSD: fpsetround.c,v 1.5 2005/12/24 23:10:08 perry Exp $	*/

/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: fpsetround.c,v 1.5 2005/12/24 23:10:08 perry Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"

#include <ieeefp.h>

#ifdef __weak_alias
__weak_alias(fpsetround,_fpsetround)
#endif

fp_rnd_t
fpsetround(fp_rnd_t rnd_dir)
{
	fp_rnd_t old;
	fp_rnd_t new;

	__asm("cfc1 %0,$31" : "=r" (old));

	new = old;
	new &= ~0x03;
	new |= (rnd_dir & 0x03);

	__asm("ctc1 %0,$31" : : "r" (new));

	return old & 0x03;
}

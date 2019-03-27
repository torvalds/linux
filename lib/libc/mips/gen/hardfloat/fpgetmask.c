/*	$NetBSD: fpgetmask.c,v 1.5 2005/12/24 23:10:08 perry Exp $	*/

/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: fpgetmask.c,v 1.5 2005/12/24 23:10:08 perry Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"

#include <ieeefp.h>

#ifdef __weak_alias
__weak_alias(fpgetmask,_fpgetmask)
#endif

fp_except_t
fpgetmask()
{
	int x;

	__asm("cfc1 %0,$31" : "=r" (x));
	return (x >> 7) & 0x1f;
}

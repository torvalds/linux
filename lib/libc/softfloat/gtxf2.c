/* $NetBSD: gtxf2.c,v 1.2 2004/09/27 10:16:24 he Exp $ */

/*
 * Written by Ben Harris, 2000.  This file is in the Public Domain.
 */

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef FLOATX80

flag __gtxf2(floatx80, floatx80);

flag
__gtxf2(floatx80 a, floatx80 b)
{

	/* libgcc1.c says a > b */
	return floatx80_lt(b, a);
}
#endif /* FLOATX80 */

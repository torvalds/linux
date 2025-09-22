/*	$OpenBSD: cephes.c,v 1.1 2011/05/30 20:23:35 martynas Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <float.h>
#include <stdio.h>

#include "cephes.h"

int
main(void)
{
	int retval = 0;

	printf("=> Testing monot (double precision):\n");
	retval |= monot();
	putchar('\n');

#if	LDBL_MANT_DIG == 64
	printf("=> Testing monotl (extended precision):\n");
	retval |= monotl();
	putchar('\n');
#endif	/* LDBL_MANT_DIG == 64 */

#if	LDBL_MANT_DIG == 113
	printf("=> Testing monotll (quadruple precision):\n");
	retval |= monotll();
	putchar('\n');
#endif	/* LDBL_MANT_DIG == 113 */

	printf("=> Testing testvect (double precision):\n");
	retval |= testvect();
	putchar('\n');

#if	LDBL_MANT_DIG == 64
	printf("=> Testing testvectl (extended precision):\n");
	retval |= testvectl();
	putchar('\n');
#endif	/* LDBL_MANT_DIG == 64 */

#if	LDBL_MANT_DIG == 113
	printf("=> Testing testvectll (quadruple precision):\n");
	retval |= testvectll();
	putchar('\n');
#endif	/* LDBL_MANT_DIG == 113 */

	return (retval);
}

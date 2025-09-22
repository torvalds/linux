/*	$OpenBSD: nan.c,v 1.2 2014/07/21 01:51:10 guenther Exp $	*/

/* Written by Martynas Venckus.  Public Domain. */

#include <endian.h>
#include <math.h>

/* bytes for qNaN on an arm (IEEE single format) */
char __nan[] __attribute__((__aligned__(sizeof(float)))) =
#if BYTE_ORDER == BIG_ENDIAN
					{ 0x7f, 0xc0, 0, 0 };
#else /* BYTE_ORDER == BIG_ENDIAN */
					{ 0, 0, 0xc0, 0x7f };
#endif /* BYTE_ORDER == BIG_ENDIAN */

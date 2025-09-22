/*	$OpenBSD: nan.c,v 1.1 2017/01/11 18:09:24 patrick Exp $	*/

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

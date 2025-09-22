/*	$OpenBSD: infinity.c,v 1.1 2017/01/11 18:09:24 patrick Exp $	*/
/*	$NetBSD: infinity.c,v 1.3 2002/02/19 20:08:19 bjh21 Exp $	*/

/*
 * IEEE-compatible infinity.c -- public domain.
 */

#include <endian.h>
#include <math.h>

char __infinity[] __attribute__((__aligned__(sizeof(double)))) =
#if BYTE_ORDER == BIG_ENDIAN
	{ 0x7f, 0xf0,    0,    0, 0, 0,    0,    0};
#else
	{    0,    0,    0,    0, 0, 0, 0xf0, 0x7f};
#endif

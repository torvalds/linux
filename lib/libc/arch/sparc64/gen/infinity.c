/*	$OpenBSD: infinity.c,v 1.2 2001/09/05 23:24:46 art Exp $	*/

/* infinity.c */

#include <math.h>

/* bytes for +Infinity on a sparc */
char __infinity[] __attribute__((__aligned__(sizeof(double)))) =
	{ 0x7f, (char)0xf0, 0, 0, 0, 0, 0, 0 };

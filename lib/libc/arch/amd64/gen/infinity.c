/*	$OpenBSD: infinity.c,v 1.3 2005/08/07 16:40:13 espie Exp $ */
/* infinity.c */

#include <math.h>

/* bytes for +Infinity on a 387 */
char __infinity[] __attribute__((__aligned__(sizeof(double)))) =
	{ 0, 0, 0, 0, 0, 0, (char)0xf0, 0x7f };


/*	$OpenBSD: infinity.c,v 1.6 2006/03/31 04:28:37 drahn Exp $	*/

/* infinity.c */

#include <math.h>

/* bytes for +Infinity on a PowerPC */
char __infinity[] __attribute__((__aligned__(sizeof(double)))) =
    { 0x7f, (char)0xf0, 0, 0, 0, 0, 0, 0 };

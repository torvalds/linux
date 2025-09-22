/*	$OpenBSD: infinity.c,v 1.4 2014/07/21 01:51:10 guenther Exp $	*/

/* infinity.c */

#include <endian.h>
#include <math.h>

/* bytes for +Infinity on a SH4 FPU (double precision) */
char __infinity[] __attribute__((__aligned__(sizeof(double)))) =
#if BYTE_ORDER == LITTLE_ENDIAN
    { 0, 0, 0, 0, 0, 0, (char)0xf0, 0x7f };
#else
    { 0x7f, (char)0xf0, 0, 0, 0, 0, 0, 0 };
#endif

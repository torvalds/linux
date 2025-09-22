/*	$OpenBSD: infinity.c,v 1.3 2014/07/21 01:51:10 guenther Exp $ */
/* infinity.c */

#include <endian.h>
#include <math.h>

/* bytes for +Infinity on a MIPS */
#if BYTE_ORDER == BIG_ENDIAN
char __infinity[] = { 0x7f, 0xf0, 0, 0, 0, 0, 0, 0 };
#else
char __infinity[] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x7f };
#endif

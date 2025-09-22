/*	$OpenBSD: infinity.c,v 1.4 2005/08/07 11:30:38 espie Exp $ */
/* infinity.c */

#include <math.h>

/* bytes for +Infinity on a 387 */
char __infinity[] = { 0, 0, 0, 0, 0, 0, (char)0xf0, 0x7f };

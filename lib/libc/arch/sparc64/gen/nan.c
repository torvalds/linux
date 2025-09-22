/*	$OpenBSD: nan.c,v 1.1 2008/07/24 09:31:07 martynas Exp $	*/

/* Written by Martynas Venckus.  Public Domain. */

#include <math.h>

/* bytes for qNaN on a sparc64 (IEEE single format) */
char __nan[] __attribute__((__aligned__(sizeof(float)))) =
					{ 0x7f, 0xc0, 0, 0 };

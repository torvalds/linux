/*	$OpenBSD: nan.c,v 1.1 2020/06/25 02:03:55 drahn Exp $	*/

/* Written by Martynas Venckus.  Public Domain. */

#include <math.h>

/* bytes for qNaN on a powerpc (IEEE single format) */
char __nan[] __attribute__((__aligned__(sizeof(float)))) =
					{ 0x7f, 0xc0, 0, 0 };

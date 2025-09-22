/*	$OpenBSD: lrand48.c,v 1.5 2015/08/27 04:33:31 guenther Exp $ */
/*
 * Copyright (c) 1993 Martin Birgmeier
 * All rights reserved.
 *
 * You may redistribute unmodified or modified versions of this source
 * code provided that the above copyright notice and this and the
 * following conditions are retained.
 *
 * This software is provided ``as is'', and comes with no warranties
 * of any kind. I shall in no event be liable for anything that happens
 * to anyone/anything when using this software.
 */

#include "rand48.h"

long
lrand48(void)
{
	if (__rand48_deterministic == 0)
		return arc4random() & 0x7fffffff;
	__dorand48(__rand48_seed);
	return ((long) __rand48_seed[2] << 15) + ((long) __rand48_seed[1] >> 1);
}

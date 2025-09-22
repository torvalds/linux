/*	$OpenBSD: mrand48.c,v 1.6 2015/08/27 04:33:31 guenther Exp $ */
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
mrand48(void)
{
	if (__rand48_deterministic == 0)
		return (int)arc4random();
	__dorand48(__rand48_seed);
	return ((long) __rand48_seed[2] << 16) + (long) __rand48_seed[1];
}

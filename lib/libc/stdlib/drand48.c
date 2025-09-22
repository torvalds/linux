/*	$OpenBSD: drand48.c,v 1.7 2015/09/14 13:30:17 guenther Exp $ */
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

#include <math.h>
#include "rand48.h"

double
drand48(void)
{
	if (__rand48_deterministic == 0) {
		unsigned short rseed[3];

		arc4random_buf(rseed, sizeof rseed);
		return ldexp((double) rseed[0], -48) +
		       ldexp((double) rseed[1], -32) +
		       ldexp((double) rseed[2], -16);
	}
	return erand48(__rand48_seed);
}

/*	$OpenBSD: seed48.c,v 1.6 2015/09/13 15:20:40 guenther Exp $ */
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

unsigned short *
seed48(unsigned short xseed[3])
{
	unsigned short *res;

	res = seed48_deterministic(xseed);
	__rand48_deterministic = 0;
	return res;
}

unsigned short *
seed48_deterministic(unsigned short xseed[3])
{
	static unsigned short sseed[3];

	__rand48_deterministic = 1;
	sseed[0] = __rand48_seed[0];
	sseed[1] = __rand48_seed[1];
	sseed[2] = __rand48_seed[2];
	__rand48_seed[0] = xseed[0];
	__rand48_seed[1] = xseed[1];
	__rand48_seed[2] = xseed[2];
	__rand48_mult[0] = RAND48_MULT_0;
	__rand48_mult[1] = RAND48_MULT_1;
	__rand48_mult[2] = RAND48_MULT_2;
	__rand48_add = RAND48_ADD;
	return sseed;
}
DEF_WEAK(seed48_deterministic);

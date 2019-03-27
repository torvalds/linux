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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rand48.h"

extern unsigned short _rand48_seed[3];
extern unsigned short _rand48_mult[3];
extern unsigned short _rand48_add;

unsigned short *
seed48(unsigned short xseed[3])
{
	static unsigned short sseed[3];

	sseed[0] = _rand48_seed[0];
	sseed[1] = _rand48_seed[1];
	sseed[2] = _rand48_seed[2];
	_rand48_seed[0] = xseed[0];
	_rand48_seed[1] = xseed[1];
	_rand48_seed[2] = xseed[2];
	_rand48_mult[0] = RAND48_MULT_0;
	_rand48_mult[1] = RAND48_MULT_1;
	_rand48_mult[2] = RAND48_MULT_2;
	_rand48_add = RAND48_ADD;
	return sseed;
}

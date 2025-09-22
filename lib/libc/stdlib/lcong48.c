/*	$OpenBSD: lcong48.c,v 1.6 2015/09/13 08:31:47 guenther Exp $ */
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

void
lcong48(unsigned short p[7])
{
	lcong48_deterministic(p);
	__rand48_deterministic = 0;
}

void
lcong48_deterministic(unsigned short p[7])
{
	__rand48_deterministic = 1;
	__rand48_seed[0] = p[0];
	__rand48_seed[1] = p[1];
	__rand48_seed[2] = p[2];
	__rand48_mult[0] = p[3];
	__rand48_mult[1] = p[4];
	__rand48_mult[2] = p[5];
	__rand48_add = p[6];
}
DEF_WEAK(lcong48_deterministic);

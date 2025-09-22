/*	$OpenBSD: nrand48.c,v 1.3 2005/08/08 08:05:37 espie Exp $ */
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
nrand48(unsigned short xseed[3])
{
	__dorand48(xseed);
	return ((long) xseed[2] << 15) + ((long) xseed[1] >> 1);
}

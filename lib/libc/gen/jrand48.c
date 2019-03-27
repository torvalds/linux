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

#include <stdint.h>

#include "rand48.h"

long
jrand48(unsigned short xseed[3])
{

	_dorand48(xseed);
	return ((int32_t)(((uint32_t)xseed[2] << 16) | (uint32_t)xseed[1]));
}

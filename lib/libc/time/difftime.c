/*	$OpenBSD: difftime.c,v 1.13 2025/05/21 01:27:29 millert Exp $ */
/* This file is placed in the public domain by Matthew Dempsky. */

#include "private.h"

#define HI(t) ((double)(t & 0xffffffff00000000LL))
#define LO(t) ((double)(t & 0x00000000ffffffffLL))

double __pure
difftime(time_t t1, time_t t0)
{
	return (HI(t1) - HI(t0)) + (LO(t1) - LO(t0));
}

/*	$OpenBSD: drand.c,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*							drand.c
 *
 *	Pseudorandom number generator
 *
 *
 *
 * SYNOPSIS:
 *
 * double y, drand();
 *
 * drand( &y );
 *
 *
 *
 * DESCRIPTION:
 *
 * Yields a random number 1.0 <= y < 2.0.
 *
 * The three-generator congruential algorithm by Brian
 * Wichmann and David Hill (BYTE magazine, March, 1987,
 * pp 127-8) is used. The period, given by them, is
 * 6953607871644.
 *
 * Versions invoked by the different arithmetic compile
 * time options DEC, IBMPC, and MIEEE, produce
 * approximately the same sequences, differing only in the
 * least significant bits of the numbers. The UNK option
 * implements the algorithm as recommended in the BYTE
 * article.  It may be used on all computers. However,
 * the low order bits of a double precision number may
 * not be adequately random, and may vary due to arithmetic
 * implementation details on different computers.
 *
 * The other compile options generate an additional random
 * integer that overwrites the low order bits of the double
 * precision number.  This reduces the period by a factor of
 * two but tends to overcome the problems mentioned.
 *
 */

#include "mconf.h"


/*  Three-generator random number algorithm
 * of Brian Wichmann and David Hill
 * BYTE magazine, March, 1987 pp 127-8
 *
 * The period, given by them, is (p-1)(q-1)(r-1)/4 = 6.95e12.
 */

static int sx = 1;
static int sy = 10000;
static int sz = 3000;

static union {
 double d;
 unsigned short s[4];
} unkans;

/* This function implements the three
 * congruential generators.
 */
 
static int ranwh()
{
int r, s;

/*  sx = sx * 171 mod 30269 */
r = sx/177;
s = sx - 177 * r;
sx = 171 * s - 2 * r;
if( sx < 0 )
	sx += 30269;


/* sy = sy * 172 mod 30307 */
r = sy/176;
s = sy - 176 * r;
sy = 172 * s - 35 * r;
if( sy < 0 )
	sy += 30307;

/* sz = 170 * sz mod 30323 */
r = sz/178;
s = sz - 178 * r;
sz = 170 * s - 63 * r;
if( sz < 0 )
	sz += 30323;
/* The results are in static sx, sy, sz. */
return 0;
}

/*	drand.c
 *
 * Random double precision floating point number between 1 and 2.
 *
 * C callable:
 *	drand( &x );
 */

int drand( a )
double *a;
{
unsigned short r;
#ifdef DEC
unsigned short s, t;
#endif

/* This algorithm of Wichmann and Hill computes a floating point
 * result:
 */
ranwh();
unkans.d = sx/30269.0  +  sy/30307.0  +  sz/30323.0;
r = unkans.d;
unkans.d -= r;
unkans.d += 1.0;

/* if UNK option, do nothing further.
 * Otherwise, make a random 16 bit integer
 * to overwrite the least significant word
 * of unkans.
 */
#ifdef UNK
/* do nothing */
#else
ranwh();
r = sx * sy + sz;
#endif

#ifdef DEC
/* To make the numbers as similar as possible
 * in all arithmetics, the random integer has
 * to be inserted 3 bits higher up in a DEC number.
 * An alternative would be put it 3 bits lower down
 * in all the other number types.
 */
s = unkans.s[2];
t = s & 07;	/* save these bits to put in at the bottom */
s &= 0177770;
s |= (r >> 13) & 07;
unkans.s[2] = s;
t |= r << 3;
unkans.s[3] = t;
#endif

#ifdef IBMPC
unkans.s[0] = r;
#endif

#ifdef MIEEE
unkans.s[3] = r;
#endif

*a = unkans.d;
return 0;
}

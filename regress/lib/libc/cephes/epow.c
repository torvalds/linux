/*	$OpenBSD: epow.c,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

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

/*						epow.c	*/
/*  power function: z = x**y */
/*  by Stephen L. Moshier. */


#include "ehead.h"

extern int rndprc;
void epowi();

void epow( x, y, z )
unsigned short *x, *y, *z;
{
unsigned short w[NE];
int rndsav;
long li;

efloor( y, w );
if( ecmp(y,w) == 0 )
	{
	eifrac( y, &li, w );
	if( li < 0 )
		li = -li;
	if( (li < 0x7fffffff) && (li != 0x80000000) )
		{
		epowi( x, y, z );
		return;
		}
	}
/* z = exp( y * log(x) ) */
rndsav = rndprc;
rndprc = NBITS;
elog( x, w );
emul( y, w, w );
eexp( w, z );
rndprc = rndsav;
emul( eone, z, z );
}


/* y is integer valued. */

void epowi( x, y, z )
unsigned short x[], y[], z[];
{
unsigned short w[NE];
long li, lx;
unsigned long lu;
int rndsav;
unsigned short signx;
/* unsigned short signy; */

rndsav = rndprc;
eifrac( y, &li, w );
if( li < 0 )
	lx = -li;
else
	lx = li;

if( (lx == 0x7fffffff) || (lx == 0x80000000) )
	{
	epow( x, y, z );
	goto done;
	}

if( (x[NE-1] & (unsigned short )0x7fff) == 0 )
	{
	if( li == 0 )
		{
		emov( eone, z );
		return;
		}
	else if( li < 0 )
		{
		einfin( z );
		return;
		}
	else
		{
		eclear( z );
		return;
		}
	}

if( li == 0L )
	{
	emov( eone, z );
	return;
	}

emov( x, w );
signx = w[NE-1] & (unsigned short )0x8000;
w[NE-1] &= (unsigned short )0x7fff;

/* Overflow detection */
/*
lx = li * (w[NE-1] - 0x3fff);
if( lx > 16385L )
	{
	einfin( z );
	mtherr( "epowi", OVERFLOW );
	goto done;
	}
if( lx < -16450L )
	{
	eclear( z );
	return;
	}
*/
rndprc = NBITS;

if( li < 0 )
	{
	lu = (unsigned int )( -li );
/*	signy = 0xffff;*/
	ediv( w, eone, w );
	}
else
	{
	lu = (unsigned int )li;
/*	signy = 0;*/
	}

/* First bit of the power */
if( lu & 1 )
	{
	emov( w, z );
	}	
else
	{
	emov( eone, z );
	signx = 0;
	}


lu >>= 1;
while( lu != 0L )
	{
	emul( w, w, w );	/* arg to the 2-to-the-kth power */
	if( lu & 1L )	/* if that bit is set, then include in product */
		emul( w, z, z );
	lu >>= 1;
	}


done:

if( signx )
	eneg( z ); /* odd power of negative number */

/*
if( signy )
  	{
  	if( ecmp( z, ezero ) != 0 )
 		{
		ediv( z, eone, z );
		}
	else
		{
		einfin( z );
		printf( "epowi OVERFLOW\n" );
		}
	}
*/
rndprc = rndsav;
emul( eone, z, z );
}



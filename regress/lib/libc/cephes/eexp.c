/*	$OpenBSD: eexp.c,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

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

/*							xexp.c		*/
/* exponential function check routine */
/* by Stephen L. Moshier. */


#include "ehead.h"

void eexp( x, y )
unsigned short *x, *y;
{
unsigned short num[NE], den[NE], x2[NE];
long i;
unsigned short sign, expchk;

/* range reduction theory: x = i + f, 0<=f<1;
 * e**x = e**i * e**f 
 * e**i = 2**(i/log 2).
 * Let i/log2 = i1 + f1, 0<=f1<1.
 * Then e**i = 2**i1 * 2**f1, so
 * e**x = 2**i1 * e**(log 2 * f1) * e**f.
 */
if( ecmp(x, ezero) == 0 )
	{
	emov( eone, y );
	return;
	}
emov(x, x2);
expchk = x2[NE-1];
sign = expchk & 0x8000;
x2[NE-1] &= 0x7fff;

/* Test for excessively large argument */
expchk &= 0x7fff;
if( expchk > (EXONE + 15) )
	{
	eclear( y );
	if( sign == 0 )
		einfin( y );
	return;
	}

eifrac( x2, &i, num );		/* x = i + f		*/

if( i != 0 )
 {
 ltoe( &i, den );		/* floating point i	*/
 ediv( elog2, den, den );	/* i/log 2		*/
 eifrac( den, &i, den );	/* i/log 2  =  i1 + f1	*/
 emul( elog2, den, den );	/* log 2 * f1		*/
 eadd( den, num, x2 );		/* log 2 * f1  + f	*/
 }

/*x2[NE-1] -= 1;*/
eldexp( x2, -1L, x2 ); /* divide by 2 */
etanh( x2, x2 );	/* tanh( x/2 )			*/
eadd( x2, eone, num );	/* 1 + tanh			*/
eneg( x2 );
eadd( x2, eone, den );	/* 1 - tanh			*/
ediv( den, num, y );	/* (1 + tanh)/(1 - tanh)	*/

/*y[NE-1] += i;*/
if( sign )
	{
	ediv( y, eone, y );
	i = -i;
	}
eldexp( y, i, y );	/* multiply by 2**i */
}

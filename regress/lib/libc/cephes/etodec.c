/*	$OpenBSD: etodec.c,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

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

#include "ehead.h"
void emovi(), emovo(), ecleaz(), eshdn8(), emdnorm();
void todec();
/*
;	convert DEC double precision to e type
;	double d;
;	short e[NE];
;	dectoe( &d, e );
*/
void dectoe( d, e )
unsigned short *d;
unsigned short *e;
{
unsigned short y[NI];
register unsigned short r, *p;

ecleaz(y);		/* start with a zero */
p = y;			/* point to our number */
r = *d;			/* get DEC exponent word */
if( *d & (unsigned int )0x8000 )
	*p = 0xffff;	/* fill in our sign */
++p;			/* bump pointer to our exponent word */
r &= 0x7fff;		/* strip the sign bit */
if( r == 0 )		/* answer = 0 if high order DEC word = 0 */
	goto done;


r >>= 7;	/* shift exponent word down 7 bits */
r += EXONE - 0201;	/* subtract DEC exponent offset */
			/* add our e type exponent offset */
*p++ = r;	/* to form our exponent */

r = *d++;	/* now do the high order mantissa */
r &= 0177;	/* strip off the DEC exponent and sign bits */
r |= 0200;	/* the DEC understood high order mantissa bit */
*p++ = r;	/* put result in our high guard word */

*p++ = *d++;	/* fill in the rest of our mantissa */
*p++ = *d++;
*p = *d;

eshdn8(y);	/* shift our mantissa down 8 bits */
done:
emovo( y, e );
}



/*
;	convert e type to DEC double precision
;	double d;
;	short e[NE];
;	etodec( e, &d );
*/
#if 0
static unsigned short decbit[NI] = {0,0,0,0,0,0,0200,0};
void etodec( x, d )
unsigned short *x, *d;
{
unsigned short xi[NI];
register unsigned short r;
int i, j;

emovi( x, xi );
*d = 0;
if( xi[0] != 0 )
	*d = 0100000;
r = xi[E];
if( r < (EXONE - 128) )
	goto zout;
i = xi[M+4];
if( (i & 0200) != 0 )
	{
	if( (i & 0377) == 0200 )
		{
		if( (i & 0400) != 0 )
			{
		/* check all less significant bits */
			for( j=M+5; j<NI; j++ )
				{
				if( xi[j] != 0 )
					goto yesrnd;
				}
			}
		goto nornd;
		}
yesrnd:
	eaddm( decbit, xi );
	r -= enormlz(xi);
	}

nornd:

r -= EXONE;
r += 0201;
if( r < 0 )
	{
zout:
	*d++ = 0;
	*d++ = 0;
	*d++ = 0;
	*d++ = 0;
	return;
	}
if( r >= 0377 )
	{
	*d++ = 077777;
	*d++ = -1;
	*d++ = -1;
	*d++ = -1;
	return;
	}
r &= 0377;
r <<= 7;
eshup8( xi );
xi[M] &= 0177;
r |= xi[M];
*d++ |= r;
*d++ = xi[M+1];
*d++ = xi[M+2];
*d++ = xi[M+3];
}
#else

extern int rndprc;

void etodec( x, d )
unsigned short *x, *d;
{
unsigned short xi[NI];
long exp;
int rndsav;

emovi( x, xi );
exp = (long )xi[E] - (EXONE - 0201); /* adjust exponent for offsets */
/* round off to nearest or even */
rndsav = rndprc;
rndprc = 56;
emdnorm( xi, 0, 0, exp, 64 );
rndprc = rndsav;
todec( xi, d );
}

void todec( x, y )
unsigned short *x, *y;
{
unsigned short i;
unsigned short *p;

p = x;
*y = 0;
if( *p++ )
	*y = 0100000;
i = *p++;
if( i == 0 )
	{
	*y++ = 0;
	*y++ = 0;
	*y++ = 0;
	*y++ = 0;
	return;
	}
if( i > 0377 )
	{
	*y++ |= 077777;
	*y++ = 0xffff;
	*y++ = 0xffff;
	*y++ = 0xffff;
	return;
	}
i &= 0377;
i <<= 7;
eshup8( x );
x[M] &= 0177;
i |= x[M];
*y++ |= i;
*y++ = x[M+1];
*y++ = x[M+2];
*y++ = x[M+3];
}
#endif

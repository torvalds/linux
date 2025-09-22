/*	$OpenBSD: polevll.c,v 1.2 2011/06/02 21:47:40 martynas Exp $	*/

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

/*							polevll.c
 *							p1evll.c
 *
 *	Evaluate polynomial
 *
 *
 *
 * SYNOPSIS:
 *
 * int N;
 * long double x, y, coef[N+1], polevl[];
 *
 * y = polevll( x, coef, N );
 *
 *
 *
 * DESCRIPTION:
 *
 * Evaluates polynomial of degree N:
 *
 *                     2          N
 * y  =  C  + C x + C x  +...+ C x
 *        0    1     2          N
 *
 * Coefficients are stored in reverse order:
 *
 * coef[0] = C  , ..., coef[N] = C  .
 *            N                   0
 *
 *  The function p1evll() assumes that coef[N] = 1.0 and is
 * omitted from the array.  Its calling arguments are
 * otherwise the same as polevll().
 *
 *  This module also contains the following globally declared constants:
 * MAXNUML = 1.189731495357231765021263853E4932L;
 * MACHEPL = 5.42101086242752217003726400434970855712890625E-20L;
 * MAXLOGL =  1.1356523406294143949492E4L;
 * MINLOGL = -1.1355137111933024058873E4L;
 * LOGE2L  = 6.9314718055994530941723E-1L;
 * LOG2EL  = 1.4426950408889634073599E0L;
 * PIL     = 3.1415926535897932384626L;
 * PIO2L   = 1.5707963267948966192313L;
 * PIO4L   = 7.8539816339744830961566E-1L;
 *
 * SPEED:
 *
 * In the interest of speed, there are no checks for out
 * of bounds arithmetic.  This routine is used by most of
 * the functions in the library.  Depending on available
 * equipment features, the user may wish to rewrite the
 * program in microcode or assembly language.
 *
 */

#include <float.h>

#if	LDBL_MANT_DIG == 64
#include "mconf.h"

#if UNK
/* almost 2^16384 */
long double MAXNUML = 1.189731495357231765021263853E4932L;
/* 2^-64 */
long double MACHEPL = 5.42101086242752217003726400434970855712890625E-20L;
/* log( MAXNUML ) */
long double MAXLOGL =  1.1356523406294143949492E4L;
#ifdef DENORMAL
/* log(smallest denormal number = 2^-16446) */
long double MINLOGL = -1.13994985314888605586758E4L;
#else
/* log( underflow threshold = 2^(-16382) ) */
long double MINLOGL = -1.1355137111933024058873E4L;
#endif
long double LOGE2L  = 6.9314718055994530941723E-1L;
long double LOG2EL  = 1.4426950408889634073599E0L;
long double PIL     = 3.1415926535897932384626L;
long double PIO2L   = 1.5707963267948966192313L;
long double PIO4L   = 7.8539816339744830961566E-1L;
#ifdef INFINITIES
long double NANL = 0.0L / 0.0L;
long double INFINITYL = 1.0L / 0.0L;
#else
long double INFINITYL = 1.189731495357231765021263853E4932L;
long double NANL = 0.0L;
#endif
#endif
#if IBMPC
short MAXNUML[] = {0xffff,0xffff,0xffff,0xffff,0x7ffe, XPD};
short MAXLOGL[] = {0x79ab,0xd1cf,0x17f7,0xb172,0x400c, XPD};
#ifdef INFINITIES
short INFINITYL[] = {0,0,0,0x8000,0x7fff, XPD};
short NANL[] = {0,0,0,0xc000,0x7fff, XPD};
#else
short INFINITYL[] = {0xffff,0xffff,0xffff,0xffff,0x7ffe, XPD};
long double NANL = 0.0L;
#endif
#ifdef DENORMAL
short MINLOGL[] = {0xbaaa,0x09e2,0xfe7f,0xb21d,0xc00c, XPD};
#else
short MINLOGL[] = {0xeb2f,0x1210,0x8c67,0xb16c,0xc00c, XPD};
#endif
short MACHEPL[] = {0x0000,0x0000,0x0000,0x8000,0x3fbf, XPD};
short LOGE2L[]  = {0x79ac,0xd1cf,0x17f7,0xb172,0x3ffe, XPD};
short LOG2EL[]  = {0xf0bc,0x5c17,0x3b29,0xb8aa,0x3fff, XPD};
short PIL[]     = {0xc235,0x2168,0xdaa2,0xc90f,0x4000, XPD};
short PIO2L[]   = {0xc235,0x2168,0xdaa2,0xc90f,0x3fff, XPD};
short PIO4L[]   = {0xc235,0x2168,0xdaa2,0xc90f,0x3ffe, XPD};
#endif
#if MIEEE
long MAXNUML[] = {0x7ffe0000,0xffffffff,0xffffffff};
long MAXLOGL[] = {0x400c0000,0xb17217f7,0xd1cf79ab};
#ifdef INFINITIES
long INFINITY[] = {0x7fff0000,0x80000000,0x00000000};
long NANL[] = {0x7fff0000,0xffffffff,0xffffffff};
#else
long INFINITYL[] = {0x7ffe0000,0xffffffff,0xffffffff};
long double NANL = 0.0L;
#endif
#ifdef DENORMAL
long MINLOGL[] = {0xc00c0000,0xb21dfe7f,0x09e2baaa};
#else
long MINLOGL[] = {0xc00c0000,0xb16c8c67,0x1210eb2f};
#endif
long MACHEPL[] = {0x3fbf0000,0x80000000,0x00000000};
long LOGE2L[]  = {0x3ffe0000,0xb17217f7,0xd1cf79ac};
long LOG2EL[]  = {0x3fff0000,0xb8aa3b29,0x5c17f0bc};
long PIL[]     = {0x40000000,0xc90fdaa2,0x2168c235};
long PIO2L[]   = {0x3fff0000,0xc90fdaa2,0x2168c235};
long PIO4L[]   = {0x3ffe0000,0xc90fdaa2,0x2168c235};
#endif

#ifdef MINUSZERO
long double NEGZEROL = -0.0L;
#else
long double NEGZEROL = 0.0L;
#endif

/* Polynomial evaluator:
 *  P[0] x^n  +  P[1] x^(n-1)  +  ...  +  P[n]
 */
long double polevll( x, p, n )
long double x;
void *p;
int n;
{
register long double y;
register long double *P = (long double *)p;

y = *P++;
do
	{
	y = y * x + *P++;
	}
while( --n );
return(y);
}



/* Polynomial evaluator:
 *  x^n  +  P[0] x^(n-1)  +  P[1] x^(n-2)  +  ...  +  P[n]
 */
long double p1evll( x, p, n )
long double x;
void *p;
int n;
{
register long double y;
register long double *P = (long double *)p;

n -= 1;
y = x + *P++;
do
	{
	y = y * x + *P++;
	}
while( --n );
return( y );
}
#endif	/* LDBL_MANT_DIG == 64 */

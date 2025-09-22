/*	$OpenBSD: header.h,v 1.1 2009/04/09 01:24:43 martynas Exp $	*/

/*
 * Copyright (c) 2009 Gaston H. Gonnet <gonnet@inf.ethz.ch>
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

/* C program to test the accuracy of a given function.
   This will be done in terms of ulps.  A ulp is a "unit in
   the last place".  No program can guarantee results better
   than 0.5 ulp.  As a matter of fact, 0.5 ulp could require
   infinite computation.  So very good implementations
   usually achieve 0.51 ulp.  0.6 ulp is still very good, 1 ulp
   starts to get a bit sloppy.

   The model of computation is the standard one, the result of f(x)
   is the correctly rounded value of f(x) when x is assumed to be
   an exact rational.

   This program selects values which are difficult to compute to
   measure the quality of the computation.

   Hardware which has floating point registers with extra bits of
   precision (e.g. Pentium) usually do much better than others.
   This function computes the accuracy for both cases.   */

/* We represent double precision floating point numbers x as a
   pair of an integer and a base 2 exponent.  x = mant*2^expo,
   or x = scalb(mant,expo).  2^52 <= mant < 2^53.  This is to
   avoid sloppy compilers which do not transcribe correctly
   some input doubles */

/* scalb( F(arg[i]), val_e[i] ) == val[i]+eps[i]
	to about twice precision.
	The scaling is done so that val[i] is in ulps
	(val is consequently an integer)
	and to avoid underflows in certain cases. */

/* Shell sort, needs the definition of KEY(x)
   From: Gonnet & Baeza, Hanbook of Algorithms and Data structures, 4.1.4.
   Can sort any array r from r[lo] to r[hi] in place.
   Uses a temporary variable t, of the same type as r[].
   The comparison is done between KEY(r[i]) vs KEY(r[j]),
	so the macro KEY(x) has to be defined appropriately */
#define SORT(r,lo,up,t) \
     { int SORTd, SORTi, SORTj; \
     for ( SORTd=(up)-(lo)+1; SORTd>1; ) { \
	  SORTd = SORTd<5 ? 1 : (5*SORTd-1)/11; \
	  for ( SORTi=(up)-SORTd; SORTi>=(lo); SORTi-- ) { \
	       (t) = (r)[SORTi]; \
	       for ( SORTj=SORTi+SORTd; SORTj<=(up) && KEY((t)) > KEY((r)[SORTj]); SORTj+=SORTd ) \
		    (r)[SORTj-SORTd] = (r)[SORTj]; \
	       (r)[SORTj-SORTd] = (t); \
	       } \
	  }}

double scalb( double x, double n );

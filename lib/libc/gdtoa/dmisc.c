/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998 by Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

****************************************************************/

/* Please send bug reports to David M. Gay (dmg at acm dot org,
 * with " at " changed at "@" and " dot " changed to ".").	*/

#include "gdtoaimp.h"

#ifndef MULTIPLE_THREADS
 char *dtoa_result;
#endif

 char *
#ifdef KR_headers
rv_alloc(i) int i;
#else
rv_alloc(int i)
#endif
{
	int j, k, *r;

	j = sizeof(ULong);
	for(k = 0;
		sizeof(Bigint) - sizeof(ULong) - sizeof(int) + j <= i;
		j <<= 1)
			k++;
	r = (int*)Balloc(k);
	if (r == NULL)
		return (
#ifndef MULTIPLE_THREADS
		dtoa_result =
#endif
			NULL);
	*r = k;
	return
#ifndef MULTIPLE_THREADS
	dtoa_result =
#endif
		(char *)(r+1);
	}

 char *
#ifdef KR_headers
nrv_alloc(s, rve, n) char *s, **rve; int n;
#else
nrv_alloc(char *s, char **rve, int n)
#endif
{
	char *rv, *t;

	t = rv = rv_alloc(n);
	if (t == NULL)
		return (NULL);
	while((*t = *s++) !=0)
		t++;
	if (rve)
		*rve = t;
	return rv;
	}

/* freedtoa(s) must be used to free values s returned by dtoa
 * when MULTIPLE_THREADS is #defined.  It should be used in all cases,
 * but for consistency with earlier versions of dtoa, it is optional
 * when MULTIPLE_THREADS is not defined.
 */

 void
#ifdef KR_headers
freedtoa(s) char *s;
#else
freedtoa(char *s)
#endif
{
	Bigint *b = (Bigint *)((int *)s - 1);
	b->maxwds = 1 << (b->k = *(int*)b);
	Bfree(b);
#ifndef MULTIPLE_THREADS
	if (s == dtoa_result)
		dtoa_result = 0;
#endif
	}
DEF_STRONG(freedtoa);

 int
quorem
#ifdef KR_headers
	(b, S) Bigint *b, *S;
#else
	(Bigint *b, Bigint *S)
#endif
{
	int n;
	ULong *bx, *bxe, q, *sx, *sxe;
#ifdef ULLong
	ULLong borrow, carry, y, ys;
#else
	ULong borrow, carry, y, ys;
#ifdef Pack_32
	ULong si, z, zs;
#endif
#endif

	n = S->wds;
#ifdef DEBUG
	/*debug*/ if (b->wds > n)
	/*debug*/	Bug("oversize b in quorem");
#endif
	if (b->wds < n)
		return 0;
	sx = S->x;
	sxe = sx + --n;
	bx = b->x;
	bxe = bx + n;
	q = *bxe / (*sxe + 1);	/* ensure q <= true quotient */
#ifdef DEBUG
	/*debug*/ if (q > 9)
	/*debug*/	Bug("oversized quotient in quorem");
#endif
	if (q) {
		borrow = 0;
		carry = 0;
		do {
#ifdef ULLong
			ys = *sx++ * (ULLong)q + carry;
			carry = ys >> 32;
			y = *bx - (ys & 0xffffffffUL) - borrow;
			borrow = y >> 32 & 1UL;
			*bx++ = y & 0xffffffffUL;
#else
#ifdef Pack_32
			si = *sx++;
			ys = (si & 0xffff) * q + carry;
			zs = (si >> 16) * q + (ys >> 16);
			carry = zs >> 16;
			y = (*bx & 0xffff) - (ys & 0xffff) - borrow;
			borrow = (y & 0x10000) >> 16;
			z = (*bx >> 16) - (zs & 0xffff) - borrow;
			borrow = (z & 0x10000) >> 16;
			Storeinc(bx, z, y);
#else
			ys = *sx++ * q + carry;
			carry = ys >> 16;
			y = *bx - (ys & 0xffff) - borrow;
			borrow = (y & 0x10000) >> 16;
			*bx++ = y & 0xffff;
#endif
#endif
			}
			while(sx <= sxe);
		if (!*bxe) {
			bx = b->x;
			while(--bxe > bx && !*bxe)
				--n;
			b->wds = n;
			}
		}
	if (cmp(b, S) >= 0) {
		q++;
		borrow = 0;
		carry = 0;
		bx = b->x;
		sx = S->x;
		do {
#ifdef ULLong
			ys = *sx++ + carry;
			carry = ys >> 32;
			y = *bx - (ys & 0xffffffffUL) - borrow;
			borrow = y >> 32 & 1UL;
			*bx++ = y & 0xffffffffUL;
#else
#ifdef Pack_32
			si = *sx++;
			ys = (si & 0xffff) + carry;
			zs = (si >> 16) + (ys >> 16);
			carry = zs >> 16;
			y = (*bx & 0xffff) - (ys & 0xffff) - borrow;
			borrow = (y & 0x10000) >> 16;
			z = (*bx >> 16) - (zs & 0xffff) - borrow;
			borrow = (z & 0x10000) >> 16;
			Storeinc(bx, z, y);
#else
			ys = *sx++ + carry;
			carry = ys >> 16;
			y = *bx - (ys & 0xffff) - borrow;
			borrow = (y & 0x10000) >> 16;
			*bx++ = y & 0xffff;
#endif
#endif
			}
			while(sx <= sxe);
		bx = b->x;
		bxe = bx + n;
		if (!*bxe) {
			while(--bxe > bx && !*bxe)
				--n;
			b->wds = n;
			}
		}
	return q;
	}

/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998, 2000 by Lucent Technologies
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

 void
#ifdef KR_headers
ULtod(L, bits, exp, k) ULong *L; ULong *bits; Long exp; int k;
#else
ULtod(ULong *L, ULong *bits, Long exp, int k)
#endif
{
	switch(k & STRTOG_Retmask) {
	  case STRTOG_NoNumber:
	  case STRTOG_Zero:
		L[0] = L[1] = 0;
		break;

	  case STRTOG_Denormal:
		L[_1] = bits[0];
		L[_0] = bits[1];
		break;

	  case STRTOG_Normal:
	  case STRTOG_NaNbits:
		L[_1] = bits[0];
		L[_0] = (bits[1] & ~0x100000) | ((exp + 0x3ff + 52) << 20);
		break;

	  case STRTOG_NoMemory:
		errno = ERANGE;
		/* FALLTHROUGH */
	  case STRTOG_Infinite:
		L[_0] = 0x7ff00000;
		L[_1] = 0;
		break;

	  case STRTOG_NaN:
		L[0] = d_QNAN0;
		L[1] = d_QNAN1;
	  }
	if (k & STRTOG_Neg)
		L[_0] |= 0x80000000L;
	}

 int
#ifdef KR_headers
strtord(s, sp, rounding, d) CONST char *s; char **sp; int rounding; double *d;
#else
strtord(CONST char *s, char **sp, int rounding, double *d)
#endif
{
	static FPI fpi0 = { 53, 1-1023-53+1, 2046-1023-53+1, 1, SI };
	FPI *fpi, fpi1;
	ULong bits[2];
	Long exp;
	int k;

	fpi = &fpi0;
	if (rounding != FPI_Round_near) {
		fpi1 = fpi0;
		fpi1.rounding = rounding;
		fpi = &fpi1;
		}
	k = strtodg(s, sp, fpi, &exp, bits);
	ULtod((ULong*)d, bits, exp, k);
	return k;
	}

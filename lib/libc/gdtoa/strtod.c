/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998-2001 by Lucent Technologies
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
#ifndef NO_FENV_H
#include <fenv.h>
#endif

#ifdef USE_LOCALE
#include "locale.h"
#endif

#ifdef IEEE_Arith
#ifndef NO_IEEE_Scale
#define Avoid_Underflow
#undef tinytens
/* The factor of 2^106 in tinytens[4] helps us avoid setting the underflow */
/* flag unnecessarily.  It leads to a song and dance at the end of strtod. */
static CONST double tinytens[] = { 1e-16, 1e-32, 1e-64, 1e-128,
		9007199254740992.*9007199254740992.e-256
		};
#endif
#endif

#ifdef Honor_FLT_ROUNDS
#undef Check_FLT_ROUNDS
#define Check_FLT_ROUNDS
#else
#define Rounding Flt_Rounds
#endif

#ifdef Avoid_Underflow /*{*/
 static double
sulp
#ifdef KR_headers
	(x, scale) U *x; int scale;
#else
	(U *x, int scale)
#endif
{
	U u;
	double rv;
	int i;

	rv = ulp(x);
	if (!scale || (i = 2*P + 1 - ((word0(x) & Exp_mask) >> Exp_shift)) <= 0)
		return rv; /* Is there an example where i <= 0 ? */
	word0(&u) = Exp_1 + (i << Exp_shift);
	word1(&u) = 0;
	return rv * u.d;
	}
#endif /*}*/

 double
strtod
#ifdef KR_headers
	(s00, se) CONST char *s00; char **se;
#else
	(CONST char *s00, char **se)
#endif
{
#ifdef Avoid_Underflow
	int scale;
#endif
	int bb2, bb5, bbe, bd2, bd5, bbbits, bs2, c, decpt, dsign,
		 e, e1, esign, i, j, k, nd, nd0, nf, nz, nz0, sign;
	CONST char *s, *s0, *s1;
	double aadj;
	Long L;
	U adj, aadj1, rv, rv0;
	ULong y, z;
	Bigint *bb = NULL, *bb1, *bd = NULL, *bd0 = NULL, *bs = NULL, *delta = NULL;
#ifdef Avoid_Underflow
	ULong Lsb, Lsb1;
#endif
#ifdef SET_INEXACT
	int inexact, oldinexact;
#endif
#ifdef USE_LOCALE /*{{*/
#ifdef NO_LOCALE_CACHE
	char *decimalpoint = localeconv()->decimal_point;
	int dplen = strlen(decimalpoint);
#else
	char *decimalpoint;
	static char *decimalpoint_cache;
	static int dplen;
	if (!(s0 = decimalpoint_cache)) {
		s0 = localeconv()->decimal_point;
		decimalpoint_cache = strdup(s0);
		dplen = strlen(s0);
		}
	decimalpoint = (char*)s0;
#endif /*NO_LOCALE_CACHE*/
#else  /*USE_LOCALE}{*/
#define dplen 1
#endif /*USE_LOCALE}}*/

#ifdef Honor_FLT_ROUNDS /*{*/
	int Rounding;
#ifdef Trust_FLT_ROUNDS /*{{ only define this if FLT_ROUNDS really works! */
	Rounding = Flt_Rounds;
#else /*}{*/
	Rounding = 1;
	switch(fegetround()) {
	  case FE_TOWARDZERO:	Rounding = 0; break;
	  case FE_UPWARD:	Rounding = 2; break;
	  case FE_DOWNWARD:	Rounding = 3;
	  }
#endif /*}}*/
#endif /*}*/

	sign = nz0 = nz = decpt = 0;
	dval(&rv) = 0.;
	for(s = s00;;s++) switch(*s) {
		case '-':
			sign = 1;
			/* no break */
		case '+':
			if (*++s)
				goto break2;
			/* no break */
		case 0:
			goto ret0;
		case '\t':
		case '\n':
		case '\v':
		case '\f':
		case '\r':
		case ' ':
			continue;
		default:
			goto break2;
		}
 break2:
	if (*s == '0') {
#ifndef NO_HEX_FP /*{*/
		{
		static FPI fpi = { 53, 1-1023-53+1, 2046-1023-53+1, 1, SI };
		Long exp;
		ULong bits[2];
		switch(s[1]) {
		  case 'x':
		  case 'X':
			{
#ifdef Honor_FLT_ROUNDS
			FPI fpi1 = fpi;
			fpi1.rounding = Rounding;
#else
#define fpi1 fpi
#endif
			switch((i = gethex(&s, &fpi1, &exp, &bb, sign)) & STRTOG_Retmask) {
			  case STRTOG_NoMemory:
				goto ovfl;
			  case STRTOG_NoNumber:
				s = s00;
				sign = 0;
			  case STRTOG_Zero:
				break;
			  default:
				if (bb) {
					copybits(bits, fpi.nbits, bb);
					Bfree(bb);
					}
				ULtod(((U*)&rv)->L, bits, exp, i);
			  }}
			goto ret;
		  }
		}
#endif /*}*/
		nz0 = 1;
		while(*++s == '0') ;
		if (!*s)
			goto ret;
		}
	s0 = s;
	y = z = 0;
	for(nd = nf = 0; (c = *s) >= '0' && c <= '9'; nd++, s++)
		if (nd < 9)
			y = 10*y + c - '0';
		else if (nd < 16)
			z = 10*z + c - '0';
	nd0 = nd;
#ifdef USE_LOCALE
	if (c == *decimalpoint) {
		for(i = 1; decimalpoint[i]; ++i)
			if (s[i] != decimalpoint[i])
				goto dig_done;
		s += i;
		c = *s;
#else
	if (c == '.') {
		c = *++s;
#endif
		decpt = 1;
		if (!nd) {
			for(; c == '0'; c = *++s)
				nz++;
			if (c > '0' && c <= '9') {
				s0 = s;
				nf += nz;
				nz = 0;
				goto have_dig;
				}
			goto dig_done;
			}
		for(; c >= '0' && c <= '9'; c = *++s) {
 have_dig:
			nz++;
			if (c -= '0') {
				nf += nz;
				for(i = 1; i < nz; i++)
					if (nd++ < 9)
						y *= 10;
					else if (nd <= DBL_DIG + 1)
						z *= 10;
				if (nd++ < 9)
					y = 10*y + c;
				else if (nd <= DBL_DIG + 1)
					z = 10*z + c;
				nz = 0;
				}
			}
		}/*}*/
 dig_done:
	e = 0;
	if (c == 'e' || c == 'E') {
		if (!nd && !nz && !nz0) {
			goto ret0;
			}
		s00 = s;
		esign = 0;
		switch(c = *++s) {
			case '-':
				esign = 1;
			case '+':
				c = *++s;
			}
		if (c >= '0' && c <= '9') {
			while(c == '0')
				c = *++s;
			if (c > '0' && c <= '9') {
				L = c - '0';
				s1 = s;
				while((c = *++s) >= '0' && c <= '9')
					L = 10*L + c - '0';
				if (s - s1 > 8 || L > 19999)
					/* Avoid confusion from exponents
					 * so large that e might overflow.
					 */
					e = 19999; /* safe for 16 bit ints */
				else
					e = (int)L;
				if (esign)
					e = -e;
				}
			else
				e = 0;
			}
		else
			s = s00;
		}
	if (!nd) {
		if (!nz && !nz0) {
#ifdef INFNAN_CHECK
			/* Check for Nan and Infinity */
			ULong bits[2];
			static FPI fpinan =	/* only 52 explicit bits */
				{ 52, 1-1023-53+1, 2046-1023-53+1, 1, SI };
			if (!decpt)
			 switch(c) {
			  case 'i':
			  case 'I':
				if (match(&s,"nf")) {
					--s;
					if (!match(&s,"inity"))
						++s;
					word0(&rv) = 0x7ff00000;
					word1(&rv) = 0;
					goto ret;
					}
				break;
			  case 'n':
			  case 'N':
				if (match(&s, "an")) {
#ifndef No_Hex_NaN
					if (*s == '(' /*)*/
					 && hexnan(&s, &fpinan, bits)
							== STRTOG_NaNbits) {
						word0(&rv) = 0x7ff00000 | bits[1];
						word1(&rv) = bits[0];
						}
					else {
#endif
						word0(&rv) = NAN_WORD0;
						word1(&rv) = NAN_WORD1;
#ifndef No_Hex_NaN
						}
#endif
					goto ret;
					}
			  }
#endif /* INFNAN_CHECK */
 ret0:
			s = s00;
			sign = 0;
			}
		goto ret;
		}
	e1 = e -= nf;

	/* Now we have nd0 digits, starting at s0, followed by a
	 * decimal point, followed by nd-nd0 digits.  The number we're
	 * after is the integer represented by those digits times
	 * 10**e */

	if (!nd0)
		nd0 = nd;
	k = nd < DBL_DIG + 1 ? nd : DBL_DIG + 1;
	dval(&rv) = y;
	if (k > 9) {
#ifdef SET_INEXACT
		if (k > DBL_DIG)
			oldinexact = get_inexact();
#endif
		dval(&rv) = tens[k - 9] * dval(&rv) + z;
		}
	if (nd <= DBL_DIG
#ifndef RND_PRODQUOT
#ifndef Honor_FLT_ROUNDS
		&& Flt_Rounds == 1
#endif
#endif
			) {
		if (!e)
			goto ret;
#ifndef ROUND_BIASED_without_Round_Up
		if (e > 0) {
			if (e <= Ten_pmax) {
#ifdef VAX
				goto vax_ovfl_check;
#else
#ifdef Honor_FLT_ROUNDS
				/* round correctly FLT_ROUNDS = 2 or 3 */
				if (sign) {
					rv.d = -rv.d;
					sign = 0;
					}
#endif
				/* rv = */ rounded_product(dval(&rv), tens[e]);
				goto ret;
#endif
				}
			i = DBL_DIG - nd;
			if (e <= Ten_pmax + i) {
				/* A fancier test would sometimes let us do
				 * this for larger i values.
				 */
#ifdef Honor_FLT_ROUNDS
				/* round correctly FLT_ROUNDS = 2 or 3 */
				if (sign) {
					rv.d = -rv.d;
					sign = 0;
					}
#endif
				e -= i;
				dval(&rv) *= tens[i];
#ifdef VAX
				/* VAX exponent range is so narrow we must
				 * worry about overflow here...
				 */
 vax_ovfl_check:
				word0(&rv) -= P*Exp_msk1;
				/* rv = */ rounded_product(dval(&rv), tens[e]);
				if ((word0(&rv) & Exp_mask)
				 > Exp_msk1*(DBL_MAX_EXP+Bias-1-P))
					goto ovfl;
				word0(&rv) += P*Exp_msk1;
#else
				/* rv = */ rounded_product(dval(&rv), tens[e]);
#endif
				goto ret;
				}
			}
#ifndef Inaccurate_Divide
		else if (e >= -Ten_pmax) {
#ifdef Honor_FLT_ROUNDS
			/* round correctly FLT_ROUNDS = 2 or 3 */
			if (sign) {
				rv.d = -rv.d;
				sign = 0;
				}
#endif
			/* rv = */ rounded_quotient(dval(&rv), tens[-e]);
			goto ret;
			}
#endif
#endif /* ROUND_BIASED_without_Round_Up */
		}
	e1 += nd - k;

#ifdef IEEE_Arith
#ifdef SET_INEXACT
	inexact = 1;
	if (k <= DBL_DIG)
		oldinexact = get_inexact();
#endif
#ifdef Avoid_Underflow
	scale = 0;
#endif
#ifdef Honor_FLT_ROUNDS
	if (Rounding >= 2) {
		if (sign)
			Rounding = Rounding == 2 ? 0 : 2;
		else
			if (Rounding != 2)
				Rounding = 0;
		}
#endif
#endif /*IEEE_Arith*/

	/* Get starting approximation = rv * 10**e1 */

	if (e1 > 0) {
		if ( (i = e1 & 15) !=0)
			dval(&rv) *= tens[i];
		if (e1 &= ~15) {
			if (e1 > DBL_MAX_10_EXP) {
 ovfl:
				/* Can't trust HUGE_VAL */
#ifdef IEEE_Arith
#ifdef Honor_FLT_ROUNDS
				switch(Rounding) {
				  case 0: /* toward 0 */
				  case 3: /* toward -infinity */
					word0(&rv) = Big0;
					word1(&rv) = Big1;
					break;
				  default:
					word0(&rv) = Exp_mask;
					word1(&rv) = 0;
				  }
#else /*Honor_FLT_ROUNDS*/
				word0(&rv) = Exp_mask;
				word1(&rv) = 0;
#endif /*Honor_FLT_ROUNDS*/
#ifdef SET_INEXACT
				/* set overflow bit */
				dval(&rv0) = 1e300;
				dval(&rv0) *= dval(&rv0);
#endif
#else /*IEEE_Arith*/
				word0(&rv) = Big0;
				word1(&rv) = Big1;
#endif /*IEEE_Arith*/
 range_err:
				if (bd0) {
					Bfree(bb);
					Bfree(bd);
					Bfree(bs);
					Bfree(bd0);
					Bfree(delta);
					}
#ifndef NO_ERRNO
				errno = ERANGE;
#endif
				goto ret;
				}
			e1 >>= 4;
			for(j = 0; e1 > 1; j++, e1 >>= 1)
				if (e1 & 1)
					dval(&rv) *= bigtens[j];
		/* The last multiplication could overflow. */
			word0(&rv) -= P*Exp_msk1;
			dval(&rv) *= bigtens[j];
			if ((z = word0(&rv) & Exp_mask)
			 > Exp_msk1*(DBL_MAX_EXP+Bias-P))
				goto ovfl;
			if (z > Exp_msk1*(DBL_MAX_EXP+Bias-1-P)) {
				/* set to largest number */
				/* (Can't trust DBL_MAX) */
				word0(&rv) = Big0;
				word1(&rv) = Big1;
				}
			else
				word0(&rv) += P*Exp_msk1;
			}
		}
	else if (e1 < 0) {
		e1 = -e1;
		if ( (i = e1 & 15) !=0)
			dval(&rv) /= tens[i];
		if (e1 >>= 4) {
			if (e1 >= 1 << n_bigtens)
				goto undfl;
#ifdef Avoid_Underflow
			if (e1 & Scale_Bit)
				scale = 2*P;
			for(j = 0; e1 > 0; j++, e1 >>= 1)
				if (e1 & 1)
					dval(&rv) *= tinytens[j];
			if (scale && (j = 2*P + 1 - ((word0(&rv) & Exp_mask)
						>> Exp_shift)) > 0) {
				/* scaled rv is denormal; zap j low bits */
				if (j >= 32) {
					word1(&rv) = 0;
					if (j >= 53)
					 word0(&rv) = (P+2)*Exp_msk1;
					else
					 word0(&rv) &= 0xffffffff << (j-32);
					}
				else
					word1(&rv) &= 0xffffffff << j;
				}
#else
			for(j = 0; e1 > 1; j++, e1 >>= 1)
				if (e1 & 1)
					dval(&rv) *= tinytens[j];
			/* The last multiplication could underflow. */
			dval(&rv0) = dval(&rv);
			dval(&rv) *= tinytens[j];
			if (!dval(&rv)) {
				dval(&rv) = 2.*dval(&rv0);
				dval(&rv) *= tinytens[j];
#endif
				if (!dval(&rv)) {
 undfl:
					dval(&rv) = 0.;
					goto range_err;
					}
#ifndef Avoid_Underflow
				word0(&rv) = Tiny0;
				word1(&rv) = Tiny1;
				/* The refinement below will clean
				 * this approximation up.
				 */
				}
#endif
			}
		}

	/* Now the hard part -- adjusting rv to the correct value.*/

	/* Put digits into bd: true value = bd * 10^e */

	bd0 = s2b(s0, nd0, nd, y, dplen);
	if (bd0 == NULL)
		goto ovfl;

	for(;;) {
		bd = Balloc(bd0->k);
		if (bd == NULL)
			goto ovfl;
		Bcopy(bd, bd0);
		bb = d2b(dval(&rv), &bbe, &bbbits);	/* rv = bb * 2^bbe */
		if (bb == NULL)
			goto ovfl;
		bs = i2b(1);
		if (bs == NULL)
			goto ovfl;

		if (e >= 0) {
			bb2 = bb5 = 0;
			bd2 = bd5 = e;
			}
		else {
			bb2 = bb5 = -e;
			bd2 = bd5 = 0;
			}
		if (bbe >= 0)
			bb2 += bbe;
		else
			bd2 -= bbe;
		bs2 = bb2;
#ifdef Honor_FLT_ROUNDS
		if (Rounding != 1)
			bs2++;
#endif
#ifdef Avoid_Underflow
		Lsb = LSB;
		Lsb1 = 0;
		j = bbe - scale;
		i = j + bbbits - 1;	/* logb(rv) */
		j = P + 1 - bbbits;
		if (i < Emin) {	/* denormal */
			i = Emin - i;
			j -= i;
			if (i < 32)
				Lsb <<= i;
			else
				Lsb1 = Lsb << (i-32);
			}
#else /*Avoid_Underflow*/
#ifdef Sudden_Underflow
#ifdef IBM
		j = 1 + 4*P - 3 - bbbits + ((bbe + bbbits - 1) & 3);
#else
		j = P + 1 - bbbits;
#endif
#else /*Sudden_Underflow*/
		j = bbe;
		i = j + bbbits - 1;	/* logb(&rv) */
		if (i < Emin)	/* denormal */
			j += P - Emin;
		else
			j = P + 1 - bbbits;
#endif /*Sudden_Underflow*/
#endif /*Avoid_Underflow*/
		bb2 += j;
		bd2 += j;
#ifdef Avoid_Underflow
		bd2 += scale;
#endif
		i = bb2 < bd2 ? bb2 : bd2;
		if (i > bs2)
			i = bs2;
		if (i > 0) {
			bb2 -= i;
			bd2 -= i;
			bs2 -= i;
			}
		if (bb5 > 0) {
			bs = pow5mult(bs, bb5);
			if (bs == NULL)
				goto ovfl;
			bb1 = mult(bs, bb);
			if (bb1 == NULL)
				goto ovfl;
			Bfree(bb);
			bb = bb1;
			}
		if (bb2 > 0) {
			bb = lshift(bb, bb2);
			if (bb == NULL)
				goto ovfl;
			}
		if (bd5 > 0) {
			bd = pow5mult(bd, bd5);
			if (bd == NULL)
				goto ovfl;
			}
		if (bd2 > 0) {
			bd = lshift(bd, bd2);
			if (bd == NULL)
				goto ovfl;
			}
		if (bs2 > 0) {
			bs = lshift(bs, bs2);
			if (bs == NULL)
				goto ovfl;
			}
		delta = diff(bb, bd);
		if (delta == NULL)
			goto ovfl;
		dsign = delta->sign;
		delta->sign = 0;
		i = cmp(delta, bs);
#ifdef Honor_FLT_ROUNDS
		if (Rounding != 1) {
			if (i < 0) {
				/* Error is less than an ulp */
				if (!delta->x[0] && delta->wds <= 1) {
					/* exact */
#ifdef SET_INEXACT
					inexact = 0;
#endif
					break;
					}
				if (Rounding) {
					if (dsign) {
						dval(&adj) = 1.;
						goto apply_adj;
						}
					}
				else if (!dsign) {
					dval(&adj) = -1.;
					if (!word1(&rv)
					 && !(word0(&rv) & Frac_mask)) {
						y = word0(&rv) & Exp_mask;
#ifdef Avoid_Underflow
						if (!scale || y > 2*P*Exp_msk1)
#else
						if (y)
#endif
						  {
						  delta = lshift(delta,Log2P);
						  if (delta == NULL)
							goto ovfl;
						  if (cmp(delta, bs) <= 0)
							dval(&adj) = -0.5;
						  }
						}
 apply_adj:
#ifdef Avoid_Underflow
					if (scale && (y = word0(&rv) & Exp_mask)
						<= 2*P*Exp_msk1)
					  word0(&adj) += (2*P+1)*Exp_msk1 - y;
#else
#ifdef Sudden_Underflow
					if ((word0(&rv) & Exp_mask) <=
							P*Exp_msk1) {
						word0(&rv) += P*Exp_msk1;
						dval(&rv) += adj*ulp(&rv);
						word0(&rv) -= P*Exp_msk1;
						}
					else
#endif /*Sudden_Underflow*/
#endif /*Avoid_Underflow*/
					dval(&rv) += adj.d*ulp(&rv);
					}
				break;
				}
			dval(&adj) = ratio(delta, bs);
			if (adj.d < 1.)
				dval(&adj) = 1.;
			if (adj.d <= 0x7ffffffe) {
				/* dval(&adj) = Rounding ? ceil(&adj) : floor(&adj); */
				y = adj.d;
				if (y != adj.d) {
					if (!((Rounding>>1) ^ dsign))
						y++;
					dval(&adj) = y;
					}
				}
#ifdef Avoid_Underflow
			if (scale && (y = word0(&rv) & Exp_mask) <= 2*P*Exp_msk1)
				word0(&adj) += (2*P+1)*Exp_msk1 - y;
#else
#ifdef Sudden_Underflow
			if ((word0(&rv) & Exp_mask) <= P*Exp_msk1) {
				word0(&rv) += P*Exp_msk1;
				dval(&adj) *= ulp(&rv);
				if (dsign)
					dval(&rv) += adj;
				else
					dval(&rv) -= adj;
				word0(&rv) -= P*Exp_msk1;
				goto cont;
				}
#endif /*Sudden_Underflow*/
#endif /*Avoid_Underflow*/
			dval(&adj) *= ulp(&rv);
			if (dsign) {
				if (word0(&rv) == Big0 && word1(&rv) == Big1)
					goto ovfl;
				dval(&rv) += adj.d;
				}
			else
				dval(&rv) -= adj.d;
			goto cont;
			}
#endif /*Honor_FLT_ROUNDS*/

		if (i < 0) {
			/* Error is less than half an ulp -- check for
			 * special case of mantissa a power of two.
			 */
			if (dsign || word1(&rv) || word0(&rv) & Bndry_mask
#ifdef IEEE_Arith
#ifdef Avoid_Underflow
			 || (word0(&rv) & Exp_mask) <= (2*P+1)*Exp_msk1
#else
			 || (word0(&rv) & Exp_mask) <= Exp_msk1
#endif
#endif
				) {
#ifdef SET_INEXACT
				if (!delta->x[0] && delta->wds <= 1)
					inexact = 0;
#endif
				break;
				}
			if (!delta->x[0] && delta->wds <= 1) {
				/* exact result */
#ifdef SET_INEXACT
				inexact = 0;
#endif
				break;
				}
			delta = lshift(delta,Log2P);
			if (delta == NULL)
				goto ovfl;
			if (cmp(delta, bs) > 0)
				goto drop_down;
			break;
			}
		if (i == 0) {
			/* exactly half-way between */
			if (dsign) {
				if ((word0(&rv) & Bndry_mask1) == Bndry_mask1
				 &&  word1(&rv) == (
#ifdef Avoid_Underflow
			(scale && (y = word0(&rv) & Exp_mask) <= 2*P*Exp_msk1)
		? (0xffffffff & (0xffffffff << (2*P+1-(y>>Exp_shift)))) :
#endif
						   0xffffffff)) {
					/*boundary case -- increment exponent*/
					if (word0(&rv) == Big0 && word1(&rv) == Big1)
						goto ovfl;
					word0(&rv) = (word0(&rv) & Exp_mask)
						+ Exp_msk1
#ifdef IBM
						| Exp_msk1 >> 4
#endif
						;
					word1(&rv) = 0;
#ifdef Avoid_Underflow
					dsign = 0;
#endif
					break;
					}
				}
			else if (!(word0(&rv) & Bndry_mask) && !word1(&rv)) {
 drop_down:
				/* boundary case -- decrement exponent */
#ifdef Sudden_Underflow /*{{*/
				L = word0(&rv) & Exp_mask;
#ifdef IBM
				if (L <  Exp_msk1)
#else
#ifdef Avoid_Underflow
				if (L <= (scale ? (2*P+1)*Exp_msk1 : Exp_msk1))
#else
				if (L <= Exp_msk1)
#endif /*Avoid_Underflow*/
#endif /*IBM*/
					goto undfl;
				L -= Exp_msk1;
#else /*Sudden_Underflow}{*/
#ifdef Avoid_Underflow
				if (scale) {
					L = word0(&rv) & Exp_mask;
					if (L <= (2*P+1)*Exp_msk1) {
						if (L > (P+2)*Exp_msk1)
							/* round even ==> */
							/* accept rv */
							break;
						/* rv = smallest denormal */
						goto undfl;
						}
					}
#endif /*Avoid_Underflow*/
				L = (word0(&rv) & Exp_mask) - Exp_msk1;
#endif /*Sudden_Underflow}}*/
				word0(&rv) = L | Bndry_mask1;
				word1(&rv) = 0xffffffff;
#ifdef IBM
				goto cont;
#else
				break;
#endif
				}
#ifndef ROUND_BIASED
#ifdef Avoid_Underflow
			if (Lsb1) {
				if (!(word0(&rv) & Lsb1))
					break;
				}
			else if (!(word1(&rv) & Lsb))
				break;
#else
			if (!(word1(&rv) & LSB))
				break;
#endif
#endif
			if (dsign)
#ifdef Avoid_Underflow
				dval(&rv) += sulp(&rv, scale);
#else
				dval(&rv) += ulp(&rv);
#endif
#ifndef ROUND_BIASED
			else {
#ifdef Avoid_Underflow
				dval(&rv) -= sulp(&rv, scale);
#else
				dval(&rv) -= ulp(&rv);
#endif
#ifndef Sudden_Underflow
				if (!dval(&rv))
					goto undfl;
#endif
				}
#ifdef Avoid_Underflow
			dsign = 1 - dsign;
#endif
#endif
			break;
			}
		if ((aadj = ratio(delta, bs)) <= 2.) {
			if (dsign)
				aadj = dval(&aadj1) = 1.;
			else if (word1(&rv) || word0(&rv) & Bndry_mask) {
#ifndef Sudden_Underflow
				if (word1(&rv) == Tiny1 && !word0(&rv))
					goto undfl;
#endif
				aadj = 1.;
				dval(&aadj1) = -1.;
				}
			else {
				/* special case -- power of FLT_RADIX to be */
				/* rounded down... */

				if (aadj < 2./FLT_RADIX)
					aadj = 1./FLT_RADIX;
				else
					aadj *= 0.5;
				dval(&aadj1) = -aadj;
				}
			}
		else {
			aadj *= 0.5;
			dval(&aadj1) = dsign ? aadj : -aadj;
#ifdef Check_FLT_ROUNDS
			switch(Rounding) {
				case 2: /* towards +infinity */
					dval(&aadj1) -= 0.5;
					break;
				case 0: /* towards 0 */
				case 3: /* towards -infinity */
					dval(&aadj1) += 0.5;
				}
#else
			if (Flt_Rounds == 0)
				dval(&aadj1) += 0.5;
#endif /*Check_FLT_ROUNDS*/
			}
		y = word0(&rv) & Exp_mask;

		/* Check for overflow */

		if (y == Exp_msk1*(DBL_MAX_EXP+Bias-1)) {
			dval(&rv0) = dval(&rv);
			word0(&rv) -= P*Exp_msk1;
			dval(&adj) = dval(&aadj1) * ulp(&rv);
			dval(&rv) += dval(&adj);
			if ((word0(&rv) & Exp_mask) >=
					Exp_msk1*(DBL_MAX_EXP+Bias-P)) {
				if (word0(&rv0) == Big0 && word1(&rv0) == Big1)
					goto ovfl;
				word0(&rv) = Big0;
				word1(&rv) = Big1;
				goto cont;
				}
			else
				word0(&rv) += P*Exp_msk1;
			}
		else {
#ifdef Avoid_Underflow
			if (scale && y <= 2*P*Exp_msk1) {
				if (aadj <= 0x7fffffff) {
					if ((z = aadj) <= 0)
						z = 1;
					aadj = z;
					dval(&aadj1) = dsign ? aadj : -aadj;
					}
				word0(&aadj1) += (2*P+1)*Exp_msk1 - y;
				}
			dval(&adj) = dval(&aadj1) * ulp(&rv);
			dval(&rv) += dval(&adj);
#else
#ifdef Sudden_Underflow
			if ((word0(&rv) & Exp_mask) <= P*Exp_msk1) {
				dval(&rv0) = dval(&rv);
				word0(&rv) += P*Exp_msk1;
				dval(&adj) = dval(&aadj1) * ulp(&rv);
				dval(&rv) += dval(&adj);
#ifdef IBM
				if ((word0(&rv) & Exp_mask) <  P*Exp_msk1)
#else
				if ((word0(&rv) & Exp_mask) <= P*Exp_msk1)
#endif
					{
					if (word0(&rv0) == Tiny0
					 && word1(&rv0) == Tiny1)
						goto undfl;
					word0(&rv) = Tiny0;
					word1(&rv) = Tiny1;
					goto cont;
					}
				else
					word0(&rv) -= P*Exp_msk1;
				}
			else {
				dval(&adj) = dval(&aadj1) * ulp(&rv);
				dval(&rv) += dval(&adj);
				}
#else /*Sudden_Underflow*/
			/* Compute dval(&adj) so that the IEEE rounding rules will
			 * correctly round rv + dval(&adj) in some half-way cases.
			 * If rv * ulp(&rv) is denormalized (i.e.,
			 * y <= (P-1)*Exp_msk1), we must adjust aadj to avoid
			 * trouble from bits lost to denormalization;
			 * example: 1.2e-307 .
			 */
			if (y <= (P-1)*Exp_msk1 && aadj > 1.) {
				dval(&aadj1) = (double)(int)(aadj + 0.5);
				if (!dsign)
					dval(&aadj1) = -dval(&aadj1);
				}
			dval(&adj) = dval(&aadj1) * ulp(&rv);
			dval(&rv) += adj;
#endif /*Sudden_Underflow*/
#endif /*Avoid_Underflow*/
			}
		z = word0(&rv) & Exp_mask;
#ifndef SET_INEXACT
#ifdef Avoid_Underflow
		if (!scale)
#endif
		if (y == z) {
			/* Can we stop now? */
			L = (Long)aadj;
			aadj -= L;
			/* The tolerances below are conservative. */
			if (dsign || word1(&rv) || word0(&rv) & Bndry_mask) {
				if (aadj < .4999999 || aadj > .5000001)
					break;
				}
			else if (aadj < .4999999/FLT_RADIX)
				break;
			}
#endif
 cont:
		Bfree(bb);
		Bfree(bd);
		Bfree(bs);
		Bfree(delta);
		}
	Bfree(bb);
	Bfree(bd);
	Bfree(bs);
	Bfree(bd0);
	Bfree(delta);
#ifdef SET_INEXACT
	if (inexact) {
		if (!oldinexact) {
			word0(&rv0) = Exp_1 + (70 << Exp_shift);
			word1(&rv0) = 0;
			dval(&rv0) += 1.;
			}
		}
	else if (!oldinexact)
		clear_inexact();
#endif
#ifdef Avoid_Underflow
	if (scale) {
		word0(&rv0) = Exp_1 - 2*P*Exp_msk1;
		word1(&rv0) = 0;
		dval(&rv) *= dval(&rv0);
#ifndef NO_ERRNO
		/* try to avoid the bug of testing an 8087 register value */
#ifdef IEEE_Arith
		if (!(word0(&rv) & Exp_mask))
#else
		if (word0(&rv) == 0 && word1(&rv) == 0)
#endif
			errno = ERANGE;
#endif
		}
#endif /* Avoid_Underflow */
#ifdef SET_INEXACT
	if (inexact && !(word0(&rv) & Exp_mask)) {
		/* set underflow bit */
		dval(&rv0) = 1e-300;
		dval(&rv0) *= dval(&rv0);
		}
#endif
 ret:
	if (se)
		*se = (char *)s;
	return sign ? -dval(&rv) : dval(&rv);
	}
DEF_STRONG(strtod);

// SPDX-License-Identifier: GPL-2.0

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.
 */

#include <linux/libgcc.h>

#define count_leading_zeros(COUNT, X)   ((COUNT) = __builtin_clz(X))

#define W_TYPE_SIZE 32

#define __ll_B ((unsigned long) 1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((unsigned long) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((unsigned long) (t) >> (W_TYPE_SIZE / 2))

/* If we still don't have umul_ppmm, define it using plain C. */
#if !defined(umul_ppmm)
#define umul_ppmm(w1, w0, u, v)						\
	do {								\
		unsigned long __x0, __x1, __x2, __x3;			\
		unsigned short __ul, __vl, __uh, __vh;			\
									\
		__ul = __ll_lowpart(u);					\
		__uh = __ll_highpart(u);				\
		__vl = __ll_lowpart(v);					\
		__vh = __ll_highpart(v);				\
									\
		__x0 = (unsigned long) __ul * __vl;			\
		__x1 = (unsigned long) __ul * __vh;			\
		__x2 = (unsigned long) __uh * __vl;			\
		__x3 = (unsigned long) __uh * __vh;			\
									\
		__x1 += __ll_highpart(__x0);				\
		__x1 += __x2;						\
		if (__x1 < __x2)					\
			__x3 += __ll_B;					\
									\
		(w1) = __x3 + __ll_highpart(__x1);			\
		(w0) = __ll_lowpart(__x1) * __ll_B + __ll_lowpart(__x0);\
	} while (0)
#endif

#if !defined(sub_ddmmss)
#define sub_ddmmss(sh, sl, ah, al, bh, bl)				\
	do {								\
		unsigned long __x;					\
		__x = (al) - (bl);					\
		(sh) = (ah) - (bh) - (__x > (al));			\
		(sl) = __x;						\
	} while (0)
#endif

/* Define this unconditionally, so it can be used for debugging. */
#define __udiv_qrnnd_c(q, r, n1, n0, d)					\
	do {								\
		unsigned long __d1, __d0, __q1, __q0;			\
		unsigned long __r1, __r0, __m;				\
		__d1 = __ll_highpart(d);				\
		__d0 = __ll_lowpart(d);				\
									\
		__r1 = (n1) % __d1;					\
		__q1 = (n1) / __d1;					\
		__m = (unsigned long) __q1 * __d0;			\
		__r1 = __r1 * __ll_B | __ll_highpart(n0);		\
		if (__r1 < __m) {					\
			__q1--, __r1 += (d);				\
			if (__r1 >= (d))				\
				if (__r1 < __m)				\
					__q1--, __r1 += (d);		\
		}							\
		__r1 -= __m;						\
									\
		__r0 = __r1 % __d1;					\
		__q0 = __r1 / __d1;					\
		__m = (unsigned long) __q0 * __d0;			\
		__r0 = __r0 * __ll_B | __ll_lowpart(n0);		\
		if (__r0 < __m) {					\
			__q0--, __r0 += (d);				\
			if (__r0 >= (d))				\
				if (__r0 < __m)				\
					__q0--, __r0 += (d);		\
		}							\
		__r0 -= __m;						\
									\
		(q) = (unsigned long) __q1 * __ll_B | __q0;		\
		(r) = __r0;						\
	} while (0)

/* If udiv_qrnnd was not defined for this processor, use __udiv_qrnnd_c. */
#if !defined(udiv_qrnnd)
#define UDIV_NEEDS_NORMALIZATION 1
#define udiv_qrnnd __udiv_qrnnd_c
#endif

unsigned long long __udivmoddi4(unsigned long long u, unsigned long long v,
				unsigned long long *rp)
{
	const DWunion nn = {.ll = u };
	const DWunion dd = {.ll = v };
	DWunion rr, ww;
	unsigned long d0, d1, n0, n1, n2;
	unsigned long q0 = 0, q1 = 0;
	unsigned long b, bm;

	d0 = dd.s.low;
	d1 = dd.s.high;
	n0 = nn.s.low;
	n1 = nn.s.high;

#if !UDIV_NEEDS_NORMALIZATION

	if (d1 == 0) {
		if (d0 > n1) {
			/* 0q = nn / 0D */

			udiv_qrnnd(q0, n0, n1, n0, d0);
			q1 = 0;

			/* Remainder in n0. */
		} else {
			/* qq = NN / 0d */

			if (d0 == 0)
				/* Divide intentionally by zero. */
				d0 = 1 / d0;

			udiv_qrnnd(q1, n1, 0, n1, d0);
			udiv_qrnnd(q0, n0, n1, n0, d0);

			/* Remainder in n0. */
		}

		if (rp != 0) {
			rr.s.low = n0;
			rr.s.high = 0;
			*rp = rr.ll;
		}

#else /* UDIV_NEEDS_NORMALIZATION */

	if (d1 == 0) {
		if (d0 > n1) {
			/* 0q = nn / 0D */

			count_leading_zeros(bm, d0);

			if (bm != 0) {
				/*
				 * Normalize, i.e. make the most significant bit
				 * of the denominator set.
				 */

				d0 = d0 << bm;
				n1 = (n1 << bm) | (n0 >> (W_TYPE_SIZE - bm));
				n0 = n0 << bm;
			}

			udiv_qrnnd(q0, n0, n1, n0, d0);
			q1 = 0;

			/* Remainder in n0 >> bm. */
		} else {
			/* qq = NN / 0d */

			if (d0 == 0)
				/* Divide intentionally by zero. */
				d0 = 1 / d0;

			count_leading_zeros(bm, d0);

			if (bm == 0) {
				/*
				 * From (n1 >= d0) /\ (the most significant bit
				 * of d0 is set), conclude (the most significant
				 * bit of n1 is set) /\ (theleading quotient
				 * digit q1 = 1).
				 *
				 * This special case is necessary, not an
				 * optimization. (Shifts counts of W_TYPE_SIZE
				 * are undefined.)
				 */

				n1 -= d0;
				q1 = 1;
			} else {
				/* Normalize. */

				b = W_TYPE_SIZE - bm;

				d0 = d0 << bm;
				n2 = n1 >> b;
				n1 = (n1 << bm) | (n0 >> b);
				n0 = n0 << bm;

				udiv_qrnnd(q1, n1, n2, n1, d0);
			}

			/* n1 != d0... */

			udiv_qrnnd(q0, n0, n1, n0, d0);

			/* Remainder in n0 >> bm. */
		}

		if (rp != 0) {
			rr.s.low = n0 >> bm;
			rr.s.high = 0;
			*rp = rr.ll;
		}

#endif /* UDIV_NEEDS_NORMALIZATION */

	} else {
		if (d1 > n1) {
			/* 00 = nn / DD */

			q0 = 0;
			q1 = 0;

			/* Remainder in n1n0. */
			if (rp != 0) {
				rr.s.low = n0;
				rr.s.high = n1;
				*rp = rr.ll;
			}
		} else {
			/* 0q = NN / dd */

			count_leading_zeros(bm, d1);
			if (bm == 0) {
				/*
				 * From (n1 >= d1) /\ (the most significant bit
				 * of d1 is set), conclude (the most significant
				 * bit of n1 is set) /\ (the quotient digit q0 =
				 * 0 or 1).
				 *
				 * This special case is necessary, not an
				 * optimization.
				 */

				/*
				 * The condition on the next line takes
				 * advantage of that n1 >= d1 (true due to
				 * program flow).
				 */
				if (n1 > d1 || n0 >= d0) {
					q0 = 1;
					sub_ddmmss(n1, n0, n1, n0, d1, d0);
				} else {
					q0 = 0;
				}

				q1 = 0;

				if (rp != 0) {
					rr.s.low = n0;
					rr.s.high = n1;
					*rp = rr.ll;
				}
			} else {
				unsigned long m1, m0;
				/* Normalize. */

				b = W_TYPE_SIZE - bm;

				d1 = (d1 << bm) | (d0 >> b);
				d0 = d0 << bm;
				n2 = n1 >> b;
				n1 = (n1 << bm) | (n0 >> b);
				n0 = n0 << bm;

				udiv_qrnnd(q0, n1, n2, n1, d1);
				umul_ppmm(m1, m0, q0, d0);

				if (m1 > n1 || (m1 == n1 && m0 > n0)) {
					q0--;
					sub_ddmmss(m1, m0, m1, m0, d1, d0);
				}

				q1 = 0;

				/* Remainder in (n1n0 - m1m0) >> bm. */
				if (rp != 0) {
					sub_ddmmss(n1, n0, n1, n0, m1, m0);
					rr.s.low = (n1 << b) | (n0 >> bm);
					rr.s.high = n1 >> bm;
					*rp = rr.ll;
				}
			}
		}
	}

	ww.s.low = q0;
	ww.s.high = q1;

	return ww.ll;
}

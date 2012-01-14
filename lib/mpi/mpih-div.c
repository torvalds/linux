/* mpihelp-div.c  -  MPI helper functions
 *	Copyright (C) 1994, 1996 Free Software Foundation, Inc.
 *	Copyright (C) 1998, 1999 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 *	 The GNU MP Library itself is published under the LGPL;
 *	 however I decided to publish this code under the plain GPL.
 */

#include "mpi-internal.h"
#include "longlong.h"

#ifndef UMUL_TIME
#define UMUL_TIME 1
#endif
#ifndef UDIV_TIME
#define UDIV_TIME UMUL_TIME
#endif

/* FIXME: We should be using invert_limb (or invert_normalized_limb)
 * here (not udiv_qrnnd).
 */

mpi_limb_t
mpihelp_mod_1(mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
	      mpi_limb_t divisor_limb)
{
	mpi_size_t i;
	mpi_limb_t n1, n0, r;
	int dummy;

	/* Botch: Should this be handled at all?  Rely on callers?  */
	if (!dividend_size)
		return 0;

	/* If multiplication is much faster than division, and the
	 * dividend is large, pre-invert the divisor, and use
	 * only multiplications in the inner loop.
	 *
	 * This test should be read:
	 *   Does it ever help to use udiv_qrnnd_preinv?
	 *     && Does what we save compensate for the inversion overhead?
	 */
	if (UDIV_TIME > (2 * UMUL_TIME + 6)
	    && (UDIV_TIME - (2 * UMUL_TIME + 6)) * dividend_size > UDIV_TIME) {
		int normalization_steps;

		count_leading_zeros(normalization_steps, divisor_limb);
		if (normalization_steps) {
			mpi_limb_t divisor_limb_inverted;

			divisor_limb <<= normalization_steps;

			/* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
			 * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
			 * most significant bit (with weight 2**N) implicit.
			 *
			 * Special case for DIVISOR_LIMB == 100...000.
			 */
			if (!(divisor_limb << 1))
				divisor_limb_inverted = ~(mpi_limb_t) 0;
			else
				udiv_qrnnd(divisor_limb_inverted, dummy,
					   -divisor_limb, 0, divisor_limb);

			n1 = dividend_ptr[dividend_size - 1];
			r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);

			/* Possible optimization:
			 * if (r == 0
			 * && divisor_limb > ((n1 << normalization_steps)
			 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
			 * ...one division less...
			 */
			for (i = dividend_size - 2; i >= 0; i--) {
				n0 = dividend_ptr[i];
				UDIV_QRNND_PREINV(dummy, r, r,
						  ((n1 << normalization_steps)
						   | (n0 >>
						      (BITS_PER_MPI_LIMB -
						       normalization_steps))),
						  divisor_limb,
						  divisor_limb_inverted);
				n1 = n0;
			}
			UDIV_QRNND_PREINV(dummy, r, r,
					  n1 << normalization_steps,
					  divisor_limb, divisor_limb_inverted);
			return r >> normalization_steps;
		} else {
			mpi_limb_t divisor_limb_inverted;

			/* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
			 * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
			 * most significant bit (with weight 2**N) implicit.
			 *
			 * Special case for DIVISOR_LIMB == 100...000.
			 */
			if (!(divisor_limb << 1))
				divisor_limb_inverted = ~(mpi_limb_t) 0;
			else
				udiv_qrnnd(divisor_limb_inverted, dummy,
					   -divisor_limb, 0, divisor_limb);

			i = dividend_size - 1;
			r = dividend_ptr[i];

			if (r >= divisor_limb)
				r = 0;
			else
				i--;

			for (; i >= 0; i--) {
				n0 = dividend_ptr[i];
				UDIV_QRNND_PREINV(dummy, r, r,
						  n0, divisor_limb,
						  divisor_limb_inverted);
			}
			return r;
		}
	} else {
		if (UDIV_NEEDS_NORMALIZATION) {
			int normalization_steps;

			count_leading_zeros(normalization_steps, divisor_limb);
			if (normalization_steps) {
				divisor_limb <<= normalization_steps;

				n1 = dividend_ptr[dividend_size - 1];
				r = n1 >> (BITS_PER_MPI_LIMB -
					   normalization_steps);

				/* Possible optimization:
				 * if (r == 0
				 * && divisor_limb > ((n1 << normalization_steps)
				 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
				 * ...one division less...
				 */
				for (i = dividend_size - 2; i >= 0; i--) {
					n0 = dividend_ptr[i];
					udiv_qrnnd(dummy, r, r,
						   ((n1 << normalization_steps)
						    | (n0 >>
						       (BITS_PER_MPI_LIMB -
							normalization_steps))),
						   divisor_limb);
					n1 = n0;
				}
				udiv_qrnnd(dummy, r, r,
					   n1 << normalization_steps,
					   divisor_limb);
				return r >> normalization_steps;
			}
		}
		/* No normalization needed, either because udiv_qrnnd doesn't require
		 * it, or because DIVISOR_LIMB is already normalized.  */
		i = dividend_size - 1;
		r = dividend_ptr[i];

		if (r >= divisor_limb)
			r = 0;
		else
			i--;

		for (; i >= 0; i--) {
			n0 = dividend_ptr[i];
			udiv_qrnnd(dummy, r, r, n0, divisor_limb);
		}
		return r;
	}
}

/* Divide num (NP/NSIZE) by den (DP/DSIZE) and write
 * the NSIZE-DSIZE least significant quotient limbs at QP
 * and the DSIZE long remainder at NP.	If QEXTRA_LIMBS is
 * non-zero, generate that many fraction bits and append them after the
 * other quotient limbs.
 * Return the most significant limb of the quotient, this is always 0 or 1.
 *
 * Preconditions:
 * 0. NSIZE >= DSIZE.
 * 1. The most significant bit of the divisor must be set.
 * 2. QP must either not overlap with the input operands at all, or
 *    QP + DSIZE >= NP must hold true.	(This means that it's
 *    possible to put the quotient in the high part of NUM, right after the
 *    remainder in NUM.
 * 3. NSIZE >= DSIZE, even if QEXTRA_LIMBS is non-zero.
 */

mpi_limb_t
mpihelp_divrem(mpi_ptr_t qp, mpi_size_t qextra_limbs,
	       mpi_ptr_t np, mpi_size_t nsize, mpi_ptr_t dp, mpi_size_t dsize)
{
	mpi_limb_t most_significant_q_limb = 0;

	switch (dsize) {
	case 0:
		/* We are asked to divide by zero, so go ahead and do it!  (To make
		   the compiler not remove this statement, return the value.)  */
		return 1 / dsize;

	case 1:
		{
			mpi_size_t i;
			mpi_limb_t n1;
			mpi_limb_t d;

			d = dp[0];
			n1 = np[nsize - 1];

			if (n1 >= d) {
				n1 -= d;
				most_significant_q_limb = 1;
			}

			qp += qextra_limbs;
			for (i = nsize - 2; i >= 0; i--)
				udiv_qrnnd(qp[i], n1, n1, np[i], d);
			qp -= qextra_limbs;

			for (i = qextra_limbs - 1; i >= 0; i--)
				udiv_qrnnd(qp[i], n1, n1, 0, d);

			np[0] = n1;
		}
		break;

	case 2:
		{
			mpi_size_t i;
			mpi_limb_t n1, n0, n2;
			mpi_limb_t d1, d0;

			np += nsize - 2;
			d1 = dp[1];
			d0 = dp[0];
			n1 = np[1];
			n0 = np[0];

			if (n1 >= d1 && (n1 > d1 || n0 >= d0)) {
				sub_ddmmss(n1, n0, n1, n0, d1, d0);
				most_significant_q_limb = 1;
			}

			for (i = qextra_limbs + nsize - 2 - 1; i >= 0; i--) {
				mpi_limb_t q;
				mpi_limb_t r;

				if (i >= qextra_limbs)
					np--;
				else
					np[0] = 0;

				if (n1 == d1) {
					/* Q should be either 111..111 or 111..110.  Need special
					 * treatment of this rare case as normal division would
					 * give overflow.  */
					q = ~(mpi_limb_t) 0;

					r = n0 + d1;
					if (r < d1) {	/* Carry in the addition? */
						add_ssaaaa(n1, n0, r - d0,
							   np[0], 0, d0);
						qp[i] = q;
						continue;
					}
					n1 = d0 - (d0 != 0 ? 1 : 0);
					n0 = -d0;
				} else {
					udiv_qrnnd(q, r, n1, n0, d1);
					umul_ppmm(n1, n0, d0, q);
				}

				n2 = np[0];
q_test:
				if (n1 > r || (n1 == r && n0 > n2)) {
					/* The estimated Q was too large.  */
					q--;
					sub_ddmmss(n1, n0, n1, n0, 0, d0);
					r += d1;
					if (r >= d1)	/* If not carry, test Q again.  */
						goto q_test;
				}

				qp[i] = q;
				sub_ddmmss(n1, n0, r, n2, n1, n0);
			}
			np[1] = n1;
			np[0] = n0;
		}
		break;

	default:
		{
			mpi_size_t i;
			mpi_limb_t dX, d1, n0;

			np += nsize - dsize;
			dX = dp[dsize - 1];
			d1 = dp[dsize - 2];
			n0 = np[dsize - 1];

			if (n0 >= dX) {
				if (n0 > dX
				    || mpihelp_cmp(np, dp, dsize - 1) >= 0) {
					mpihelp_sub_n(np, np, dp, dsize);
					n0 = np[dsize - 1];
					most_significant_q_limb = 1;
				}
			}

			for (i = qextra_limbs + nsize - dsize - 1; i >= 0; i--) {
				mpi_limb_t q;
				mpi_limb_t n1, n2;
				mpi_limb_t cy_limb;

				if (i >= qextra_limbs) {
					np--;
					n2 = np[dsize];
				} else {
					n2 = np[dsize - 1];
					MPN_COPY_DECR(np + 1, np, dsize - 1);
					np[0] = 0;
				}

				if (n0 == dX) {
					/* This might over-estimate q, but it's probably not worth
					 * the extra code here to find out.  */
					q = ~(mpi_limb_t) 0;
				} else {
					mpi_limb_t r;

					udiv_qrnnd(q, r, n0, np[dsize - 1], dX);
					umul_ppmm(n1, n0, d1, q);

					while (n1 > r
					       || (n1 == r
						   && n0 > np[dsize - 2])) {
						q--;
						r += dX;
						if (r < dX)	/* I.e. "carry in previous addition?" */
							break;
						n1 -= n0 < d1;
						n0 -= d1;
					}
				}

				/* Possible optimization: We already have (q * n0) and (1 * n1)
				 * after the calculation of q.  Taking advantage of that, we
				 * could make this loop make two iterations less.  */
				cy_limb = mpihelp_submul_1(np, dp, dsize, q);

				if (n2 != cy_limb) {
					mpihelp_add_n(np, np, dp, dsize);
					q--;
				}

				qp[i] = q;
				n0 = np[dsize - 1];
			}
		}
	}

	return most_significant_q_limb;
}

/****************
 * Divide (DIVIDEND_PTR,,DIVIDEND_SIZE) by DIVISOR_LIMB.
 * Write DIVIDEND_SIZE limbs of quotient at QUOT_PTR.
 * Return the single-limb remainder.
 * There are no constraints on the value of the divisor.
 *
 * QUOT_PTR and DIVIDEND_PTR might point to the same limb.
 */

mpi_limb_t
mpihelp_divmod_1(mpi_ptr_t quot_ptr,
		 mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
		 mpi_limb_t divisor_limb)
{
	mpi_size_t i;
	mpi_limb_t n1, n0, r;
	int dummy;

	if (!dividend_size)
		return 0;

	/* If multiplication is much faster than division, and the
	 * dividend is large, pre-invert the divisor, and use
	 * only multiplications in the inner loop.
	 *
	 * This test should be read:
	 * Does it ever help to use udiv_qrnnd_preinv?
	 * && Does what we save compensate for the inversion overhead?
	 */
	if (UDIV_TIME > (2 * UMUL_TIME + 6)
	    && (UDIV_TIME - (2 * UMUL_TIME + 6)) * dividend_size > UDIV_TIME) {
		int normalization_steps;

		count_leading_zeros(normalization_steps, divisor_limb);
		if (normalization_steps) {
			mpi_limb_t divisor_limb_inverted;

			divisor_limb <<= normalization_steps;

			/* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
			 * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
			 * most significant bit (with weight 2**N) implicit.
			 */
			/* Special case for DIVISOR_LIMB == 100...000.  */
			if (!(divisor_limb << 1))
				divisor_limb_inverted = ~(mpi_limb_t) 0;
			else
				udiv_qrnnd(divisor_limb_inverted, dummy,
					   -divisor_limb, 0, divisor_limb);

			n1 = dividend_ptr[dividend_size - 1];
			r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);

			/* Possible optimization:
			 * if (r == 0
			 * && divisor_limb > ((n1 << normalization_steps)
			 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
			 * ...one division less...
			 */
			for (i = dividend_size - 2; i >= 0; i--) {
				n0 = dividend_ptr[i];
				UDIV_QRNND_PREINV(quot_ptr[i + 1], r, r,
						  ((n1 << normalization_steps)
						   | (n0 >>
						      (BITS_PER_MPI_LIMB -
						       normalization_steps))),
						  divisor_limb,
						  divisor_limb_inverted);
				n1 = n0;
			}
			UDIV_QRNND_PREINV(quot_ptr[0], r, r,
					  n1 << normalization_steps,
					  divisor_limb, divisor_limb_inverted);
			return r >> normalization_steps;
		} else {
			mpi_limb_t divisor_limb_inverted;

			/* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
			 * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
			 * most significant bit (with weight 2**N) implicit.
			 */
			/* Special case for DIVISOR_LIMB == 100...000.  */
			if (!(divisor_limb << 1))
				divisor_limb_inverted = ~(mpi_limb_t) 0;
			else
				udiv_qrnnd(divisor_limb_inverted, dummy,
					   -divisor_limb, 0, divisor_limb);

			i = dividend_size - 1;
			r = dividend_ptr[i];

			if (r >= divisor_limb)
				r = 0;
			else
				quot_ptr[i--] = 0;

			for (; i >= 0; i--) {
				n0 = dividend_ptr[i];
				UDIV_QRNND_PREINV(quot_ptr[i], r, r,
						  n0, divisor_limb,
						  divisor_limb_inverted);
			}
			return r;
		}
	} else {
		if (UDIV_NEEDS_NORMALIZATION) {
			int normalization_steps;

			count_leading_zeros(normalization_steps, divisor_limb);
			if (normalization_steps) {
				divisor_limb <<= normalization_steps;

				n1 = dividend_ptr[dividend_size - 1];
				r = n1 >> (BITS_PER_MPI_LIMB -
					   normalization_steps);

				/* Possible optimization:
				 * if (r == 0
				 * && divisor_limb > ((n1 << normalization_steps)
				 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
				 * ...one division less...
				 */
				for (i = dividend_size - 2; i >= 0; i--) {
					n0 = dividend_ptr[i];
					udiv_qrnnd(quot_ptr[i + 1], r, r,
						   ((n1 << normalization_steps)
						    | (n0 >>
						       (BITS_PER_MPI_LIMB -
							normalization_steps))),
						   divisor_limb);
					n1 = n0;
				}
				udiv_qrnnd(quot_ptr[0], r, r,
					   n1 << normalization_steps,
					   divisor_limb);
				return r >> normalization_steps;
			}
		}
		/* No normalization needed, either because udiv_qrnnd doesn't require
		 * it, or because DIVISOR_LIMB is already normalized.  */
		i = dividend_size - 1;
		r = dividend_ptr[i];

		if (r >= divisor_limb)
			r = 0;
		else
			quot_ptr[i--] = 0;

		for (; i >= 0; i--) {
			n0 = dividend_ptr[i];
			udiv_qrnnd(quot_ptr[i], r, r, n0, divisor_limb);
		}
		return r;
	}
}

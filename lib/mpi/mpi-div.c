/* mpi-div.c  -  MPI functions
 * Copyright (C) 1994, 1996, 1998, 2001, 2002,
 *               2003 Free Software Foundation, Inc.
 *
 * This file is part of Libgcrypt.
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 */

#include "mpi-internal.h"
#include "longlong.h"

void mpi_tdiv_qr(MPI quot, MPI rem, MPI num, MPI den);
void mpi_fdiv_qr(MPI quot, MPI rem, MPI dividend, MPI divisor);

void mpi_fdiv_r(MPI rem, MPI dividend, MPI divisor)
{
	int divisor_sign = divisor->sign;
	MPI temp_divisor = NULL;

	/* We need the original value of the divisor after the remainder has been
	 * preliminary calculated.	We have to copy it to temporary space if it's
	 * the same variable as REM.
	 */
	if (rem == divisor) {
		temp_divisor = mpi_copy(divisor);
		divisor = temp_divisor;
	}

	mpi_tdiv_r(rem, dividend, divisor);

	if (((divisor_sign?1:0) ^ (dividend->sign?1:0)) && rem->nlimbs)
		mpi_add(rem, rem, divisor);

	if (temp_divisor)
		mpi_free(temp_divisor);
}

void mpi_fdiv_q(MPI quot, MPI dividend, MPI divisor)
{
	MPI tmp = mpi_alloc(mpi_get_nlimbs(quot));
	mpi_fdiv_qr(quot, tmp, dividend, divisor);
	mpi_free(tmp);
}

void mpi_fdiv_qr(MPI quot, MPI rem, MPI dividend, MPI divisor)
{
	int divisor_sign = divisor->sign;
	MPI temp_divisor = NULL;

	if (quot == divisor || rem == divisor) {
		temp_divisor = mpi_copy(divisor);
		divisor = temp_divisor;
	}

	mpi_tdiv_qr(quot, rem, dividend, divisor);

	if ((divisor_sign ^ dividend->sign) && rem->nlimbs) {
		mpi_sub_ui(quot, quot, 1);
		mpi_add(rem, rem, divisor);
	}

	if (temp_divisor)
		mpi_free(temp_divisor);
}

/* If den == quot, den needs temporary storage.
 * If den == rem, den needs temporary storage.
 * If num == quot, num needs temporary storage.
 * If den has temporary storage, it can be normalized while being copied,
 *   i.e no extra storage should be allocated.
 */

void mpi_tdiv_r(MPI rem, MPI num, MPI den)
{
	mpi_tdiv_qr(NULL, rem, num, den);
}

void mpi_tdiv_qr(MPI quot, MPI rem, MPI num, MPI den)
{
	mpi_ptr_t np, dp;
	mpi_ptr_t qp, rp;
	mpi_size_t nsize = num->nlimbs;
	mpi_size_t dsize = den->nlimbs;
	mpi_size_t qsize, rsize;
	mpi_size_t sign_remainder = num->sign;
	mpi_size_t sign_quotient = num->sign ^ den->sign;
	unsigned int normalization_steps;
	mpi_limb_t q_limb;
	mpi_ptr_t marker[5];
	unsigned int marker_nlimbs[5];
	int markidx = 0;

	/* Ensure space is enough for quotient and remainder.
	 * We need space for an extra limb in the remainder, because it's
	 * up-shifted (normalized) below.
	 */
	rsize = nsize + 1;
	mpi_resize(rem, rsize);

	qsize = rsize - dsize;	  /* qsize cannot be bigger than this.	*/
	if (qsize <= 0) {
		if (num != rem) {
			rem->nlimbs = num->nlimbs;
			rem->sign = num->sign;
			MPN_COPY(rem->d, num->d, nsize);
		}
		if (quot) {
			/* This needs to follow the assignment to rem, in case the
			 * numerator and quotient are the same.
			 */
			quot->nlimbs = 0;
			quot->sign = 0;
		}
		return;
	}

	if (quot)
		mpi_resize(quot, qsize);

	/* Read pointers here, when reallocation is finished.  */
	np = num->d;
	dp = den->d;
	rp = rem->d;

	/* Optimize division by a single-limb divisor.  */
	if (dsize == 1) {
		mpi_limb_t rlimb;
		if (quot) {
			qp = quot->d;
			rlimb = mpihelp_divmod_1(qp, np, nsize, dp[0]);
			qsize -= qp[qsize - 1] == 0;
			quot->nlimbs = qsize;
			quot->sign = sign_quotient;
		} else
			rlimb = mpihelp_mod_1(np, nsize, dp[0]);
		rp[0] = rlimb;
		rsize = rlimb != 0?1:0;
		rem->nlimbs = rsize;
		rem->sign = sign_remainder;
		return;
	}


	if (quot) {
		qp = quot->d;
		/* Make sure QP and NP point to different objects.  Otherwise the
		 * numerator would be gradually overwritten by the quotient limbs.
		 */
		if (qp == np) { /* Copy NP object to temporary space.  */
			marker_nlimbs[markidx] = nsize;
			np = marker[markidx++] = mpi_alloc_limb_space(nsize);
			MPN_COPY(np, qp, nsize);
		}
	} else /* Put quotient at top of remainder. */
		qp = rp + dsize;

	normalization_steps = count_leading_zeros(dp[dsize - 1]);

	/* Normalize the denominator, i.e. make its most significant bit set by
	 * shifting it NORMALIZATION_STEPS bits to the left.  Also shift the
	 * numerator the same number of steps (to keep the quotient the same!).
	 */
	if (normalization_steps) {
		mpi_ptr_t tp;
		mpi_limb_t nlimb;

		/* Shift up the denominator setting the most significant bit of
		 * the most significant word.  Use temporary storage not to clobber
		 * the original contents of the denominator.
		 */
		marker_nlimbs[markidx] = dsize;
		tp = marker[markidx++] = mpi_alloc_limb_space(dsize);
		mpihelp_lshift(tp, dp, dsize, normalization_steps);
		dp = tp;

		/* Shift up the numerator, possibly introducing a new most
		 * significant word.  Move the shifted numerator in the remainder
		 * meanwhile.
		 */
		nlimb = mpihelp_lshift(rp, np, nsize, normalization_steps);
		if (nlimb) {
			rp[nsize] = nlimb;
			rsize = nsize + 1;
		} else
			rsize = nsize;
	} else {
		/* The denominator is already normalized, as required.	Copy it to
		 * temporary space if it overlaps with the quotient or remainder.
		 */
		if (dp == rp || (quot && (dp == qp))) {
			mpi_ptr_t tp;

			marker_nlimbs[markidx] = dsize;
			tp = marker[markidx++] = mpi_alloc_limb_space(dsize);
			MPN_COPY(tp, dp, dsize);
			dp = tp;
		}

		/* Move the numerator to the remainder.  */
		if (rp != np)
			MPN_COPY(rp, np, nsize);

		rsize = nsize;
	}

	q_limb = mpihelp_divrem(qp, 0, rp, rsize, dp, dsize);

	if (quot) {
		qsize = rsize - dsize;
		if (q_limb) {
			qp[qsize] = q_limb;
			qsize += 1;
		}

		quot->nlimbs = qsize;
		quot->sign = sign_quotient;
	}

	rsize = dsize;
	MPN_NORMALIZE(rp, rsize);

	if (normalization_steps && rsize) {
		mpihelp_rshift(rp, rp, rsize, normalization_steps);
		rsize -= rp[rsize - 1] == 0?1:0;
	}

	rem->nlimbs = rsize;
	rem->sign	= sign_remainder;
	while (markidx) {
		markidx--;
		mpi_free_limb_space(marker[markidx]);
	}
}

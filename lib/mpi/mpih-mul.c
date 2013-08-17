/* mpihelp-mul.c  -  MPI helper functions
 * Copyright (C) 1994, 1996, 1998, 1999,
 *               2000 Free Software Foundation, Inc.
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

#include <linux/string.h>
#include "mpi-internal.h"
#include "longlong.h"

#define MPN_MUL_N_RECURSE(prodp, up, vp, size, tspace)		\
	do {							\
		if ((size) < KARATSUBA_THRESHOLD)		\
			mul_n_basecase(prodp, up, vp, size);	\
		else						\
			mul_n(prodp, up, vp, size, tspace);	\
	} while (0);

#define MPN_SQR_N_RECURSE(prodp, up, size, tspace)		\
	do {							\
		if ((size) < KARATSUBA_THRESHOLD)		\
			mpih_sqr_n_basecase(prodp, up, size);	\
		else						\
			mpih_sqr_n(prodp, up, size, tspace);	\
	} while (0);

/* Multiply the natural numbers u (pointed to by UP) and v (pointed to by VP),
 * both with SIZE limbs, and store the result at PRODP.  2 * SIZE limbs are
 * always stored.  Return the most significant limb.
 *
 * Argument constraints:
 * 1. PRODP != UP and PRODP != VP, i.e. the destination
 *    must be distinct from the multiplier and the multiplicand.
 *
 *
 * Handle simple cases with traditional multiplication.
 *
 * This is the most critical code of multiplication.  All multiplies rely
 * on this, both small and huge.  Small ones arrive here immediately.  Huge
 * ones arrive here as this is the base case for Karatsuba's recursive
 * algorithm below.
 */

static mpi_limb_t
mul_n_basecase(mpi_ptr_t prodp, mpi_ptr_t up, mpi_ptr_t vp, mpi_size_t size)
{
	mpi_size_t i;
	mpi_limb_t cy;
	mpi_limb_t v_limb;

	/* Multiply by the first limb in V separately, as the result can be
	 * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
	v_limb = vp[0];
	if (v_limb <= 1) {
		if (v_limb == 1)
			MPN_COPY(prodp, up, size);
		else
			MPN_ZERO(prodp, size);
		cy = 0;
	} else
		cy = mpihelp_mul_1(prodp, up, size, v_limb);

	prodp[size] = cy;
	prodp++;

	/* For each iteration in the outer loop, multiply one limb from
	 * U with one limb from V, and add it to PROD.  */
	for (i = 1; i < size; i++) {
		v_limb = vp[i];
		if (v_limb <= 1) {
			cy = 0;
			if (v_limb == 1)
				cy = mpihelp_add_n(prodp, prodp, up, size);
		} else
			cy = mpihelp_addmul_1(prodp, up, size, v_limb);

		prodp[size] = cy;
		prodp++;
	}

	return cy;
}

static void
mul_n(mpi_ptr_t prodp, mpi_ptr_t up, mpi_ptr_t vp,
		mpi_size_t size, mpi_ptr_t tspace)
{
	if (size & 1) {
		/* The size is odd, and the code below doesn't handle that.
		 * Multiply the least significant (size - 1) limbs with a recursive
		 * call, and handle the most significant limb of S1 and S2
		 * separately.
		 * A slightly faster way to do this would be to make the Karatsuba
		 * code below behave as if the size were even, and let it check for
		 * odd size in the end.  I.e., in essence move this code to the end.
		 * Doing so would save us a recursive call, and potentially make the
		 * stack grow a lot less.
		 */
		mpi_size_t esize = size - 1;	/* even size */
		mpi_limb_t cy_limb;

		MPN_MUL_N_RECURSE(prodp, up, vp, esize, tspace);
		cy_limb = mpihelp_addmul_1(prodp + esize, up, esize, vp[esize]);
		prodp[esize + esize] = cy_limb;
		cy_limb = mpihelp_addmul_1(prodp + esize, vp, size, up[esize]);
		prodp[esize + size] = cy_limb;
	} else {
		/* Anatolij Alekseevich Karatsuba's divide-and-conquer algorithm.
		 *
		 * Split U in two pieces, U1 and U0, such that
		 * U = U0 + U1*(B**n),
		 * and V in V1 and V0, such that
		 * V = V0 + V1*(B**n).
		 *
		 * UV is then computed recursively using the identity
		 *
		 *        2n   n          n                     n
		 * UV = (B  + B )U V  +  B (U -U )(V -V )  +  (B + 1)U V
		 *                1 1        1  0   0  1              0 0
		 *
		 * Where B = 2**BITS_PER_MP_LIMB.
		 */
		mpi_size_t hsize = size >> 1;
		mpi_limb_t cy;
		int negflg;

		/* Product H.      ________________  ________________
		 *                |_____U1 x V1____||____U0 x V0_____|
		 * Put result in upper part of PROD and pass low part of TSPACE
		 * as new TSPACE.
		 */
		MPN_MUL_N_RECURSE(prodp + size, up + hsize, vp + hsize, hsize,
				  tspace);

		/* Product M.      ________________
		 *                |_(U1-U0)(V0-V1)_|
		 */
		if (mpihelp_cmp(up + hsize, up, hsize) >= 0) {
			mpihelp_sub_n(prodp, up + hsize, up, hsize);
			negflg = 0;
		} else {
			mpihelp_sub_n(prodp, up, up + hsize, hsize);
			negflg = 1;
		}
		if (mpihelp_cmp(vp + hsize, vp, hsize) >= 0) {
			mpihelp_sub_n(prodp + hsize, vp + hsize, vp, hsize);
			negflg ^= 1;
		} else {
			mpihelp_sub_n(prodp + hsize, vp, vp + hsize, hsize);
			/* No change of NEGFLG.  */
		}
		/* Read temporary operands from low part of PROD.
		 * Put result in low part of TSPACE using upper part of TSPACE
		 * as new TSPACE.
		 */
		MPN_MUL_N_RECURSE(tspace, prodp, prodp + hsize, hsize,
				  tspace + size);

		/* Add/copy product H. */
		MPN_COPY(prodp + hsize, prodp + size, hsize);
		cy = mpihelp_add_n(prodp + size, prodp + size,
				   prodp + size + hsize, hsize);

		/* Add product M (if NEGFLG M is a negative number) */
		if (negflg)
			cy -=
			    mpihelp_sub_n(prodp + hsize, prodp + hsize, tspace,
					  size);
		else
			cy +=
			    mpihelp_add_n(prodp + hsize, prodp + hsize, tspace,
					  size);

		/* Product L.      ________________  ________________
		 *                |________________||____U0 x V0_____|
		 * Read temporary operands from low part of PROD.
		 * Put result in low part of TSPACE using upper part of TSPACE
		 * as new TSPACE.
		 */
		MPN_MUL_N_RECURSE(tspace, up, vp, hsize, tspace + size);

		/* Add/copy Product L (twice) */

		cy += mpihelp_add_n(prodp + hsize, prodp + hsize, tspace, size);
		if (cy)
			mpihelp_add_1(prodp + hsize + size,
				      prodp + hsize + size, hsize, cy);

		MPN_COPY(prodp, tspace, hsize);
		cy = mpihelp_add_n(prodp + hsize, prodp + hsize, tspace + hsize,
				   hsize);
		if (cy)
			mpihelp_add_1(prodp + size, prodp + size, size, 1);
	}
}

void mpih_sqr_n_basecase(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t size)
{
	mpi_size_t i;
	mpi_limb_t cy_limb;
	mpi_limb_t v_limb;

	/* Multiply by the first limb in V separately, as the result can be
	 * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
	v_limb = up[0];
	if (v_limb <= 1) {
		if (v_limb == 1)
			MPN_COPY(prodp, up, size);
		else
			MPN_ZERO(prodp, size);
		cy_limb = 0;
	} else
		cy_limb = mpihelp_mul_1(prodp, up, size, v_limb);

	prodp[size] = cy_limb;
	prodp++;

	/* For each iteration in the outer loop, multiply one limb from
	 * U with one limb from V, and add it to PROD.  */
	for (i = 1; i < size; i++) {
		v_limb = up[i];
		if (v_limb <= 1) {
			cy_limb = 0;
			if (v_limb == 1)
				cy_limb = mpihelp_add_n(prodp, prodp, up, size);
		} else
			cy_limb = mpihelp_addmul_1(prodp, up, size, v_limb);

		prodp[size] = cy_limb;
		prodp++;
	}
}

void
mpih_sqr_n(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t size, mpi_ptr_t tspace)
{
	if (size & 1) {
		/* The size is odd, and the code below doesn't handle that.
		 * Multiply the least significant (size - 1) limbs with a recursive
		 * call, and handle the most significant limb of S1 and S2
		 * separately.
		 * A slightly faster way to do this would be to make the Karatsuba
		 * code below behave as if the size were even, and let it check for
		 * odd size in the end.  I.e., in essence move this code to the end.
		 * Doing so would save us a recursive call, and potentially make the
		 * stack grow a lot less.
		 */
		mpi_size_t esize = size - 1;	/* even size */
		mpi_limb_t cy_limb;

		MPN_SQR_N_RECURSE(prodp, up, esize, tspace);
		cy_limb = mpihelp_addmul_1(prodp + esize, up, esize, up[esize]);
		prodp[esize + esize] = cy_limb;
		cy_limb = mpihelp_addmul_1(prodp + esize, up, size, up[esize]);

		prodp[esize + size] = cy_limb;
	} else {
		mpi_size_t hsize = size >> 1;
		mpi_limb_t cy;

		/* Product H.      ________________  ________________
		 *                |_____U1 x U1____||____U0 x U0_____|
		 * Put result in upper part of PROD and pass low part of TSPACE
		 * as new TSPACE.
		 */
		MPN_SQR_N_RECURSE(prodp + size, up + hsize, hsize, tspace);

		/* Product M.      ________________
		 *                |_(U1-U0)(U0-U1)_|
		 */
		if (mpihelp_cmp(up + hsize, up, hsize) >= 0)
			mpihelp_sub_n(prodp, up + hsize, up, hsize);
		else
			mpihelp_sub_n(prodp, up, up + hsize, hsize);

		/* Read temporary operands from low part of PROD.
		 * Put result in low part of TSPACE using upper part of TSPACE
		 * as new TSPACE.  */
		MPN_SQR_N_RECURSE(tspace, prodp, hsize, tspace + size);

		/* Add/copy product H  */
		MPN_COPY(prodp + hsize, prodp + size, hsize);
		cy = mpihelp_add_n(prodp + size, prodp + size,
				   prodp + size + hsize, hsize);

		/* Add product M (if NEGFLG M is a negative number).  */
		cy -= mpihelp_sub_n(prodp + hsize, prodp + hsize, tspace, size);

		/* Product L.      ________________  ________________
		 *                |________________||____U0 x U0_____|
		 * Read temporary operands from low part of PROD.
		 * Put result in low part of TSPACE using upper part of TSPACE
		 * as new TSPACE.  */
		MPN_SQR_N_RECURSE(tspace, up, hsize, tspace + size);

		/* Add/copy Product L (twice).  */
		cy += mpihelp_add_n(prodp + hsize, prodp + hsize, tspace, size);
		if (cy)
			mpihelp_add_1(prodp + hsize + size,
				      prodp + hsize + size, hsize, cy);

		MPN_COPY(prodp, tspace, hsize);
		cy = mpihelp_add_n(prodp + hsize, prodp + hsize, tspace + hsize,
				   hsize);
		if (cy)
			mpihelp_add_1(prodp + size, prodp + size, size, 1);
	}
}

/* This should be made into an inline function in gmp.h.  */
int mpihelp_mul_n(mpi_ptr_t prodp, mpi_ptr_t up, mpi_ptr_t vp, mpi_size_t size)
{
	if (up == vp) {
		if (size < KARATSUBA_THRESHOLD)
			mpih_sqr_n_basecase(prodp, up, size);
		else {
			mpi_ptr_t tspace;
			tspace = mpi_alloc_limb_space(2 * size);
			if (!tspace)
				return -ENOMEM;
			mpih_sqr_n(prodp, up, size, tspace);
			mpi_free_limb_space(tspace);
		}
	} else {
		if (size < KARATSUBA_THRESHOLD)
			mul_n_basecase(prodp, up, vp, size);
		else {
			mpi_ptr_t tspace;
			tspace = mpi_alloc_limb_space(2 * size);
			if (!tspace)
				return -ENOMEM;
			mul_n(prodp, up, vp, size, tspace);
			mpi_free_limb_space(tspace);
		}
	}

	return 0;
}

int
mpihelp_mul_karatsuba_case(mpi_ptr_t prodp,
			   mpi_ptr_t up, mpi_size_t usize,
			   mpi_ptr_t vp, mpi_size_t vsize,
			   struct karatsuba_ctx *ctx)
{
	mpi_limb_t cy;

	if (!ctx->tspace || ctx->tspace_size < vsize) {
		if (ctx->tspace)
			mpi_free_limb_space(ctx->tspace);
		ctx->tspace = mpi_alloc_limb_space(2 * vsize);
		if (!ctx->tspace)
			return -ENOMEM;
		ctx->tspace_size = vsize;
	}

	MPN_MUL_N_RECURSE(prodp, up, vp, vsize, ctx->tspace);

	prodp += vsize;
	up += vsize;
	usize -= vsize;
	if (usize >= vsize) {
		if (!ctx->tp || ctx->tp_size < vsize) {
			if (ctx->tp)
				mpi_free_limb_space(ctx->tp);
			ctx->tp = mpi_alloc_limb_space(2 * vsize);
			if (!ctx->tp) {
				if (ctx->tspace)
					mpi_free_limb_space(ctx->tspace);
				ctx->tspace = NULL;
				return -ENOMEM;
			}
			ctx->tp_size = vsize;
		}

		do {
			MPN_MUL_N_RECURSE(ctx->tp, up, vp, vsize, ctx->tspace);
			cy = mpihelp_add_n(prodp, prodp, ctx->tp, vsize);
			mpihelp_add_1(prodp + vsize, ctx->tp + vsize, vsize,
				      cy);
			prodp += vsize;
			up += vsize;
			usize -= vsize;
		} while (usize >= vsize);
	}

	if (usize) {
		if (usize < KARATSUBA_THRESHOLD) {
			mpi_limb_t tmp;
			if (mpihelp_mul(ctx->tspace, vp, vsize, up, usize, &tmp)
			    < 0)
				return -ENOMEM;
		} else {
			if (!ctx->next) {
				ctx->next = kzalloc(sizeof *ctx, GFP_KERNEL);
				if (!ctx->next)
					return -ENOMEM;
			}
			if (mpihelp_mul_karatsuba_case(ctx->tspace,
						       vp, vsize,
						       up, usize,
						       ctx->next) < 0)
				return -ENOMEM;
		}

		cy = mpihelp_add_n(prodp, prodp, ctx->tspace, vsize);
		mpihelp_add_1(prodp + vsize, ctx->tspace + vsize, usize, cy);
	}

	return 0;
}

void mpihelp_release_karatsuba_ctx(struct karatsuba_ctx *ctx)
{
	struct karatsuba_ctx *ctx2;

	if (ctx->tp)
		mpi_free_limb_space(ctx->tp);
	if (ctx->tspace)
		mpi_free_limb_space(ctx->tspace);
	for (ctx = ctx->next; ctx; ctx = ctx2) {
		ctx2 = ctx->next;
		if (ctx->tp)
			mpi_free_limb_space(ctx->tp);
		if (ctx->tspace)
			mpi_free_limb_space(ctx->tspace);
		kfree(ctx);
	}
}

/* Multiply the natural numbers u (pointed to by UP, with USIZE limbs)
 * and v (pointed to by VP, with VSIZE limbs), and store the result at
 * PRODP.  USIZE + VSIZE limbs are always stored, but if the input
 * operands are normalized.  Return the most significant limb of the
 * result.
 *
 * NOTE: The space pointed to by PRODP is overwritten before finished
 * with U and V, so overlap is an error.
 *
 * Argument constraints:
 * 1. USIZE >= VSIZE.
 * 2. PRODP != UP and PRODP != VP, i.e. the destination
 *    must be distinct from the multiplier and the multiplicand.
 */

int
mpihelp_mul(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t usize,
	    mpi_ptr_t vp, mpi_size_t vsize, mpi_limb_t *_result)
{
	mpi_ptr_t prod_endp = prodp + usize + vsize - 1;
	mpi_limb_t cy;
	struct karatsuba_ctx ctx;

	if (vsize < KARATSUBA_THRESHOLD) {
		mpi_size_t i;
		mpi_limb_t v_limb;

		if (!vsize) {
			*_result = 0;
			return 0;
		}

		/* Multiply by the first limb in V separately, as the result can be
		 * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
		v_limb = vp[0];
		if (v_limb <= 1) {
			if (v_limb == 1)
				MPN_COPY(prodp, up, usize);
			else
				MPN_ZERO(prodp, usize);
			cy = 0;
		} else
			cy = mpihelp_mul_1(prodp, up, usize, v_limb);

		prodp[usize] = cy;
		prodp++;

		/* For each iteration in the outer loop, multiply one limb from
		 * U with one limb from V, and add it to PROD.  */
		for (i = 1; i < vsize; i++) {
			v_limb = vp[i];
			if (v_limb <= 1) {
				cy = 0;
				if (v_limb == 1)
					cy = mpihelp_add_n(prodp, prodp, up,
							   usize);
			} else
				cy = mpihelp_addmul_1(prodp, up, usize, v_limb);

			prodp[usize] = cy;
			prodp++;
		}

		*_result = cy;
		return 0;
	}

	memset(&ctx, 0, sizeof ctx);
	if (mpihelp_mul_karatsuba_case(prodp, up, usize, vp, vsize, &ctx) < 0)
		return -ENOMEM;
	mpihelp_release_karatsuba_ctx(&ctx);
	*_result = *prod_endp;
	return 0;
}

/* mpi-mod.c -  Modular reduction
 * Copyright (C) 1998, 1999, 2001, 2002, 2003,
 *               2007  Free Software Foundation, Inc.
 *
 * This file is part of Libgcrypt.
 */


#include "mpi-internal.h"
#include "longlong.h"

/* Context used with Barrett reduction.  */
struct barrett_ctx_s {
	MPI m;   /* The modulus - may not be modified. */
	int m_copied;   /* If true, M needs to be released.  */
	int k;
	MPI y;
	MPI r1;  /* Helper MPI. */
	MPI r2;  /* Helper MPI. */
	MPI r3;  /* Helper MPI allocated on demand. */
};



void mpi_mod(MPI rem, MPI dividend, MPI divisor)
{
	mpi_fdiv_r(rem, dividend, divisor);
}

/* This function returns a new context for Barrett based operations on
 * the modulus M.  This context needs to be released using
 * _gcry_mpi_barrett_free.  If COPY is true M will be transferred to
 * the context and the user may change M.  If COPY is false, M may not
 * be changed until gcry_mpi_barrett_free has been called.
 */
mpi_barrett_t mpi_barrett_init(MPI m, int copy)
{
	mpi_barrett_t ctx;
	MPI tmp;

	mpi_normalize(m);
	ctx = kcalloc(1, sizeof(*ctx), GFP_KERNEL);

	if (copy) {
		ctx->m = mpi_copy(m);
		ctx->m_copied = 1;
	} else
		ctx->m = m;

	ctx->k = mpi_get_nlimbs(m);
	tmp = mpi_alloc(ctx->k + 1);

	/* Barrett precalculation: y = floor(b^(2k) / m). */
	mpi_set_ui(tmp, 1);
	mpi_lshift_limbs(tmp, 2 * ctx->k);
	mpi_fdiv_q(tmp, tmp, m);

	ctx->y  = tmp;
	ctx->r1 = mpi_alloc(2 * ctx->k + 1);
	ctx->r2 = mpi_alloc(2 * ctx->k + 1);

	return ctx;
}

void mpi_barrett_free(mpi_barrett_t ctx)
{
	if (ctx) {
		mpi_free(ctx->y);
		mpi_free(ctx->r1);
		mpi_free(ctx->r2);
		if (ctx->r3)
			mpi_free(ctx->r3);
		if (ctx->m_copied)
			mpi_free(ctx->m);
		kfree(ctx);
	}
}


/* R = X mod M
 *
 * Using Barrett reduction.  Before using this function
 * _gcry_mpi_barrett_init must have been called to do the
 * precalculations.  CTX is the context created by this precalculation
 * and also conveys M.  If the Barret reduction could no be done a
 * straightforward reduction method is used.
 *
 * We assume that these conditions are met:
 * Input:  x =(x_2k-1 ...x_0)_b
 *     m =(m_k-1 ....m_0)_b	  with m_k-1 != 0
 * Output: r = x mod m
 */
void mpi_mod_barrett(MPI r, MPI x, mpi_barrett_t ctx)
{
	MPI m = ctx->m;
	int k = ctx->k;
	MPI y = ctx->y;
	MPI r1 = ctx->r1;
	MPI r2 = ctx->r2;
	int sign;

	mpi_normalize(x);
	if (mpi_get_nlimbs(x) > 2*k) {
		mpi_mod(r, x, m);
		return;
	}

	sign = x->sign;
	x->sign = 0;

	/* 1. q1 = floor( x / b^k-1)
	 *    q2 = q1 * y
	 *    q3 = floor( q2 / b^k+1 )
	 * Actually, we don't need qx, we can work direct on r2
	 */
	mpi_set(r2, x);
	mpi_rshift_limbs(r2, k-1);
	mpi_mul(r2, r2, y);
	mpi_rshift_limbs(r2, k+1);

	/* 2. r1 = x mod b^k+1
	 *	r2 = q3 * m mod b^k+1
	 *	r  = r1 - r2
	 * 3. if r < 0 then  r = r + b^k+1
	 */
	mpi_set(r1, x);
	if (r1->nlimbs > k+1) /* Quick modulo operation.  */
		r1->nlimbs = k+1;
	mpi_mul(r2, r2, m);
	if (r2->nlimbs > k+1) /* Quick modulo operation. */
		r2->nlimbs = k+1;
	mpi_sub(r, r1, r2);

	if (mpi_has_sign(r)) {
		if (!ctx->r3) {
			ctx->r3 = mpi_alloc(k + 2);
			mpi_set_ui(ctx->r3, 1);
			mpi_lshift_limbs(ctx->r3, k + 1);
		}
		mpi_add(r, r, ctx->r3);
	}

	/* 4. while r >= m do r = r - m */
	while (mpi_cmp(r, m) >= 0)
		mpi_sub(r, r, m);

	x->sign = sign;
}


void mpi_mul_barrett(MPI w, MPI u, MPI v, mpi_barrett_t ctx)
{
	mpi_mul(w, u, v);
	mpi_mod_barrett(w, w, ctx);
}

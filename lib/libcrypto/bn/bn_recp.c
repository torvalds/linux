/* $OpenBSD: bn_recp.c,v 1.34 2025/05/10 05:54:38 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include "bn_local.h"
#include "err_local.h"

struct bn_recp_ctx_st {
	BIGNUM *N;	/* the divisor */
	BIGNUM *Nr;	/* the reciprocal 2^shift / N */
	int num_bits;	/* number of bits in N */
	int shift;
} /* BN_RECP_CTX */;

BN_RECP_CTX *
BN_RECP_CTX_create(const BIGNUM *N)
{
	BN_RECP_CTX *recp;

	if ((recp = calloc(1, sizeof(*recp))) == NULL)
		goto err;

	if ((recp->N = BN_dup(N)) == NULL)
		goto err;
	BN_set_negative(recp->N, 0);
	recp->num_bits = BN_num_bits(recp->N);

	if ((recp->Nr = BN_new()) == NULL)
		goto err;

	return recp;

 err:
	BN_RECP_CTX_free(recp);

	return NULL;
}

void
BN_RECP_CTX_free(BN_RECP_CTX *recp)
{
	if (recp == NULL)
		return;

	BN_free(recp->N);
	BN_free(recp->Nr);
	freezero(recp, sizeof(*recp));
}

int
BN_div_reciprocal(BIGNUM *dv, BIGNUM *rem, const BIGNUM *m, BN_RECP_CTX *recp,
    BN_CTX *ctx)
{
	int i, j, ret = 0;
	BIGNUM *a, *b, *d, *r;

	if (BN_ucmp(m, recp->N) < 0) {
		if (dv != NULL)
			BN_zero(dv);
		if (rem != NULL)
			return bn_copy(rem, m);
		return 1;
	}

	BN_CTX_start(ctx);
	if ((a = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;

	if ((d = dv) == NULL)
		d = BN_CTX_get(ctx);
	if (d == NULL)
		goto err;

	if ((r = rem) == NULL)
		r = BN_CTX_get(ctx);
	if (r == NULL)
		goto err;

	/*
	 * We want the remainder. Given input of ABCDEF / ab we need to
	 * multiply ABCDEF by 3 digits of the reciprocal of ab.
	 */

	/* i := max(BN_num_bits(m), 2*BN_num_bits(N)) */
	i = BN_num_bits(m);
	j = recp->num_bits << 1;
	if (j > i)
		i = j;

	/* Compute Nr := (1 << i) / N if necessary. */
	if (i != recp->shift) {
		BN_zero(recp->Nr);
		if (!BN_set_bit(recp->Nr, i))
			goto err;
		if (!BN_div_ct(recp->Nr, NULL, recp->Nr, recp->N, ctx))
			goto err;
		recp->shift = i;
	}

	/*
	 * d := |((m >> BN_num_bits(N)) * recp->Nr)     >> (i - BN_num_bits(N))|
	 *    = |((m >> BN_num_bits(N)) * (1 << i) / N) >> (i - BN_num_bits(N))|
	 *   <= |(m / 2^BN_num_bits(N)) * (2^i / N) * 2^BN_num_bits(N) / 2^i |
	 *    = |m / N|
	 */
	if (!BN_rshift(a, m, recp->num_bits))
		goto err;
	if (!BN_mul(b, a, recp->Nr, ctx))
		goto err;
	if (!BN_rshift(d, b, i - recp->num_bits))
		goto err;
	d->neg = 0;

	if (!BN_mul(b, recp->N, d, ctx))
		goto err;
	if (!BN_usub(r, m, b))
		goto err;
	r->neg = 0;

#if 1
	j = 0;
	while (BN_ucmp(r, recp->N) >= 0) {
		if (j++ > 2) {
			BNerror(BN_R_BAD_RECIPROCAL);
			goto err;
		}
		if (!BN_usub(r, r, recp->N))
			goto err;
		if (!BN_add_word(d, 1))
			goto err;
	}
#endif

	BN_set_negative(r, m->neg);
	BN_set_negative(d, m->neg ^ recp->N->neg);

	ret = 1;

err:
	BN_CTX_end(ctx);
	return ret;
}

/* Compute r = (x * y) % m. */
int
BN_mod_mul_reciprocal(BIGNUM *r, const BIGNUM *x, const BIGNUM *y,
    BN_RECP_CTX *recp, BN_CTX *ctx)
{
	if (!BN_mul(r, x, y, ctx))
		return 0;

	return BN_div_reciprocal(NULL, r, r, recp, ctx);
}

/* Compute r = x^2 % m. */
int
BN_mod_sqr_reciprocal(BIGNUM *r, const BIGNUM *x, BN_RECP_CTX *recp, BN_CTX *ctx)
{
	if (!BN_sqr(r, x, ctx))
		return 0;

	return BN_div_reciprocal(NULL, r, r, recp, ctx);
}

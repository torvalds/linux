/*	$OpenBSD: bn_isqrt.c,v 1.11 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <stddef.h>
#include <stdint.h>

#include <openssl/bn.h>

#include "bn_local.h"
#include "crypto_internal.h"
#include "err_local.h"

/*
 * Calculate integer square root of |n| using a variant of Newton's method.
 *
 * Returns the integer square root of |n| in the caller-provided |out_sqrt|;
 * |*out_perfect| is set to 1 if and only if |n| is a perfect square.
 * One of |out_sqrt| and |out_perfect| can be NULL; |in_ctx| can be NULL.
 *
 * Returns 0 on error, 1 on success.
 *
 * Adapted from pure Python describing cpython's math.isqrt(), without bothering
 * with any of the optimizations in the C code. A correctness proof is here:
 * https://github.com/mdickinson/snippets/blob/master/proofs/isqrt/src/isqrt.lean
 * The comments in the Python code also give a rather detailed proof.
 */

int
bn_isqrt(BIGNUM *out_sqrt, int *out_perfect, const BIGNUM *n, BN_CTX *in_ctx)
{
	BN_CTX *ctx = NULL;
	BIGNUM *a, *b;
	int c, d, e, s;
	int cmp, perfect;
	int ret = 0;

	if (out_perfect == NULL && out_sqrt == NULL) {
		BNerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if (BN_is_negative(n)) {
		BNerror(BN_R_INVALID_RANGE);
		goto err;
	}

	if ((ctx = in_ctx) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (BN_is_zero(n)) {
		perfect = 1;
		BN_zero(a);
		goto done;
	}

	if (!BN_one(a))
		goto err;

	c = (BN_num_bits(n) - 1) / 2;
	d = 0;

	/* Calculate s = floor(log(c)). */
	if (!BN_set_word(b, c))
		goto err;
	s = BN_num_bits(b) - 1;

	/*
	 * By definition, the loop below is run <= floor(log(log(n))) times.
	 * Comments in the cpython code establish the loop invariant that
	 *
	 *	(a - 1)^2 < n / 4^(c - d) < (a + 1)^2
	 *
	 * holds true in every iteration. Once this is proved via induction,
	 * correctness of the algorithm is easy.
	 *
	 * Roughly speaking, A = (a << (d - e)) is used for one Newton step
	 * "a = (A >> 1) + (m >> 1) / A" approximating m = (n >> 2 * (c - d)).
	 */

	for (; s >= 0; s--) {
		e = d;
		d = c >> s;

		if (!BN_rshift(b, n, 2 * c - d - e + 1))
			goto err;

		if (!BN_div_ct(b, NULL, b, a, ctx))
			goto err;

		if (!BN_lshift(a, a, d - e - 1))
			goto err;

		if (!BN_add(a, a, b))
			goto err;
	}

	/*
	 * The loop invariant implies that either a or a - 1 is isqrt(n).
	 * Figure out which one it is. The invariant also implies that for
	 * a perfect square n, a must be the square root.
	 */

	if (!BN_sqr(b, a, ctx))
		goto err;

	/* If a^2 > n, we must have isqrt(n) == a - 1. */
	if ((cmp = BN_cmp(b, n)) > 0) {
		if (!BN_sub_word(a, 1))
			goto err;
	}

	perfect = cmp == 0;

 done:
	if (out_perfect != NULL)
		*out_perfect = perfect;

	if (out_sqrt != NULL) {
		if (!bn_copy(out_sqrt, a))
			goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	if (ctx != in_ctx)
		BN_CTX_free(ctx);

	return ret;
}

/*
 * is_square_mod_N[r % N] indicates whether r % N has a square root modulo N.
 * The tables are generated in regress/lib/libcrypto/bn/bn_isqrt.c.
 */

const uint8_t is_square_mod_11[] = {
	1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0,
};
CTASSERT(sizeof(is_square_mod_11) == 11);

const uint8_t is_square_mod_63[] = {
	1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0,
	1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
};
CTASSERT(sizeof(is_square_mod_63) == 63);

const uint8_t is_square_mod_64[] = {
	1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
	1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
	0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
	0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
};
CTASSERT(sizeof(is_square_mod_64) == 64);

const uint8_t is_square_mod_65[] = {
	1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
	0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0,
	1,
};
CTASSERT(sizeof(is_square_mod_65) == 65);

/*
 * Determine whether n is a perfect square or not.
 *
 * Returns 1 on success and 0 on error. In case of success, |*out_perfect| is
 * set to 1 if and only if |n| is a perfect square.
 */

int
bn_is_perfect_square(int *out_perfect, const BIGNUM *n, BN_CTX *ctx)
{
	BN_ULONG r;

	*out_perfect = 0;

	if (BN_is_negative(n))
		return 1;

	/*
	 * Before performing an expensive bn_isqrt() operation, weed out many
	 * obvious non-squares. See H. Cohen, "A course in computational
	 * algebraic number theory", Algorithm 1.7.3.
	 *
	 * The idea is that a square remains a square when reduced modulo any
	 * number. The moduli are chosen in such a way that a non-square has
	 * probability < 1% of passing the four table lookups.
	 */

	/* n % 64 */
	r = BN_lsw(n) & 0x3f;

	if (!is_square_mod_64[r % 64])
		return 1;

	if ((r = BN_mod_word(n, 11 * 63 * 65)) == (BN_ULONG)-1)
		return 0;

	if (!is_square_mod_63[r % 63] ||
	    !is_square_mod_65[r % 65] ||
	    !is_square_mod_11[r % 11])
		return 1;

	return bn_isqrt(NULL, out_perfect, n, ctx);
}

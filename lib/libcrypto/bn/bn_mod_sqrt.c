/*	$OpenBSD: bn_mod_sqrt.c,v 1.4 2025/05/10 05:54:38 tb Exp $ */

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

#include "bn_local.h"
#include "err_local.h"

/*
 * Tonelli-Shanks according to H. Cohen "A Course in Computational Algebraic
 * Number Theory", Section 1.5.1, Springer GTM volume 138, Berlin, 1996.
 *
 * Under the assumption that p is prime and a is a quadratic residue, we know:
 *
 *	a^[(p-1)/2] = 1 (mod p).					(*)
 *
 * To find a square root of a (mod p), we handle three cases of increasing
 * complexity. In the first two cases, we can compute a square root using an
 * explicit formula, thus avoiding the probabilistic nature of Tonelli-Shanks.
 *
 * 1. p = 3 (mod 4).
 *
 *    Set n = (p+1)/4. Then 2n = 1 + (p-1)/2 and (*) shows that x = a^n (mod p)
 *    is a square root of a: x^2 = a^(2n) = a * a^[(p-1)/2] = a (mod p).
 *
 * 2. p = 5 (mod 8).
 *
 *    This uses a simplification due to Atkin. By Theorem 1.4.7 and 1.4.9, the
 *    Kronecker symbol (2/p) evaluates to (-1)^[(p^2-1)/8]. From p = 5 (mod 8)
 *    we get (p^2-1)/8 = 1 (mod 2), so (2/p) = -1, and thus
 *
 *	2^[(p-1)/2] = -1 (mod p).					(**)
 *
 *    Set b = (2a)^[(p-5)/8]. With (p-1)/2 = 2 + (p-5)/2, (*) and (**) show
 *
 *	i = 2 a b^2	is a square root of -1 (mod p).
 *
 *    Indeed, i^2 = 2^2 a^2 b^4 = 2^[(p-1)/2] a^[(p-1)/2] = -1 (mod p). Because
 *    of (i-1)^2 = -2i (mod p) and i (-i) = 1 (mod p), a square root of a is
 *
 *	x = a b (i-1)
 *
 *    as x^2 = a^2 b^2 (-2i) = a (2 a b^2) (-i) = a (mod p).
 *
 * 3. p = 1 (mod 8).
 *
 *    This is the Tonelli-Shanks algorithm. For a prime p, the multiplicative
 *    group of GF(p) is cyclic of order p - 1 = 2^s q, with odd q. Denote its
 *    2-Sylow subgroup by S. It is cyclic of order 2^s. The squares in S have
 *    order dividing 2^(s-1). They are the even powers of any generator z of S.
 *    If a is a quadratic residue, 1 = a^[(p-1)/2] = (a^q)^[2^(s-1)], so b = a^q
 *    is a square in S. Therefore there is an integer k such that b z^(2k) = 1.
 *    Set x = a^[(q+1)/2] z^k, and find x^2 = a (mod p).
 *
 *    The problem is thus reduced to finding a generator z of the 2-Sylow
 *    subgroup S of GF(p)* and finding k. An iterative constructions avoids
 *    the need for an explicit k, a generator is found by a randomized search.
 *
 * While we do not actually know that p is a prime number, we can still apply
 * the formulas in cases 1 and 2 and verify that we have indeed found a square
 * root of p. Similarly, in case 3, we can try to find a quadratic non-residue,
 * which will fail for example if p is a square. The iterative construction
 * may or may not find a candidate square root which we can then validate.
 */

/*
 * Handle the cases where p is 2, p isn't odd or p is one. Since BN_mod_sqrt()
 * can run on untrusted data, a primality check is too expensive. Also treat
 * the obvious cases where a is 0 or 1.
 */

static int
bn_mod_sqrt_trivial_cases(int *done, BIGNUM *out_sqrt, const BIGNUM *a,
    const BIGNUM *p, BN_CTX *ctx)
{
	*done = 1;

	if (BN_abs_is_word(p, 2))
		return BN_set_word(out_sqrt, BN_is_odd(a));

	if (!BN_is_odd(p) || BN_abs_is_word(p, 1)) {
		BNerror(BN_R_P_IS_NOT_PRIME);
		return 0;
	}

	if (BN_is_zero(a) || BN_is_one(a))
		return BN_set_word(out_sqrt, BN_is_one(a));

	*done = 0;

	return 1;
}

/*
 * Case 1. We know that (a/p) = 1 and that p = 3 (mod 4).
 */

static int
bn_mod_sqrt_p_is_3_mod_4(BIGNUM *out_sqrt, const BIGNUM *a, const BIGNUM *p,
    BN_CTX *ctx)
{
	BIGNUM *n;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((n = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Calculate n = (|p| + 1) / 4. */
	if (!BN_uadd(n, p, BN_value_one()))
		goto err;
	if (!BN_rshift(n, n, 2))
		goto err;

	/* By case 1 above, out_sqrt = a^n is a square root of a (mod p). */
	if (!BN_mod_exp_ct(out_sqrt, a, n, p, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Case 2. We know that (a/p) = 1 and that p = 5 (mod 8).
 */

static int
bn_mod_sqrt_p_is_5_mod_8(BIGNUM *out_sqrt, const BIGNUM *a, const BIGNUM *p,
    BN_CTX *ctx)
{
	BIGNUM *b, *i, *n, *tmp;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((i = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((n = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Calculate n = (|p| - 5) / 8. Since p = 5 (mod 8), simply shift. */
	if (!BN_rshift(n, p, 3))
		goto err;
	BN_set_negative(n, 0);

	/* Compute tmp = 2a (mod p) for later use. */
	if (!BN_mod_lshift1(tmp, a, p, ctx))
		goto err;

	/* Calculate b = (2a)^n (mod p). */
	if (!BN_mod_exp_ct(b, tmp, n, p, ctx))
		goto err;

	/* Calculate i = 2 a b^2 (mod p). */
	if (!BN_mod_sqr(i, b, p, ctx))
		goto err;
	if (!BN_mod_mul(i, tmp, i, p, ctx))
		goto err;

	/* A square root is out_sqrt = a b (i-1) (mod p). */
	if (!BN_sub_word(i, 1))
		goto err;
	if (!BN_mod_mul(out_sqrt, a, b, p, ctx))
		goto err;
	if (!BN_mod_mul(out_sqrt, out_sqrt, i, p, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Case 3. We know that (a/p) = 1 and that p = 1 (mod 8).
 */

/*
 * Simple helper. To find a generator of the 2-Sylow subgroup of GF(p)*, we
 * need to find a quadratic non-residue of p, i.e., n such that (n/p) = -1.
 */

static int
bn_mod_sqrt_n_is_non_residue(int *is_non_residue, const BIGNUM *n,
    const BIGNUM *p, BN_CTX *ctx)
{
	switch (BN_kronecker(n, p, ctx)) {
	case -1:
		*is_non_residue = 1;
		return 1;
	case 1:
		*is_non_residue = 0;
		return 1;
	case 0:
		/* n divides p, so ... */
		BNerror(BN_R_P_IS_NOT_PRIME);
		return 0;
	default:
		return 0;
	}
}

/*
 * The following is the only non-deterministic part preparing Tonelli-Shanks.
 *
 * If we find n such that (n/p) = -1, then n^q (mod p) is a generator of the
 * 2-Sylow subgroup of GF(p)*. To find such n, first try some small numbers,
 * then random ones.
 */

static int
bn_mod_sqrt_find_sylow_generator(BIGNUM *out_generator, const BIGNUM *p,
    const BIGNUM *q, BN_CTX *ctx)
{
	BIGNUM *n, *p_abs;
	int i, is_non_residue;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((n = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((p_abs = BN_CTX_get(ctx)) == NULL)
		goto err;

	for (i = 2; i < 32; i++) {
		if (!BN_set_word(n, i))
			goto err;
		if (!bn_mod_sqrt_n_is_non_residue(&is_non_residue, n, p, ctx))
			goto err;
		if (is_non_residue)
			goto found;
	}

	if (!bn_copy(p_abs, p))
		goto err;
	BN_set_negative(p_abs, 0);

	for (i = 0; i < 128; i++) {
		if (!bn_rand_interval(n, 32, p_abs))
			goto err;
		if (!bn_mod_sqrt_n_is_non_residue(&is_non_residue, n, p, ctx))
			goto err;
		if (is_non_residue)
			goto found;
	}

	/*
	 * The probability to get here is < 2^(-128) for prime p. For squares
	 * it is easy: for p = 1369 = 37^2 this happens in ~3% of runs.
	 */

	BNerror(BN_R_TOO_MANY_ITERATIONS);
	goto err;

 found:
	/*
	 * If p is prime, n^q generates the 2-Sylow subgroup S of GF(p)*.
	 */

	if (!BN_mod_exp_ct(out_generator, n, q, p, ctx))
		goto err;

	/* Sanity: p is not necessarily prime, so we could have found 0 or 1. */
	if (BN_is_zero(out_generator) || BN_is_one(out_generator)) {
		BNerror(BN_R_P_IS_NOT_PRIME);
		goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Initialization step for Tonelli-Shanks.
 *
 * In the end, b = a^q (mod p) and x = a^[(q+1)/2] (mod p). Cohen optimizes this
 * to minimize taking powers of a. This is a bit confusing and distracting, so
 * factor this into a separate function.
 */

static int
bn_mod_sqrt_tonelli_shanks_initialize(BIGNUM *b, BIGNUM *x, const BIGNUM *a,
    const BIGNUM *p, const BIGNUM *q, BN_CTX *ctx)
{
	BIGNUM *k;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((k = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* k = (q-1)/2. Since q is odd, we can shift. */
	if (!BN_rshift1(k, q))
		goto err;

	/* x = a^[(q-1)/2] (mod p). */
	if (!BN_mod_exp_ct(x, a, k, p, ctx))
		goto err;

	/* b = ax^2 = a^q (mod p). */
	if (!BN_mod_sqr(b, x, p, ctx))
		goto err;
	if (!BN_mod_mul(b, a, b, p, ctx))
		goto err;

	/* x = ax = a^[(q+1)/2] (mod p). */
	if (!BN_mod_mul(x, a, x, p, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Find smallest exponent m such that b^(2^m) = 1 (mod p). Assuming that a
 * is a quadratic residue and p is a prime, we know that 1 <= m < r.
 */

static int
bn_mod_sqrt_tonelli_shanks_find_exponent(int *out_exponent, const BIGNUM *b,
    const BIGNUM *p, int r, BN_CTX *ctx)
{
	BIGNUM *x;
	int m;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * If r <= 1, the Tonelli-Shanks iteration should have terminated as
	 * r == 1 implies b == 1.
	 */
	if (r <= 1) {
		BNerror(BN_R_P_IS_NOT_PRIME);
		goto err;
	}

	/*
	 * Sanity check to ensure taking squares actually does something:
	 * If b is 1, the Tonelli-Shanks iteration should have terminated.
	 * If b is 0, something's very wrong, in particular p can't be prime.
	 */
	if (BN_is_zero(b) || BN_is_one(b)) {
		BNerror(BN_R_P_IS_NOT_PRIME);
		goto err;
	}

	if (!bn_copy(x, b))
		goto err;

	for (m = 1; m < r; m++) {
		if (!BN_mod_sqr(x, x, p, ctx))
			goto err;
		if (BN_is_one(x))
			break;
	}

	if (m >= r) {
		/* This means a is not a quadratic residue. As (a/p) = 1, ... */
		BNerror(BN_R_P_IS_NOT_PRIME);
		goto err;
	}

	*out_exponent = m;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * The update step. With the minimal m such that b^(2^m) = 1 (mod m),
 * set t = y^[2^(r-m-1)] (mod p) and update x = xt, y = t^2, b = by.
 * This preserves the loop invariants a b = x^2, y^[2^(r-1)] = -1 and
 * b^[2^(r-1)] = 1.
 */

static int
bn_mod_sqrt_tonelli_shanks_update(BIGNUM *b, BIGNUM *x, BIGNUM *y,
    const BIGNUM *p, int m, int r, BN_CTX *ctx)
{
	BIGNUM *t;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((t = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* t = y^[2^(r-m-1)] (mod p). */
	if (!BN_set_bit(t, r - m - 1))
		goto err;
	if (!BN_mod_exp_ct(t, y, t, p, ctx))
		goto err;

	/* x = xt (mod p). */
	if (!BN_mod_mul(x, x, t, p, ctx))
		goto err;

	/* y = t^2 = y^[2^(r-m)] (mod p). */
	if (!BN_mod_sqr(y, t, p, ctx))
		goto err;

	/* b = by (mod p). */
	if (!BN_mod_mul(b, b, y, p, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
bn_mod_sqrt_p_is_1_mod_8(BIGNUM *out_sqrt, const BIGNUM *a, const BIGNUM *p,
    BN_CTX *ctx)
{
	BIGNUM *b, *q, *x, *y;
	int e, m, r;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((q = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((y = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Factor p - 1 = 2^e q with odd q. Since p = 1 (mod 8), we know e >= 3.
	 */

	e = 1;
	while (!BN_is_bit_set(p, e))
		e++;
	if (!BN_rshift(q, p, e))
		goto err;

	if (!bn_mod_sqrt_find_sylow_generator(y, p, q, ctx))
		goto err;

	/*
	 * Set b = a^q (mod p) and x = a^[(q+1)/2] (mod p).
	 */
	if (!bn_mod_sqrt_tonelli_shanks_initialize(b, x, a, p, q, ctx))
		goto err;

	/*
	 * The Tonelli-Shanks iteration. Starting with r = e, the following loop
	 * invariants hold at the start of the loop.
	 *
	 *	a b		= x^2	(mod p)
	 *	y^[2^(r-1)]	= -1	(mod p)
	 *	b^[2^(r-1)]	= 1	(mod p)
	 *
	 * In particular, if b = 1 (mod p), x is a square root of a.
	 *
	 * Since p - 1 = 2^e q, we have 2^(e-1) q = (p - 1) / 2, so in the first
	 * iteration this follows from (a/p) = 1, (n/p) = -1, y = n^q, b = a^q.
	 *
	 * In subsequent iterations, t = y^[2^(r-m-1)], where m is the smallest
	 * m such that b^(2^m) = 1. With x = xt (mod p) and b = bt^2 (mod p) the
	 * first invariant is preserved, the second and third follow from
	 * y = t^2 (mod p) and r = m as well as the choice of m.
	 *
	 * Finally, r is strictly decreasing in each iteration. If p is prime,
	 * let S be the 2-Sylow subgroup of GF(p)*. We can prove the algorithm
	 * stops: Let S_r be the subgroup of S consisting of elements of order
	 * dividing 2^r. Then S_r = <y> and b is in S_(r-1). The S_r form a
	 * descending filtration of S and when r = 1, then b = 1.
	 */

	for (r = e; r >= 1; r = m) {
		/*
		 * Termination condition. If b == 1 then x is a square root.
		 */
		if (BN_is_one(b))
			goto done;

		/* Find smallest exponent 1 <= m < r such that b^(2^m) == 1. */
		if (!bn_mod_sqrt_tonelli_shanks_find_exponent(&m, b, p, r, ctx))
			goto err;

		/*
		 * With t = y^[2^(r-m-1)], update x = xt, y = t^2, b = by.
		 */
		if (!bn_mod_sqrt_tonelli_shanks_update(b, x, y, p, m, r, ctx))
			goto err;

		/*
		 * Sanity check to make sure we don't loop indefinitely.
		 * bn_mod_sqrt_tonelli_shanks_find_exponent() ensures m < r.
		 */
		if (r <= m)
			goto err;
	}

	/*
	 * If p is prime, we should not get here.
	 */

	BNerror(BN_R_NOT_A_SQUARE);
	goto err;

 done:
	if (!bn_copy(out_sqrt, x))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Choose the smaller of sqrt and |p| - sqrt.
 */

static int
bn_mod_sqrt_normalize(BIGNUM *sqrt, const BIGNUM *p, BN_CTX *ctx)
{
	BIGNUM *x;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_lshift1(x, sqrt))
		goto err;

	if (BN_ucmp(x, p) > 0) {
		if (!BN_usub(sqrt, p, sqrt))
			goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Verify that a = (sqrt_a)^2 (mod p). Requires that a is reduced (mod p).
 */

static int
bn_mod_sqrt_verify(const BIGNUM *a, const BIGNUM *sqrt_a, const BIGNUM *p,
    BN_CTX *ctx)
{
	BIGNUM *x;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_mod_sqr(x, sqrt_a, p, ctx))
		goto err;

	if (BN_cmp(x, a) != 0) {
		BNerror(BN_R_NOT_A_SQUARE);
		goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
bn_mod_sqrt_internal(BIGNUM *out_sqrt, const BIGNUM *a, const BIGNUM *p,
    BN_CTX *ctx)
{
	BIGNUM *a_mod_p, *sqrt;
	BN_ULONG lsw;
	int done;
	int kronecker;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((a_mod_p = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((sqrt = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_nnmod(a_mod_p, a, p, ctx))
		goto err;

	if (!bn_mod_sqrt_trivial_cases(&done, sqrt, a_mod_p, p, ctx))
		goto err;
	if (done)
		goto verify;

	/*
	 * Make sure that the Kronecker symbol (a/p) == 1. In case p is prime
	 * this is equivalent to a having a square root (mod p). The cost of
	 * BN_kronecker() is O(log^2(n)). This is small compared to the cost
	 * O(log^4(n)) of Tonelli-Shanks.
	 */

	if ((kronecker = BN_kronecker(a_mod_p, p, ctx)) == -2)
		goto err;
	if (kronecker <= 0) {
		/* This error is only accurate if p is known to be a prime. */
		BNerror(BN_R_NOT_A_SQUARE);
		goto err;
	}

	lsw = BN_lsw(p);

	if (lsw % 4 == 3) {
		if (!bn_mod_sqrt_p_is_3_mod_4(sqrt, a_mod_p, p, ctx))
			goto err;
	} else if (lsw % 8 == 5) {
		if (!bn_mod_sqrt_p_is_5_mod_8(sqrt, a_mod_p, p, ctx))
			goto err;
	} else if (lsw % 8 == 1) {
		if (!bn_mod_sqrt_p_is_1_mod_8(sqrt, a_mod_p, p, ctx))
			goto err;
	} else {
		/* Impossible to hit since the trivial cases ensure p is odd. */
		BNerror(BN_R_P_IS_NOT_PRIME);
		goto err;
	}

	if (!bn_mod_sqrt_normalize(sqrt, p, ctx))
		goto err;

 verify:
	if (!bn_mod_sqrt_verify(a_mod_p, sqrt, p, ctx))
		goto err;

	if (!bn_copy(out_sqrt, sqrt))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

BIGNUM *
BN_mod_sqrt(BIGNUM *in, const BIGNUM *a, const BIGNUM *p, BN_CTX *ctx)
{
	BIGNUM *out_sqrt;

	if ((out_sqrt = in) == NULL)
		out_sqrt = BN_new();
	if (out_sqrt == NULL)
		goto err;

	if (!bn_mod_sqrt_internal(out_sqrt, a, p, ctx))
		goto err;

	return out_sqrt;

 err:
	if (out_sqrt != in)
		BN_free(out_sqrt);

	return NULL;
}
LCRYPTO_ALIAS(BN_mod_sqrt);

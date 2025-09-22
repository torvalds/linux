/*	$OpenBSD: bn_mod_exp.c,v 1.41 2025/02/12 21:22:15 tb Exp $ */

/*
 * Copyright (c) 2022,2023 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>

#include "bn_local.h"

#define N_MOD_EXP_TESTS		100
#define N_MOD_EXP2_TESTS	50

#define INIT_MOD_EXP_FN(f) { .name = #f, .mod_exp_fn = (f), }
#define INIT_MOD_EXP_MONT_FN(f) { .name = #f, .mod_exp_mont_fn = (f), }

static int
bn_mod_exp2_mont_first(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *mctx)
{
	const BIGNUM *one = BN_value_one();

	return BN_mod_exp2_mont(r, a, p, one, one, m, ctx, mctx);
}

static int
bn_mod_exp2_mont_second(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *mctx)
{
	const BIGNUM *one = BN_value_one();

	return BN_mod_exp2_mont(r, one, one, a, p, m, ctx, mctx);
}

static const struct mod_exp_test {
	const char *name;
	int (*mod_exp_fn)(BIGNUM *, const BIGNUM *, const BIGNUM *,
	    const BIGNUM *, BN_CTX *);
	int (*mod_exp_mont_fn)(BIGNUM *, const BIGNUM *, const BIGNUM *,
	    const BIGNUM *, BN_CTX *, BN_MONT_CTX *);
} mod_exp_fn[] = {
	INIT_MOD_EXP_FN(BN_mod_exp),
	INIT_MOD_EXP_FN(BN_mod_exp_ct),
	INIT_MOD_EXP_FN(BN_mod_exp_nonct),
	INIT_MOD_EXP_FN(BN_mod_exp_reciprocal),
	INIT_MOD_EXP_FN(BN_mod_exp_simple),
	INIT_MOD_EXP_MONT_FN(BN_mod_exp_mont),
	INIT_MOD_EXP_MONT_FN(BN_mod_exp_mont_ct),
	INIT_MOD_EXP_MONT_FN(BN_mod_exp_mont_consttime),
	INIT_MOD_EXP_MONT_FN(BN_mod_exp_mont_nonct),
	INIT_MOD_EXP_MONT_FN(bn_mod_exp2_mont_first),
	INIT_MOD_EXP_MONT_FN(bn_mod_exp2_mont_second),
};

#define N_MOD_EXP_FN (sizeof(mod_exp_fn) / sizeof(mod_exp_fn[0]))

static void
bn_print(const char *name, const BIGNUM *bn)
{
	size_t len;
	int pad = 0;

	if ((len = strlen(name)) < 7)
		pad = 6 - len;

	fprintf(stderr, "%s: %*s", name, pad, "");
	BN_print_fp(stderr, bn);
	fprintf(stderr, "\n");
}

static void
print_zero_test_failure(const BIGNUM *got, const BIGNUM *a, const BIGNUM *m,
    const char *name)
{
	fprintf(stderr, "%s() zero test failed:\n", name);

	bn_print("a", a);
	bn_print("m", m);
	bn_print("got", got);
}

static int
bn_mod_exp_zero_test(const struct mod_exp_test *test, BN_CTX *ctx,
    int neg_modulus, int random_base)
{
	BIGNUM *a, *m, *p, *got;
	int mod_exp_ret;
	int failed = 1;

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");
	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");

	if (!BN_one(m))
		errx(1, "BN_one");
	if (neg_modulus)
		BN_set_negative(m, 1);
	BN_zero(a);
	BN_zero(p);

	if (random_base) {
		if (!BN_rand(a, 1024, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY))
			errx(1, "BN_rand");
	}

	if (test->mod_exp_fn != NULL) {
		mod_exp_ret = test->mod_exp_fn(got, a, p, m, ctx);
	} else {
		mod_exp_ret = test->mod_exp_mont_fn(got, a, p, m, ctx, NULL);
	}

	if (!mod_exp_ret) {
		fprintf(stderr, "%s failed\n", test->name);
		ERR_print_errors_fp(stderr);
		goto err;
	}

	if (!BN_is_zero(got)) {
		print_zero_test_failure(got, a, m, test->name);
		goto err;
	}

	failed = 0;

 err:
	BN_CTX_end(ctx);

	return failed;
}

static int
bn_mod_exp_zero_word_test(BN_CTX *ctx, int neg_modulus)
{
	const char *name = "BN_mod_exp_mont_word";
	BIGNUM *m, *p, *got;
	int failed = 1;

	BN_CTX_start(ctx);

	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "BN_CTX_get");

	if (!BN_one(m))
		errx(1, "BN_one");
	if (neg_modulus)
		BN_set_negative(m, neg_modulus);
	BN_zero(p);

	if (!BN_mod_exp_mont_word(got, 1, p, m, ctx, NULL)) {
		fprintf(stderr, "%s failed\n", name);
		ERR_print_errors_fp(stderr);
		goto err;
	}

	if (!BN_is_zero(got)) {
		print_zero_test_failure(got, p, m, name);
		goto err;
	}

	failed = 0;

 err:
	BN_CTX_end(ctx);

	return failed;
}

static int
test_bn_mod_exp_zero(void)
{
	BN_CTX *ctx;
	size_t i, j;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	for (i = 0; i < N_MOD_EXP_FN; i++) {
		for (j = 0; j < 4; j++) {
			int neg_modulus = (j >> 0) & 1;
			int random_base = (j >> 1) & 1;

			failed |= bn_mod_exp_zero_test(&mod_exp_fn[i], ctx,
			    neg_modulus, random_base);
		}
	}

	failed |= bn_mod_exp_zero_word_test(ctx, 0);
	failed |= bn_mod_exp_zero_word_test(ctx, 1);

	BN_CTX_free(ctx);

	return failed;
}

static int
generate_bn(BIGNUM *bn, int avg_bits, int deviate, int force_odd)
{
	int bits;

	if (bn == NULL)
		return 1;

	if (avg_bits <= 0 || deviate <= 0 || deviate >= avg_bits)
		return 0;

	bits = avg_bits + arc4random_uniform(deviate) - deviate;

	return BN_rand(bn, bits, 0, force_odd);
}

static int
generate_test_quintuple(int reduce, BIGNUM *a, BIGNUM *p, BIGNUM *b, BIGNUM *q,
    BIGNUM *m, BN_CTX *ctx)
{
	BIGNUM *mmodified;
	BN_ULONG multiple;
	int avg = 2 * BN_BITS, deviate = BN_BITS / 2;
	int ret = 0;

	if (!generate_bn(a, avg, deviate, 0))
		return 0;

	if (!generate_bn(p, avg, deviate, 0))
		return 0;

	if (!generate_bn(b, avg, deviate, 0))
		return 0;

	if (!generate_bn(q, avg, deviate, 0))
		return 0;

	if (!generate_bn(m, avg, deviate, 1))
		return 0;

	if (reduce) {
		if (!BN_mod(a, a, m, ctx))
			return 0;

		if (b == NULL)
			return 1;

		return BN_mod(b, b, m, ctx);
	}

	/*
	 * Add a random multiple of m to a to test unreduced exponentiation.
	 */

	BN_CTX_start(ctx);

	if ((mmodified = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!bn_copy(mmodified, m))
		goto err;

	multiple = arc4random_uniform(16) + 2;

	if (!BN_mul_word(mmodified, multiple))
		goto err;

	if (!BN_add(a, a, mmodified))
		goto err;

	if (b == NULL)
		goto done;

	if (!BN_add(b, b, mmodified))
		goto err;

 done:
	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
generate_test_triple(int reduce, BIGNUM *a, BIGNUM *p, BIGNUM *m, BN_CTX *ctx)
{
	return generate_test_quintuple(reduce, a, p, NULL, NULL, m, ctx);
}

static void
dump_results(const BIGNUM *a, const BIGNUM *p, const BIGNUM *b, const BIGNUM *q,
    const BIGNUM *m, const BIGNUM *want, const BIGNUM *got, const char *name)
{
	fprintf(stderr, "BN_mod_exp_simple() and %s() disagree:\n", name);

	bn_print("want", want);
	bn_print("got", got);

	bn_print("a", a);
	bn_print("p", p);

	if (b != NULL) {
		bn_print("b", b);
		bn_print("q", q);
	}

	bn_print("m", m);

	fprintf(stderr, "\n");
}

static int
test_mod_exp(const BIGNUM *want, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, const struct mod_exp_test *test)
{
	BIGNUM *got;
	int mod_exp_ret;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((got = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (test->mod_exp_fn != NULL)
		mod_exp_ret = test->mod_exp_fn(got, a, p, m, ctx);
	else
		mod_exp_ret = test->mod_exp_mont_fn(got, a, p, m, ctx, NULL);

	if (!mod_exp_ret)
		errx(1, "%s() failed", test->name);

	if (BN_cmp(want, got) != 0) {
		dump_results(a, p, NULL, NULL, m, want, got, test->name);
		goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
bn_mod_exp_test(int reduce, BIGNUM *want, BIGNUM *a, BIGNUM *p, BIGNUM *m,
    BN_CTX *ctx)
{
	size_t i, j;
	int failed = 0;

	if (!generate_test_triple(reduce, a, p, m, ctx))
		errx(1, "generate_test_triple");

	for (i = 0; i < 8 && !failed; i++) {
		BN_set_negative(a, (i >> 0) & 1);
		BN_set_negative(p, (i >> 1) & 1);
		BN_set_negative(m, (i >> 2) & 1);

		if ((BN_mod_exp_simple(want, a, p, m, ctx)) <= 0)
			errx(1, "BN_mod_exp_simple");

		for (j = 0; j < N_MOD_EXP_FN; j++) {
			const struct mod_exp_test *test = &mod_exp_fn[j];

			if (!test_mod_exp(want, a, p, m, ctx, test))
				failed |= 1;
		}
	}

	return failed;
}

static int
test_bn_mod_exp(void)
{
	BIGNUM *a, *p, *m, *want;
	BN_CTX *ctx;
	int i;
	int reduce;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "p = BN_CTX_get()");
	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "m = BN_CTX_get()");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");

	reduce = 0;
	for (i = 0; i < N_MOD_EXP_TESTS && !failed; i++)
		failed |= bn_mod_exp_test(reduce, want, a, p, m, ctx);

	reduce = 1;
	for (i = 0; i < N_MOD_EXP_TESTS && !failed; i++)
		failed |= bn_mod_exp_test(reduce, want, a, p, m, ctx);

	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

static int
bn_mod_exp2_simple(BIGNUM *out, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *b, const BIGNUM *q, const BIGNUM *m, BN_CTX *ctx)
{
	BIGNUM *fact1, *fact2;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((fact1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((fact2 = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_mod_exp_simple(fact1, a, p, m, ctx))
		goto err;
	if (!BN_mod_exp_simple(fact2, b, q, m, ctx))
		goto err;
	if (!BN_mod_mul(out, fact1, fact2, m, ctx))
		goto err;

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
bn_mod_exp2_test(int reduce, BIGNUM *want, BIGNUM *got, BIGNUM *a, BIGNUM *p,
    BIGNUM *b, BIGNUM *q, BIGNUM *m, BN_CTX *ctx)
{
	size_t i;
	int failed = 0;

	if (!generate_test_quintuple(reduce, a, p, b, q, m, ctx))
		errx(1, "generate_test_quintuple");

	for (i = 0; i < 32 && !failed; i++) {
		BN_set_negative(a, (i >> 0) & 1);
		BN_set_negative(p, (i >> 1) & 1);
		BN_set_negative(b, (i >> 2) & 1);
		BN_set_negative(q, (i >> 3) & 1);
		BN_set_negative(m, (i >> 4) & 1);

		if (!bn_mod_exp2_simple(want, a, p, b, q, m, ctx))
			errx(1, "BN_mod_exp_simple");

		if (!BN_mod_exp2_mont(got, a, p, b, q, m, ctx, NULL))
			errx(1, "BN_mod_exp2_mont");

		if (BN_cmp(want, got) != 0) {
			dump_results(a, p, b, q, m, want, got, "BN_mod_exp2_mont");
			failed |= 1;
		}
	}

	return failed;
}

static int
test_bn_mod_exp2(void)
{
	BIGNUM *a, *p, *b, *q, *m, *want, *got;
	BN_CTX *ctx;
	int i;
	int reduce;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "p = BN_CTX_get()");
	if ((b = BN_CTX_get(ctx)) == NULL)
		errx(1, "b = BN_CTX_get()");
	if ((q = BN_CTX_get(ctx)) == NULL)
		errx(1, "q = BN_CTX_get()");
	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "m = BN_CTX_get()");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "got = BN_CTX_get()");

	reduce = 0;
	for (i = 0; i < N_MOD_EXP_TESTS && !failed; i++)
		failed |= bn_mod_exp2_test(reduce, want, got, a, p, b, q, m, ctx);

	reduce = 1;
	for (i = 0; i < N_MOD_EXP_TESTS && !failed; i++)
		failed |= bn_mod_exp2_test(reduce, want, got, a, p, b, q, m, ctx);

	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

/*
 * Small test for a crash reported by Guido Vranken, fixed in bn_exp2.c r1.13.
 * https://github.com/openssl/openssl/issues/17648
 */

static int
test_bn_mod_exp2_mont_crash(void)
{
	BIGNUM *m;
	int failed = 0;

	if ((m = BN_new()) == NULL)
		errx(1, "BN_new");

	if (BN_mod_exp2_mont(NULL, NULL, NULL, NULL, NULL, m, NULL, NULL)) {
		fprintf(stderr, "BN_mod_exp2_mont succeeded\n");
		failed |= 1;
	}

	BN_free(m);

	return failed;
}

const struct aliasing_test_case {
	BN_ULONG a;
	BN_ULONG p;
	BN_ULONG m;
} aliasing_test_cases[] = {
	{
		.a = 1031,
		.p = 1033,
		.m = 1039,
	},
	{
		.a = 3,
		.p = 4,
		.m = 5,
	},
	{
		.a = 97,
		.p = 17,
		.m = 11,
	},
	{
		.a = 999961,
		.p = 999979,
		.m = 999983,
	},
};

#define N_ALIASING_TEST_CASES \
	(sizeof(aliasing_test_cases) / sizeof(aliasing_test_cases[0]))

static void
test_bn_mod_exp_aliasing_setup(BIGNUM *want, BIGNUM *a, BIGNUM *p, BIGNUM *m,
    BN_CTX *ctx, const struct aliasing_test_case *tc)
{
	if (!BN_set_word(a, tc->a))
		errx(1, "BN_set_word");
	if (!BN_set_word(p, tc->p))
		errx(1, "BN_set_word");
	if (!BN_set_word(m, tc->m))
		errx(1, "BN_set_word");

	if (!BN_mod_exp_simple(want, a, p, m, ctx))
		errx(1, "BN_mod_exp");
}

static int
test_mod_exp_aliased(const char *alias, int want_ret, BIGNUM *got,
    const BIGNUM *want, const BIGNUM *a, const BIGNUM *p, const BIGNUM *m,
    BN_CTX *ctx, const struct mod_exp_test *test)
{
	int mod_exp_ret;
	int ret = 0;

	BN_CTX_start(ctx);

	if (test->mod_exp_fn != NULL)
		mod_exp_ret = test->mod_exp_fn(got, a, p, m, ctx);
	else
		mod_exp_ret = test->mod_exp_mont_fn(got, a, p, m, ctx, NULL);

	if (mod_exp_ret != want_ret) {
		warnx("%s() %s aliased with result failed", test->name, alias);
		goto err;
	}

	if (!mod_exp_ret)
		goto done;

	if (BN_cmp(want, got) != 0) {
		dump_results(a, p, NULL, NULL, m, want, got, test->name);
		goto err;
	}

 done:
	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
test_bn_mod_exp_aliasing_test(const struct mod_exp_test *test,
    BIGNUM *a, BIGNUM *p, BIGNUM *m, BIGNUM *want, BIGNUM *got, BN_CTX *ctx)
{
	int modulus_alias_works = test->mod_exp_fn != BN_mod_exp_simple;
	size_t i;
	int failed = 0;

	for (i = 0; i < N_ALIASING_TEST_CASES; i++) {
		const struct aliasing_test_case *tc = &aliasing_test_cases[i];

		test_bn_mod_exp_aliasing_setup(want, a, p, m, ctx, tc);
		if (!test_mod_exp_aliased("nothing", 1, got, want, a, p, m, ctx,
		    test))
			failed |= 1;
		test_bn_mod_exp_aliasing_setup(want, a, p, m, ctx, tc);
		if (!test_mod_exp_aliased("a", 1, a, want, a, p, m, ctx, test))
			failed |= 1;
		test_bn_mod_exp_aliasing_setup(want, a, p, m, ctx, tc);
		if (!test_mod_exp_aliased("p", 1, p, want, a, p, m, ctx, test))
			failed |= 1;
		test_bn_mod_exp_aliasing_setup(want, a, p, m, ctx, tc);
		if (!test_mod_exp_aliased("m", modulus_alias_works, m, want,
		    a, p, m, ctx, test))
			failed |= 1;
	}

	return failed;
}

static int
test_bn_mod_exp_aliasing(void)
{
	BN_CTX *ctx;
	BIGNUM *a, *p, *m, *want, *got;
	size_t i;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((p = BN_CTX_get(ctx)) == NULL)
		errx(1, "p = BN_CTX_get()");
	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "m = BN_CTX_get()");
	if ((want = BN_CTX_get(ctx)) == NULL)
		errx(1, "want = BN_CTX_get()");
	if ((got = BN_CTX_get(ctx)) == NULL)
		errx(1, "got = BN_CTX_get()");

	for (i = 0; i < N_MOD_EXP_FN; i++) {
		const struct mod_exp_test *test = &mod_exp_fn[i];
		failed |= test_bn_mod_exp_aliasing_test(test, a, p, m,
		    want, got, ctx);
	}

	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_bn_mod_exp_zero();
	failed |= test_bn_mod_exp();
	failed |= test_bn_mod_exp2();
	failed |= test_bn_mod_exp2_mont_crash();
	failed |= test_bn_mod_exp_aliasing();

	return failed;
}

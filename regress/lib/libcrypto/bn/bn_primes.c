/*	$OpenBSD: bn_primes.c,v 1.3 2023/04/25 15:30:03 tb Exp $ */
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/bn.h>

#include "bn_prime.h"

static int
test_bn_is_prime_fasttest(int do_trial_division)
{
	BIGNUM *n = NULL;
	char *descr = NULL;
	uint16_t i, j, max;
	int is_prime, ret;
	int failed = 1;

	if (asprintf(&descr, "with%s trial divisions",
	    do_trial_division ? "" : "out") == -1) {
		descr = NULL;
		fprintf(stderr, "asprintf failed\n");
		goto err;
	}

	if ((n = BN_new()) == NULL) {
		fprintf(stderr, "BN_new failed\n");
		goto err;
	}

	max = primes[NUMPRIMES - 1] + 1;

	failed = 0;
	for (i = 1, j = 0; i < max && j < NUMPRIMES; i++) {
		if (!BN_set_word(n, i)) {
			fprintf(stderr, "BN_set_word(%d) failed", i);
			failed = 1;
			goto err;
		}

		is_prime = i == primes[j];
		if (is_prime)
			j++;

		ret = BN_is_prime_fasttest_ex(n, BN_prime_checks, NULL,
		    do_trial_division, NULL);
		if (ret != is_prime) {
			fprintf(stderr,
			    "BN_is_prime_fasttest_ex(%d) %s: want %d, got %d\n",
			    i, descr, is_prime, ret);
			failed = 1;
		}
	}

	if (i < max || j < NUMPRIMES) {
		fprintf(stderr, "%s: %d < %d or %d < %d\n", descr, i, max, j,
		    NUMPRIMES);
		failed = 1;
	}

 err:
	BN_free(n);
	free(descr);
	return failed;
}

#define BN_PRIME_FN_INIT(a) { .fn = a, .name = #a }

static const struct test_dynamic_api {
	BIGNUM *(*fn)(BIGNUM *);
	const char *name;
} dynamic_api_data[] = {
	BN_PRIME_FN_INIT(BN_get_rfc2409_prime_1024),
	BN_PRIME_FN_INIT(BN_get_rfc2409_prime_768),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_1536),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_2048),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_3072),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_4096),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_6144),
	BN_PRIME_FN_INIT(BN_get_rfc3526_prime_8192),
};

#define N_DYNAMIC_TESTS (sizeof(dynamic_api_data) / sizeof(dynamic_api_data[0]))

static int
test_prime_dynamic_api(const struct test_dynamic_api *tc)
{
	BIGNUM *prime;
	int ret;
	int failed = 1;

	if ((prime = tc->fn(NULL)) == NULL) {
		fprintf(stderr, "%s failed\n", tc->name);
		goto err;
	}

	if ((ret = BN_is_prime_fasttest_ex(prime, 1, NULL, 1, NULL)) != 1) {
		fprintf(stderr, "%s: %s want 1, got %d\n", tc->name,
		    "BN_is_prime_fasttest_ex", ret);
		goto err;
	}

	failed = 0;

 err:
	BN_free(prime);
	return failed;
}

static int
test_prime_constants(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_DYNAMIC_TESTS; i++)
		failed |= test_prime_dynamic_api(&dynamic_api_data[i]);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_bn_is_prime_fasttest(0);
	failed |= test_bn_is_prime_fasttest(1);
	failed |= test_prime_constants();

	return failed;
}

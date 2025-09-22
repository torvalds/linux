/*	$OpenBSD: mlkem_iteration_tests.c,v 1.8 2025/08/17 19:26:35 tb Exp $ */
/*
 * Copyright (c) 2024 Google Inc.
 * Copyright (c) 2024 Bob Beck <beck@obtuse.com>
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/mlkem.h>

#include "mlkem_internal.h"
#include "mlkem_tests_util.h"
#include "sha3_internal.h"

/*
 * Based on https://c2sp.org/CCTV/ML-KEM
 *
 * The final value has been updated to reflect the change from Kyber to ML-KEM.
 *
 * The deterministic RNG is a single SHAKE-128 instance with an empty input.
 * (The RNG stream starts with 7f9c2ba4e88f827d616045507605853e.)
 */
const uint8_t kExpectedSeedStart[16] = {
	0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d, 0x61, 0x60, 0x45,
	0x50, 0x76, 0x05, 0x85, 0x3e
};

/*
 * Filippo says:
 * ML-KEM-768: f7db260e1137a742e05fe0db9525012812b004d29040a5b606aad3d134b548d3
 * but Boring believes this:
 */
const uint8_t kExpectedAdam768[32] = {
	0xf9, 0x59, 0xd1, 0x8d, 0x3d, 0x11, 0x80, 0x12, 0x14, 0x33, 0xbf,
	0x0e, 0x05, 0xf1, 0x1e, 0x79, 0x08, 0xcf, 0x9d, 0x03, 0xed, 0xc1,
	0x50, 0xb2, 0xb0, 0x7c, 0xb9, 0x0b, 0xef, 0x5b, 0xc1, 0xc1
};

/*
 * Filippo says:
 * ML-KEM-1024: 47ac888fe61544efc0518f46094b4f8a600965fc89822acb06dc7169d24f3543
 * but Boring believes this:
 */
const uint8_t kExpectedAdam1024[32] = {
	0xe3, 0xbf, 0x82, 0xb0, 0x13, 0x30, 0x7b, 0x2e, 0x9d, 0x47, 0xdd,
	0xe7, 0x91, 0xff, 0x6d, 0xfc, 0x82, 0xe6, 0x94, 0xe6, 0x38, 0x24,
	0x04, 0xab, 0xdb, 0x94, 0x8b, 0x90, 0x8b, 0x75, 0xba, 0xd5
};

static int
MlkemIterativeTest(int rank)
{
	const uint8_t *start, *expected;
	size_t start_len;
	uint8_t encap_entropy[MLKEM_ENCAP_ENTROPY];
	uint8_t seed[MLKEM_SEED_LENGTH] = {0};
	uint8_t *shared_secret = NULL;
	sha3_ctx drng, results;
	uint8_t out[32];
	int i;

	start = kExpectedSeedStart;
	start_len = sizeof(kExpectedSeedStart);
	switch(rank){
	case RANK768:
		expected = kExpectedAdam768;
		break;
	case RANK1024:
		expected = kExpectedAdam1024;
		break;
	default:
		errx(1, "invalid rank %d", rank);
	}

	shake128_init(&drng);
	shake128_init(&results);

	shake_xof(&drng);
	for (i = 0; i < 10000; i++) {
		uint8_t *encoded_public_key = NULL, *ciphertext = NULL,
		    *encoded_private_key = NULL, *invalid_ciphertext = NULL;
		size_t encoded_public_key_len, ciphertext_len,
		    encoded_private_key_len, invalid_ciphertext_len;
		MLKEM_private_key *priv;
		MLKEM_public_key *pub;
		size_t s_len = 0;

		/* allocate keys for this iteration */
		if ((priv = MLKEM_private_key_new(rank)) == NULL)
			errx(1, "malloc");
		if ((pub = MLKEM_public_key_new(rank)) == NULL)
			errx(1, "malloc");

		/*
		 * This should draw both d and z from DRNG concatenating in
		 * seed.
		 */
		shake_out(&drng, seed, sizeof(seed));
		if (i == 0) {
			if (compare_data(seed, start, start_len,
			    "seed start") != 0)
				errx(1, "compare_data");
		}

		/* generate ek as encoded_public_key */
		if (!MLKEM_generate_key_external_entropy(priv,
		    &encoded_public_key, &encoded_public_key_len,
		    seed))
			errx(1, "generate_key_external_entropy");

		if (!MLKEM_public_from_private(priv, pub))
			errx(1, "public_from_private");

		/* hash in ek */
		shake_update(&results, encoded_public_key,
		    encoded_public_key_len);

		/* marshal priv to dk as encoded_private_key */
		if (!MLKEM_marshal_private_key(priv, &encoded_private_key,
		    &encoded_private_key_len))
			errx(1, "marshal private key");

		/* hash in dk */
		shake_update(&results, encoded_private_key,
		    encoded_private_key_len);

		freezero(encoded_private_key, encoded_private_key_len);

		/* draw m as encap entropy from DRNG */
		shake_out(&drng, encap_entropy, sizeof(encap_entropy));

		/* generate ct as ciphertext, k as shared_secret */
		if (!MLKEM_encap_external_entropy(pub, encap_entropy,
		    &ciphertext, &ciphertext_len, &shared_secret, &s_len))
			errx(1, "encap_external_entropy");

		/* hash in ct */
		shake_update(&results, ciphertext, ciphertext_len);
		/* hash in k */
		shake_update(&results, shared_secret, s_len);

		freezero(shared_secret, s_len);
		shared_secret = NULL;

		invalid_ciphertext_len = ciphertext_len;
		if ((invalid_ciphertext = calloc(1, invalid_ciphertext_len))
		    == NULL)
			errx(1, "malloc");

		/* draw ct as invalid_ciphertxt from DRNG */
		shake_out(&drng, invalid_ciphertext, invalid_ciphertext_len);

		/* generate k as shared secret from invalid ciphertext */
		if (!MLKEM_decap(priv, invalid_ciphertext,
		    invalid_ciphertext_len, &shared_secret, &s_len))
			errx(1, "decap failed, iteration %d", i);

		/* hash in k */
		shake_update(&results, shared_secret, s_len);

		freezero(shared_secret, s_len);
		shared_secret = NULL;
		freezero(invalid_ciphertext, invalid_ciphertext_len);
		invalid_ciphertext = NULL;

		/* free keys and intermediate products for this iteration */
		MLKEM_private_key_free(priv);
		MLKEM_public_key_free(pub);
		freezero(encoded_public_key, encoded_public_key_len);
		freezero(ciphertext, ciphertext_len);
	}
	shake_xof(&results);
	shake_out(&results, out, sizeof(out));

	return compare_data(expected, out, sizeof(out), "final result hash");
}

int
main(void)
{
	int failed = 0;

	failed |= MlkemIterativeTest(RANK768);
	failed |= MlkemIterativeTest(RANK1024);

	return failed;
}

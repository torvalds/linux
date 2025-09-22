/*	$OpenBSD: mlkem_unittest.c,v 1.15 2025/08/17 19:26:35 tb Exp $ */
/*
 * Copyright (c) 2024 Google Inc.
 * Copyright (c) 2024 Bob Beck <beck@obtuse.com>
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
#include <string.h>

#include <openssl/mlkem.h>

#include "mlkem_internal.h"
#include "mlkem_tests_util.h"

static int
MlKemUnitTest(int rank)
{
	MLKEM_private_key *priv = NULL, *priv2 = NULL, *priv3 = NULL;
	MLKEM_public_key *pub = NULL, *pub2 = NULL, *pub3 = NULL;
	uint8_t *encoded_public_key = NULL, *ciphertext = NULL,
	    *shared_secret2 = NULL, *shared_secret1 = NULL,
	    *encoded_private_key = NULL, *tmp_buf = NULL, *seed_buf = NULL;
	size_t encoded_public_key_len, ciphertext_len,
	    encoded_private_key_len, tmp_buf_len;
	uint8_t first_two_bytes[2];
	size_t s_len = 0;
	int failed = 0;

	if ((pub = MLKEM_public_key_new(rank)) == NULL) {
		warnx("public_key_new");
		failed |= 1;
	}

	if ((pub2 = MLKEM_public_key_new(rank)) == NULL) {
		warnx("public_key_new");
		failed |= 1;
	}

	if ((priv = MLKEM_private_key_new(rank)) == NULL) {
		warnx("private_key_new");
		failed |= 1;
	}

	if ((priv2 = MLKEM_private_key_new(rank)) == NULL) {
		warnx("private_key_new");
		failed |= 1;
	}

	if (!MLKEM_generate_key(priv, &encoded_public_key,
	    &encoded_public_key_len, &seed_buf, &s_len)) {
		warnx("generate_key failed");
		failed |= 1;
	}

	if (s_len != MLKEM_SEED_LENGTH) {
		warnx("seed length %zu != %d", s_len, MLKEM_SEED_LENGTH);
		failed |= 1;
	}

	if ((priv3 = MLKEM_private_key_new(rank)) == NULL) {
		warnx("private_key_new");
		failed |= 1;
	}

	if ((pub3 = MLKEM_public_key_new(rank)) == NULL) {
		warnx("public_key_new");
		failed |= 1;
	}

	if (!MLKEM_private_key_from_seed(priv3, seed_buf, s_len)) {
		warnx("private_key_from_seed failed");
		failed |= 1;
	}

	free(seed_buf);
	seed_buf = NULL;

	if (!MLKEM_public_from_private(priv3, pub3)) {
		warnx("public_from_private");
		failed |= 1;
	}

	memcpy(first_two_bytes, encoded_public_key, sizeof(first_two_bytes));
	memset(encoded_public_key, 0xff, sizeof(first_two_bytes));

	/* Parsing should fail because the first coefficient is >= kPrime. */
	if (MLKEM_parse_public_key(pub, encoded_public_key,
	    encoded_public_key_len)) {
		warnx("parse_public_key should have failed");
		failed |= 1;
	}

	memcpy(encoded_public_key, first_two_bytes, sizeof(first_two_bytes));

	MLKEM_public_key_free(pub);
	if ((pub = MLKEM_public_key_new(rank)) == NULL) {
		warnx("public_key_new");
		failed |= 1;
	}
	if (!MLKEM_parse_public_key(pub, encoded_public_key,
	    encoded_public_key_len)) {
		warnx("MLKEM_parse_public_key");
		failed |= 1;
	}

	if (!MLKEM_marshal_public_key(pub, &tmp_buf, &tmp_buf_len)) {
		warnx("marshal_public_key");
		failed |= 1;
	}
	if (encoded_public_key_len != tmp_buf_len) {
		warnx("encoded public key lengths differ %d != %d",
		    (int) encoded_public_key_len, (int) tmp_buf_len);
		failed |= 1;
	}

	if (compare_data(encoded_public_key, tmp_buf, tmp_buf_len,
	    "encoded public keys") != 0) {
		warnx("compare_data");
		failed |= 1;
	}
	free(tmp_buf);
	tmp_buf = NULL;
	tmp_buf_len = 0;

	if (!MLKEM_marshal_public_key(pub3, &tmp_buf, &tmp_buf_len)) {
		warnx("marshal_public_key");
		failed |= 1;
	}
	if (encoded_public_key_len != tmp_buf_len) {
		warnx("encoded public key lengths differ %d != %d",
		    (int) encoded_public_key_len, (int) tmp_buf_len);
		failed |= 1;
	}

	if (compare_data(encoded_public_key, tmp_buf, tmp_buf_len,
	    "encoded public keys") != 0) {
		warnx("compare_data");
		failed |= 1;
	}
	free(tmp_buf);
	tmp_buf = NULL;
	tmp_buf_len = 0;

	if (!MLKEM_public_from_private(priv, pub2)) {
		warnx("public_from_private");
		failed |= 1;
	}
	if (!MLKEM_marshal_public_key(pub2, &tmp_buf, &tmp_buf_len)) {
		warnx("marshal_public_key");
		failed |= 1;
	}
	if (encoded_public_key_len != tmp_buf_len) {
		warnx("encoded public key lengths differ %d %d",
		    (int) encoded_public_key_len, (int) tmp_buf_len);
		failed |= 1;
	}

	if (compare_data(encoded_public_key, tmp_buf, tmp_buf_len,
	    "encoded public keys") != 0) {
		warnx("compare_data");
		failed |= 1;
	}
	free(tmp_buf);
	tmp_buf = NULL;
	tmp_buf_len = 0;

	if (!MLKEM_marshal_private_key(priv, &encoded_private_key,
	    &encoded_private_key_len)) {
		warnx("marshal_private_key");
		failed |= 1;
	}

	memcpy(first_two_bytes, encoded_private_key, sizeof(first_two_bytes));
	memset(encoded_private_key, 0xff, sizeof(first_two_bytes));

	/*  Parsing should fail because the first coefficient is >= kPrime. */
	if (MLKEM_parse_private_key(priv2, encoded_private_key,
	    encoded_private_key_len)) {
		warnx("parse_private_key should have failed");
		failed |= 1;
	}

	memcpy(encoded_private_key, first_two_bytes, sizeof(first_two_bytes));

	MLKEM_private_key_free(priv2);
	priv2 = NULL;

	if ((priv2 = MLKEM_private_key_new(rank)) == NULL) {
		warnx("private_key_new");
		failed |= 1;
	}
	if (!MLKEM_parse_private_key(priv2, encoded_private_key,
	    encoded_private_key_len)) {
		warnx("parse_private_key");
		failed |= 1;
	}

	if (!MLKEM_marshal_private_key(priv2, &tmp_buf, &tmp_buf_len)) {
		warnx("marshal_private_key");
		failed |= 1;
	}

	if (encoded_private_key_len != tmp_buf_len) {
		warnx("encoded private key lengths differ");
		failed |= 1;
	}

	if (compare_data(encoded_private_key, tmp_buf, tmp_buf_len,
	    "encoded private key") != 0) {
		warnx("compare_data");
		failed |= 1;
	}

	free(tmp_buf);
	tmp_buf = NULL;

	if (!MLKEM_encap(pub, &ciphertext, &ciphertext_len, &shared_secret1,
	    &s_len)) {
		warnx("encap failed using pub");
		failed |= 1;
	}

	if (s_len != MLKEM_SHARED_SECRET_LENGTH) {
		warnx("seed length %zu != %d", s_len,
		    MLKEM_SHARED_SECRET_LENGTH);
		failed |= 1;
	}

	if (!MLKEM_decap(priv, ciphertext, ciphertext_len,
	    &shared_secret2, &s_len)) {
		warnx("decap() failed using priv");
		failed |= 1;
	}

	if (s_len != MLKEM_SHARED_SECRET_LENGTH) {
		warnx("seed length %zu != %d", s_len,
		    MLKEM_SHARED_SECRET_LENGTH);
		failed |= 1;
	}

	if (compare_data(shared_secret1, shared_secret2, s_len,
	    "shared secrets with priv") != 0) {
		warnx("compare_data");
		failed |= 1;
	}

	free(shared_secret2);
	shared_secret2 = NULL;

	if (!MLKEM_decap(priv2, ciphertext, ciphertext_len,
	    &shared_secret2, &s_len)){
		warnx("decap() failed using priv2");
		failed |= 1;
	}

	if (s_len != MLKEM_SHARED_SECRET_LENGTH) {
		warnx("seed length %zu != %d", s_len,
		    MLKEM_SHARED_SECRET_LENGTH);
		failed |= 1;
	}

	if (compare_data(shared_secret1, shared_secret2, s_len,
	    "shared secrets with priv2") != 0) {
		warnx("compare_data");
		failed |= 1;
	}

	MLKEM_public_key_free(pub);
	MLKEM_public_key_free(pub2);
	MLKEM_public_key_free(pub3);
	MLKEM_private_key_free(priv);
	MLKEM_private_key_free(priv2);
	MLKEM_private_key_free(priv3);
	free(encoded_public_key);
	free(ciphertext);
	free(encoded_private_key);
	free(shared_secret1);
	free(shared_secret2);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= MlKemUnitTest(RANK768);
	failed |= MlKemUnitTest(RANK1024);

	return failed;
}

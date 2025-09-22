/*	$OpenBSD: mlkem.c,v 1.4 2025/09/05 23:30:12 beck Exp $ */
/*
 * Copyright (c) 2025, Bob Beck <beck@obtuse.com>
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
#include <stdlib.h>

#include <openssl/mlkem.h>
#include "mlkem_internal.h"

static inline int
private_key_is_new(const MLKEM_private_key *key)
{
	return (key != NULL &&
	    key->state == MLKEM_PRIVATE_KEY_UNINITIALIZED &&
	    (key->rank == RANK768 || key->rank == RANK1024));
}

static inline int
private_key_is_valid(const MLKEM_private_key *key)
{
	return (key != NULL &&
	    key->state == MLKEM_PRIVATE_KEY_INITIALIZED &&
	    (key->rank == RANK768 || key->rank == RANK1024));
}

static inline int
public_key_is_new(const MLKEM_public_key *key)
{
	return (key != NULL &&
	    key->state == MLKEM_PUBLIC_KEY_UNINITIALIZED &&
	    (key->rank == RANK768 || key->rank == RANK1024));
}

static inline int
public_key_is_valid(const MLKEM_public_key *key)
{
	return (key != NULL &&
	    key->state == MLKEM_PUBLIC_KEY_INITIALIZED &&
	    (key->rank == RANK768 || key->rank == RANK1024));
}

/*
 * ML-KEM operations
 */

int
MLKEM_generate_key_external_entropy(MLKEM_private_key *private_key,
    uint8_t **out_encoded_public_key, size_t *out_encoded_public_key_len,
    const uint8_t *entropy)
{
	uint8_t *k = NULL;
	size_t k_len = 0;
	int ret = 0;

	if (*out_encoded_public_key != NULL)
		goto err;

	if (!private_key_is_new(private_key))
		goto err;

	k_len = MLKEM768_PUBLIC_KEY_BYTES;
	if (private_key->rank == RANK1024)
		k_len = MLKEM1024_PUBLIC_KEY_BYTES;

	if ((k = calloc(1, k_len)) == NULL)
		goto err;

	if (!mlkem_generate_key_external_entropy(k, private_key, entropy))
		goto err;

	private_key->state = MLKEM_PRIVATE_KEY_INITIALIZED;

	*out_encoded_public_key = k;
	*out_encoded_public_key_len = k_len;
	k = NULL;
	k_len = 0;

	ret = 1;

 err:
	freezero(k, k_len);

	return ret;
}

int
MLKEM_generate_key(MLKEM_private_key *private_key,
    uint8_t **out_encoded_public_key, size_t *out_encoded_public_key_len,
    uint8_t **out_optional_seed, size_t *out_optional_seed_len)
{
	uint8_t *entropy_buf = NULL;
	int ret = 0;

	if (*out_encoded_public_key != NULL)
		goto err;

	if (out_optional_seed != NULL && *out_optional_seed != NULL)
		goto err;

	if ((entropy_buf = calloc(1, MLKEM_SEED_LENGTH)) == NULL)
		goto err;

	arc4random_buf(entropy_buf, MLKEM_SEED_LENGTH);
	if (!MLKEM_generate_key_external_entropy(private_key,
	    out_encoded_public_key, out_encoded_public_key_len,
	    entropy_buf))
		goto err;

	if (out_optional_seed != NULL) {
		*out_optional_seed = entropy_buf;
		*out_optional_seed_len = MLKEM_SEED_LENGTH;
		entropy_buf = NULL;
	}

	ret = 1;

 err:
	freezero(entropy_buf, MLKEM_SEED_LENGTH);

	return ret;
}
LCRYPTO_ALIAS(MLKEM_generate_key);

int
MLKEM_private_key_from_seed(MLKEM_private_key *private_key,
    const uint8_t *seed, size_t seed_len)
{
	int ret = 0;

	if (!private_key_is_new(private_key))
		goto err;

	if (seed_len != MLKEM_SEED_LENGTH)
		goto err;

	if (!mlkem_private_key_from_seed(seed, seed_len, private_key))
		goto err;

	private_key->state = MLKEM_PRIVATE_KEY_INITIALIZED;

	ret = 1;

 err:

	return ret;
}
LCRYPTO_ALIAS(MLKEM_private_key_from_seed);

int
MLKEM_public_from_private(const MLKEM_private_key *private_key,
    MLKEM_public_key *public_key)
{
	if (!private_key_is_valid(private_key))
		return 0;
	if (!public_key_is_new(public_key))
		return 0;
	if (public_key->rank != private_key->rank)
		return 0;

	mlkem_public_from_private(private_key, public_key);

	public_key->state = MLKEM_PUBLIC_KEY_INITIALIZED;

	return 1;
}
LCRYPTO_ALIAS(MLKEM_public_from_private);

int
MLKEM_encap_external_entropy(const MLKEM_public_key *public_key,
    const uint8_t *entropy, uint8_t **out_ciphertext,
    size_t *out_ciphertext_len, uint8_t **out_shared_secret,
    size_t *out_shared_secret_len)
{
	uint8_t *secret = NULL;
	uint8_t *ciphertext = NULL;
	size_t ciphertext_len = 0;
	int ret = 0;

	if (*out_ciphertext != NULL)
		goto err;

	if (*out_shared_secret != NULL)
		goto err;

	if (!public_key_is_valid(public_key))
		goto err;

	if ((secret = calloc(1, MLKEM_SHARED_SECRET_LENGTH)) == NULL)
		goto err;

	ciphertext_len = MLKEM_public_key_ciphertext_length(public_key);

	if ((ciphertext = calloc(1, ciphertext_len)) == NULL)
		goto err;

	mlkem_encap_external_entropy(ciphertext, secret, public_key, entropy);

	*out_ciphertext = ciphertext;
	*out_ciphertext_len = ciphertext_len;
	ciphertext = NULL;
	*out_shared_secret = secret;
	*out_shared_secret_len = MLKEM_SHARED_SECRET_LENGTH;
	secret = NULL;

	ret = 1;

 err:
	freezero(secret, MLKEM_SHARED_SECRET_LENGTH);
	freezero(ciphertext, ciphertext_len);

	return ret;
}

int
MLKEM_encap(const MLKEM_public_key *public_key,
    uint8_t **out_ciphertext, size_t *out_ciphertext_len,
    uint8_t **out_shared_secret, size_t *out_shared_secret_len)
{
	uint8_t entropy[MLKEM_ENCAP_ENTROPY];

	arc4random_buf(entropy, MLKEM_ENCAP_ENTROPY);

	return MLKEM_encap_external_entropy(public_key, entropy, out_ciphertext,
	    out_ciphertext_len, out_shared_secret, out_shared_secret_len);
}
LCRYPTO_ALIAS(MLKEM_encap);

int
MLKEM_decap(const MLKEM_private_key *private_key,
    const uint8_t *ciphertext, size_t ciphertext_len,
    uint8_t **out_shared_secret, size_t *out_shared_secret_len)
{
	uint8_t *s = NULL;
	int ret = 0;

	if (*out_shared_secret != NULL)
		goto err;

	if (!private_key_is_valid(private_key))
		goto err;

	if (ciphertext_len != MLKEM_private_key_ciphertext_length(private_key))
		goto err;

	if ((s = calloc(1, MLKEM_SHARED_SECRET_LENGTH)) == NULL)
		goto err;

	mlkem_decap(private_key, ciphertext, ciphertext_len, s);

	*out_shared_secret = s;
	*out_shared_secret_len = MLKEM_SHARED_SECRET_LENGTH;
	s = NULL;

	ret = 1;

 err:
	freezero(s, MLKEM_SHARED_SECRET_LENGTH);

	return ret;
}
LCRYPTO_ALIAS(MLKEM_decap);

int
MLKEM_marshal_public_key(const MLKEM_public_key *public_key, uint8_t **out,
    size_t *out_len)
{
	if (*out != NULL)
		return 0;

	if (!public_key_is_valid(public_key))
		return 0;

	return mlkem_marshal_public_key(public_key, out, out_len);
}
LCRYPTO_ALIAS(MLKEM_marshal_public_key);

/*
 * Not exposed publicly, becuase the NIST private key format is gigantisch, and
 * seeds should be used instead.  Used for the NIST tests.
 */
int
MLKEM_marshal_private_key(const MLKEM_private_key *private_key, uint8_t **out,
    size_t *out_len)
{
	if (*out != NULL)
		return 0;

	if (!private_key_is_valid(private_key))
		return 0;

	return mlkem_marshal_private_key(private_key, out, out_len);
}
LCRYPTO_ALIAS(MLKEM_marshal_private_key);

int
MLKEM_parse_public_key(MLKEM_public_key *public_key, const uint8_t *in,
    size_t in_len)
{
	if (!public_key_is_new(public_key))
		return 0;

	if (in_len != MLKEM_public_key_encoded_length(public_key))
		return 0;

	if (!mlkem_parse_public_key(in, in_len, public_key))
		return 0;

	public_key->state = MLKEM_PUBLIC_KEY_INITIALIZED;

	return 1;
}
LCRYPTO_ALIAS(MLKEM_parse_public_key);

int
MLKEM_parse_private_key(MLKEM_private_key *private_key, const uint8_t *in,
    size_t in_len)
{
	if (!private_key_is_new(private_key))
		return 0;

	if (in_len != MLKEM_private_key_encoded_length(private_key))
		return 0;

	if (!mlkem_parse_private_key(in, in_len, private_key))
		return 0;

	private_key->state = MLKEM_PRIVATE_KEY_INITIALIZED;

	return 1;
}
LCRYPTO_ALIAS(MLKEM_parse_private_key);

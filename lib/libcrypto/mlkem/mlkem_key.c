/* $OpenBSD: mlkem_key.c,v 1.4 2025/09/16 06:10:24 tb Exp $ */
/*
 * Copyright (c) 2025 Bob Beck <beck@obtuse.com>
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

#include <stdlib.h>
#include <string.h>

#include <openssl/mlkem.h>

#include "mlkem_internal.h"

MLKEM_private_key *
MLKEM_private_key_new(int rank)
{
	MLKEM_private_key *key = NULL;
	MLKEM_private_key *ret = NULL;

	if ((key = calloc(1, sizeof(*key))) == NULL)
		goto err;

	switch (rank) {
	case RANK768:
		if ((key->key_768 = calloc(1, sizeof(*key->key_768))) == NULL)
			goto err;
		break;
	case RANK1024:
		if ((key->key_1024 = calloc(1, sizeof(*key->key_1024))) == NULL)
			goto err;
		break;
	default:
		goto err;
	}
	key->rank = rank;
	key->state = MLKEM_PRIVATE_KEY_UNINITIALIZED;

	ret = key;
	key = NULL;

 err:
	MLKEM_private_key_free(key);

	return ret;
}
LCRYPTO_ALIAS(MLKEM_private_key_new);

void
MLKEM_private_key_free(MLKEM_private_key *key)
{
	if (key == NULL)
		return;

	freezero(key->key_768, sizeof(*key->key_768));
	freezero(key->key_1024, sizeof(*key->key_1024));
	freezero(key, sizeof(*key));
}
LCRYPTO_ALIAS(MLKEM_private_key_free);

size_t
MLKEM_private_key_encoded_length(const MLKEM_private_key *key)
{
	if (key == NULL)
		return 0;

	switch (key->rank) {
	case RANK768:
		return MLKEM768_PRIVATE_KEY_BYTES;
	case RANK1024:
		return MLKEM1024_PRIVATE_KEY_BYTES;
	default:
		return 0;
	}
	return 0;
}
LCRYPTO_ALIAS(MLKEM_private_key_encoded_length);

size_t
MLKEM_private_key_ciphertext_length(const MLKEM_private_key *key)
{
	if (key == NULL)
		return 0;

	switch (key->rank) {
	case RANK768:
		return MLKEM768_CIPHERTEXT_BYTES;
	case RANK1024:
		return MLKEM1024_CIPHERTEXT_BYTES;
	default:
		return 0;
	}
	return 0;
}
LCRYPTO_ALIAS(MLKEM_private_key_ciphertext_length);

MLKEM_public_key *
MLKEM_public_key_new(int rank)
{
	MLKEM_public_key *key = NULL;
	MLKEM_public_key *ret = NULL;

	if ((key = calloc(1, sizeof(*key))) == NULL)
		goto err;

	switch (rank) {
	case RANK768:
		if ((key->key_768 = calloc(1, sizeof(*key->key_768))) == NULL)
			goto err;
		break;
	case RANK1024:
		if ((key->key_1024 = calloc(1, sizeof(*key->key_1024))) == NULL)
			goto err;
		break;
	default:
		goto err;
	}

	key->rank = rank;
	key->state = MLKEM_PUBLIC_KEY_UNINITIALIZED;

	ret = key;
	key = NULL;

 err:
	MLKEM_public_key_free(key);

	return ret;
}
LCRYPTO_ALIAS(MLKEM_public_key_new);

void
MLKEM_public_key_free(MLKEM_public_key *key)
{
	if (key == NULL)
		return;

	freezero(key->key_768, sizeof(*key->key_768));
	freezero(key->key_1024, sizeof(*key->key_1024));
	freezero(key, sizeof(*key));
}
LCRYPTO_ALIAS(MLKEM_public_key_free);

size_t
MLKEM_public_key_encoded_length(const MLKEM_public_key *key)
{
	if (key == NULL)
		return 0;

	switch (key->rank) {
	case RANK768:
		return MLKEM768_PUBLIC_KEY_BYTES;
	case RANK1024:
		return MLKEM1024_PUBLIC_KEY_BYTES;
	default:
		return 0;
	}
	return 0;
}
LCRYPTO_ALIAS(MLKEM_public_key_encoded_length);

size_t
MLKEM_public_key_ciphertext_length(const MLKEM_public_key *key)
{
	if (key == NULL)
		return 0;

	switch (key->rank) {
	case RANK768:
		return MLKEM768_CIPHERTEXT_BYTES;
	case RANK1024:
		return MLKEM1024_CIPHERTEXT_BYTES;
	default:
		return 0;
	}
	return 0;
}
LCRYPTO_ALIAS(MLKEM_public_key_ciphertext_length);

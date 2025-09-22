/* $OpenBSD: e_chacha20poly1305.c,v 1.38 2025/05/10 05:54:38 tb Exp $ */

/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2015 Reyk Floter <reyk@openbsd.org>
 * Copyright (c) 2014, Google Inc.
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

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <openssl/opensslconf.h>

#if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)

#include <openssl/evp.h>
#include <openssl/chacha.h>
#include <openssl/poly1305.h>

#include "bytestring.h"
#include "err_local.h"
#include "evp_local.h"

#define POLY1305_TAG_LEN 16

#define CHACHA20_CONSTANT_LEN 4
#define CHACHA20_IV_LEN 8
#define CHACHA20_NONCE_LEN (CHACHA20_CONSTANT_LEN + CHACHA20_IV_LEN)
#define XCHACHA20_NONCE_LEN 24

struct aead_chacha20_poly1305_ctx {
	unsigned char key[32];
	unsigned char tag_len;
};

static int
aead_chacha20_poly1305_init(EVP_AEAD_CTX *ctx, const unsigned char *key,
    size_t key_len, size_t tag_len)
{
	struct aead_chacha20_poly1305_ctx *c20_ctx;

	if (tag_len == 0)
		tag_len = POLY1305_TAG_LEN;

	if (tag_len > POLY1305_TAG_LEN) {
		EVPerror(EVP_R_TOO_LARGE);
		return 0;
	}

	/* Internal error - EVP_AEAD_CTX_init should catch this. */
	if (key_len != sizeof(c20_ctx->key))
		return 0;

	c20_ctx = malloc(sizeof(struct aead_chacha20_poly1305_ctx));
	if (c20_ctx == NULL)
		return 0;

	memcpy(&c20_ctx->key[0], key, key_len);
	c20_ctx->tag_len = tag_len;
	ctx->aead_state = c20_ctx;

	return 1;
}

static void
aead_chacha20_poly1305_cleanup(EVP_AEAD_CTX *ctx)
{
	struct aead_chacha20_poly1305_ctx *c20_ctx = ctx->aead_state;

	freezero(c20_ctx, sizeof(*c20_ctx));
}

static void
poly1305_update_with_length(poly1305_state *poly1305,
    const unsigned char *data, size_t data_len)
{
	size_t j = data_len;
	unsigned char length_bytes[8];
	unsigned i;

	for (i = 0; i < sizeof(length_bytes); i++) {
		length_bytes[i] = j;
		j >>= 8;
	}

	if (data != NULL)
		CRYPTO_poly1305_update(poly1305, data, data_len);
	CRYPTO_poly1305_update(poly1305, length_bytes, sizeof(length_bytes));
}

static void
poly1305_pad16(poly1305_state *poly1305, size_t data_len)
{
	static const unsigned char zero_pad16[16];
	size_t pad_len;

	/* pad16() is defined in RFC 8439 2.8.1. */
	if ((pad_len = data_len % 16) == 0)
		return;

	CRYPTO_poly1305_update(poly1305, zero_pad16, 16 - pad_len);
}

static void
poly1305_update_with_pad16(poly1305_state *poly1305,
    const unsigned char *data, size_t data_len)
{
	CRYPTO_poly1305_update(poly1305, data, data_len);
	poly1305_pad16(poly1305, data_len);
}

static int
aead_chacha20_poly1305_seal(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len)
{
	const struct aead_chacha20_poly1305_ctx *c20_ctx = ctx->aead_state;
	unsigned char poly1305_key[32];
	poly1305_state poly1305;
	const unsigned char *iv;
	uint64_t ctr;

	if (max_out_len < in_len + c20_ctx->tag_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if (nonce_len != ctx->aead->nonce_len) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	ctr = (uint64_t)((uint32_t)(nonce[0]) | (uint32_t)(nonce[1]) << 8 |
	    (uint32_t)(nonce[2]) << 16 | (uint32_t)(nonce[3]) << 24) << 32;
	iv = nonce + CHACHA20_CONSTANT_LEN;

	memset(poly1305_key, 0, sizeof(poly1305_key));
	CRYPTO_chacha_20(poly1305_key, poly1305_key,
	    sizeof(poly1305_key), c20_ctx->key, iv, ctr);

	CRYPTO_poly1305_init(&poly1305, poly1305_key);
	poly1305_update_with_pad16(&poly1305, ad, ad_len);
	CRYPTO_chacha_20(out, in, in_len, c20_ctx->key, iv, ctr + 1);
	poly1305_update_with_pad16(&poly1305, out, in_len);
	poly1305_update_with_length(&poly1305, NULL, ad_len);
	poly1305_update_with_length(&poly1305, NULL, in_len);

	if (c20_ctx->tag_len != POLY1305_TAG_LEN) {
		unsigned char tag[POLY1305_TAG_LEN];
		CRYPTO_poly1305_finish(&poly1305, tag);
		memcpy(out + in_len, tag, c20_ctx->tag_len);
		*out_len = in_len + c20_ctx->tag_len;
		return 1;
	}

	CRYPTO_poly1305_finish(&poly1305, out + in_len);
	*out_len = in_len + POLY1305_TAG_LEN;
	return 1;
}

static int
aead_chacha20_poly1305_open(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len)
{
	const struct aead_chacha20_poly1305_ctx *c20_ctx = ctx->aead_state;
	unsigned char mac[POLY1305_TAG_LEN];
	unsigned char poly1305_key[32];
	const unsigned char *iv = nonce;
	poly1305_state poly1305;
	size_t plaintext_len;
	uint64_t ctr = 0;

	if (in_len < c20_ctx->tag_len) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	if (nonce_len != ctx->aead->nonce_len) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	plaintext_len = in_len - c20_ctx->tag_len;

	if (max_out_len < plaintext_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	ctr = (uint64_t)((uint32_t)(nonce[0]) | (uint32_t)(nonce[1]) << 8 |
	    (uint32_t)(nonce[2]) << 16 | (uint32_t)(nonce[3]) << 24) << 32;
	iv = nonce + CHACHA20_CONSTANT_LEN;

	memset(poly1305_key, 0, sizeof(poly1305_key));
	CRYPTO_chacha_20(poly1305_key, poly1305_key,
	    sizeof(poly1305_key), c20_ctx->key, iv, ctr);

	CRYPTO_poly1305_init(&poly1305, poly1305_key);
	poly1305_update_with_pad16(&poly1305, ad, ad_len);
	poly1305_update_with_pad16(&poly1305, in, plaintext_len);
	poly1305_update_with_length(&poly1305, NULL, ad_len);
	poly1305_update_with_length(&poly1305, NULL, plaintext_len);

	CRYPTO_poly1305_finish(&poly1305, mac);

	if (timingsafe_memcmp(mac, in + plaintext_len, c20_ctx->tag_len) != 0) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	CRYPTO_chacha_20(out, in, plaintext_len, c20_ctx->key, iv, ctr + 1);
	*out_len = plaintext_len;
	return 1;
}

static int
aead_xchacha20_poly1305_seal(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len)
{
	const struct aead_chacha20_poly1305_ctx *c20_ctx = ctx->aead_state;
	unsigned char poly1305_key[32];
	unsigned char subkey[32];
	poly1305_state poly1305;

	if (max_out_len < in_len + c20_ctx->tag_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if (nonce_len != ctx->aead->nonce_len) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	CRYPTO_hchacha_20(subkey, c20_ctx->key, nonce);

	CRYPTO_chacha_20(out, in, in_len, subkey, nonce + 16, 1);

	memset(poly1305_key, 0, sizeof(poly1305_key));
	CRYPTO_chacha_20(poly1305_key, poly1305_key, sizeof(poly1305_key),
	    subkey, nonce + 16, 0);

	CRYPTO_poly1305_init(&poly1305, poly1305_key);
	poly1305_update_with_pad16(&poly1305, ad, ad_len);
	poly1305_update_with_pad16(&poly1305, out, in_len);
	poly1305_update_with_length(&poly1305, NULL, ad_len);
	poly1305_update_with_length(&poly1305, NULL, in_len);

	if (c20_ctx->tag_len != POLY1305_TAG_LEN) {
		unsigned char tag[POLY1305_TAG_LEN];
		CRYPTO_poly1305_finish(&poly1305, tag);
		memcpy(out + in_len, tag, c20_ctx->tag_len);
		*out_len = in_len + c20_ctx->tag_len;
		return 1;
	}

	CRYPTO_poly1305_finish(&poly1305, out + in_len);
	*out_len = in_len + POLY1305_TAG_LEN;
	return 1;
}

static int
aead_xchacha20_poly1305_open(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len)
{
	const struct aead_chacha20_poly1305_ctx *c20_ctx = ctx->aead_state;
	unsigned char mac[POLY1305_TAG_LEN];
	unsigned char poly1305_key[32];
	unsigned char subkey[32];
	poly1305_state poly1305;
	size_t plaintext_len;

	if (in_len < c20_ctx->tag_len) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	if (nonce_len != ctx->aead->nonce_len) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	plaintext_len = in_len - c20_ctx->tag_len;

	if (max_out_len < plaintext_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	CRYPTO_hchacha_20(subkey, c20_ctx->key, nonce);

	memset(poly1305_key, 0, sizeof(poly1305_key));
	CRYPTO_chacha_20(poly1305_key, poly1305_key, sizeof(poly1305_key),
	    subkey, nonce + 16, 0);

	CRYPTO_poly1305_init(&poly1305, poly1305_key);
	poly1305_update_with_pad16(&poly1305, ad, ad_len);
	poly1305_update_with_pad16(&poly1305, in, plaintext_len);
	poly1305_update_with_length(&poly1305, NULL, ad_len);
	poly1305_update_with_length(&poly1305, NULL, plaintext_len);

	CRYPTO_poly1305_finish(&poly1305, mac);
	if (timingsafe_memcmp(mac, in + plaintext_len, c20_ctx->tag_len) != 0) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	CRYPTO_chacha_20(out, in, plaintext_len, subkey, nonce + 16, 1);

	*out_len = plaintext_len;
	return 1;
}

/* RFC 8439 */
static const EVP_AEAD aead_chacha20_poly1305 = {
	.key_len = 32,
	.nonce_len = CHACHA20_NONCE_LEN,
	.overhead = POLY1305_TAG_LEN,
	.max_tag_len = POLY1305_TAG_LEN,

	.init = aead_chacha20_poly1305_init,
	.cleanup = aead_chacha20_poly1305_cleanup,
	.seal = aead_chacha20_poly1305_seal,
	.open = aead_chacha20_poly1305_open,
};

const EVP_AEAD *
EVP_aead_chacha20_poly1305(void)
{
	return &aead_chacha20_poly1305;
}
LCRYPTO_ALIAS(EVP_aead_chacha20_poly1305);

static const EVP_AEAD aead_xchacha20_poly1305 = {
	.key_len = 32,
	.nonce_len = XCHACHA20_NONCE_LEN,
	.overhead = POLY1305_TAG_LEN,
	.max_tag_len = POLY1305_TAG_LEN,

	.init = aead_chacha20_poly1305_init,
	.cleanup = aead_chacha20_poly1305_cleanup,
	.seal = aead_xchacha20_poly1305_seal,
	.open = aead_xchacha20_poly1305_open,
};

const EVP_AEAD *
EVP_aead_xchacha20_poly1305(void)
{
	return &aead_xchacha20_poly1305;
}
LCRYPTO_ALIAS(EVP_aead_xchacha20_poly1305);

struct chacha20_poly1305_ctx {
	ChaCha_ctx chacha;
	poly1305_state poly1305;

	unsigned char key[32];
	unsigned char nonce[CHACHA20_NONCE_LEN];
	size_t nonce_len;
	unsigned char tag[POLY1305_TAG_LEN];
	size_t tag_len;

	size_t ad_len;
	size_t in_len;

	int in_ad;
	int started;
};

static int
chacha20_poly1305_init(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int encrypt)
{
	struct chacha20_poly1305_ctx *cpx = ctx->cipher_data;
	uint8_t *data;
	CBB cbb;
	int ret = 0;

	memset(&cbb, 0, sizeof(cbb));

	if (key == NULL && iv == NULL)
		goto done;

	cpx->started = 0;

	if (key != NULL)
		memcpy(cpx->key, key, sizeof(cpx->key));

	if (iv != NULL) {
		/*
		 * Left zero pad if configured nonce length is less than ChaCha
		 * nonce length.
		 */
		if (!CBB_init_fixed(&cbb, cpx->nonce, sizeof(cpx->nonce)))
			goto err;
		if (!CBB_add_space(&cbb, &data, sizeof(cpx->nonce) - cpx->nonce_len))
			goto err;
		if (!CBB_add_bytes(&cbb, iv, cpx->nonce_len))
			goto err;
		if (!CBB_finish(&cbb, NULL, NULL))
			goto err;
	}

 done:
	ret = 1;

 err:
	CBB_cleanup(&cbb);

	return ret;
}

static int
chacha20_poly1305_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	struct chacha20_poly1305_ctx *cpx = ctx->cipher_data;

	/*
	 * Since we're making AEAD work within the constraints of EVP_CIPHER...
	 * If in is non-NULL then this is an update, while if in is NULL then
	 * this is a final. If in is non-NULL but out is NULL, then the input
	 * being provided is associated data. Plus we have to handle encryption
	 * (sealing) and decryption (opening) in the same function.
	 */

	if (!cpx->started) {
		unsigned char poly1305_key[32];
		const unsigned char *iv;
		uint64_t ctr;

		ctr = (uint64_t)((uint32_t)(cpx->nonce[0]) |
		    (uint32_t)(cpx->nonce[1]) << 8 |
		    (uint32_t)(cpx->nonce[2]) << 16 |
		    (uint32_t)(cpx->nonce[3]) << 24) << 32;
		iv = cpx->nonce + CHACHA20_CONSTANT_LEN;

		ChaCha_set_key(&cpx->chacha, cpx->key, 8 * sizeof(cpx->key));
		ChaCha_set_iv(&cpx->chacha, iv, NULL);

		/* See chacha.c for details re handling of counter. */
		cpx->chacha.input[12] = (uint32_t)ctr;
		cpx->chacha.input[13] = (uint32_t)(ctr >> 32);

		memset(poly1305_key, 0, sizeof(poly1305_key));
		ChaCha(&cpx->chacha, poly1305_key, poly1305_key,
		    sizeof(poly1305_key));
		CRYPTO_poly1305_init(&cpx->poly1305, poly1305_key);

		/* Mark remaining key block as used. */
		cpx->chacha.unused = 0;

		cpx->ad_len = 0;
		cpx->in_len = 0;
		cpx->in_ad = 0;

		cpx->started = 1;
	}

	if (len > SIZE_MAX - cpx->in_len) {
		EVPerror(EVP_R_TOO_LARGE);
		return -1;
	}

	/* Disallow authenticated data after plaintext/ciphertext. */
	if (cpx->in_len > 0 && in != NULL && out == NULL)
		return -1;

	if (cpx->in_ad && (in == NULL || out != NULL)) {
		poly1305_pad16(&cpx->poly1305, cpx->ad_len);
		cpx->in_ad = 0;
	}

	/* Update with AD or plaintext/ciphertext. */
	if (in != NULL) {
		if (!ctx->encrypt || out == NULL)
			CRYPTO_poly1305_update(&cpx->poly1305, in, len);
		if (out == NULL) {
			cpx->ad_len += len;
			cpx->in_ad = 1;
		} else {
			ChaCha(&cpx->chacha, out, in, len);
			cpx->in_len += len;
		}
		if (ctx->encrypt && out != NULL)
			CRYPTO_poly1305_update(&cpx->poly1305, out, len);

		return len;
	}

	/* Final. */
	poly1305_pad16(&cpx->poly1305, cpx->in_len);
	poly1305_update_with_length(&cpx->poly1305, NULL, cpx->ad_len);
	poly1305_update_with_length(&cpx->poly1305, NULL, cpx->in_len);

	if (ctx->encrypt) {
		CRYPTO_poly1305_finish(&cpx->poly1305, cpx->tag);
		cpx->tag_len = sizeof(cpx->tag);
	} else {
		unsigned char tag[POLY1305_TAG_LEN];

		/* Ensure that a tag has been provided. */
		if (cpx->tag_len <= 0)
			return -1;

		CRYPTO_poly1305_finish(&cpx->poly1305, tag);
		if (timingsafe_memcmp(tag, cpx->tag, cpx->tag_len) != 0)
			return -1;
	}

	cpx->started = 0;

	return len;
}

static int
chacha20_poly1305_cleanup(EVP_CIPHER_CTX *ctx)
{
	struct chacha20_poly1305_ctx *cpx = ctx->cipher_data;

	explicit_bzero(cpx, sizeof(*cpx));

	return 1;
}

static int
chacha20_poly1305_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
{
	struct chacha20_poly1305_ctx *cpx = ctx->cipher_data;

	switch (type) {
	case EVP_CTRL_INIT:
		memset(cpx, 0, sizeof(*cpx));
		cpx->nonce_len = sizeof(cpx->nonce);
		return 1;

	case EVP_CTRL_AEAD_GET_IVLEN:
		if (cpx->nonce_len > INT_MAX)
			return 0;
		*(int *)ptr = (int)cpx->nonce_len;
		return 1;

	case EVP_CTRL_AEAD_SET_IVLEN:
		if (arg <= 0 || arg > sizeof(cpx->nonce))
			return 0;
		cpx->nonce_len = arg;
		return 1;

	case EVP_CTRL_AEAD_SET_TAG:
		if (ctx->encrypt)
			return 0;
		if (arg <= 0 || arg > sizeof(cpx->tag))
			return 0;
		if (ptr != NULL) {
			memcpy(cpx->tag, ptr, arg);
			cpx->tag_len = arg;
		}
		return 1;

	case EVP_CTRL_AEAD_GET_TAG:
		if (!ctx->encrypt)
			return 0;
		if (arg <= 0 || arg > cpx->tag_len)
			return 0;
		memcpy(ptr, cpx->tag, arg);
		return 1;

	case EVP_CTRL_AEAD_SET_IV_FIXED:
		if (arg != sizeof(cpx->nonce))
			return 0;
		memcpy(cpx->nonce, ptr, arg);
		return 1;
	}

	return -1;
}

static const EVP_CIPHER cipher_chacha20_poly1305 = {
	.nid = NID_chacha20_poly1305,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 12,
	.flags = EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT |
	    EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_IV_LENGTH |
	    EVP_CIPH_FLAG_AEAD_CIPHER | EVP_CIPH_FLAG_CUSTOM_CIPHER |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = chacha20_poly1305_init,
	.do_cipher = chacha20_poly1305_cipher,
	.cleanup = chacha20_poly1305_cleanup,
	.ctx_size = sizeof(struct chacha20_poly1305_ctx),
	.ctrl = chacha20_poly1305_ctrl,
};

const EVP_CIPHER *
EVP_chacha20_poly1305(void)
{
	return &cipher_chacha20_poly1305;
}
LCRYPTO_ALIAS(EVP_chacha20_poly1305);

#endif  /* !OPENSSL_NO_CHACHA && !OPENSSL_NO_POLY1305 */

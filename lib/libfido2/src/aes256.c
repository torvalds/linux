/*
 * Copyright (c) 2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

static int
aes256_cbc(const fido_blob_t *key, const u_char *iv, const fido_blob_t *in,
    fido_blob_t *out, int encrypt)
{
	EVP_CIPHER_CTX *ctx = NULL;
	const EVP_CIPHER *cipher;
	int ok = -1;

	memset(out, 0, sizeof(*out));

	if (key->len != 32) {
		fido_log_debug("%s: invalid key len %zu", __func__, key->len);
		goto fail;
	}
	if (in->len > UINT_MAX || in->len % 16 || in->len == 0) {
		fido_log_debug("%s: invalid input len %zu", __func__, in->len);
		goto fail;
	}
	out->len = in->len;
	if ((out->ptr = calloc(1, out->len)) == NULL) {
		fido_log_debug("%s: calloc", __func__);
		goto fail;
	}
	if ((ctx = EVP_CIPHER_CTX_new()) == NULL ||
	    (cipher = EVP_aes_256_cbc()) == NULL) {
		fido_log_debug("%s: EVP_CIPHER_CTX_new", __func__);
		goto fail;
	}
	if (EVP_CipherInit(ctx, cipher, key->ptr, iv, encrypt) == 0 ||
	    EVP_Cipher(ctx, out->ptr, in->ptr, (u_int)out->len) < 0) {
		fido_log_debug("%s: EVP_Cipher", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (ctx != NULL)
		EVP_CIPHER_CTX_free(ctx);
	if (ok < 0)
		fido_blob_reset(out);

	return ok;
}

static int
aes256_cbc_proto1(const fido_blob_t *key, const fido_blob_t *in,
    fido_blob_t *out, int encrypt)
{
	u_char iv[16];

	memset(&iv, 0, sizeof(iv));

	return aes256_cbc(key, iv, in, out, encrypt);
}

static int
aes256_cbc_fips(const fido_blob_t *secret, const fido_blob_t *in,
    fido_blob_t *out, int encrypt)
{
	fido_blob_t key, cin, cout;
	u_char iv[16];

	memset(out, 0, sizeof(*out));

	if (secret->len != 64) {
		fido_log_debug("%s: invalid secret len %zu", __func__,
		    secret->len);
		return -1;
	}
	if (in->len < sizeof(iv)) {
		fido_log_debug("%s: invalid input len %zu", __func__, in->len);
		return -1;
	}
	if (encrypt) {
		if (fido_get_random(iv, sizeof(iv)) < 0) {
			fido_log_debug("%s: fido_get_random", __func__);
			return -1;
		}
		cin = *in;
	} else {
		memcpy(iv, in->ptr, sizeof(iv));
		cin.ptr = in->ptr + sizeof(iv);
		cin.len = in->len - sizeof(iv);
	}
	key.ptr = secret->ptr + 32;
	key.len = secret->len - 32;
	if (aes256_cbc(&key, iv, &cin, &cout, encrypt) < 0)
		return -1;
	if (encrypt) {
		if (cout.len > SIZE_MAX - sizeof(iv) ||
		    (out->ptr = calloc(1, sizeof(iv) + cout.len)) == NULL) {
			fido_blob_reset(&cout);
			return -1;
		}
		out->len = sizeof(iv) + cout.len;
		memcpy(out->ptr, iv, sizeof(iv));
		memcpy(out->ptr + sizeof(iv), cout.ptr, cout.len);
		fido_blob_reset(&cout);
	} else
		*out = cout;

	return 0;
}

static int
aes256_gcm(const fido_blob_t *key, const fido_blob_t *nonce,
    const fido_blob_t *aad, const fido_blob_t *in, fido_blob_t *out,
    int encrypt)
{
	EVP_CIPHER_CTX *ctx = NULL;
	const EVP_CIPHER *cipher;
	size_t textlen;
	int ok = -1;

	memset(out, 0, sizeof(*out));

	if (nonce->len != 12 || key->len != 32 || aad->len > UINT_MAX) {
		fido_log_debug("%s: invalid params %zu, %zu, %zu", __func__,
		    nonce->len, key->len, aad->len);
		goto fail;
	}
	if (in->len > UINT_MAX || in->len > SIZE_MAX - 16 || in->len < 16) {
		fido_log_debug("%s: invalid input len %zu", __func__, in->len);
		goto fail;
	}
	/* add tag to (on encrypt) or trim tag from the output (on decrypt) */
	out->len = encrypt ? in->len + 16 : in->len - 16;
	if ((out->ptr = calloc(1, out->len)) == NULL) {
		fido_log_debug("%s: calloc", __func__);
		goto fail;
	}
	if ((ctx = EVP_CIPHER_CTX_new()) == NULL ||
	    (cipher = EVP_aes_256_gcm()) == NULL) {
		fido_log_debug("%s: EVP_CIPHER_CTX_new", __func__);
		goto fail;
	}
	if (EVP_CipherInit(ctx, cipher, key->ptr, nonce->ptr, encrypt) == 0) {
		fido_log_debug("%s: EVP_CipherInit", __func__);
		goto fail;
	}

	if (encrypt)
		textlen = in->len;
	else {
		textlen = in->len - 16;
		/* point openssl at the mac tag */
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
		    in->ptr + in->len - 16) == 0) {
			fido_log_debug("%s: EVP_CIPHER_CTX_ctrl", __func__);
			goto fail;
		}
	}
	/* the last EVP_Cipher() will either compute or verify the mac tag */
	if (EVP_Cipher(ctx, NULL, aad->ptr, (u_int)aad->len) < 0 ||
	    EVP_Cipher(ctx, out->ptr, in->ptr, (u_int)textlen) < 0 ||
	    EVP_Cipher(ctx, NULL, NULL, 0) < 0) {
		fido_log_debug("%s: EVP_Cipher", __func__);
		goto fail;
	}
	if (encrypt) {
		/* append the mac tag */
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16,
		    out->ptr + out->len - 16) == 0) {
			fido_log_debug("%s: EVP_CIPHER_CTX_ctrl", __func__);
			goto fail;
		}
	}

	ok = 0;
fail:
	if (ctx != NULL)
		EVP_CIPHER_CTX_free(ctx);
	if (ok < 0)
		fido_blob_reset(out);

	return ok;
}

int
aes256_cbc_enc(const fido_dev_t *dev, const fido_blob_t *secret,
    const fido_blob_t *in, fido_blob_t *out)
{
	return fido_dev_get_pin_protocol(dev) == 2 ? aes256_cbc_fips(secret,
	    in, out, 1) : aes256_cbc_proto1(secret, in, out, 1);
}

int
aes256_cbc_dec(const fido_dev_t *dev, const fido_blob_t *secret,
    const fido_blob_t *in, fido_blob_t *out)
{
	return fido_dev_get_pin_protocol(dev) == 2 ? aes256_cbc_fips(secret,
	    in, out, 0) : aes256_cbc_proto1(secret, in, out, 0);
}

int
aes256_gcm_enc(const fido_blob_t *key, const fido_blob_t *nonce,
    const fido_blob_t *aad, const fido_blob_t *in, fido_blob_t *out)
{
	return aes256_gcm(key, nonce, aad, in, out, 1);
}

int
aes256_gcm_dec(const fido_blob_t *key, const fido_blob_t *nonce,
    const fido_blob_t *aad, const fido_blob_t *in, fido_blob_t *out)
{
	return aes256_gcm(key, nonce, aad, in, out, 0);
}

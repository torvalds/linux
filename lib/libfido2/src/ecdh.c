/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/evp.h>
#include <openssl/sha.h>
#if defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/hkdf.h>
#else
#include <openssl/kdf.h>
#endif

#include "fido.h"
#include "fido/es256.h"

#if defined(LIBRESSL_VERSION_NUMBER)
static int
hkdf_sha256(uint8_t *key, const char *info, const fido_blob_t *secret)
{
	const EVP_MD *md;
	uint8_t salt[32];

	memset(salt, 0, sizeof(salt));
	if ((md = EVP_sha256()) == NULL ||
	    HKDF(key, SHA256_DIGEST_LENGTH, md, secret->ptr, secret->len, salt,
	    sizeof(salt), (const uint8_t *)info, strlen(info)) != 1)
		return -1;

	return 0;
}
#else
static int
hkdf_sha256(uint8_t *key, char *info, fido_blob_t *secret)
{
	const EVP_MD *const_md;
	EVP_MD *md = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	size_t keylen = SHA256_DIGEST_LENGTH;
	uint8_t	salt[32];
	int ok = -1;

	memset(salt, 0, sizeof(salt));
	if (secret->len > INT_MAX || strlen(info) > INT_MAX) {
		fido_log_debug("%s: invalid param", __func__);
		goto fail;
	}
	if ((const_md = EVP_sha256()) == NULL ||
	    (md = EVP_MD_meth_dup(const_md)) == NULL ||
	    (ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL)) == NULL) {
		fido_log_debug("%s: init", __func__);
		goto fail;
	}
	if (EVP_PKEY_derive_init(ctx) < 1 ||
	    EVP_PKEY_CTX_set_hkdf_md(ctx, md) < 1 ||
	    EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt, sizeof(salt)) < 1 ||
	    EVP_PKEY_CTX_set1_hkdf_key(ctx, secret->ptr, (int)secret->len) < 1 ||
	    EVP_PKEY_CTX_add1_hkdf_info(ctx, (void *)info, (int)strlen(info)) < 1) {
		fido_log_debug("%s: EVP_PKEY_CTX", __func__);
		goto fail;
	}
	if (EVP_PKEY_derive(ctx, key, &keylen) < 1) {
		fido_log_debug("%s: EVP_PKEY_derive", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (md != NULL)
		EVP_MD_meth_free(md);
	if (ctx != NULL)
		EVP_PKEY_CTX_free(ctx);

	return ok;
}
#endif /* defined(LIBRESSL_VERSION_NUMBER) */

static int
kdf(uint8_t prot, fido_blob_t *key, /* const */ fido_blob_t *secret)
{
	char hmac_info[] = "CTAP2 HMAC key"; /* const */
	char aes_info[] = "CTAP2 AES key"; /* const */

	switch (prot) {
	case CTAP_PIN_PROTOCOL1:
		/* use sha256 on the resulting secret */
		key->len = SHA256_DIGEST_LENGTH;
		if ((key->ptr = calloc(1, key->len)) == NULL ||
		    SHA256(secret->ptr, secret->len, key->ptr) != key->ptr) {
			fido_log_debug("%s: SHA256", __func__);
			return -1;
		}
		break;
	case CTAP_PIN_PROTOCOL2:
		/* use two instances of hkdf-sha256 on the resulting secret */
		key->len = 2 * SHA256_DIGEST_LENGTH;
		if ((key->ptr = calloc(1, key->len)) == NULL ||
		    hkdf_sha256(key->ptr, hmac_info, secret) < 0 ||
		    hkdf_sha256(key->ptr + SHA256_DIGEST_LENGTH, aes_info,
		    secret) < 0) {
			fido_log_debug("%s: hkdf", __func__);
			return -1;
		}
		break;
	default:
		fido_log_debug("%s: unknown pin protocol %u", __func__, prot);
		return -1;
	}

	return 0;
}

static int
do_ecdh(const fido_dev_t *dev, const es256_sk_t *sk, const es256_pk_t *pk,
    fido_blob_t **ecdh)
{
	EVP_PKEY *pk_evp = NULL;
	EVP_PKEY *sk_evp = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	fido_blob_t *secret = NULL;
	int ok = -1;

	*ecdh = NULL;
	if ((secret = fido_blob_new()) == NULL ||
	    (*ecdh = fido_blob_new()) == NULL)
		goto fail;
	if ((pk_evp = es256_pk_to_EVP_PKEY(pk)) == NULL ||
	    (sk_evp = es256_sk_to_EVP_PKEY(sk)) == NULL) {
		fido_log_debug("%s: es256_to_EVP_PKEY", __func__);
		goto fail;
	}
	if ((ctx = EVP_PKEY_CTX_new(sk_evp, NULL)) == NULL ||
	    EVP_PKEY_derive_init(ctx) <= 0 ||
	    EVP_PKEY_derive_set_peer(ctx, pk_evp) <= 0) {
		fido_log_debug("%s: EVP_PKEY_derive_init", __func__);
		goto fail;
	}
	if (EVP_PKEY_derive(ctx, NULL, &secret->len) <= 0 ||
	    (secret->ptr = calloc(1, secret->len)) == NULL ||
	    EVP_PKEY_derive(ctx, secret->ptr, &secret->len) <= 0) {
		fido_log_debug("%s: EVP_PKEY_derive", __func__);
		goto fail;
	}
	if (kdf(fido_dev_get_pin_protocol(dev), *ecdh, secret) < 0) {
		fido_log_debug("%s: kdf", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pk_evp != NULL)
		EVP_PKEY_free(pk_evp);
	if (sk_evp != NULL)
		EVP_PKEY_free(sk_evp);
	if (ctx != NULL)
		EVP_PKEY_CTX_free(ctx);
	if (ok < 0)
		fido_blob_free(ecdh);

	fido_blob_free(&secret);

	return ok;
}

int
fido_do_ecdh(fido_dev_t *dev, es256_pk_t **pk, fido_blob_t **ecdh, int *ms)
{
	es256_sk_t *sk = NULL; /* our private key */
	es256_pk_t *ak = NULL; /* authenticator's public key */
	int r;

	*pk = NULL;
	*ecdh = NULL;
	if ((sk = es256_sk_new()) == NULL || (*pk = es256_pk_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if (es256_sk_create(sk) < 0 || es256_derive_pk(sk, *pk) < 0) {
		fido_log_debug("%s: es256_derive_pk", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if ((ak = es256_pk_new()) == NULL ||
	    fido_dev_authkey(dev, ak, ms) != FIDO_OK) {
		fido_log_debug("%s: fido_dev_authkey", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if (do_ecdh(dev, sk, ak, ecdh) < 0) {
		fido_log_debug("%s: do_ecdh", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	es256_sk_free(&sk);
	es256_pk_free(&ak);

	if (r != FIDO_OK) {
		es256_pk_free(pk);
		fido_blob_free(ecdh);
	}

	return r;
}

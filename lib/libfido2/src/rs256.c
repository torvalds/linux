/*
 * Copyright (c) 2018-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/obj_mac.h>

#include "fido.h"
#include "fido/rs256.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000
#define get0_RSA(x)	EVP_PKEY_get0_RSA((x))
#else
#define get0_RSA(x)	EVP_PKEY_get0((x))
#endif

#define PRAGMA(s)

static EVP_MD *
rs256_get_EVP_MD(void)
{
	PRAGMA("GCC diagnostic push");
	PRAGMA("GCC diagnostic ignored \"-Wcast-qual\"");
	return ((EVP_MD *)EVP_sha256());
	PRAGMA("GCC diagnostic pop");
}

static int
decode_bignum(const cbor_item_t *item, void *ptr, size_t len)
{
	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false ||
	    cbor_bytestring_length(item) != len) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	memcpy(ptr, cbor_bytestring_handle(item), len);

	return (0);
}

static int
decode_rsa_pubkey(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	rs256_pk_t *k = arg;

	if (cbor_isa_negint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8)
		return (0); /* ignore */

	switch (cbor_get_uint8(key)) {
	case 0: /* modulus */
		return (decode_bignum(val, &k->n, sizeof(k->n)));
	case 1: /* public exponent */
		return (decode_bignum(val, &k->e, sizeof(k->e)));
	}

	return (0); /* ignore */
}

int
rs256_pk_decode(const cbor_item_t *item, rs256_pk_t *k)
{
	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, k, decode_rsa_pubkey) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	return (0);
}

rs256_pk_t *
rs256_pk_new(void)
{
	return (calloc(1, sizeof(rs256_pk_t)));
}

void
rs256_pk_free(rs256_pk_t **pkp)
{
	rs256_pk_t *pk;

	if (pkp == NULL || (pk = *pkp) == NULL)
		return;

	freezero(pk, sizeof(*pk));
	*pkp = NULL;
}

int
rs256_pk_from_ptr(rs256_pk_t *pk, const void *ptr, size_t len)
{
	EVP_PKEY *pkey;

	if (len < sizeof(*pk))
		return (FIDO_ERR_INVALID_ARGUMENT);

	memcpy(pk, ptr, sizeof(*pk));

	if ((pkey = rs256_pk_to_EVP_PKEY(pk)) == NULL) {
		fido_log_debug("%s: rs256_pk_to_EVP_PKEY", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	EVP_PKEY_free(pkey);

	return (FIDO_OK);
}

EVP_PKEY *
rs256_pk_to_EVP_PKEY(const rs256_pk_t *k)
{
	RSA		*rsa = NULL;
	EVP_PKEY	*pkey = NULL;
	BIGNUM		*n = NULL;
	BIGNUM		*e = NULL;
	int		 ok = -1;

	if ((n = BN_new()) == NULL || (e = BN_new()) == NULL)
		goto fail;

	if (BN_bin2bn(k->n, sizeof(k->n), n) == NULL ||
	    BN_bin2bn(k->e, sizeof(k->e), e) == NULL) {
		fido_log_debug("%s: BN_bin2bn", __func__);
		goto fail;
	}

	if ((rsa = RSA_new()) == NULL || RSA_set0_key(rsa, n, e, NULL) == 0) {
		fido_log_debug("%s: RSA_set0_key", __func__);
		goto fail;
	}

	/* at this point, n and e belong to rsa */
	n = NULL;
	e = NULL;

	if (RSA_bits(rsa) != 2048) {
		fido_log_debug("%s: invalid key length", __func__);
		goto fail;
	}

	if ((pkey = EVP_PKEY_new()) == NULL ||
	    EVP_PKEY_assign_RSA(pkey, rsa) == 0) {
		fido_log_debug("%s: EVP_PKEY_assign_RSA", __func__);
		goto fail;
	}

	rsa = NULL; /* at this point, rsa belongs to evp */

	ok = 0;
fail:
	if (n != NULL)
		BN_free(n);
	if (e != NULL)
		BN_free(e);
	if (rsa != NULL)
		RSA_free(rsa);
	if (ok < 0 && pkey != NULL) {
		EVP_PKEY_free(pkey);
		pkey = NULL;
	}

	return (pkey);
}

int
rs256_pk_from_RSA(rs256_pk_t *pk, const RSA *rsa)
{
	const BIGNUM	*n = NULL;
	const BIGNUM	*e = NULL;
	const BIGNUM	*d = NULL;
	int		 k;

	if (RSA_bits(rsa) != 2048) {
		fido_log_debug("%s: invalid key length", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	RSA_get0_key(rsa, &n, &e, &d);

	if (n == NULL || e == NULL) {
		fido_log_debug("%s: RSA_get0_key", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((k = BN_num_bytes(n)) < 0 || (size_t)k > sizeof(pk->n) ||
	    (k = BN_num_bytes(e)) < 0 || (size_t)k > sizeof(pk->e)) {
		fido_log_debug("%s: invalid key", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((k = BN_bn2bin(n, pk->n)) < 0 || (size_t)k > sizeof(pk->n) ||
	    (k = BN_bn2bin(e, pk->e)) < 0 || (size_t)k > sizeof(pk->e)) {
		fido_log_debug("%s: BN_bn2bin", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	return (FIDO_OK);
}

int
rs256_pk_from_EVP_PKEY(rs256_pk_t *pk, const EVP_PKEY *pkey)
{
	const RSA *rsa;

	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA ||
	    (rsa = get0_RSA(pkey)) == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (rs256_pk_from_RSA(pk, rsa));
}

int
rs256_verify_sig(const fido_blob_t *dgst, EVP_PKEY *pkey,
    const fido_blob_t *sig)
{
	EVP_PKEY_CTX	*pctx = NULL;
	EVP_MD		*md = NULL;
	int		 ok = -1;

	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA) {
		fido_log_debug("%s: EVP_PKEY_base_id", __func__);
		goto fail;
	}

	if ((md = rs256_get_EVP_MD()) == NULL) {
		fido_log_debug("%s: rs256_get_EVP_MD", __func__);
		goto fail;
	}

	if ((pctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL ||
	    EVP_PKEY_verify_init(pctx) != 1 ||
	    EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) != 1 ||
	    EVP_PKEY_CTX_set_signature_md(pctx, md) != 1) {
		fido_log_debug("%s: EVP_PKEY_CTX", __func__);
		goto fail;
	}

	if (EVP_PKEY_verify(pctx, sig->ptr, sig->len, dgst->ptr,
	    dgst->len) != 1) {
		fido_log_debug("%s: EVP_PKEY_verify", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_PKEY_CTX_free(pctx);

	return (ok);
}

int
rs256_pk_verify_sig(const fido_blob_t *dgst, const rs256_pk_t *pk,
    const fido_blob_t *sig)
{
	EVP_PKEY	*pkey;
	int		 ok = -1;

	if ((pkey = rs256_pk_to_EVP_PKEY(pk)) == NULL ||
	    rs256_verify_sig(dgst, pkey, sig) < 0) {
		fido_log_debug("%s: rs256_verify_sig", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_PKEY_free(pkey);

	return (ok);
}

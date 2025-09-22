/*
 * Copyright (c) 2019-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#include "fido.h"
#include "fido/eddsa.h"

#if defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x3070000f
EVP_PKEY *
EVP_PKEY_new_raw_public_key(int type, ENGINE *e, const unsigned char *key,
    size_t keylen)
{
	(void)type;
	(void)e;
	(void)key;
	(void)keylen;

	fido_log_debug("%s: unimplemented", __func__);

	return (NULL);
}

int
EVP_PKEY_get_raw_public_key(const EVP_PKEY *pkey, unsigned char *pub,
    size_t *len)
{
	(void)pkey;
	(void)pub;
	(void)len;

	fido_log_debug("%s: unimplemented", __func__);

	return (0);
}
#endif /* LIBRESSL_VERSION_NUMBER */

#if defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x3040000f
int
EVP_DigestVerify(EVP_MD_CTX *ctx, const unsigned char *sigret, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	(void)ctx;
	(void)sigret;
	(void)siglen;
	(void)tbs;
	(void)tbslen;

	fido_log_debug("%s: unimplemented", __func__);

	return (0);
}
#endif /* LIBRESSL_VERSION_NUMBER < 0x3040000f */

static int
decode_coord(const cbor_item_t *item, void *xy, size_t xy_len)
{
	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false ||
	    cbor_bytestring_length(item) != xy_len) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	memcpy(xy, cbor_bytestring_handle(item), xy_len);

	return (0);
}

static int
decode_pubkey_point(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	eddsa_pk_t *k = arg;

	if (cbor_isa_negint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8)
		return (0); /* ignore */

	switch (cbor_get_uint8(key)) {
	case 1: /* x coordinate */
		return (decode_coord(val, &k->x, sizeof(k->x)));
	}

	return (0); /* ignore */
}

int
eddsa_pk_decode(const cbor_item_t *item, eddsa_pk_t *k)
{
	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, k, decode_pubkey_point) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	return (0);
}

eddsa_pk_t *
eddsa_pk_new(void)
{
	return (calloc(1, sizeof(eddsa_pk_t)));
}

void
eddsa_pk_free(eddsa_pk_t **pkp)
{
	eddsa_pk_t *pk;

	if (pkp == NULL || (pk = *pkp) == NULL)
		return;

	freezero(pk, sizeof(*pk));
	*pkp = NULL;
}

int
eddsa_pk_from_ptr(eddsa_pk_t *pk, const void *ptr, size_t len)
{
	EVP_PKEY *pkey;

	if (len < sizeof(*pk))
		return (FIDO_ERR_INVALID_ARGUMENT);

	memcpy(pk, ptr, sizeof(*pk));

	if ((pkey = eddsa_pk_to_EVP_PKEY(pk)) == NULL) {
		fido_log_debug("%s: eddsa_pk_to_EVP_PKEY", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	EVP_PKEY_free(pkey);

	return (FIDO_OK);
}

EVP_PKEY *
eddsa_pk_to_EVP_PKEY(const eddsa_pk_t *k)
{
	EVP_PKEY *pkey = NULL;

	if ((pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, k->x,
	    sizeof(k->x))) == NULL)
		fido_log_debug("%s: EVP_PKEY_new_raw_public_key", __func__);

	return (pkey);
}

int
eddsa_pk_from_EVP_PKEY(eddsa_pk_t *pk, const EVP_PKEY *pkey)
{
	size_t len = 0;

	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_ED25519)
		return (FIDO_ERR_INVALID_ARGUMENT);
	if (EVP_PKEY_get_raw_public_key(pkey, NULL, &len) != 1 ||
	    len != sizeof(pk->x))
		return (FIDO_ERR_INTERNAL);
	if (EVP_PKEY_get_raw_public_key(pkey, pk->x, &len) != 1 ||
	    len != sizeof(pk->x))
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

int
eddsa_verify_sig(const fido_blob_t *dgst, EVP_PKEY *pkey,
    const fido_blob_t *sig)
{
	EVP_MD_CTX	*mdctx = NULL;
	int		 ok = -1;

	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_ED25519) {
		fido_log_debug("%s: EVP_PKEY_base_id", __func__);
		goto fail;
	}

	/* EVP_DigestVerify needs ints */
	if (dgst->len > INT_MAX || sig->len > INT_MAX) {
		fido_log_debug("%s: dgst->len=%zu, sig->len=%zu", __func__,
		    dgst->len, sig->len);
		return (-1);
	}

	if ((mdctx = EVP_MD_CTX_new()) == NULL) {
		fido_log_debug("%s: EVP_MD_CTX_new", __func__);
		goto fail;
	}

	if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey) != 1) {
		fido_log_debug("%s: EVP_DigestVerifyInit", __func__);
		goto fail;
	}

	if (EVP_DigestVerify(mdctx, sig->ptr, sig->len, dgst->ptr,
	    dgst->len) != 1) {
		fido_log_debug("%s: EVP_DigestVerify", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_MD_CTX_free(mdctx);

	return (ok);
}

int
eddsa_pk_verify_sig(const fido_blob_t *dgst, const eddsa_pk_t *pk,
    const fido_blob_t *sig)
{
	EVP_PKEY	*pkey;
	int		 ok = -1;

	if ((pkey = eddsa_pk_to_EVP_PKEY(pk)) == NULL ||
	    eddsa_verify_sig(dgst, pkey, sig) < 0) {
		fido_log_debug("%s: eddsa_verify_sig", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_PKEY_free(pkey);

	return (ok);
}

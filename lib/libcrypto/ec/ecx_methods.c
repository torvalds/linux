/*	$OpenBSD: ecx_methods.c,v 1.15 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
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

#include <string.h>

#include <openssl/cms.h>
#include <openssl/curve25519.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "curve25519_internal.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

/*
 * EVP PKEY and PKEY ASN.1 methods Ed25519 and X25519.
 *
 * RFC 7748 - Elliptic Curves for Security.
 * RFC 8032 - Edwards-Curve Digital Signature Algorithm (EdDSA).
 */

#define ED25519_BITS		253
#define ED25519_SECURITY_BITS	128
#define ED25519_SIG_SIZE	64

#define X25519_BITS		253
#define X25519_SECURITY_BITS	128

static int
ecx_key_len(int nid)
{
	switch (nid) {
	case NID_ED25519:
		return ED25519_KEYLEN;
	case NID_X25519:
		return X25519_KEYLEN;
	}

	return 0;
}

static struct ecx_key_st *
ecx_key_new(int nid)
{
	struct ecx_key_st *ecx_key;
	int key_len;

	if ((key_len = ecx_key_len(nid)) == 0)
		return NULL;

	if ((ecx_key = calloc(1, sizeof(*ecx_key))) == NULL)
		return NULL;

	ecx_key->nid = nid;
	ecx_key->key_len = key_len;

	return ecx_key;
}

static void
ecx_key_clear(struct ecx_key_st *ecx_key)
{
	freezero(ecx_key->priv_key, ecx_key->priv_key_len);
	ecx_key->priv_key = NULL;
	ecx_key->priv_key_len = 0;

	freezero(ecx_key->pub_key, ecx_key->pub_key_len);
	ecx_key->pub_key = NULL;
	ecx_key->pub_key_len = 0;
}

static void
ecx_key_free(struct ecx_key_st *ecx_key)
{
	if (ecx_key == NULL)
		return;

	ecx_key_clear(ecx_key);

	freezero(ecx_key, sizeof(*ecx_key));
}

static int
ecx_key_generate(struct ecx_key_st *ecx_key)
{
	uint8_t *pub_key = NULL, *priv_key = NULL;
	int ret = 0;

	ecx_key_clear(ecx_key);

	if ((pub_key = calloc(1, ecx_key->key_len)) == NULL)
		goto err;
	if ((priv_key = calloc(1, ecx_key->key_len)) == NULL)
		goto err;

	switch (ecx_key->nid) {
	case NID_ED25519:
		ED25519_keypair(pub_key, priv_key);
		break;
	case NID_X25519:
		X25519_keypair(pub_key, priv_key);
		break;
	default:
		goto err;
	}

	ecx_key->priv_key = priv_key;
	ecx_key->priv_key_len = ecx_key->key_len;
	priv_key = NULL;

	ecx_key->pub_key = pub_key;
	ecx_key->pub_key_len = ecx_key->key_len;
	pub_key = NULL;

	ret = 1;

 err:
	freezero(pub_key, ecx_key->key_len);
	freezero(priv_key, ecx_key->key_len);

	return ret;
}

static int
ecx_key_set_priv(struct ecx_key_st *ecx_key, const uint8_t *priv_key,
    size_t priv_key_len)
{
	uint8_t *pub_key = NULL;
	CBS cbs;

	ecx_key_clear(ecx_key);

	if (priv_key_len != ecx_key->key_len)
		goto err;

	if ((pub_key = calloc(1, ecx_key->key_len)) == NULL)
		goto err;

	switch (ecx_key->nid) {
	case NID_ED25519:
		ED25519_public_from_private(pub_key, priv_key);
		break;
	case NID_X25519:
		X25519_public_from_private(pub_key, priv_key);
		break;
	default:
		goto err;
	}

	CBS_init(&cbs, priv_key, priv_key_len);
	if (!CBS_stow(&cbs, &ecx_key->priv_key, &ecx_key->priv_key_len))
		goto err;

	ecx_key->pub_key = pub_key;
	ecx_key->pub_key_len = ecx_key->key_len;
	pub_key = NULL;

 err:
	freezero(pub_key, ecx_key->key_len);

	return 1;
}

static int
ecx_key_set_pub(struct ecx_key_st *ecx_key, const uint8_t *pub_key,
    size_t pub_key_len)
{
	CBS cbs;

	ecx_key_clear(ecx_key);

	if (pub_key_len != ecx_key->key_len)
		return 0;

	CBS_init(&cbs, pub_key, pub_key_len);
	if (!CBS_stow(&cbs, &ecx_key->pub_key, &ecx_key->pub_key_len))
		return 0;

	return 1;
}

static int
ecx_pub_decode(EVP_PKEY *pkey, X509_PUBKEY *xpubkey)
{
	struct ecx_key_st *ecx_key = NULL;
	X509_ALGOR *algor;
	int algor_type;
	const uint8_t *param;
	int param_len;
	int ret = 0;

	if (!X509_PUBKEY_get0_param(NULL, &param, &param_len, &algor, xpubkey))
		goto err;

	/* Ensure that parameters have not been specified in the encoding. */
	if (algor != NULL) {
		X509_ALGOR_get0(NULL, &algor_type, NULL, algor);
		if (algor_type != V_ASN1_UNDEF) {
			ECerror(EC_R_INVALID_ENCODING);
			goto err;
		}
	}

	if (param == NULL || param_len != ecx_key_len(pkey->ameth->pkey_id)) {
		ECerror(EC_R_INVALID_ENCODING);
		goto err;
	}

	if ((ecx_key = ecx_key_new(pkey->ameth->pkey_id)) == NULL)
		goto err;
	if (!ecx_key_set_pub(ecx_key, param, param_len))
		goto err;
	if (!EVP_PKEY_assign(pkey, pkey->ameth->pkey_id, ecx_key))
		goto err;
	ecx_key = NULL;

	ret = 1;

 err:
	ecx_key_free(ecx_key);

	return ret;
}

static int
ecx_pub_encode(X509_PUBKEY *xpubkey, const EVP_PKEY *pkey)
{
	const struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	uint8_t *pub_key = NULL;
	size_t pub_key_len = 0;
	ASN1_OBJECT *aobj;
	CBS cbs;
	int ret = 0;

	if (ecx_key == NULL) {
		ECerror(EC_R_INVALID_KEY);
		goto err;
	}

	if (ecx_key->pub_key_len != ecx_key->key_len)
		goto err;

	if ((aobj = OBJ_nid2obj(pkey->ameth->pkey_id)) == NULL)
		goto err;

	CBS_init(&cbs, ecx_key->pub_key, ecx_key->pub_key_len);
	if (!CBS_stow(&cbs, &pub_key, &pub_key_len))
		goto err;

	if (!X509_PUBKEY_set0_param(xpubkey, aobj, V_ASN1_UNDEF, NULL,
	    pub_key, pub_key_len))
		goto err;

	pub_key = NULL;
	pub_key_len = 0;

	ret = 1;

 err:
	free(pub_key);

	return ret;
}

static int
ecx_pub_cmp(const EVP_PKEY *pkey1, const EVP_PKEY *pkey2)
{
	if (pkey1->pkey.ecx == NULL || pkey1->pkey.ecx->pub_key == NULL)
		return -2;
	if (pkey2->pkey.ecx == NULL || pkey2->pkey.ecx->pub_key == NULL)
		return -2;
	if (pkey1->pkey.ecx->pub_key_len != pkey2->pkey.ecx->pub_key_len)
		return -2;

	return timingsafe_memcmp(pkey1->pkey.ecx->pub_key, pkey2->pkey.ecx->pub_key,
	    pkey1->pkey.ecx->pub_key_len) == 0;
}

/* Reimplementation of ASN1_buf_print() that adds a secondary indent of 4. */
static int
ecx_buf_print(BIO *bio, const uint8_t *buf, size_t buf_len, int indent)
{
	uint8_t u8;
	size_t octets = 0;
	const char *sep = ":", *nl = "";
	CBS cbs;

	if (indent > 60)
		indent = 60;
	indent += 4;
	if (indent < 0)
		indent = 0;

	CBS_init(&cbs, buf, buf_len);
	while (CBS_len(&cbs) > 0) {
		if (!CBS_get_u8(&cbs, &u8))
			return 0;
		if (octets++ % 15 == 0) {
			if (BIO_printf(bio, "%s%*s", nl, indent, "") < 0)
				return 0;
			nl = "\n";
		}
		if (CBS_len(&cbs) == 0)
			sep = "";
		if (BIO_printf(bio, "%02x%s", u8, sep) <= 0)
			return 0;
	}

	if (BIO_printf(bio, "\n") <= 0)
		return 0;

	return 1;
}

static int
ecx_pub_print(BIO *bio, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	const char *name;

	if ((name = OBJ_nid2ln(pkey->ameth->pkey_id)) == NULL)
		return 0;

	if (ecx_key == NULL || ecx_key->pub_key == NULL)
		return BIO_printf(bio, "%*s<INVALID PUBLIC KEY>\n",
		    indent, "") > 0;

	if (BIO_printf(bio, "%*s%s Public-Key:\n", indent, "", name) <= 0)
		return 0;
	if (BIO_printf(bio, "%*spub:\n", indent, "") <= 0)
		return 0;
	if (!ecx_buf_print(bio, ecx_key->pub_key, ecx_key->pub_key_len, indent))
		return 0;

	return 1;
}

static int
ecx_priv_decode(EVP_PKEY *pkey, const PKCS8_PRIV_KEY_INFO *p8pki)
{
	struct ecx_key_st *ecx_key = NULL;
	ASN1_OCTET_STRING *aos = NULL;
	const X509_ALGOR *algor;
	int algor_type;
	const uint8_t *param;
	int param_len;
	int ret = 0;

	if (!PKCS8_pkey_get0(NULL, &param, &param_len, &algor, p8pki))
		goto err;
	if ((aos = d2i_ASN1_OCTET_STRING(NULL, &param, param_len)) == NULL)
		goto err;

	/* Ensure that parameters have not been specified in the encoding. */
	if (algor != NULL) {
		X509_ALGOR_get0(NULL, &algor_type, NULL, algor);
		if (algor_type != V_ASN1_UNDEF) {
			ECerror(EC_R_INVALID_ENCODING);
			goto err;
		}
	}

	if (ASN1_STRING_get0_data(aos) == NULL ||
	    ASN1_STRING_length(aos) != ecx_key_len(pkey->ameth->pkey_id)) {
		ECerror(EC_R_INVALID_ENCODING);
		goto err;
	}

	if ((ecx_key = ecx_key_new(pkey->ameth->pkey_id)) == NULL)
		goto err;
	if (!ecx_key_set_priv(ecx_key, ASN1_STRING_get0_data(aos),
	    ASN1_STRING_length(aos)))
		goto err;
	if (!EVP_PKEY_assign(pkey, pkey->ameth->pkey_id, ecx_key))
		goto err;
	ecx_key = NULL;

	ret = 1;

 err:
	ASN1_OCTET_STRING_free(aos);
	ecx_key_free(ecx_key);

	return ret;
}

static int
ecx_priv_encode(PKCS8_PRIV_KEY_INFO *p8pki, const EVP_PKEY *pkey)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	ASN1_OCTET_STRING *aos = NULL;
	ASN1_OBJECT *aobj;
	uint8_t *der = NULL;
	int der_len = 0;
	int ret = 0;

	if (ecx_key == NULL || ecx_key->priv_key == NULL) {
		ECerror(EC_R_INVALID_PRIVATE_KEY);
		goto err;
	}

	if ((aobj = OBJ_nid2obj(pkey->ameth->pkey_id)) == NULL)
		goto err;

	if ((aos = ASN1_OCTET_STRING_new()) == NULL)
		goto err;
	if (!ASN1_OCTET_STRING_set(aos, ecx_key->priv_key,
	    ecx_key->priv_key_len))
		goto err;
	if ((der_len = i2d_ASN1_OCTET_STRING(aos, &der)) < 0)
		goto err;
	if (!PKCS8_pkey_set0(p8pki, aobj, 0, V_ASN1_UNDEF, NULL, der, der_len))
		goto err;

	der = NULL;
	der_len = 0;

	ret = 1;

 err:
	freezero(der, der_len);
	ASN1_OCTET_STRING_free(aos);

	return ret;
}

static int
ecx_priv_print(BIO *bio, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	const char *name;

	if ((name = OBJ_nid2ln(pkey->ameth->pkey_id)) == NULL)
		return 0;

	if (ecx_key == NULL || ecx_key->priv_key == NULL)
		return BIO_printf(bio, "%*s<INVALID PRIVATE KEY>\n",
		    indent, "") > 0;

	if (BIO_printf(bio, "%*s%s Private-Key:\n", indent, "", name) <= 0)
		return 0;
	if (BIO_printf(bio, "%*spriv:\n", indent, "") <= 0)
		return 0;
	if (!ecx_buf_print(bio, ecx_key->priv_key, ecx_key->priv_key_len, indent))
		return 0;
	if (BIO_printf(bio, "%*spub:\n", indent, "") <= 0)
		return 0;
	if (!ecx_buf_print(bio, ecx_key->pub_key, ecx_key->pub_key_len, indent))
		return 0;

	return 1;
}

static int
ecx_size(const EVP_PKEY *pkey)
{
	return ecx_key_len(pkey->ameth->pkey_id);
}

static int
ecx_sig_size(const EVP_PKEY *pkey)
{
	switch (pkey->ameth->pkey_id) {
	case EVP_PKEY_ED25519:
		return ED25519_SIG_SIZE;
	}
	return 0;
}

static int
ecx_bits(const EVP_PKEY *pkey)
{
	switch (pkey->ameth->pkey_id) {
	case EVP_PKEY_ED25519:
		return ED25519_BITS;
	case EVP_PKEY_X25519:
		return X25519_BITS;
	}
	return 0;
}

static int
ecx_security_bits(const EVP_PKEY *pkey)
{
	switch (pkey->ameth->pkey_id) {
	case EVP_PKEY_ED25519:
		return ED25519_SECURITY_BITS;
	case EVP_PKEY_X25519:
		return X25519_SECURITY_BITS;
	}
	return 0;
}

static int
ecx_signature_info(const X509_ALGOR *algor, int *md_nid, int *pkey_nid,
    int *security_bits, uint32_t *flags)
{
	const ASN1_OBJECT *aobj;

	X509_ALGOR_get0(&aobj, NULL, NULL, algor);
	if (OBJ_obj2nid(aobj) != EVP_PKEY_ED25519)
		return 0;

	*md_nid = NID_undef;
	*pkey_nid = NID_ED25519;
	*security_bits = ED25519_SECURITY_BITS;
	*flags = X509_SIG_INFO_TLS | X509_SIG_INFO_VALID;

	return 1;
}

static int
ecx_param_cmp(const EVP_PKEY *pkey1, const EVP_PKEY *pkey2)
{
	/* No parameters, so always equivalent. */
	return 1;
}

static void
ecx_free(EVP_PKEY *pkey)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;

	ecx_key_free(ecx_key);
}

static int
ecx_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
	/* Not supported. */
	return -2;
}

#ifndef OPENSSL_NO_CMS
static int
ecx_cms_sign_or_verify(EVP_PKEY *pkey, long verify, CMS_SignerInfo *si)
{
	X509_ALGOR *digestAlgorithm, *signatureAlgorithm;

	if (verify != 0 && verify != 1)
		return -1;

	/* Check that we have an Ed25519 public key. */
	if (EVP_PKEY_id(pkey) != NID_ED25519)
		return -1;

	CMS_SignerInfo_get0_algs(si, NULL, NULL, &digestAlgorithm,
	    &signatureAlgorithm);

	/* RFC 8419, section 2.3: digestAlgorithm MUST be SHA-512. */
	if (digestAlgorithm == NULL)
		return -1;
	if (OBJ_obj2nid(digestAlgorithm->algorithm) != NID_sha512)
		return -1;

	/*
	 * RFC 8419, section 2.4: signatureAlgorithm MUST be Ed25519, and the
	 * parameters MUST be absent. For verification check that this is the
	 * case, for signing set the signatureAlgorithm accordingly.
	 */
	if (verify) {
		const ASN1_OBJECT *obj;
		int param_type;

		if (signatureAlgorithm == NULL)
			return -1;

		X509_ALGOR_get0(&obj, &param_type, NULL, signatureAlgorithm);
		if (OBJ_obj2nid(obj) != NID_ED25519)
			return -1;
		if (param_type != V_ASN1_UNDEF)
			return -1;

		return 1;
	}

	if (!X509_ALGOR_set0_by_nid(signatureAlgorithm, NID_ED25519,
	    V_ASN1_UNDEF, NULL))
		return -1;

	return 1;
}
#endif

static int
ecx_sign_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
	switch (op) {
#ifndef OPENSSL_NO_CMS
	case ASN1_PKEY_CTRL_CMS_SIGN:
		return ecx_cms_sign_or_verify(pkey, arg1, arg2);
#endif
	case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
		/* PureEdDSA does its own hashing. */
		*(int *)arg2 = NID_undef;
		return 2;
	}
	return -2;
}

static int
ecx_set_priv_key(EVP_PKEY *pkey, const uint8_t *priv, size_t len)
{
	struct ecx_key_st *ecx_key = NULL;
	int ret = 0;

	if (priv == NULL || len != ecx_key_len(pkey->ameth->pkey_id)) {
		ECerror(EC_R_INVALID_ENCODING);
		goto err;
	}

	if ((ecx_key = ecx_key_new(pkey->ameth->pkey_id)) == NULL)
		goto err;
	if (!ecx_key_set_priv(ecx_key, priv, len))
		goto err;
	if (!EVP_PKEY_assign(pkey, pkey->ameth->pkey_id, ecx_key))
		goto err;
	ecx_key = NULL;

	ret = 1;

 err:
	ecx_key_free(ecx_key);

	return ret;
}

static int
ecx_set_pub_key(EVP_PKEY *pkey, const uint8_t *pub, size_t len)
{
	struct ecx_key_st *ecx_key = NULL;
	int ret = 0;

	if (pub == NULL || len != ecx_key_len(pkey->ameth->pkey_id)) {
		ECerror(EC_R_INVALID_ENCODING);
		goto err;
	}

	if ((ecx_key = ecx_key_new(pkey->ameth->pkey_id)) == NULL)
		goto err;
	if (!ecx_key_set_pub(ecx_key, pub, len))
		goto err;
	if (!EVP_PKEY_assign(pkey, pkey->ameth->pkey_id, ecx_key))
		goto err;
	ecx_key = NULL;

	ret = 1;

 err:
	ecx_key_free(ecx_key);

	return ret;
}

static int
ecx_get_priv_key(const EVP_PKEY *pkey, unsigned char *out_priv, size_t *out_len)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	CBS cbs;

	if (out_priv == NULL) {
		*out_len = ecx_key_len(pkey->ameth->pkey_id);
		return 1;
	}

	if (ecx_key == NULL || ecx_key->priv_key == NULL)
		return 0;

	CBS_init(&cbs, ecx_key->priv_key, ecx_key->priv_key_len);
	if (!CBS_write_bytes(&cbs, out_priv, *out_len, out_len))
		return 0;

	return 1;
}

static int
ecx_get_pub_key(const EVP_PKEY *pkey, unsigned char *out_pub, size_t *out_len)
{
	struct ecx_key_st *ecx_key = pkey->pkey.ecx;
	CBS cbs;

	if (out_pub == NULL) {
		*out_len = ecx_key_len(pkey->ameth->pkey_id);
		return 1;
	}

	if (ecx_key == NULL || ecx_key->pub_key == NULL)
		return 0;

	CBS_init(&cbs, ecx_key->pub_key, ecx_key->pub_key_len);
	if (!CBS_write_bytes(&cbs, out_pub, *out_len, out_len))
		return 0;

	return 1;
}

static int
pkey_ecx_keygen(EVP_PKEY_CTX *pkey_ctx, EVP_PKEY *pkey)
{
	struct ecx_key_st *ecx_key = NULL;
	int ret = 0;

	if ((ecx_key = ecx_key_new(pkey_ctx->pmeth->pkey_id)) == NULL)
		goto err;
	if (!ecx_key_generate(ecx_key))
		goto err;
	if (!EVP_PKEY_assign(pkey, pkey_ctx->pmeth->pkey_id, ecx_key))
		goto err;
	ecx_key = NULL;

	ret = 1;

 err:
	ecx_key_free(ecx_key);

	return ret;
}

static int
pkey_ecx_derive(EVP_PKEY_CTX *pkey_ctx, unsigned char *out_key,
    size_t *out_key_len)
{
	struct ecx_key_st *ecx_key, *ecx_peer_key;

	if (pkey_ctx->pkey == NULL || pkey_ctx->peerkey == NULL) {
		ECerror(EC_R_KEYS_NOT_SET);
		return 0;
	}

	if ((ecx_key = pkey_ctx->pkey->pkey.ecx) == NULL) {
		ECerror(EC_R_INVALID_PRIVATE_KEY);
		return 0;
	}
	if (ecx_key->priv_key == NULL) {
		ECerror(EC_R_INVALID_PRIVATE_KEY);
		return 0;
	}

	if ((ecx_peer_key = pkey_ctx->peerkey->pkey.ecx) == NULL) {
		ECerror(EC_R_INVALID_PEER_KEY);
		return 0;
	}

	if (out_key != NULL) {
		if (!X25519(out_key, ecx_key->priv_key, ecx_peer_key->pub_key))
			return 0;
	}

	*out_key_len = X25519_KEYLEN;

	return 1;
}

static int
pkey_ecx_ctrl(EVP_PKEY_CTX *pkey_ctx, int op, int arg1, void *arg2)
{
	if (op == EVP_PKEY_CTRL_PEER_KEY)
		return 1;

	return -2;
}

static int
ecx_item_verify(EVP_MD_CTX *md_ctx, const ASN1_ITEM *it, void *asn,
   X509_ALGOR *algor, ASN1_BIT_STRING *abs, EVP_PKEY *pkey)
{
	const ASN1_OBJECT *aobj;
	int nid, param_type;

	X509_ALGOR_get0(&aobj, &param_type, NULL, algor);

	nid = OBJ_obj2nid(aobj);

	if (nid != NID_ED25519 || param_type != V_ASN1_UNDEF) {
		ECerror(EC_R_INVALID_ENCODING);
		return -1;
	}

	if (!EVP_DigestVerifyInit(md_ctx, NULL, NULL, NULL, pkey))
		return -1;

	return 2;
}

static int
ecx_item_sign(EVP_MD_CTX *md_ctx, const ASN1_ITEM *it, void *asn,
    X509_ALGOR *algor1, X509_ALGOR *algor2, ASN1_BIT_STRING *abs)
{
	if (!X509_ALGOR_set0_by_nid(algor1, NID_ED25519, V_ASN1_UNDEF, NULL))
		return 0;

	if (algor2 != NULL) {
		if (!X509_ALGOR_set0_by_nid(algor2, NID_ED25519, V_ASN1_UNDEF,
		    NULL))
			return 0;
	}

	/* Tell ASN1_item_sign_ctx() that identifiers are set and it needs to sign. */
	return 3;
}

static int
pkey_ecx_digestsign(EVP_MD_CTX *md_ctx, unsigned char *out_sig,
    size_t *out_sig_len, const unsigned char *message, size_t message_len)
{
	struct ecx_key_st *ecx_key;
	EVP_PKEY_CTX *pkey_ctx;

	pkey_ctx = EVP_MD_CTX_pkey_ctx(md_ctx);
	ecx_key = pkey_ctx->pkey->pkey.ecx;

	if (out_sig == NULL) {
		*out_sig_len = ecx_sig_size(pkey_ctx->pkey);
		return 1;
	}
	if (*out_sig_len < ecx_sig_size(pkey_ctx->pkey)) {
		ECerror(EC_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if (ecx_key == NULL)
		return 0;
	if (ecx_key->priv_key == NULL || ecx_key->pub_key == NULL)
		return 0;

	if (!ED25519_sign(out_sig, message, message_len, ecx_key->pub_key,
	    ecx_key->priv_key))
		return 0;

	*out_sig_len = ecx_sig_size(pkey_ctx->pkey);

	return 1;
}

static int
pkey_ecx_digestverify(EVP_MD_CTX *md_ctx, const unsigned char *sig,
   size_t sig_len, const unsigned char *message, size_t message_len)
{
	struct ecx_key_st *ecx_key;
	EVP_PKEY_CTX *pkey_ctx;

	pkey_ctx = EVP_MD_CTX_pkey_ctx(md_ctx);
	ecx_key = pkey_ctx->pkey->pkey.ecx;

	if (ecx_key == NULL || ecx_key->pub_key == NULL)
		return -1;
	if (sig_len != ecx_sig_size(pkey_ctx->pkey))
		return -1;

	return ED25519_verify(message, message_len, sig, ecx_key->pub_key);
}

static int
pkey_ecx_ed_ctrl(EVP_PKEY_CTX *pkey_ctx, int op, int arg1, void *arg2)
{
	switch (op) {
	case EVP_PKEY_CTRL_MD:
		/* PureEdDSA does its own hashing. */
		if (arg2 != NULL && (const EVP_MD *)arg2 != EVP_md_null()) {
			ECerror(EC_R_INVALID_DIGEST_TYPE);
			return 0;
		}
		return 1;

#ifndef OPENSSL_NO_CMS
	case EVP_PKEY_CTRL_CMS_SIGN:
#endif
	case EVP_PKEY_CTRL_DIGESTINIT:
		return 1;
	}
	return -2;
}

const EVP_PKEY_ASN1_METHOD x25519_asn1_meth = {
	.base_method = &x25519_asn1_meth,
	.pkey_id = EVP_PKEY_X25519,
	.pkey_flags = 0,
	.pem_str = "X25519",
	.info = "OpenSSL X25519 algorithm",

	.pub_decode = ecx_pub_decode,
	.pub_encode = ecx_pub_encode,
	.pub_cmp = ecx_pub_cmp,
	.pub_print = ecx_pub_print,

	.priv_decode = ecx_priv_decode,
	.priv_encode = ecx_priv_encode,
	.priv_print = ecx_priv_print,

	.pkey_size = ecx_size,
	.pkey_bits = ecx_bits,
	.pkey_security_bits = ecx_security_bits,

	.param_cmp = ecx_param_cmp,

	.pkey_free = ecx_free,
	.pkey_ctrl = ecx_ctrl,

	.set_priv_key = ecx_set_priv_key,
	.set_pub_key = ecx_set_pub_key,
	.get_priv_key = ecx_get_priv_key,
	.get_pub_key = ecx_get_pub_key,
};

const EVP_PKEY_METHOD x25519_pkey_meth = {
	.pkey_id = EVP_PKEY_X25519,
	.keygen = pkey_ecx_keygen,
	.derive = pkey_ecx_derive,
	.ctrl = pkey_ecx_ctrl,
};

const EVP_PKEY_ASN1_METHOD ed25519_asn1_meth = {
	.base_method = &ed25519_asn1_meth,
	.pkey_id = EVP_PKEY_ED25519,
	.pkey_flags = 0,
	.pem_str = "ED25519",
	.info = "OpenSSL ED25519 algorithm",

	.pub_decode = ecx_pub_decode,
	.pub_encode = ecx_pub_encode,
	.pub_cmp = ecx_pub_cmp,
	.pub_print = ecx_pub_print,

	.priv_decode = ecx_priv_decode,
	.priv_encode = ecx_priv_encode,
	.priv_print = ecx_priv_print,

	.pkey_size = ecx_sig_size,
	.pkey_bits = ecx_bits,
	.pkey_security_bits = ecx_security_bits,

	.signature_info = ecx_signature_info,

	.param_cmp = ecx_param_cmp,

	.pkey_free = ecx_free,
	.pkey_ctrl = ecx_sign_ctrl,

	.item_verify = ecx_item_verify,
	.item_sign = ecx_item_sign,

	.set_priv_key = ecx_set_priv_key,
	.set_pub_key = ecx_set_pub_key,
	.get_priv_key = ecx_get_priv_key,
	.get_pub_key = ecx_get_pub_key,
};

const EVP_PKEY_METHOD ed25519_pkey_meth = {
	.pkey_id = EVP_PKEY_ED25519,
	.flags = EVP_PKEY_FLAG_SIGCTX_CUSTOM,
	.keygen = pkey_ecx_keygen,
	.ctrl = pkey_ecx_ed_ctrl,
	.digestsign = pkey_ecx_digestsign,
	.digestverify = pkey_ecx_digestverify,
};

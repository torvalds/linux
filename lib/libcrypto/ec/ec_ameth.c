/* $OpenBSD: ec_ameth.c,v 1.74 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <stddef.h>
#include <stdlib.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/cms.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pkcs7.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "bn_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

#ifndef OPENSSL_NO_CMS
static int ecdh_cms_decrypt(CMS_RecipientInfo *ri);
static int ecdh_cms_encrypt(CMS_RecipientInfo *ri);
#endif

static void
eckey_param_free(int ptype, void *pval)
{
	if (pval == NULL)
		return;
	if (ptype == V_ASN1_OBJECT)
		ASN1_OBJECT_free(pval);		/* XXX - really necessary? */
	else
		ASN1_STRING_free(pval);
}

static int
eckey_get_curve_name(const EC_KEY *eckey, int *nid)
{
	const EC_GROUP *group;

	*nid = NID_undef;

	if ((group = EC_KEY_get0_group(eckey)) == NULL) {
		ECerror(EC_R_MISSING_PARAMETERS);
		return 0;
	}
	if ((EC_GROUP_get_asn1_flag(group) & OPENSSL_EC_NAMED_CURVE) != 0)
		*nid = EC_GROUP_get_curve_name(group);

	return 1;
}

static int
eckey_to_explicit_params(EC_KEY *eckey, void **out_val)
{
	ASN1_STRING *astr = NULL;
	unsigned char *params = NULL;
	int params_len = 0;
	int ret = 0;

	*out_val = NULL;

	if ((params_len = i2d_ECParameters(eckey, &params)) <= 0) {
		ECerror(ERR_R_EC_LIB);
		params_len = 0;
		goto err;
	}

	if ((astr = ASN1_STRING_new()) == NULL)
		goto err;
	ASN1_STRING_set0(astr, params, params_len);
	params = NULL;
	params_len = 0;

	*out_val = astr;
	astr = NULL;

	ret = 1;

 err:
	freezero(params, params_len);
	ASN1_STRING_free(astr);

	return ret;
}

static int
eckey_from_explicit_params(const ASN1_STRING *astr, EC_KEY **out_eckey)
{
	const unsigned char *params = astr->data;
	int params_len = astr->length;

	EC_KEY_free(*out_eckey);
	if ((*out_eckey = d2i_ECParameters(NULL, &params, params_len)) == NULL) {
		ECerror(EC_R_DECODE_ERROR);
		return 0;
	}

	return 1;
}

static int
eckey_to_object(const EC_KEY *eckey, void **out_val)
{
	int nid = NID_undef;

	*out_val = NULL;

	if (!eckey_get_curve_name(eckey, &nid))
		return 0;
	if ((*out_val = OBJ_nid2obj(nid)) == NULL)
		return 0;

	return 1;
}

static int
eckey_from_object(const ASN1_OBJECT *aobj, EC_KEY **out_eckey)
{
	int nid;

	EC_KEY_free(*out_eckey);
	*out_eckey = NULL;

	if ((nid = OBJ_obj2nid(aobj)) == NID_undef)
		return 0;
	if ((*out_eckey = EC_KEY_new_by_curve_name(nid)) == NULL)
		return 0;

	return 1;
}

static int
eckey_to_params(EC_KEY *eckey, int *out_type, void **out_val)
{
	int nid;

	*out_type = NID_undef;
	*out_val = NULL;

	if (!eckey_get_curve_name(eckey, &nid))
		return 0;

	if (nid == NID_undef) {
		*out_type = V_ASN1_SEQUENCE;
		return eckey_to_explicit_params(eckey, out_val);
	} else {
		*out_type = V_ASN1_OBJECT;
		return eckey_to_object(eckey, out_val);
	}
}

static int
eckey_from_params(int ptype, const void *pval, EC_KEY **out_eckey)
{
	EC_KEY_free(*out_eckey);
	*out_eckey = NULL;

	if (ptype == V_ASN1_SEQUENCE)
		return eckey_from_explicit_params(pval, out_eckey);
	if (ptype == V_ASN1_OBJECT)
		return eckey_from_object(pval, out_eckey);

	ECerror(EC_R_DECODE_ERROR);
	return 0;
}

static int
eckey_pub_encode(X509_PUBKEY *pk, const EVP_PKEY *pkey)
{
	EC_KEY *eckey = pkey->pkey.ec;
	int ptype = V_ASN1_UNDEF;
	void *pval = NULL;
	ASN1_OBJECT *aobj;
	unsigned char *key = NULL;
	int key_len = 0;
	int ret = 0;

	if (!eckey_to_params(eckey, &ptype, &pval)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	if ((key_len = i2o_ECPublicKey(eckey, &key)) <= 0) {
		key_len = 0;
		goto err;
	}
	if ((aobj = OBJ_nid2obj(EVP_PKEY_EC)) == NULL)
		goto err;
	if (!X509_PUBKEY_set0_param(pk, aobj, ptype, pval, key, key_len))
		goto err;
	pval = NULL;
	key = NULL;
	key_len = 0;

	ret = 1;

 err:
	eckey_param_free(ptype, pval);
	freezero(key, key_len);

	return ret;
}

static int
eckey_pub_decode(EVP_PKEY *pkey, X509_PUBKEY *pubkey)
{
	const unsigned char *p = NULL;
	const void *pval;
	int ptype, pklen;
	EC_KEY *eckey = NULL;
	X509_ALGOR *palg;
	int ret = 0;

	if (!X509_PUBKEY_get0_param(NULL, &p, &pklen, &palg, pubkey))
		goto err;

	X509_ALGOR_get0(NULL, &ptype, &pval, palg);
	if (!eckey_from_params(ptype, pval, &eckey))
		goto err;

	if (!o2i_ECPublicKey(&eckey, &p, pklen)) {
		ECerror(EC_R_DECODE_ERROR);
		goto err;
	}
	if (!EVP_PKEY_assign_EC_KEY(pkey, eckey))
		goto err;
	eckey = NULL;

	ret = 1;

 err:
	EC_KEY_free(eckey);

	return ret;
}

static int
eckey_pub_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	const EC_GROUP *group = EC_KEY_get0_group(b->pkey.ec);
	const EC_POINT *pa = EC_KEY_get0_public_key(a->pkey.ec);
	const EC_POINT *pb = EC_KEY_get0_public_key(b->pkey.ec);
	int r;

	r = EC_POINT_cmp(group, pa, pb, NULL);
	if (r == 0)
		return 1;
	if (r == 1)
		return 0;
	return -2;
}

int
eckey_compute_pubkey(EC_KEY *eckey)
{
	const BIGNUM *priv_key;
	const EC_GROUP *group;
	EC_POINT *pub_key = NULL;
	int ret = 0;

	if ((priv_key = EC_KEY_get0_private_key(eckey)) == NULL)
		goto err;
	if ((group = EC_KEY_get0_group(eckey)) == NULL)
		goto err;
	if ((pub_key = EC_POINT_new(group)) == NULL)
		goto err;
	if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, NULL))
		goto err;
	if (!EC_KEY_set_public_key(eckey, pub_key))
		goto err;

	ret = 1;

 err:
	EC_POINT_free(pub_key);

	return ret;
}

static int
eckey_priv_decode(EVP_PKEY *pkey, const PKCS8_PRIV_KEY_INFO *p8)
{
	const unsigned char *priv = NULL;
	int priv_len;
	const void *pval;
	int ptype;
	EC_KEY *eckey = NULL;
	const X509_ALGOR *palg;
	int ret = 0;

	if (!PKCS8_pkey_get0(NULL, &priv, &priv_len, &palg, p8))
		goto err;

	X509_ALGOR_get0(NULL, &ptype, &pval, palg);
	if (!eckey_from_params(ptype, pval, &eckey))
		goto err;

	/* Decode private key into eckey. */
	if (d2i_ECPrivateKey(&eckey, &priv, priv_len) == NULL) {
		ECerror(EC_R_DECODE_ERROR);
		goto err;
	}
	/* If public key was missing from SEC1 key, compute it. */
	if (EC_KEY_get0_public_key(eckey) == NULL) {
		if (!eckey_compute_pubkey(eckey))
			goto err;
	}

	if (!EVP_PKEY_assign_EC_KEY(pkey, eckey))
		goto err;
	eckey = NULL;

	ret = 1;

 err:
	EC_KEY_free(eckey);

	return ret;
}

static int
eckey_priv_encode(PKCS8_PRIV_KEY_INFO *p8, const EVP_PKEY *pkey)
{
	EC_KEY *eckey = pkey->pkey.ec;
	void *pval = NULL;
	int ptype = V_ASN1_UNDEF;
	ASN1_OBJECT *aobj;
	unsigned char *key = NULL;
	int key_len = 0;
	unsigned int flags;
	int ret = 0;

	flags = EC_KEY_get_enc_flags(eckey);

	if (!eckey_to_params(eckey, &ptype, &pval)) {
		ECerror(EC_R_DECODE_ERROR);
		goto err;
	}

	/* PKCS#11 12.11: don't include parameters in the SEC1 private key. */
	EC_KEY_set_enc_flags(eckey, flags | EC_PKEY_NO_PARAMETERS);

	if ((key_len = i2d_ECPrivateKey(eckey, &key)) <= 0) {
		ECerror(ERR_R_EC_LIB);
		key_len = 0;
		goto err;
	}
	if ((aobj = OBJ_nid2obj(NID_X9_62_id_ecPublicKey)) == NULL)
		goto err;
	if (!PKCS8_pkey_set0(p8, aobj, 0, ptype, pval, key, key_len))
		goto err;
	pval = NULL;
	key = NULL;
	key_len = 0;

	ret = 1;

 err:
	eckey_param_free(ptype, pval);
	freezero(key, key_len);

	EC_KEY_set_enc_flags(eckey, flags);

	return ret;
}

static int
ec_size(const EVP_PKEY *pkey)
{
	return ECDSA_size(pkey->pkey.ec);
}

static int
ec_bits(const EVP_PKEY *pkey)
{
	const EC_GROUP *group;

	if ((group = EC_KEY_get0_group(pkey->pkey.ec)) == NULL)
		return 0;

	return EC_GROUP_order_bits(group);
}

static int
ec_security_bits(const EVP_PKEY *pkey)
{
	int ecbits = ec_bits(pkey);

	if (ecbits >= 512)
		return 256;
	if (ecbits >= 384)
		return 192;
	if (ecbits >= 256)
		return 128;
	if (ecbits >= 224)
		return 112;
	if (ecbits >= 160)
		return 80;

	return ecbits / 2;
}

static int
ec_missing_parameters(const EVP_PKEY *pkey)
{
	if (EC_KEY_get0_group(pkey->pkey.ec) == NULL)
		return 1;
	return 0;
}

static int
ec_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from)
{
	return EC_KEY_set_group(to->pkey.ec, EC_KEY_get0_group(from->pkey.ec));
}

static int
ec_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
	const EC_GROUP *group_a = EC_KEY_get0_group(a->pkey.ec);
	const EC_GROUP *group_b = EC_KEY_get0_group(b->pkey.ec);

	if (EC_GROUP_cmp(group_a, group_b, NULL))
		return 0;
	else
		return 1;
}

static void
ec_free(EVP_PKEY *pkey)
{
	EC_KEY_free(pkey->pkey.ec);
}

static int
do_EC_KEY_print(BIO *bp, const EC_KEY *x, int off, int ktype)
{
	const char *ecstr;
	int ret = 0, reason = ERR_R_BIO_LIB;
	BIGNUM *pub_key = NULL;
	BN_CTX *ctx = NULL;
	const EC_GROUP *group;
	const EC_POINT *public_key;
	const BIGNUM *priv_key;

	if (x == NULL || (group = EC_KEY_get0_group(x)) == NULL) {
		reason = ERR_R_PASSED_NULL_PARAMETER;
		goto err;
	}
	ctx = BN_CTX_new();
	if (ctx == NULL) {
		reason = ERR_R_MALLOC_FAILURE;
		goto err;
	}
	if (ktype > 0) {
		public_key = EC_KEY_get0_public_key(x);
		if (public_key != NULL) {
			if ((pub_key = EC_POINT_point2bn(group, public_key,
			    EC_KEY_get_conv_form(x), NULL, ctx)) == NULL) {
				reason = ERR_R_EC_LIB;
				goto err;
			}
		}
	}
	if (ktype == 2) {
		priv_key = EC_KEY_get0_private_key(x);
	} else
		priv_key = NULL;

	if (ktype == 2)
		ecstr = "Private-Key";
	else if (ktype == 1)
		ecstr = "Public-Key";
	else
		ecstr = "ECDSA-Parameters";

	if (!BIO_indent(bp, off, 128))
		goto err;
	if (BIO_printf(bp, "%s: (%d bit)\n", ecstr,
	    EC_GROUP_order_bits(group)) <= 0)
		goto err;

	if (!bn_printf(bp, priv_key, off, "priv:"))
		goto err;
	if (!bn_printf(bp, pub_key, off, "pub: "))
		goto err;
	if (!ECPKParameters_print(bp, group, off))
		goto err;

	ret = 1;

 err:
	if (!ret)
		ECerror(reason);
	BN_free(pub_key);
	BN_CTX_free(ctx);

	return (ret);
}

static int
eckey_param_decode(EVP_PKEY *pkey, const unsigned char **param, int param_len)
{
	EC_KEY *eckey;
	int ret = 0;

	if ((eckey = d2i_ECParameters(NULL, param, param_len)) == NULL)
		goto err;
	if (!EVP_PKEY_assign_EC_KEY(pkey, eckey))
		goto err;
	eckey = NULL;

	ret = 1;

 err:
	EC_KEY_free(eckey);

	return ret;
}

static int
eckey_param_encode(const EVP_PKEY *pkey, unsigned char **param)
{
	return i2d_ECParameters(pkey->pkey.ec, param);
}

static int
eckey_param_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_EC_KEY_print(bp, pkey->pkey.ec, indent, 0);
}

static int
eckey_pub_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_EC_KEY_print(bp, pkey->pkey.ec, indent, 1);
}


static int
eckey_priv_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_EC_KEY_print(bp, pkey->pkey.ec, indent, 2);
}

static int
old_ec_priv_decode(EVP_PKEY *pkey, const unsigned char **priv, int priv_len)
{
	EC_KEY *eckey;
	int ret = 0;

	if ((eckey = d2i_ECPrivateKey(NULL, priv, priv_len)) == NULL)
		goto err;
	if (!EVP_PKEY_assign_EC_KEY(pkey, eckey))
		goto err;
	eckey = NULL;

	ret = 1;

 err:
	EC_KEY_free(eckey);

	return ret;
}

static int
old_ec_priv_encode(const EVP_PKEY *pkey, unsigned char **priv)
{
	return i2d_ECPrivateKey(pkey->pkey.ec, priv);
}

static int
ec_pkey_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
	switch (op) {
	case ASN1_PKEY_CTRL_PKCS7_SIGN:
		if (arg1 == 0) {
			int snid, hnid;
			X509_ALGOR *alg1, *alg2;
			PKCS7_SIGNER_INFO_get0_algs(arg2, NULL, &alg1, &alg2);
			if (alg1 == NULL || alg1->algorithm == NULL)
				return -1;
			hnid = OBJ_obj2nid(alg1->algorithm);
			if (hnid == NID_undef)
				return -1;
			if (!OBJ_find_sigid_by_algs(&snid, hnid, EVP_PKEY_id(pkey)))
				return -1;
			if (!X509_ALGOR_set0_by_nid(alg2, snid, V_ASN1_UNDEF,
			    NULL))
				return -1;
		}
		return 1;

#ifndef OPENSSL_NO_CMS
	case ASN1_PKEY_CTRL_CMS_SIGN:
		if (arg1 == 0) {
			X509_ALGOR *alg1, *alg2;
			int snid, hnid;

			CMS_SignerInfo_get0_algs(arg2, NULL, NULL, &alg1, &alg2);
			if (alg1 == NULL || alg1->algorithm == NULL)
				return -1;
			hnid = OBJ_obj2nid(alg1->algorithm);
			if (hnid == NID_undef)
				return -1;
			if (!OBJ_find_sigid_by_algs(&snid, hnid, EVP_PKEY_id(pkey)))
				return -1;
			if (!X509_ALGOR_set0_by_nid(alg2, snid, V_ASN1_UNDEF,
			    NULL))
				return -1;
		}
		return 1;

	case ASN1_PKEY_CTRL_CMS_ENVELOPE:
		if (arg1 == 0)
			return ecdh_cms_encrypt(arg2);
		else if (arg1 == 1)
			return ecdh_cms_decrypt(arg2);
		return -2;

	case ASN1_PKEY_CTRL_CMS_RI_TYPE:
		*(int *)arg2 = CMS_RECIPINFO_AGREE;
		return 1;
#endif

	case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
		*(int *) arg2 = NID_sha1;
		return 2;

	default:
		return -2;

	}

}

#ifndef OPENSSL_NO_CMS

static int
ecdh_cms_set_peerkey(EVP_PKEY_CTX *pctx, X509_ALGOR *alg,
    ASN1_BIT_STRING *pubkey)
{
	const ASN1_OBJECT *aoid;
	int atype;
	const void *aval;
	int rv = 0;
	EVP_PKEY *pkpeer = NULL;
	EC_KEY *ecpeer = NULL;
	const unsigned char *p;
	int plen;

	X509_ALGOR_get0(&aoid, &atype, &aval, alg);
	if (OBJ_obj2nid(aoid) != NID_X9_62_id_ecPublicKey)
		goto err;

	/* If absent parameters get group from main key */
	if (atype == V_ASN1_UNDEF || atype == V_ASN1_NULL) {
		const EC_GROUP *grp;
		EVP_PKEY *pk;

		pk = EVP_PKEY_CTX_get0_pkey(pctx);
		if (!pk)
			goto err;
		grp = EC_KEY_get0_group(pk->pkey.ec);
		ecpeer = EC_KEY_new();
		if (ecpeer == NULL)
			goto err;
		if (!EC_KEY_set_group(ecpeer, grp))
			goto err;
	} else {
		if (!eckey_from_params(atype, aval, &ecpeer))
			goto err;
	}

	/* We have parameters now set public key */
	plen = ASN1_STRING_length(pubkey);
	p = ASN1_STRING_get0_data(pubkey);
	if (!p || !plen)
		goto err;
	if (!o2i_ECPublicKey(&ecpeer, &p, plen))
		goto err;
	pkpeer = EVP_PKEY_new();
	if (pkpeer == NULL)
		goto err;
	EVP_PKEY_set1_EC_KEY(pkpeer, ecpeer);
	if (EVP_PKEY_derive_set_peer(pctx, pkpeer) > 0)
		rv = 1;
 err:
	EC_KEY_free(ecpeer);
	EVP_PKEY_free(pkpeer);
	return rv;
}

/* Set KDF parameters based on KDF NID */
static int
ecdh_cms_set_kdf_param(EVP_PKEY_CTX *pctx, int eckdf_nid)
{
	int kdf_nid, kdfmd_nid, cofactor;
	const EVP_MD *kdf_md;

	if (eckdf_nid == NID_undef)
		return 0;

	/* Lookup KDF type, cofactor mode and digest */
	if (!OBJ_find_sigid_algs(eckdf_nid, &kdfmd_nid, &kdf_nid))
		return 0;

	if (kdf_nid == NID_dh_std_kdf)
		cofactor = 0;
	else if (kdf_nid == NID_dh_cofactor_kdf)
		cofactor = 1;
	else
		return 0;

	if (EVP_PKEY_CTX_set_ecdh_cofactor_mode(pctx, cofactor) <= 0)
		return 0;

	if (EVP_PKEY_CTX_set_ecdh_kdf_type(pctx, EVP_PKEY_ECDH_KDF_X9_63) <= 0)
		return 0;

	kdf_md = EVP_get_digestbynid(kdfmd_nid);
	if (!kdf_md)
		return 0;

	if (EVP_PKEY_CTX_set_ecdh_kdf_md(pctx, kdf_md) <= 0)
		return 0;

	return 1;
}

static int
ecdh_cms_set_shared_info(EVP_PKEY_CTX *pctx, CMS_RecipientInfo *ri)
{
	X509_ALGOR *alg, *kekalg = NULL;
	const ASN1_OBJECT *obj;
	int nid;
	const void *parameter;
	int parameter_type;
	ASN1_OCTET_STRING *ukm;
	const unsigned char *p;
	unsigned char *der = NULL;
	int plen, keylen;
	const EVP_CIPHER *kekcipher;
	EVP_CIPHER_CTX *kekctx;
	int ret = 0;

	if (!CMS_RecipientInfo_kari_get0_alg(ri, &alg, &ukm))
		goto err;

	X509_ALGOR_get0(&obj, &parameter_type, &parameter, alg);

	if ((nid = OBJ_obj2nid(obj)) == NID_undef)
		goto err;
	if (!ecdh_cms_set_kdf_param(pctx, nid)) {
		ECerror(EC_R_KDF_PARAMETER_ERROR);
		goto err;
	}

	if (parameter_type != V_ASN1_SEQUENCE)
		goto err;
	if ((p = ASN1_STRING_get0_data(parameter)) == NULL)
		goto err;
	plen = ASN1_STRING_length(parameter);
	if ((kekalg = d2i_X509_ALGOR(NULL, &p, plen)) == NULL)
		goto err;

	/*
	 * XXX - the reaching into kekalg below is ugly, but unfortunately the
	 * now internal legacy EVP_CIPHER_asn1_to_param() API doesn't interact
	 * nicely with the X509_ALGOR API.
	 */

	if ((kekctx = CMS_RecipientInfo_kari_get0_ctx(ri)) == NULL)
		goto err;
	if ((kekcipher = EVP_get_cipherbyobj(kekalg->algorithm)) == NULL)
		goto err;
	if (EVP_CIPHER_mode(kekcipher) != EVP_CIPH_WRAP_MODE)
		goto err;
	if (!EVP_EncryptInit_ex(kekctx, kekcipher, NULL, NULL, NULL))
		goto err;
	if (EVP_CIPHER_asn1_to_param(kekctx, kekalg->parameter) <= 0)
		goto err;

	keylen = EVP_CIPHER_CTX_key_length(kekctx);
	if (EVP_PKEY_CTX_set_ecdh_kdf_outlen(pctx, keylen) <= 0)
		goto err;

	if ((plen = CMS_SharedInfo_encode(&der, kekalg, ukm, keylen)) <= 0)
		goto err;

	if (EVP_PKEY_CTX_set0_ecdh_kdf_ukm(pctx, der, plen) <= 0)
		goto err;
	der = NULL;

	ret = 1;

 err:
	X509_ALGOR_free(kekalg);
	free(der);

	return ret;
}

static int
ecdh_cms_decrypt(CMS_RecipientInfo *ri)
{
	EVP_PKEY_CTX *pctx;

	pctx = CMS_RecipientInfo_get0_pkey_ctx(ri);
	if (!pctx)
		return 0;

	/* See if we need to set peer key */
	if (!EVP_PKEY_CTX_get0_peerkey(pctx)) {
		X509_ALGOR *alg;
		ASN1_BIT_STRING *pubkey;

		if (!CMS_RecipientInfo_kari_get0_orig_id(ri, &alg, &pubkey,
		    NULL, NULL, NULL))
			return 0;
		if (!alg || !pubkey)
			return 0;
		if (!ecdh_cms_set_peerkey(pctx, alg, pubkey)) {
			ECerror(EC_R_PEER_KEY_ERROR);
			return 0;
		}
	}

	/* Set ECDH derivation parameters and initialise unwrap context */
	if (!ecdh_cms_set_shared_info(pctx, ri)) {
		ECerror(EC_R_SHARED_INFO_ERROR);
		return 0;
	}

	return 1;
}

static int
ecdh_cms_encrypt(CMS_RecipientInfo *ri)
{
	EVP_PKEY_CTX *pctx;
	EVP_CIPHER_CTX *ctx;
	int keylen;
	X509_ALGOR *talg, *wrap_alg = NULL;
	const ASN1_OBJECT *aoid;
	ASN1_BIT_STRING *pubkey;
	ASN1_STRING *wrap_str = NULL;
	ASN1_OCTET_STRING *ukm;
	unsigned char *penc = NULL;
	int penclen;
	int ecdh_nid, kdf_nid, wrap_nid;
	const EVP_MD *kdf_md;
	int ret = 0;

	if ((pctx = CMS_RecipientInfo_get0_pkey_ctx(ri)) == NULL)
		goto err;
	if (!CMS_RecipientInfo_kari_get0_orig_id(ri, &talg, &pubkey,
	    NULL, NULL, NULL))
		goto err;

	X509_ALGOR_get0(&aoid, NULL, NULL, talg);
	if (aoid == OBJ_nid2obj(NID_undef)) {
		EVP_PKEY *pkey;

		if ((pkey = EVP_PKEY_CTX_get0_pkey(pctx)) == NULL)
			goto err;

		penc = NULL;
		if ((penclen = i2o_ECPublicKey(pkey->pkey.ec, &penc)) <= 0)
			goto err;

		ASN1_STRING_set0(pubkey, penc, penclen);
		penc = NULL;

		if (!asn1_abs_set_unused_bits(pubkey, 0))
			goto err;

		if (!X509_ALGOR_set0_by_nid(talg, NID_X9_62_id_ecPublicKey,
		    V_ASN1_UNDEF, NULL))
			goto err;
	}

	if (EVP_PKEY_CTX_get_ecdh_kdf_type(pctx) != EVP_PKEY_ECDH_KDF_NONE)
		goto err;
	if (EVP_PKEY_CTX_set_ecdh_kdf_type(pctx, EVP_PKEY_ECDH_KDF_X9_63) <= 0)
		goto err;

	if ((ecdh_nid = EVP_PKEY_CTX_get_ecdh_cofactor_mode(pctx)) < 0)
		goto err;
	if (ecdh_nid == 0)
		ecdh_nid = NID_dh_std_kdf;
	else if (ecdh_nid == 1)
		ecdh_nid = NID_dh_cofactor_kdf;

	if (!EVP_PKEY_CTX_get_ecdh_kdf_md(pctx, &kdf_md))
		goto err;
	if (kdf_md == NULL) {
		/* Fixme later for better MD */
		kdf_md = EVP_sha1();
		if (EVP_PKEY_CTX_set_ecdh_kdf_md(pctx, kdf_md) <= 0)
			goto err;
	}

	if (!CMS_RecipientInfo_kari_get0_alg(ri, &talg, &ukm))
		goto err;

	/* Lookup NID for KDF+cofactor+digest */
	if (!OBJ_find_sigid_by_algs(&kdf_nid, EVP_MD_type(kdf_md), ecdh_nid))
		goto err;

	/* Get wrap NID */
	ctx = CMS_RecipientInfo_kari_get0_ctx(ri);
	wrap_nid = EVP_CIPHER_CTX_type(ctx);
	keylen = EVP_CIPHER_CTX_key_length(ctx);

	/*
	 * Package wrap algorithm in an AlgorithmIdentifier.
	 *
	 * Incompatibility of X509_ALGOR_set0() with EVP_CIPHER_param_to_asn1()
	 * makes this really gross. See the XXX in ecdh_cms_set_shared_info().
	 */

	if ((wrap_alg = X509_ALGOR_new()) == NULL)
		goto err;
	if ((wrap_alg->algorithm = OBJ_nid2obj(wrap_nid)) == NULL)
		goto err;
	if ((wrap_alg->parameter = ASN1_TYPE_new()) == NULL)
		goto err;
	if (EVP_CIPHER_param_to_asn1(ctx, wrap_alg->parameter) <= 0)
		goto err;
	if (ASN1_TYPE_get(wrap_alg->parameter) == V_ASN1_UNDEF) {
		ASN1_TYPE_free(wrap_alg->parameter);
		wrap_alg->parameter = NULL;
	}

	if ((penclen = CMS_SharedInfo_encode(&penc, wrap_alg, ukm, keylen)) <= 0)
		goto err;

	if (EVP_PKEY_CTX_set_ecdh_kdf_outlen(pctx, keylen) <= 0)
		goto err;
	if (EVP_PKEY_CTX_set0_ecdh_kdf_ukm(pctx, penc, penclen) <= 0)
		goto err;
	penc = NULL;

	/*
	 * Wrap encoded wrap AlgorithmIdentifier into parameter of another
	 * AlgorithmIdentifier.
	 */

	if ((penclen = i2d_X509_ALGOR(wrap_alg, &penc)) <= 0)
		goto err;

	if ((wrap_str = ASN1_STRING_new()) == NULL)
		goto err;
	ASN1_STRING_set0(wrap_str, penc, penclen);
	penc = NULL;

	if (!X509_ALGOR_set0_by_nid(talg, kdf_nid, V_ASN1_SEQUENCE, wrap_str))
		goto err;
	wrap_str = NULL;

	ret = 1;

 err:
	free(penc);
	ASN1_STRING_free(wrap_str);
	X509_ALGOR_free(wrap_alg);

	return ret;
}

#endif

const EVP_PKEY_ASN1_METHOD eckey_asn1_meth = {
	.base_method = &eckey_asn1_meth,
	.pkey_id = EVP_PKEY_EC,

	.pem_str = "EC",
	.info = "OpenSSL EC algorithm",

	.pub_decode = eckey_pub_decode,
	.pub_encode = eckey_pub_encode,
	.pub_cmp = eckey_pub_cmp,
	.pub_print = eckey_pub_print,

	.priv_decode = eckey_priv_decode,
	.priv_encode = eckey_priv_encode,
	.priv_print = eckey_priv_print,

	.pkey_size = ec_size,
	.pkey_bits = ec_bits,
	.pkey_security_bits = ec_security_bits,

	.param_decode = eckey_param_decode,
	.param_encode = eckey_param_encode,
	.param_missing = ec_missing_parameters,
	.param_copy = ec_copy_parameters,
	.param_cmp = ec_cmp_parameters,
	.param_print = eckey_param_print,

	.pkey_free = ec_free,
	.pkey_ctrl = ec_pkey_ctrl,
	.old_priv_decode = old_ec_priv_decode,
	.old_priv_encode = old_ec_priv_encode,
};

/* $OpenBSD: dsa_ameth.c,v 1.60 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/cms.h>
#include <openssl/dsa.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "bn_local.h"
#include "dsa_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

static int
dsa_pub_decode(EVP_PKEY *pkey, X509_PUBKEY *pubkey)
{
	X509_ALGOR *algor;
	int ptype;
	const void *pval;
	const ASN1_STRING *astr;
	const unsigned char *key, *params, *p;
	int key_len, params_len;
	ASN1_INTEGER *aint = NULL;
	DSA *dsa = NULL;
	int ret = 0;

	if (!X509_PUBKEY_get0_param(NULL, &key, &key_len, &algor, pubkey))
		goto err;
	X509_ALGOR_get0(NULL, &ptype, &pval, algor);

	if (ptype == V_ASN1_SEQUENCE) {
		astr = pval;
		params = astr->data;
		params_len = astr->length;

		p = params;
		if ((dsa = d2i_DSAparams(NULL, &p, params_len)) == NULL) {
			DSAerror(DSA_R_DECODE_ERROR);
			goto err;
		}
	} else if (ptype == V_ASN1_NULL || ptype == V_ASN1_UNDEF) {
		if ((dsa = DSA_new()) == NULL) {
			DSAerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
	} else {
		DSAerror(DSA_R_PARAMETER_ENCODING_ERROR);
		goto err;
	}

	p = key;
	if ((aint = d2i_ASN1_INTEGER(NULL, &p, key_len)) == NULL) {
		DSAerror(DSA_R_DECODE_ERROR);
		goto err;
	}
	BN_free(dsa->pub_key);
	if ((dsa->pub_key = ASN1_INTEGER_to_BN(aint, NULL)) == NULL) {
		DSAerror(DSA_R_BN_DECODE_ERROR);
		goto err;
	}

	/* We can only check for key consistency if we have parameters. */
	if (ptype == V_ASN1_SEQUENCE) {
		if (!dsa_check_key(dsa))
			goto err;
	}

	if (!EVP_PKEY_assign_DSA(pkey, dsa))
		goto err;
	dsa = NULL;

	ret = 1;

 err:
	ASN1_INTEGER_free(aint);
	DSA_free(dsa);

	return ret;
}

static int
dsa_pub_encode(X509_PUBKEY *pk, const EVP_PKEY *pkey)
{
	const DSA *dsa = pkey->pkey.dsa;
	ASN1_STRING *astr = NULL;
	int ptype = V_ASN1_UNDEF;
	ASN1_INTEGER *aint = NULL;
	ASN1_OBJECT *aobj;
	unsigned char *params = NULL, *key = NULL;
	int params_len = 0, key_len = 0;
	int ret = 0;

	if (pkey->save_parameters > 0 && !EVP_PKEY_missing_parameters(pkey)) {
		if ((params_len = i2d_DSAparams(dsa, &params)) <= 0) {
			DSAerror(ERR_R_MALLOC_FAILURE);
			params_len = 0;
			goto err;
		}
		if ((astr = ASN1_STRING_new()) == NULL) {
			DSAerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		ASN1_STRING_set0(astr, params, params_len);
		params = NULL;
		params_len = 0;
		ptype = V_ASN1_SEQUENCE;
	}

	if ((aint = BN_to_ASN1_INTEGER(dsa->pub_key, NULL)) == NULL) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((key_len = i2d_ASN1_INTEGER(aint, &key)) <= 0) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		key_len = 0;
		goto err;
	}

	if ((aobj = OBJ_nid2obj(EVP_PKEY_DSA)) == NULL)
		goto err;
	if (!X509_PUBKEY_set0_param(pk, aobj, ptype, astr, key, key_len))
		goto err;
	astr = NULL;
	key = NULL;
	key_len = 0;

	ret = 1;

 err:
	ASN1_STRING_free(astr);
	ASN1_INTEGER_free(aint);
	freezero(params, params_len);
	freezero(key, key_len);

	return ret;
}

/*
 * In PKCS#8 DSA: you just get a private key integer and parameters in the
 * AlgorithmIdentifier the pubkey must be recalculated.
 */
static int
dsa_priv_decode(EVP_PKEY *pkey, const PKCS8_PRIV_KEY_INFO *p8)
{
	const X509_ALGOR *algor;
	int ptype;
	const void *pval;
	const ASN1_STRING *astr;
	const unsigned char *key, *params, *p;
	int key_len, params_len;
	ASN1_INTEGER *aint = NULL;
	BN_CTX *ctx = NULL;
	DSA *dsa = NULL;
	int ret = 0;

	if (!PKCS8_pkey_get0(NULL, &key, &key_len, &algor, p8))
		goto err;
	X509_ALGOR_get0(NULL, &ptype, &pval, algor);

	if (ptype != V_ASN1_SEQUENCE) {
		DSAerror(DSA_R_PARAMETER_ENCODING_ERROR);
		goto err;
	}

	astr = pval;
	params = astr->data;
	params_len = astr->length;

	p = params;
	if ((dsa = d2i_DSAparams(NULL, &p, params_len)) == NULL) {
		DSAerror(DSA_R_DECODE_ERROR);
		goto err;
	}
	p = key;
	if ((aint = d2i_ASN1_INTEGER(NULL, &p, key_len)) == NULL) {
		DSAerror(DSA_R_DECODE_ERROR);
		goto err;
	}
	BN_free(dsa->priv_key);
	if ((dsa->priv_key = ASN1_INTEGER_to_BN(aint, NULL)) == NULL) {
		DSAerror(DSA_R_BN_DECODE_ERROR);
		goto err;
	}

	/* Check the key for basic consistency before doing expensive things. */
	if (!dsa_check_key(dsa))
		goto err;

	/* Calculate public key */
	BN_free(dsa->pub_key);
	if ((dsa->pub_key = BN_new()) == NULL) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((ctx = BN_CTX_new()) == NULL) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	BN_CTX_start(ctx);

	if (!BN_mod_exp_ct(dsa->pub_key, dsa->g, dsa->priv_key, dsa->p, ctx)) {
		DSAerror(DSA_R_BN_ERROR);
		goto err;
	}

	if (!EVP_PKEY_assign_DSA(pkey, dsa))
		goto err;
	dsa = NULL;

	ret = 1;

 err:
	DSA_free(dsa);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	ASN1_INTEGER_free(aint);

	return ret;
}

static int
dsa_priv_encode(PKCS8_PRIV_KEY_INFO *p8, const EVP_PKEY *pkey)
{
	const DSA *dsa = pkey->pkey.dsa;
	ASN1_STRING *astr = NULL;
	int ptype = V_ASN1_SEQUENCE;
	ASN1_INTEGER *aint = NULL;
	ASN1_OBJECT *aobj;
	unsigned char *params = NULL, *key = NULL;
	int params_len = 0, key_len = 0;
	int ret = 0;

	if ((params_len = i2d_DSAparams(dsa, &params)) <= 0) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		params_len = 0;
		goto err;
	}
	if ((astr = ASN1_STRING_type_new(V_ASN1_SEQUENCE)) == NULL) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	ASN1_STRING_set0(astr, params, params_len);
	params = NULL;
	params_len = 0;

	if ((aint = BN_to_ASN1_INTEGER(dsa->priv_key, NULL)) == NULL) {
		DSAerror(DSA_R_BN_ERROR);
		goto err;
	}
	if ((key_len = i2d_ASN1_INTEGER(aint, &key)) <= 0) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		key_len = 0;
		goto err;
	}

	if ((aobj = OBJ_nid2obj(NID_dsa)) == NULL)
		goto err;
	if (!PKCS8_pkey_set0(p8, aobj, 0, ptype, astr, key, key_len))
		goto err;
	astr = NULL;
	key = NULL;
	key_len = 0;

	ret = 1;

 err:
	ASN1_STRING_free(astr);
	ASN1_INTEGER_free(aint);
	freezero(params, params_len);
	freezero(key, key_len);

	return ret;
}

static int
dsa_size(const EVP_PKEY *pkey)
{
	return DSA_size(pkey->pkey.dsa);
}

static int
dsa_bits(const EVP_PKEY *pkey)
{
	return BN_num_bits(pkey->pkey.dsa->p);
}

static int
dsa_security_bits(const EVP_PKEY *pkey)
{
	return DSA_security_bits(pkey->pkey.dsa);
}

static int
dsa_missing_parameters(const EVP_PKEY *pkey)
{
	const DSA *dsa = pkey->pkey.dsa;

	return dsa->p == NULL || dsa->q == NULL || dsa->g == NULL;
}

static int
dsa_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from)
{
	BIGNUM *a;

	if ((a = BN_dup(from->pkey.dsa->p)) == NULL)
		return 0;
	BN_free(to->pkey.dsa->p);
	to->pkey.dsa->p = a;

	if ((a = BN_dup(from->pkey.dsa->q)) == NULL)
		return 0;
	BN_free(to->pkey.dsa->q);
	to->pkey.dsa->q = a;

	if ((a = BN_dup(from->pkey.dsa->g)) == NULL)
		return 0;
	BN_free(to->pkey.dsa->g);
	to->pkey.dsa->g = a;
	return 1;
}

static int
dsa_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (BN_cmp(a->pkey.dsa->p, b->pkey.dsa->p) ||
	    BN_cmp(a->pkey.dsa->q, b->pkey.dsa->q) ||
	    BN_cmp(a->pkey.dsa->g, b->pkey.dsa->g))
		return 0;
	else
		return 1;
}

static int
dsa_pub_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (BN_cmp(b->pkey.dsa->pub_key, a->pkey.dsa->pub_key) != 0)
		return 0;
	else
		return 1;
}

static void
dsa_free(EVP_PKEY *pkey)
{
	DSA_free(pkey->pkey.dsa);
}

static int
do_dsa_print(BIO *bp, const DSA *x, int off, int ptype)
{
	const char *ktype = NULL;
	const BIGNUM *priv_key, *pub_key;
	int ret = 0;

	if (ptype == 2)
		priv_key = x->priv_key;
	else
		priv_key = NULL;

	if (ptype > 0)
		pub_key = x->pub_key;
	else
		pub_key = NULL;

	if (ptype == 2)
		ktype = "Private-Key";
	else if (ptype == 1)
		ktype = "Public-Key";
	else
		ktype = "DSA-Parameters";

	if (priv_key) {
		if (!BIO_indent(bp, off, 128))
			goto err;
		if (BIO_printf(bp, "%s: (%d bit)\n", ktype,
		    BN_num_bits(x->p)) <= 0)
			goto err;
	}

	if (!bn_printf(bp, priv_key, off, "priv:"))
		goto err;
	if (!bn_printf(bp, pub_key, off, "pub: "))
		goto err;
	if (!bn_printf(bp, x->p, off, "P:   "))
		goto err;
	if (!bn_printf(bp, x->q, off, "Q:   "))
		goto err;
	if (!bn_printf(bp, x->g, off, "G:   "))
		goto err;

	ret = 1;

 err:
	return ret;
}

static int
dsa_param_decode(EVP_PKEY *pkey, const unsigned char **params, int params_len)
{
	DSA *dsa = NULL;
	int ret = 0;

	if ((dsa = d2i_DSAparams(NULL, params, params_len)) == NULL) {
		DSAerror(ERR_R_DSA_LIB);
		goto err;
	}
	if (!dsa_check_key(dsa))
		goto err;
	if (!EVP_PKEY_assign_DSA(pkey, dsa))
		goto err;
	dsa = NULL;

	ret = 1;

 err:
	DSA_free(dsa);

	return ret;
}

static int
dsa_param_encode(const EVP_PKEY *pkey, unsigned char **params)
{
	return i2d_DSAparams(pkey->pkey.dsa, params);
}

static int
dsa_param_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dsa_print(bp, pkey->pkey.dsa, indent, 0);
}

static int
dsa_pub_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dsa_print(bp, pkey->pkey.dsa, indent, 1);
}

static int
dsa_priv_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dsa_print(bp, pkey->pkey.dsa, indent, 2);
}

static int
old_dsa_priv_decode(EVP_PKEY *pkey, const unsigned char **key, int key_len)
{
	DSA *dsa = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *result;
	int ret = 0;

	if ((dsa = d2i_DSAPrivateKey(NULL, key, key_len)) == NULL) {
		DSAerror(ERR_R_DSA_LIB);
		goto err;
	}

	if (!dsa_check_key(dsa))
		goto err;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((result = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Check that p and q are consistent with each other. dsa_check_key()
	 * ensures that 1 < q < p. Now check that q divides p - 1.
	 */

	if (!BN_sub(result, dsa->p, BN_value_one()))
		goto err;
	if (!BN_mod_ct(result, result, dsa->q, ctx))
		goto err;
	if (!BN_is_zero(result)) {
		DSAerror(DSA_R_BAD_Q_VALUE);
		goto err;
	}

	/*
	 * Check that g generates a multiplicative subgroup of order q.
	 * We only check that g^q == 1, so the order is a divisor of q.
	 * Once we know that q is prime, this is enough.
	 */

	if (!BN_mod_exp_ct(result, dsa->g, dsa->q, dsa->p, ctx))
		goto err;
	if (BN_cmp(result, BN_value_one()) != 0) {
		DSAerror(DSA_R_INVALID_PARAMETERS);
		goto err;
	}

	/*
	 * Check that q is not a composite number.
	 */

	if (BN_is_prime_ex(dsa->q, BN_prime_checks, ctx, NULL) <= 0) {
		DSAerror(DSA_R_BAD_Q_VALUE);
		goto err;
	}

	if (!EVP_PKEY_assign_DSA(pkey, dsa))
		goto err;
	dsa = NULL;

	ret = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	DSA_free(dsa);

	return ret;
}

static int
old_dsa_priv_encode(const EVP_PKEY *pkey, unsigned char **key)
{
	return i2d_DSAPrivateKey(pkey->pkey.dsa, key);
}

static int
dsa_sig_print(BIO *bp, const X509_ALGOR *sigalg, const ASN1_STRING *sig,
    int indent, ASN1_PCTX *pctx)
{
	DSA_SIG *dsa_sig;
	const unsigned char *p;

	if (!sig) {
		if (BIO_puts(bp, "\n") <= 0)
			return 0;
		else
			return 1;
	}
	p = sig->data;
	dsa_sig = d2i_DSA_SIG(NULL, &p, sig->length);
	if (dsa_sig) {
		int rv = 0;

		if (BIO_write(bp, "\n", 1) != 1)
			goto err;

		if (!bn_printf(bp, dsa_sig->r, indent, "r:   "))
			goto err;
		if (!bn_printf(bp, dsa_sig->s, indent, "s:   "))
			goto err;
		rv = 1;
 err:
		DSA_SIG_free(dsa_sig);
		return rv;
	}
	return X509_signature_dump(bp, sig, indent);
}

static int
dsa_pkey_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
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
			int snid, hnid;
			X509_ALGOR *alg1, *alg2;

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

	case ASN1_PKEY_CTRL_CMS_RI_TYPE:
		*(int *)arg2 = CMS_RECIPINFO_NONE;
		return 1;
#endif

	case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
		*(int *)arg2 = NID_sha1;
		return 2;

	default:
		return -2;
	}
}

const EVP_PKEY_ASN1_METHOD dsa_asn1_meth = {
	.base_method = &dsa_asn1_meth,
	.pkey_id = EVP_PKEY_DSA,

	.pem_str = "DSA",
	.info = "OpenSSL DSA method",

	.pub_decode = dsa_pub_decode,
	.pub_encode = dsa_pub_encode,
	.pub_cmp = dsa_pub_cmp,
	.pub_print = dsa_pub_print,

	.priv_decode = dsa_priv_decode,
	.priv_encode = dsa_priv_encode,
	.priv_print = dsa_priv_print,

	.pkey_size = dsa_size,
	.pkey_bits = dsa_bits,
	.pkey_security_bits = dsa_security_bits,

	.param_decode = dsa_param_decode,
	.param_encode = dsa_param_encode,
	.param_missing = dsa_missing_parameters,
	.param_copy = dsa_copy_parameters,
	.param_cmp = dsa_cmp_parameters,
	.param_print = dsa_param_print,
	.sig_print = dsa_sig_print,

	.pkey_free = dsa_free,
	.pkey_ctrl = dsa_pkey_ctrl,
	.old_priv_decode = old_dsa_priv_decode,
	.old_priv_encode = old_dsa_priv_encode
};

const EVP_PKEY_ASN1_METHOD dsa1_asn1_meth = {
	.base_method = &dsa_asn1_meth,
	.pkey_id = EVP_PKEY_DSA1,
	.pkey_flags = ASN1_PKEY_ALIAS,
};

const EVP_PKEY_ASN1_METHOD dsa2_asn1_meth = {
	.base_method = &dsa_asn1_meth,
	.pkey_id = EVP_PKEY_DSA2,
	.pkey_flags = ASN1_PKEY_ALIAS,
};

const EVP_PKEY_ASN1_METHOD dsa3_asn1_meth = {
	.base_method = &dsa_asn1_meth,
	.pkey_id = EVP_PKEY_DSA3,
	.pkey_flags = ASN1_PKEY_ALIAS,
};

const EVP_PKEY_ASN1_METHOD dsa4_asn1_meth = {
	.base_method = &dsa_asn1_meth,
	.pkey_id = EVP_PKEY_DSA4,
	.pkey_flags = ASN1_PKEY_ALIAS,
};

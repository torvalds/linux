/* $OpenBSD: dh_ameth.c,v 1.43 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "bn_local.h"
#include "dh_local.h"
#include "err_local.h"
#include "evp_local.h"

static void
dh_free(EVP_PKEY *pkey)
{
	DH_free(pkey->pkey.dh);
}

static int
dh_pub_decode(EVP_PKEY *pkey, X509_PUBKEY *pubkey)
{
	X509_ALGOR *algor;
	int ptype;
	const void *pval;
	const ASN1_STRING *astr;
	const unsigned char *key, *params, *p;
	int key_len, params_len;
	ASN1_INTEGER *aint = NULL;
	DH *dh = NULL;
	int ret = 0;

	if (!X509_PUBKEY_get0_param(NULL, &key, &key_len, &algor, pubkey))
		goto err;
	X509_ALGOR_get0(NULL, &ptype, &pval, algor);

	if (ptype != V_ASN1_SEQUENCE) {
		DHerror(DH_R_PARAMETER_ENCODING_ERROR);
		goto err;
	}

	astr = pval;
	params = astr->data;
	params_len = astr->length;

	p = params;
	if ((dh = d2i_DHparams(NULL, &p, params_len)) == NULL) {
		DHerror(DH_R_DECODE_ERROR);
		goto err;
	}
	p = key;
	if ((aint = d2i_ASN1_INTEGER(NULL, &p, key_len)) == NULL) {
		DHerror(DH_R_DECODE_ERROR);
		goto err;
	}
	BN_free(dh->pub_key);
	if ((dh->pub_key = ASN1_INTEGER_to_BN(aint, NULL)) == NULL) {
		DHerror(DH_R_BN_DECODE_ERROR);
		goto err;
	}

	if (!EVP_PKEY_assign_DH(pkey, dh))
		goto err;
	dh = NULL;

	ret = 1;

 err:
	ASN1_INTEGER_free(aint);
	DH_free(dh);

	return ret;
}

static int
dh_pub_encode(X509_PUBKEY *pk, const EVP_PKEY *pkey)
{
	const DH *dh = pkey->pkey.dh;
	ASN1_STRING *astr = NULL;
	int ptype = V_ASN1_SEQUENCE;
	ASN1_INTEGER *aint = NULL;
	ASN1_OBJECT *aobj;
	unsigned char *params = NULL, *key = NULL;
	int params_len = 0, key_len = 0;
	int ret = 0;

	if ((params_len = i2d_DHparams(dh, &params)) <= 0) {
		DHerror(ERR_R_MALLOC_FAILURE);
		params_len = 0;
		goto err;
	}
	if ((astr = ASN1_STRING_new()) == NULL) {
		DHerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	ASN1_STRING_set0(astr, params, params_len);
	params = NULL;
	params_len = 0;

	if ((aint = BN_to_ASN1_INTEGER(dh->pub_key, NULL)) == NULL)
		goto err;
	if ((key_len = i2d_ASN1_INTEGER(aint, &key)) <= 0) {
		DHerror(ERR_R_MALLOC_FAILURE);
		key_len = 0;
		goto err;
	}

	if ((aobj = OBJ_nid2obj(EVP_PKEY_DH)) == NULL)
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
 * PKCS#8 DH is defined in PKCS#11 of all places. It is similar to DH in
 * that the AlgorithmIdentifier contains the parameters, the private key
 * is explicitly included and the pubkey must be recalculated.
 */

static int
dh_priv_decode(EVP_PKEY *pkey, const PKCS8_PRIV_KEY_INFO *p8)
{
	const X509_ALGOR *algor;
	int ptype;
	const void *pval;
	const ASN1_STRING *astr;
	const unsigned char *key, *params, *p;
	int key_len, params_len;
	ASN1_INTEGER *aint = NULL;
	DH *dh = NULL;
	int ret = 0;

	if (!PKCS8_pkey_get0(NULL, &key, &key_len, &algor, p8))
		goto err;
	X509_ALGOR_get0(NULL, &ptype, &pval, algor);

	if (ptype != V_ASN1_SEQUENCE) {
		DHerror(DH_R_PARAMETER_ENCODING_ERROR);
		goto err;
	}

	astr = pval;
	params = astr->data;
	params_len = astr->length;

	p = params;
	if ((dh = d2i_DHparams(NULL, &p, params_len)) == NULL) {
		DHerror(DH_R_DECODE_ERROR);
		goto err;
	}
	p = key;
	if ((aint = d2i_ASN1_INTEGER(NULL, &p, key_len)) == NULL) {
		DHerror(DH_R_DECODE_ERROR);
		goto err;
	}
	BN_free(dh->priv_key);
	if ((dh->priv_key = ASN1_INTEGER_to_BN(aint, NULL)) == NULL) {
		DHerror(DH_R_BN_DECODE_ERROR);
		goto err;
	}
	if (!DH_generate_key(dh))
		goto err;

	if (!EVP_PKEY_assign_DH(pkey, dh))
		goto err;
	dh = NULL;

	ret = 1;

 err:
	ASN1_INTEGER_free(aint);
	DH_free(dh);

	return ret;
}

static int
dh_priv_encode(PKCS8_PRIV_KEY_INFO *p8, const EVP_PKEY *pkey)
{
	const DH *dh = pkey->pkey.dh;
	ASN1_STRING *astr = NULL;
	int ptype = V_ASN1_SEQUENCE;
	ASN1_INTEGER *aint = NULL;
	ASN1_OBJECT *aobj;
	unsigned char *params = NULL, *key = NULL;
	int params_len = 0, key_len = 0;
	int ret = 0;

	if ((params_len = i2d_DHparams(dh, &params)) <= 0) {
		DHerror(ERR_R_MALLOC_FAILURE);
		params_len = 0;
		goto err;
	}
	if ((astr = ASN1_STRING_type_new(V_ASN1_SEQUENCE)) == NULL) {
		DHerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	ASN1_STRING_set0(astr, params, params_len);
	params = NULL;
	params_len = 0;

	if ((aint = BN_to_ASN1_INTEGER(dh->priv_key, NULL)) == NULL) {
		DHerror(DH_R_BN_ERROR);
		goto err;
	}
	if ((key_len = i2d_ASN1_INTEGER(aint, &key)) <= 0) {
		DHerror(ERR_R_MALLOC_FAILURE);
		key_len = 0;
		goto err;
	}

	if ((aobj = OBJ_nid2obj(NID_dhKeyAgreement)) == NULL)
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
dh_param_decode(EVP_PKEY *pkey, const unsigned char **params, int params_len)
{
	DH *dh = NULL;
	int ret = 0;

	if ((dh = d2i_DHparams(NULL, params, params_len)) == NULL) {
		DHerror(ERR_R_DH_LIB);
		goto err;
	}
	if (!EVP_PKEY_assign_DH(pkey, dh))
		goto err;
	dh = NULL;

	ret = 1;

 err:
	DH_free(dh);

	return ret;
}

static int
dh_param_encode(const EVP_PKEY *pkey, unsigned char **params)
{
	return i2d_DHparams(pkey->pkey.dh, params);
}

static int
do_dh_print(BIO *bp, const DH *x, int indent, ASN1_PCTX *ctx, int ptype)
{
	int reason = ERR_R_BUF_LIB, ret = 0;
	const char *ktype = NULL;
	BIGNUM *priv_key, *pub_key;

	if (ptype == 2)
		priv_key = x->priv_key;
	else
		priv_key = NULL;

	if (ptype > 0)
		pub_key = x->pub_key;
	else
		pub_key = NULL;

	if (ptype == 2)
		ktype = "PKCS#3 DH Private-Key";
	else if (ptype == 1)
		ktype = "PKCS#3 DH Public-Key";
	else
		ktype = "PKCS#3 DH Parameters";

	if (x->p == NULL) {
		reason = ERR_R_PASSED_NULL_PARAMETER;
		goto err;
	}

	if (!BIO_indent(bp, indent, 128))
		goto err;
	if (BIO_printf(bp, "%s: (%d bit)\n", ktype, BN_num_bits(x->p)) <= 0)
		goto err;
	indent += 4;

	if (!bn_printf(bp, priv_key, indent, "private-key:"))
		goto err;
	if (!bn_printf(bp, pub_key, indent, "public-key:"))
		goto err;

	if (!bn_printf(bp, x->p, indent, "prime:"))
		goto err;
	if (!bn_printf(bp, x->g, indent, "generator:"))
		goto err;
	if (x->length != 0) {
		if (!BIO_indent(bp, indent, 128))
			goto err;
		if (BIO_printf(bp, "recommended-private-length: %d bits\n",
		    (int)x->length) <= 0)
			goto err;
	}

	ret = 1;
	if (0) {
 err:
		DHerror(reason);
	}
	return(ret);
}

static int
dh_size(const EVP_PKEY *pkey)
{
	return DH_size(pkey->pkey.dh);
}

static int
dh_bits(const EVP_PKEY *pkey)
{
	return BN_num_bits(pkey->pkey.dh->p);
}

static int
dh_security_bits(const EVP_PKEY *pkey)
{
	return DH_security_bits(pkey->pkey.dh);
}

static int
dh_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (BN_cmp(a->pkey.dh->p, b->pkey.dh->p) ||
	    BN_cmp(a->pkey.dh->g, b->pkey.dh->g))
		return 0;
	else
		return 1;
}

static int
dh_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from)
{
	BIGNUM *a;

	if ((a = BN_dup(from->pkey.dh->p)) == NULL)
		return 0;
	BN_free(to->pkey.dh->p);
	to->pkey.dh->p = a;

	if ((a = BN_dup(from->pkey.dh->g)) == NULL)
		return 0;
	BN_free(to->pkey.dh->g);
	to->pkey.dh->g = a;

	return 1;
}

static int
dh_missing_parameters(const EVP_PKEY *pkey)
{
	const DH *dh = pkey->pkey.dh;

	return dh->p == NULL || dh->g == NULL;
}

static int
dh_pub_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (dh_cmp_parameters(a, b) == 0)
		return 0;
	if (BN_cmp(b->pkey.dh->pub_key, a->pkey.dh->pub_key) != 0)
		return 0;
	else
		return 1;
}

static int
dh_param_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dh_print(bp, pkey->pkey.dh, indent, ctx, 0);
}

static int
dh_public_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dh_print(bp, pkey->pkey.dh, indent, ctx, 1);
}

static int
dh_private_print(BIO *bp, const EVP_PKEY *pkey, int indent, ASN1_PCTX *ctx)
{
	return do_dh_print(bp, pkey->pkey.dh, indent, ctx, 2);
}

int
DHparams_print(BIO *bp, const DH *x)
{
	return do_dh_print(bp, x, 4, NULL, 0);
}
LCRYPTO_ALIAS(DHparams_print);

int
DHparams_print_fp(FILE *fp, const DH *x)
{
	BIO *b;
	int ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		DHerror(ERR_R_BUF_LIB);
		return 0;
	}

	BIO_set_fp(b, fp, BIO_NOCLOSE);
	ret = DHparams_print(b, x);
	BIO_free(b);

	return ret;
}
LCRYPTO_ALIAS(DHparams_print_fp);

const EVP_PKEY_ASN1_METHOD dh_asn1_meth = {
	.base_method = &dh_asn1_meth,
	.pkey_id = EVP_PKEY_DH,

	.pem_str = "DH",
	.info = "OpenSSL PKCS#3 DH method",

	.pub_decode = dh_pub_decode,
	.pub_encode = dh_pub_encode,
	.pub_cmp = dh_pub_cmp,
	.pub_print = dh_public_print,

	.priv_decode = dh_priv_decode,
	.priv_encode = dh_priv_encode,
	.priv_print = dh_private_print,

	.pkey_size = dh_size,
	.pkey_bits = dh_bits,
	.pkey_security_bits = dh_security_bits,

	.param_decode = dh_param_decode,
	.param_encode = dh_param_encode,
	.param_missing = dh_missing_parameters,
	.param_copy = dh_copy_parameters,
	.param_cmp = dh_cmp_parameters,
	.param_print = dh_param_print,

	.pkey_free = dh_free,
};

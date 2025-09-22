/* $OpenBSD: p5_pbev2.c,v 1.38 2025/05/24 02:57:14 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999-2004.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1t.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

/*
 * RFC 8018, sections 6.2 and 4 specify at least 64 bits for PBES2, apparently
 * FIPS will require at least 128 bits in the future, OpenSSL does that.
 */
#define PKCS5_PBE2_SALT_LEN	16

/* PKCS#5 v2.0 password based encryption structures */

static const ASN1_TEMPLATE PBE2PARAM_seq_tt[] = {
	{
		.offset = offsetof(PBE2PARAM, keyfunc),
		.field_name = "keyfunc",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(PBE2PARAM, encryption),
		.field_name = "encryption",
		.item = &X509_ALGOR_it,
	},
};

const ASN1_ITEM PBE2PARAM_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PBE2PARAM_seq_tt,
	.tcount = sizeof(PBE2PARAM_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(PBE2PARAM),
	.sname = "PBE2PARAM",
};


PBE2PARAM *
d2i_PBE2PARAM(PBE2PARAM **a, const unsigned char **in, long len)
{
	return (PBE2PARAM *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PBE2PARAM_it);
}

int
i2d_PBE2PARAM(PBE2PARAM *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PBE2PARAM_it);
}

PBE2PARAM *
PBE2PARAM_new(void)
{
	return (PBE2PARAM *)ASN1_item_new(&PBE2PARAM_it);
}

void
PBE2PARAM_free(PBE2PARAM *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PBE2PARAM_it);
}

static const ASN1_TEMPLATE PBKDF2PARAM_seq_tt[] = {
	{
		.offset = offsetof(PBKDF2PARAM, salt),
		.field_name = "salt",
		.item = &ASN1_ANY_it,
	},
	{
		.offset = offsetof(PBKDF2PARAM, iter),
		.field_name = "iter",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(PBKDF2PARAM, keylength),
		.field_name = "keylength",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(PBKDF2PARAM, prf),
		.field_name = "prf",
		.item = &X509_ALGOR_it,
	},
};

const ASN1_ITEM PBKDF2PARAM_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PBKDF2PARAM_seq_tt,
	.tcount = sizeof(PBKDF2PARAM_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(PBKDF2PARAM),
	.sname = "PBKDF2PARAM",
};


PBKDF2PARAM *
d2i_PBKDF2PARAM(PBKDF2PARAM **a, const unsigned char **in, long len)
{
	return (PBKDF2PARAM *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PBKDF2PARAM_it);
}

int
i2d_PBKDF2PARAM(PBKDF2PARAM *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PBKDF2PARAM_it);
}

PBKDF2PARAM *
PBKDF2PARAM_new(void)
{
	return (PBKDF2PARAM *)ASN1_item_new(&PBKDF2PARAM_it);
}

void
PBKDF2PARAM_free(PBKDF2PARAM *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PBKDF2PARAM_it);
}

/*
 * Return an algorithm identifier for a PKCS#5 v2.0 PBE algorithm:
 * yes I know this is horrible!
 */

X509_ALGOR *
PKCS5_pbe2_set(const EVP_CIPHER *cipher, int iter, unsigned char *salt,
    int saltlen)
{
	X509_ALGOR *scheme = NULL, *kalg = NULL, *ret = NULL;
	int prf_nid = NID_hmacWithSHA256;
	int alg_nid, keylen;
	EVP_CIPHER_CTX ctx;
	unsigned char iv[EVP_MAX_IV_LENGTH];
	PBE2PARAM *pbe2 = NULL;
	ASN1_OBJECT *obj;

	alg_nid = EVP_CIPHER_type(cipher);
	if (alg_nid == NID_undef) {
		ASN1error(ASN1_R_CIPHER_HAS_NO_OBJECT_IDENTIFIER);
		goto err;
	}
	obj = OBJ_nid2obj(alg_nid);

	if (!(pbe2 = PBE2PARAM_new()))
		goto merr;

	/* Setup the AlgorithmIdentifier for the encryption scheme */
	scheme = pbe2->encryption;

	scheme->algorithm = obj;
	if (!(scheme->parameter = ASN1_TYPE_new()))
		goto merr;

	/* Create random IV */
	if (EVP_CIPHER_iv_length(cipher) > 0)
		arc4random_buf(iv, EVP_CIPHER_iv_length(cipher));

	EVP_CIPHER_CTX_legacy_clear(&ctx);

	/* Dummy cipherinit to just setup the IV, and PRF */
	if (!EVP_CipherInit_ex(&ctx, cipher, NULL, NULL, iv, 0))
		goto err;
	if (EVP_CIPHER_param_to_asn1(&ctx, scheme->parameter) < 0) {
		ASN1error(ASN1_R_ERROR_SETTING_CIPHER_PARAMS);
		EVP_CIPHER_CTX_cleanup(&ctx);
		goto err;
	}
	EVP_CIPHER_CTX_cleanup(&ctx);

	/* If its RC2 then we'd better setup the key length */

	if (alg_nid == NID_rc2_cbc)
		keylen = EVP_CIPHER_key_length(cipher);
	else
		keylen = -1;

	/* Setup keyfunc */

	X509_ALGOR_free(pbe2->keyfunc);

	pbe2->keyfunc = PKCS5_pbkdf2_set(iter, salt, saltlen, prf_nid, keylen);

	if (!pbe2->keyfunc)
		goto merr;

	/* Now set up top level AlgorithmIdentifier */

	if (!(ret = X509_ALGOR_new()))
		goto merr;
	if (!(ret->parameter = ASN1_TYPE_new()))
		goto merr;

	ret->algorithm = OBJ_nid2obj(NID_pbes2);

	/* Encode PBE2PARAM into parameter */

	if (!ASN1_item_pack(pbe2, &PBE2PARAM_it,
		&ret->parameter->value.sequence)) goto merr;
	ret->parameter->type = V_ASN1_SEQUENCE;

	PBE2PARAM_free(pbe2);
	pbe2 = NULL;

	return ret;

 merr:
	ASN1error(ERR_R_MALLOC_FAILURE);

 err:
	PBE2PARAM_free(pbe2);
	/* Note 'scheme' is freed as part of pbe2 */
	X509_ALGOR_free(kalg);
	X509_ALGOR_free(ret);

	return NULL;
}

X509_ALGOR *
PKCS5_pbkdf2_set(int iter, unsigned char *salt, int saltlen, int prf_nid,
    int keylen)
{
	X509_ALGOR *keyfunc = NULL;
	PBKDF2PARAM *kdf = NULL;
	ASN1_OCTET_STRING *osalt = NULL;

	if (!(kdf = PBKDF2PARAM_new()))
		goto merr;
	if (!(osalt = ASN1_OCTET_STRING_new()))
		goto merr;

	kdf->salt->value.octet_string = osalt;
	kdf->salt->type = V_ASN1_OCTET_STRING;

	if (!saltlen)
		saltlen = PKCS5_PBE2_SALT_LEN;
	if (!(osalt->data = malloc (saltlen)))
		goto merr;

	osalt->length = saltlen;

	if (salt)
		memcpy (osalt->data, salt, saltlen);
	else
		arc4random_buf(osalt->data, saltlen);

	if (iter <= 0)
		iter = PKCS5_DEFAULT_ITER;

	if (!ASN1_INTEGER_set(kdf->iter, iter))
		goto merr;

	/* If have a key len set it up */

	if (keylen > 0) {
		if (!(kdf->keylength = ASN1_INTEGER_new()))
			goto merr;
		if (!ASN1_INTEGER_set(kdf->keylength, keylen))
			goto merr;
	}

	/* prf can stay NULL if we are using hmacWithSHA1 */
	if (prf_nid > 0 && prf_nid != NID_hmacWithSHA1) {
		kdf->prf = X509_ALGOR_new();
		if (!kdf->prf)
			goto merr;
		X509_ALGOR_set0(kdf->prf, OBJ_nid2obj(prf_nid),
		V_ASN1_NULL, NULL);
	}

	/* Finally setup the keyfunc structure */

	keyfunc = X509_ALGOR_new();
	if (!keyfunc)
		goto merr;

	keyfunc->algorithm = OBJ_nid2obj(NID_id_pbkdf2);

	/* Encode PBKDF2PARAM into parameter of pbe2 */

	if (!(keyfunc->parameter = ASN1_TYPE_new()))
		goto merr;

	if (!ASN1_item_pack(kdf, &PBKDF2PARAM_it,
		&keyfunc->parameter->value.sequence))
		goto merr;
	keyfunc->parameter->type = V_ASN1_SEQUENCE;

	PBKDF2PARAM_free(kdf);
	return keyfunc;

 merr:
	ASN1error(ERR_R_MALLOC_FAILURE);
	PBKDF2PARAM_free(kdf);
	X509_ALGOR_free(keyfunc);
	return NULL;
}

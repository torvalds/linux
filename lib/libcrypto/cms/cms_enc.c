/* $OpenBSD: cms_enc.c,v 1.26 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
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
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "cms_local.h"
#include "err_local.h"
#include "evp_local.h"

/* CMS EncryptedData Utilities */

/* Return BIO based on EncryptedContentInfo and key */

BIO *
cms_EncryptedContent_init_bio(CMS_EncryptedContentInfo *ec)
{
	BIO *b;
	EVP_CIPHER_CTX *ctx;
	const EVP_CIPHER *ciph;
	X509_ALGOR *calg = ec->contentEncryptionAlgorithm;
	unsigned char iv[EVP_MAX_IV_LENGTH], *piv = NULL;
	unsigned char *tkey = NULL;
	size_t tkeylen = 0;

	int ok = 0;

	int enc, keep_key = 0;

	enc = ec->cipher ? 1 : 0;

	b = BIO_new(BIO_f_cipher());
	if (b == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	BIO_get_cipher_ctx(b, &ctx);

	if (enc) {
		ciph = ec->cipher;
		/*
		 * If not keeping key set cipher to NULL so subsequent calls decrypt.
		 */
		if (ec->key)
			ec->cipher = NULL;
	} else {
		ciph = EVP_get_cipherbyobj(calg->algorithm);

		if (!ciph) {
			CMSerror(CMS_R_UNKNOWN_CIPHER);
			goto err;
		}
	}

	if (EVP_CipherInit_ex(ctx, ciph, NULL, NULL, NULL, enc) <= 0) {
		CMSerror(CMS_R_CIPHER_INITIALISATION_ERROR);
		goto err;
	}

	if (enc) {
		int ivlen;
		calg->algorithm = OBJ_nid2obj(EVP_CIPHER_CTX_type(ctx));
		/* Generate a random IV if we need one */
		ivlen = EVP_CIPHER_CTX_iv_length(ctx);
		if (ivlen > 0) {
			arc4random_buf(iv, ivlen);
			piv = iv;
		}
	} else if (EVP_CIPHER_asn1_to_param(ctx, calg->parameter) <= 0) {
		CMSerror(CMS_R_CIPHER_PARAMETER_INITIALISATION_ERROR);
		goto err;
	}
	tkeylen = EVP_CIPHER_CTX_key_length(ctx);
	/* Generate random session key */
	if (!enc || !ec->key) {
		tkey = malloc(tkeylen);
		if (tkey == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (EVP_CIPHER_CTX_rand_key(ctx, tkey) <= 0)
			goto err;
	}

	if (!ec->key) {
		ec->key = tkey;
		ec->keylen = tkeylen;
		tkey = NULL;
		if (enc)
			keep_key = 1;
		else
			ERR_clear_error();

	}

	if (ec->keylen != tkeylen) {
		/* If necessary set key length */
		if (!EVP_CIPHER_CTX_set_key_length(ctx, ec->keylen)) {
			/*
			 * Only reveal failure if debugging so we don't leak information
			 * which may be useful in MMA.
			 */
			if (enc || ec->debug) {
			    CMSerror(CMS_R_INVALID_KEY_LENGTH);
			    goto err;
			} else {
			    /* Use random key */
			    freezero(ec->key, ec->keylen);
			    ec->key = tkey;
			    ec->keylen = tkeylen;
			    tkey = NULL;
			    ERR_clear_error();
			}
		}
	}

	if (EVP_CipherInit_ex(ctx, NULL, NULL, ec->key, piv, enc) <= 0) {
		CMSerror(CMS_R_CIPHER_INITIALISATION_ERROR);
		goto err;
	}
	if (enc) {
		calg->parameter = ASN1_TYPE_new();
		if (calg->parameter == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (EVP_CIPHER_param_to_asn1(ctx, calg->parameter) <= 0) {
			CMSerror(CMS_R_CIPHER_PARAMETER_INITIALISATION_ERROR);
			goto err;
		}
		/* If parameter type not set omit parameter */
		if (calg->parameter->type == V_ASN1_UNDEF) {
			ASN1_TYPE_free(calg->parameter);
			calg->parameter = NULL;
		}
	}
	ok = 1;

 err:
	if (!keep_key || !ok) {
		freezero(ec->key, ec->keylen);
		ec->key = NULL;
	}
	freezero(tkey, tkeylen);
	if (ok)
		return b;
	BIO_free(b);
	return NULL;
}

int
cms_EncryptedContent_init(CMS_EncryptedContentInfo *ec,
    const EVP_CIPHER *cipher, const unsigned char *key, size_t keylen)
{
	ec->cipher = cipher;
	if (key) {
		if ((ec->key = malloc(keylen)) == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(ec->key, key, keylen);
	}
	ec->keylen = keylen;
	if (cipher)
		ec->contentType = OBJ_nid2obj(NID_pkcs7_data);

	return 1;
}

int
CMS_EncryptedData_set1_key(CMS_ContentInfo *cms, const EVP_CIPHER *ciph,
    const unsigned char *key, size_t keylen)
{
	CMS_EncryptedContentInfo *ec;

	if (!key || !keylen) {
		CMSerror(CMS_R_NO_KEY);
		return 0;
	}
	if (ciph) {
		cms->d.encryptedData = (CMS_EncryptedData *)ASN1_item_new(&CMS_EncryptedData_it);
		if (!cms->d.encryptedData) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		cms->contentType = OBJ_nid2obj(NID_pkcs7_encrypted);
		cms->d.encryptedData->version = 0;
	} else if (OBJ_obj2nid(cms->contentType) != NID_pkcs7_encrypted) {
		CMSerror(CMS_R_NOT_ENCRYPTED_DATA);
		return 0;
	}
	ec = cms->d.encryptedData->encryptedContentInfo;

	return cms_EncryptedContent_init(ec, ciph, key, keylen);
}
LCRYPTO_ALIAS(CMS_EncryptedData_set1_key);

BIO *
cms_EncryptedData_init_bio(CMS_ContentInfo *cms)
{
	CMS_EncryptedData *enc = cms->d.encryptedData;

	if (enc->encryptedContentInfo->cipher && enc->unprotectedAttrs)
		enc->version = 2;

	return cms_EncryptedContent_init_bio(enc->encryptedContentInfo);
}

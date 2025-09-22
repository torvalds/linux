/* $OpenBSD: pk7_doit.c,v 1.61 2025/07/27 07:06:41 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

static int
PKCS7_type_is_other(PKCS7* p7)
{
	int isOther = 1;

	int nid = OBJ_obj2nid(p7->type);

	switch (nid ) {
	case NID_pkcs7_data:
	case NID_pkcs7_signed:
	case NID_pkcs7_enveloped:
	case NID_pkcs7_signedAndEnveloped:
	case NID_pkcs7_digest:
	case NID_pkcs7_encrypted:
		isOther = 0;
		break;
	default:
		isOther = 1;
	}

	return isOther;

}

ASN1_OCTET_STRING *
PKCS7_get_octet_string(PKCS7 *p7)
{
	if (PKCS7_type_is_data(p7))
		return p7->d.data;
	if (PKCS7_type_is_other(p7) && p7->d.other &&
	    (p7->d.other->type == V_ASN1_OCTET_STRING))
		return p7->d.other->value.octet_string;
	return NULL;
}

static int
PKCS7_bio_add_digest(BIO **pbio, X509_ALGOR *alg)
{
	BIO *btmp;
	const EVP_MD *md;

	if ((btmp = BIO_new(BIO_f_md())) == NULL) {
		PKCS7error(ERR_R_BIO_LIB);
		goto err;
	}

	md = EVP_get_digestbyobj(alg->algorithm);
	if (md == NULL) {
		PKCS7error(PKCS7_R_UNKNOWN_DIGEST_TYPE);
		goto err;
	}

	if (BIO_set_md(btmp, md) <= 0) {
		PKCS7error(ERR_R_BIO_LIB);
		goto err;
	}

	if (*pbio == NULL)
		*pbio = btmp;
	else if (!BIO_push(*pbio, btmp)) {
		PKCS7error(ERR_R_BIO_LIB);
		goto err;
	}
	btmp = NULL;

	return 1;

err:
	BIO_free(btmp);
	return 0;

}

static int
pkcs7_encode_rinfo(PKCS7_RECIP_INFO *ri, unsigned char *key, int keylen)
{
	EVP_PKEY_CTX *pctx = NULL;
	EVP_PKEY *pkey = NULL;
	unsigned char *ek = NULL;
	int ret = 0;
	size_t eklen;

	pkey = X509_get_pubkey(ri->cert);
	if (!pkey)
		return 0;

	pctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (!pctx)
		return 0;

	if (EVP_PKEY_encrypt_init(pctx) <= 0)
		goto err;

	if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_ENCRYPT,
	    EVP_PKEY_CTRL_PKCS7_ENCRYPT, 0, ri) <= 0) {
		PKCS7error(PKCS7_R_CTRL_ERROR);
		goto err;
	}

	if (EVP_PKEY_encrypt(pctx, NULL, &eklen, key, keylen) <= 0)
		goto err;

	ek = malloc(eklen);

	if (ek == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (EVP_PKEY_encrypt(pctx, ek, &eklen, key, keylen) <= 0)
		goto err;

	ASN1_STRING_set0(ri->enc_key, ek, eklen);
	ek = NULL;

	ret = 1;

err:
	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(pctx);
	free(ek);
	return ret;
}


static int
pkcs7_decrypt_rinfo(unsigned char **pek, int *peklen, PKCS7_RECIP_INFO *ri,
    EVP_PKEY *pkey, size_t fixlen)
{
	EVP_PKEY_CTX *pctx = NULL;
	unsigned char *ek = NULL;
	size_t eklen;

	int ret = -1;

	pctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (!pctx)
		return -1;

	if (EVP_PKEY_decrypt_init(pctx) <= 0)
		goto err;

	if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_DECRYPT,
	    EVP_PKEY_CTRL_PKCS7_DECRYPT, 0, ri) <= 0) {
		PKCS7error(PKCS7_R_CTRL_ERROR);
		goto err;
	}

	if (EVP_PKEY_decrypt(pctx, NULL, &eklen,
	    ri->enc_key->data, ri->enc_key->length) <= 0)
		goto err;

	ek = malloc(eklen);
	if (ek == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (EVP_PKEY_decrypt(pctx, ek, &eklen, ri->enc_key->data,
	    ri->enc_key->length) <= 0 || eklen == 0 ||
	    (fixlen != 0 && eklen != fixlen)) {
		ret = 0;
		PKCS7error(ERR_R_EVP_LIB);
		goto err;
	}

	ret = 1;

	freezero(*pek, *peklen);

	*pek = ek;
	*peklen = eklen;

err:
	EVP_PKEY_CTX_free(pctx);
	if (!ret && ek)
		free(ek);

	return ret;
}

BIO *
PKCS7_dataInit(PKCS7 *p7, BIO *bio)
{
	int i;
	BIO *out = NULL, *btmp = NULL;
	X509_ALGOR *xa = NULL;
	const EVP_CIPHER *evp_cipher = NULL;
	STACK_OF(X509_ALGOR) *md_sk = NULL;
	STACK_OF(PKCS7_RECIP_INFO) *rsk = NULL;
	X509_ALGOR *xalg = NULL;
	PKCS7_RECIP_INFO *ri = NULL;
	ASN1_OCTET_STRING *os = NULL;

	if (p7 == NULL) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return NULL;
	}

	/*
	 * The content field in the PKCS7 ContentInfo is optional,
	 * but that really only applies to inner content (precisely,
	 * detached signatures).
	 *
	 * When reading content, missing outer content is therefore
	 * treated as an error.
	 *
	 * When creating content, PKCS7_content_new() must be called
	 * before calling this method, so a NULL p7->d is always
	 * an error.
	 */
	if (p7->d.ptr == NULL) {
		PKCS7error(PKCS7_R_NO_CONTENT);
		return NULL;
	}

	i = OBJ_obj2nid(p7->type);
	p7->state = PKCS7_S_HEADER;

	switch (i) {
	case NID_pkcs7_signed:
		md_sk = p7->d.sign->md_algs;
		os = PKCS7_get_octet_string(p7->d.sign->contents);
		break;
	case NID_pkcs7_signedAndEnveloped:
		rsk = p7->d.signed_and_enveloped->recipientinfo;
		md_sk = p7->d.signed_and_enveloped->md_algs;
		xalg = p7->d.signed_and_enveloped->enc_data->algorithm;
		evp_cipher = p7->d.signed_and_enveloped->enc_data->cipher;
		if (evp_cipher == NULL) {
			PKCS7error(PKCS7_R_CIPHER_NOT_INITIALIZED);
			goto err;
		}
		break;
	case NID_pkcs7_enveloped:
		rsk = p7->d.enveloped->recipientinfo;
		xalg = p7->d.enveloped->enc_data->algorithm;
		evp_cipher = p7->d.enveloped->enc_data->cipher;
		if (evp_cipher == NULL) {
			PKCS7error(PKCS7_R_CIPHER_NOT_INITIALIZED);
			goto err;
		}
		break;
	case NID_pkcs7_digest:
		xa = p7->d.digest->md;
		os = PKCS7_get_octet_string(p7->d.digest->contents);
		break;
	case NID_pkcs7_data:
		break;
	default:
		PKCS7error(PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
	}

	for (i = 0; i < sk_X509_ALGOR_num(md_sk); i++)
		if (!PKCS7_bio_add_digest(&out, sk_X509_ALGOR_value(md_sk, i)))
			goto err;

	if (xa && !PKCS7_bio_add_digest(&out, xa))
		goto err;

	if (evp_cipher != NULL) {
		unsigned char key[EVP_MAX_KEY_LENGTH];
		unsigned char iv[EVP_MAX_IV_LENGTH];
		int keylen, ivlen;
		EVP_CIPHER_CTX *ctx;

		if ((btmp = BIO_new(BIO_f_cipher())) == NULL) {
			PKCS7error(ERR_R_BIO_LIB);
			goto err;
		}
		BIO_get_cipher_ctx(btmp, &ctx);
		keylen = EVP_CIPHER_key_length(evp_cipher);
		ivlen = EVP_CIPHER_iv_length(evp_cipher);
		xalg->algorithm = OBJ_nid2obj(EVP_CIPHER_type(evp_cipher));
		if (ivlen > 0)
			arc4random_buf(iv, ivlen);
		if (EVP_CipherInit_ex(ctx, evp_cipher, NULL, NULL,
		    NULL, 1) <= 0)
			goto err;
		if (EVP_CIPHER_CTX_rand_key(ctx, key) <= 0)
			goto err;
		if (EVP_CipherInit_ex(ctx, NULL, NULL, key, iv, 1) <= 0)
			goto err;

		if (ivlen > 0) {
			if (xalg->parameter == NULL) {
				xalg->parameter = ASN1_TYPE_new();
				if (xalg->parameter == NULL)
					goto err;
			}
			if (EVP_CIPHER_param_to_asn1(ctx, xalg->parameter) < 0)
				goto err;
		}

		/* Lets do the pub key stuff :-) */
		for (i = 0; i < sk_PKCS7_RECIP_INFO_num(rsk); i++) {
			ri = sk_PKCS7_RECIP_INFO_value(rsk, i);
			if (pkcs7_encode_rinfo(ri, key, keylen) <= 0)
				goto err;
		}
		explicit_bzero(key, keylen);

		if (out == NULL)
			out = btmp;
		else
			BIO_push(out, btmp);
		btmp = NULL;
	}

	if (bio == NULL) {
		if (PKCS7_is_detached(p7))
			bio = BIO_new(BIO_s_null());
		else if (os && os->length > 0)
			bio = BIO_new_mem_buf(os->data, os->length);
		if (bio == NULL) {
			bio = BIO_new(BIO_s_mem());
			if (bio == NULL)
				goto err;
			BIO_set_mem_eof_return(bio, 0);
		}
	}
	if (out)
		BIO_push(out, bio);
	else
		out = bio;
	bio = NULL;
	if (0) {
err:
		if (out != NULL)
			BIO_free_all(out);
		if (btmp != NULL)
			BIO_free_all(btmp);
		out = NULL;
	}
	return out;
}
LCRYPTO_ALIAS(PKCS7_dataInit);

static int
pkcs7_cmp_ri(PKCS7_RECIP_INFO *ri, X509 *pcert)
{
	int ret;

	ret = X509_NAME_cmp(ri->issuer_and_serial->issuer,
	    pcert->cert_info->issuer);
	if (ret)
		return ret;
	return ASN1_INTEGER_cmp(pcert->cert_info->serialNumber,
	    ri->issuer_and_serial->serial);
}

/* int */
BIO *
PKCS7_dataDecode(PKCS7 *p7, EVP_PKEY *pkey, BIO *in_bio, X509 *pcert)
{
	int i, j;
	BIO *out = NULL, *btmp = NULL, *etmp = NULL, *bio = NULL;
	X509_ALGOR *xa;
	ASN1_OCTET_STRING *data_body = NULL;
	const EVP_MD *evp_md;
	const EVP_CIPHER *evp_cipher = NULL;
	EVP_CIPHER_CTX *evp_ctx = NULL;
	X509_ALGOR *enc_alg = NULL;
	STACK_OF(X509_ALGOR) *md_sk = NULL;
	STACK_OF(PKCS7_RECIP_INFO) *rsk = NULL;
	PKCS7_RECIP_INFO *ri = NULL;
	unsigned char *ek = NULL, *tkey = NULL;
	int eklen = 0, tkeylen = 0;

	if (p7 == NULL) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return NULL;
	}

	if (p7->d.ptr == NULL) {
		PKCS7error(PKCS7_R_NO_CONTENT);
		return NULL;
	}

	i = OBJ_obj2nid(p7->type);
	p7->state = PKCS7_S_HEADER;

	switch (i) {
	case NID_pkcs7_signed:
		data_body = PKCS7_get_octet_string(p7->d.sign->contents);
		md_sk = p7->d.sign->md_algs;
		break;
	case NID_pkcs7_signedAndEnveloped:
		rsk = p7->d.signed_and_enveloped->recipientinfo;
		md_sk = p7->d.signed_and_enveloped->md_algs;
		data_body = p7->d.signed_and_enveloped->enc_data->enc_data;
		enc_alg = p7->d.signed_and_enveloped->enc_data->algorithm;
		evp_cipher = EVP_get_cipherbyobj(enc_alg->algorithm);
		if (evp_cipher == NULL) {
			PKCS7error(PKCS7_R_UNSUPPORTED_CIPHER_TYPE);
			goto err;
		}
		break;
	case NID_pkcs7_enveloped:
		rsk = p7->d.enveloped->recipientinfo;
		enc_alg = p7->d.enveloped->enc_data->algorithm;
		data_body = p7->d.enveloped->enc_data->enc_data;
		evp_cipher = EVP_get_cipherbyobj(enc_alg->algorithm);
		if (evp_cipher == NULL) {
			PKCS7error(PKCS7_R_UNSUPPORTED_CIPHER_TYPE);
			goto err;
		}
		break;
	default:
		PKCS7error(PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
	}

	/* We will be checking the signature */
	if (md_sk != NULL) {
		for (i = 0; i < sk_X509_ALGOR_num(md_sk); i++) {
			xa = sk_X509_ALGOR_value(md_sk, i);
			if ((btmp = BIO_new(BIO_f_md())) == NULL) {
				PKCS7error(ERR_R_BIO_LIB);
				goto err;
			}

			j = OBJ_obj2nid(xa->algorithm);
			evp_md = EVP_get_digestbynid(j);
			if (evp_md == NULL) {
				PKCS7error(PKCS7_R_UNKNOWN_DIGEST_TYPE);
				goto err;
			}

			if (BIO_set_md(btmp, evp_md) <= 0) {
				PKCS7error(ERR_R_BIO_LIB);
				goto err;
			}
			if (out == NULL)
				out = btmp;
			else
				BIO_push(out, btmp);
			btmp = NULL;
		}
	}

	if (evp_cipher != NULL) {
		if ((etmp = BIO_new(BIO_f_cipher())) == NULL) {
			PKCS7error(ERR_R_BIO_LIB);
			goto err;
		}

		/* It was encrypted, we need to decrypt the secret key
		 * with the private key */

		/* Find the recipientInfo which matches the passed certificate
		 * (if any)
		 */
		if (pcert) {
			for (i = 0; i < sk_PKCS7_RECIP_INFO_num(rsk); i++) {
				ri = sk_PKCS7_RECIP_INFO_value(rsk, i);
				if (!pkcs7_cmp_ri(ri, pcert))
					break;
				ri = NULL;
			}
			if (ri == NULL) {
				PKCS7error(PKCS7_R_NO_RECIPIENT_MATCHES_CERTIFICATE);
				goto err;
			}
		}

		/* If we haven't got a certificate try each ri in turn */
		if (pcert == NULL) {
			/* Always attempt to decrypt all rinfo even
			 * after success as a defence against MMA timing
			 * attacks.
			 */
			for (i = 0; i < sk_PKCS7_RECIP_INFO_num(rsk); i++) {
				ri = sk_PKCS7_RECIP_INFO_value(rsk, i);

				if (pkcs7_decrypt_rinfo(&ek, &eklen, ri, pkey,
				    EVP_CIPHER_key_length(evp_cipher)) < 0)
					goto err;
				ERR_clear_error();
			}
		} else {
			/* Only exit on fatal errors, not decrypt failure */
			if (pkcs7_decrypt_rinfo(&ek, &eklen, ri, pkey, 0) < 0)
				goto err;
			ERR_clear_error();
		}

		evp_ctx = NULL;
		BIO_get_cipher_ctx(etmp, &evp_ctx);
		if (EVP_CipherInit_ex(evp_ctx, evp_cipher, NULL, NULL,
		    NULL, 0) <= 0)
			goto err;
		if (EVP_CIPHER_asn1_to_param(evp_ctx, enc_alg->parameter) < 0)
			goto err;
		/* Generate random key as MMA defence */
		tkeylen = EVP_CIPHER_CTX_key_length(evp_ctx);
		tkey = malloc(tkeylen);
		if (!tkey)
			goto err;
		if (EVP_CIPHER_CTX_rand_key(evp_ctx, tkey) <= 0)
			goto err;
		if (ek == NULL) {
			ek = tkey;
			eklen = tkeylen;
			tkey = NULL;
		}

		if (eklen != EVP_CIPHER_CTX_key_length(evp_ctx)) {
			/* Some S/MIME clients don't use the same key
			 * and effective key length. The key length is
			 * determined by the size of the decrypted RSA key.
			 */
			if (!EVP_CIPHER_CTX_set_key_length(evp_ctx, eklen)) {
				/* Use random key as MMA defence */
				freezero(ek, eklen);
				ek = tkey;
				eklen = tkeylen;
				tkey = NULL;
			}
		}
		/* Clear errors so we don't leak information useful in MMA */
		ERR_clear_error();
		if (EVP_CipherInit_ex(evp_ctx, NULL, NULL, ek, NULL, 0) <= 0)
			goto err;

		freezero(ek, eklen);
		ek = NULL;
		freezero(tkey, tkeylen);
		tkey = NULL;

		if (out == NULL)
			out = etmp;
		else
			BIO_push(out, etmp);
		etmp = NULL;
	}

	if (PKCS7_is_detached(p7) || (in_bio != NULL)) {
		bio = in_bio;
	} else {
		if (data_body != NULL && data_body->length > 0)
			bio = BIO_new_mem_buf(data_body->data, data_body->length);
		else {
			bio = BIO_new(BIO_s_mem());
			BIO_set_mem_eof_return(bio, 0);
		}
		if (bio == NULL)
			goto err;
	}
	BIO_push(out, bio);

	if (0) {
err:
		freezero(ek, eklen);
		freezero(tkey, tkeylen);
		if (out != NULL)
			BIO_free_all(out);
		if (btmp != NULL)
			BIO_free_all(btmp);
		if (etmp != NULL)
			BIO_free_all(etmp);
		out = NULL;
	}
	return out;
}
LCRYPTO_ALIAS(PKCS7_dataDecode);

static BIO *
PKCS7_find_digest(EVP_MD_CTX **pmd, BIO *bio, int nid)
{
	for (;;) {
		bio = BIO_find_type(bio, BIO_TYPE_MD);
		if (bio == NULL) {
			PKCS7error(PKCS7_R_UNABLE_TO_FIND_MESSAGE_DIGEST);
			return NULL;
		}
		BIO_get_md_ctx(bio, pmd);
		if (*pmd == NULL) {
			PKCS7error(ERR_R_INTERNAL_ERROR);
			return NULL;
		}
		if (EVP_MD_CTX_type(*pmd) == nid)
			return bio;
		bio = BIO_next(bio);
	}
	return NULL;
}

static int
do_pkcs7_signed_attrib(PKCS7_SIGNER_INFO *si, EVP_MD_CTX *mctx)
{
	unsigned char md_data[EVP_MAX_MD_SIZE];
	unsigned int md_len;

	/* Add signing time if not already present */
	if (!PKCS7_get_signed_attribute(si, NID_pkcs9_signingTime)) {
		if (!PKCS7_add0_attrib_signing_time(si, NULL)) {
			PKCS7error(ERR_R_MALLOC_FAILURE);
			return 0;
		}
	}

	/* Add digest */
	if (!EVP_DigestFinal_ex(mctx, md_data, &md_len)) {
		PKCS7error(ERR_R_EVP_LIB);
		return 0;
	}
	if (!PKCS7_add1_attrib_digest(si, md_data, md_len)) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	/* Now sign the attributes */
	if (!PKCS7_SIGNER_INFO_sign(si))
		return 0;

	return 1;
}


int
PKCS7_dataFinal(PKCS7 *p7, BIO *bio)
{
	int ret = 0;
	int i, j;
	BIO *btmp;
	PKCS7_SIGNER_INFO *si;
	EVP_MD_CTX *mdc, ctx_tmp;
	STACK_OF(X509_ATTRIBUTE) *sk;
	STACK_OF(PKCS7_SIGNER_INFO) *si_sk = NULL;
	ASN1_OCTET_STRING *os = NULL;

	if (p7 == NULL) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return 0;
	}

	if (p7->d.ptr == NULL) {
		PKCS7error(PKCS7_R_NO_CONTENT);
		return 0;
	}

	EVP_MD_CTX_legacy_clear(&ctx_tmp);
	i = OBJ_obj2nid(p7->type);
	p7->state = PKCS7_S_HEADER;

	switch (i) {
	case NID_pkcs7_data:
		os = p7->d.data;
		break;
	case NID_pkcs7_signedAndEnveloped:
		/* XXX */
		si_sk = p7->d.signed_and_enveloped->signer_info;
		os = p7->d.signed_and_enveloped->enc_data->enc_data;
		if (!os) {
			os = ASN1_OCTET_STRING_new();
			if (!os) {
				PKCS7error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			p7->d.signed_and_enveloped->enc_data->enc_data = os;
		}
		break;
	case NID_pkcs7_enveloped:
		/* XXX */
		os = p7->d.enveloped->enc_data->enc_data;
		if (!os) {
			os = ASN1_OCTET_STRING_new();
			if (!os) {
				PKCS7error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			p7->d.enveloped->enc_data->enc_data = os;
		}
		break;
	case NID_pkcs7_signed:
		si_sk = p7->d.sign->signer_info;
		os = PKCS7_get_octet_string(p7->d.sign->contents);
		if (!PKCS7_is_detached(p7) && os == NULL) {
			PKCS7error(PKCS7_R_DECODE_ERROR);
			goto err;
		}
		/* If detached data then the content is excluded */
		if (PKCS7_type_is_data(p7->d.sign->contents) && p7->detached) {
			ASN1_OCTET_STRING_free(os);
			os = NULL;
			p7->d.sign->contents->d.data = NULL;
		}
		break;

	case NID_pkcs7_digest:
		os = PKCS7_get_octet_string(p7->d.digest->contents);
		if (os == NULL) {
			PKCS7error(PKCS7_R_DECODE_ERROR);
			goto err;
		}
		/* If detached data then the content is excluded */
		if (PKCS7_type_is_data(p7->d.digest->contents) &&
		    p7->detached) {
			ASN1_OCTET_STRING_free(os);
			os = NULL;
			p7->d.digest->contents->d.data = NULL;
		}
		break;

	default:
		PKCS7error(PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
	}

	if (si_sk != NULL) {
		for (i = 0; i < sk_PKCS7_SIGNER_INFO_num(si_sk); i++) {
			si = sk_PKCS7_SIGNER_INFO_value(si_sk, i);
			if (si->pkey == NULL)
				continue;

			j = OBJ_obj2nid(si->digest_alg->algorithm);

			if ((btmp = PKCS7_find_digest(&mdc, bio, j)) == NULL)
				goto err;

			/* We now have the EVP_MD_CTX, lets do the
			 * signing. */
			if (!EVP_MD_CTX_copy_ex(&ctx_tmp, mdc))
				goto err;

			sk = si->auth_attr;

			/* If there are attributes, we add the digest
			 * attribute and only sign the attributes */
			if (sk_X509_ATTRIBUTE_num(sk) > 0) {
				if (!do_pkcs7_signed_attrib(si, &ctx_tmp))
					goto err;
			} else {
				unsigned char *abuf = NULL;
				unsigned int abuflen;
				abuflen = EVP_PKEY_size(si->pkey);
				abuf = malloc(abuflen);
				if (!abuf)
					goto err;

				if (!EVP_SignFinal(&ctx_tmp, abuf, &abuflen,
				    si->pkey)) {
					PKCS7error(ERR_R_EVP_LIB);
					free(abuf);
					goto err;
				}
				ASN1_STRING_set0(si->enc_digest, abuf, abuflen);
			}
		}
	} else if (i == NID_pkcs7_digest) {
		unsigned char md_data[EVP_MAX_MD_SIZE];
		unsigned int md_len;

		if (!PKCS7_find_digest(&mdc, bio,
		    OBJ_obj2nid(p7->d.digest->md->algorithm)))
			goto err;
		if (!EVP_DigestFinal_ex(mdc, md_data, &md_len))
			goto err;
		if (ASN1_STRING_set(p7->d.digest->digest, md_data,
		    md_len) == 0)
			goto err;
	}

	if (!PKCS7_is_detached(p7)) {
		/*
		 * NOTE: only reach os == NULL here because detached
		 * digested data support is broken?
		 */
		if (os == NULL)
			goto err;
		if (!(os->flags & ASN1_STRING_FLAG_NDEF)) {
			char *cont;
			long contlen;

			btmp = BIO_find_type(bio, BIO_TYPE_MEM);
			if (btmp == NULL) {
				PKCS7error(PKCS7_R_UNABLE_TO_FIND_MEM_BIO);
				goto err;
			}
			contlen = BIO_get_mem_data(btmp, &cont);
			/*
			 * Mark the BIO read only then we can use its copy
			 * of the data instead of making an extra copy.
			 */
			BIO_set_flags(btmp, BIO_FLAGS_MEM_RDONLY);
			BIO_set_mem_eof_return(btmp, 0);
			ASN1_STRING_set0(os, (unsigned char *)cont, contlen);
		}
	}
	ret = 1;
err:
	EVP_MD_CTX_cleanup(&ctx_tmp);
	return ret;
}
LCRYPTO_ALIAS(PKCS7_dataFinal);

int
PKCS7_SIGNER_INFO_sign(PKCS7_SIGNER_INFO *si)
{
	EVP_MD_CTX mctx;
	EVP_PKEY_CTX *pctx;
	unsigned char *abuf = NULL;
	int alen;
	size_t siglen;
	const EVP_MD *md = NULL;

	md = EVP_get_digestbyobj(si->digest_alg->algorithm);
	if (md == NULL)
		return 0;

	EVP_MD_CTX_legacy_clear(&mctx);
	if (EVP_DigestSignInit(&mctx, &pctx, md, NULL, si->pkey) <= 0)
		goto err;

	if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_SIGN,
	    EVP_PKEY_CTRL_PKCS7_SIGN, 0, si) <= 0) {
		PKCS7error(PKCS7_R_CTRL_ERROR);
		goto err;
	}

	alen = ASN1_item_i2d((ASN1_VALUE *)si->auth_attr, &abuf,
	    &PKCS7_ATTR_SIGN_it);
	if (!abuf)
		goto err;
	if (EVP_DigestSignUpdate(&mctx, abuf, alen) <= 0)
		goto err;
	free(abuf);
	abuf = NULL;
	if (EVP_DigestSignFinal(&mctx, NULL, &siglen) <= 0)
		goto err;
	abuf = malloc(siglen);
	if (!abuf)
		goto err;
	if (EVP_DigestSignFinal(&mctx, abuf, &siglen) <= 0)
		goto err;

	if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_SIGN,
	    EVP_PKEY_CTRL_PKCS7_SIGN, 1, si) <= 0) {
		PKCS7error(PKCS7_R_CTRL_ERROR);
		goto err;
	}

	EVP_MD_CTX_cleanup(&mctx);

	ASN1_STRING_set0(si->enc_digest, abuf, siglen);

	return 1;

err:
	free(abuf);
	EVP_MD_CTX_cleanup(&mctx);
	return 0;
}
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_sign);

int
PKCS7_dataVerify(X509_STORE *cert_store, X509_STORE_CTX *ctx, BIO *bio,
    PKCS7 *p7, PKCS7_SIGNER_INFO *si)
{
	PKCS7_ISSUER_AND_SERIAL *ias;
	int ret = 0, i;
	STACK_OF(X509) *cert;
	X509 *x509;

	if (p7 == NULL) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return 0;
	}

	if (p7->d.ptr == NULL) {
		PKCS7error(PKCS7_R_NO_CONTENT);
		return 0;
	}

	if (PKCS7_type_is_signed(p7)) {
		cert = p7->d.sign->cert;
	} else if (PKCS7_type_is_signedAndEnveloped(p7)) {
		cert = p7->d.signed_and_enveloped->cert;
	} else {
		PKCS7error(PKCS7_R_WRONG_PKCS7_TYPE);
		goto err;
	}
	/* XXXX */
	ias = si->issuer_and_serial;

	x509 = X509_find_by_issuer_and_serial(cert, ias->issuer, ias->serial);

	/* were we able to find the cert in passed to us */
	if (x509 == NULL) {
		PKCS7error(PKCS7_R_UNABLE_TO_FIND_CERTIFICATE);
		goto err;
	}

	/* Lets verify */
	if (!X509_STORE_CTX_init(ctx, cert_store, x509, cert)) {
		PKCS7error(ERR_R_X509_LIB);
		goto err;
	}
	if (X509_STORE_CTX_set_purpose(ctx, X509_PURPOSE_SMIME_SIGN) == 0) {
		X509_STORE_CTX_cleanup(ctx);
		goto err;
	}
	i = X509_verify_cert(ctx);
	if (i <= 0) {
		PKCS7error(ERR_R_X509_LIB);
		X509_STORE_CTX_cleanup(ctx);
		goto err;
	}
	X509_STORE_CTX_cleanup(ctx);

	return PKCS7_signatureVerify(bio, p7, si, x509);

err:
	return ret;
}
LCRYPTO_ALIAS(PKCS7_dataVerify);

int
PKCS7_signatureVerify(BIO *bio, PKCS7 *p7, PKCS7_SIGNER_INFO *si, X509 *x509)
{
	ASN1_OCTET_STRING *os;
	EVP_MD_CTX mdc_tmp, *mdc;
	int ret = 0, i;
	int md_type;
	STACK_OF(X509_ATTRIBUTE) *sk;
	BIO *btmp;
	EVP_PKEY *pkey;

	EVP_MD_CTX_legacy_clear(&mdc_tmp);

	if (!PKCS7_type_is_signed(p7) &&
	    !PKCS7_type_is_signedAndEnveloped(p7)) {
		PKCS7error(PKCS7_R_WRONG_PKCS7_TYPE);
		goto err;
	}

	md_type = OBJ_obj2nid(si->digest_alg->algorithm);

	btmp = bio;
	for (;;) {
		if ((btmp == NULL) ||
		    ((btmp = BIO_find_type(btmp, BIO_TYPE_MD)) == NULL)) {
			PKCS7error(PKCS7_R_UNABLE_TO_FIND_MESSAGE_DIGEST);
			goto err;
		}
		BIO_get_md_ctx(btmp, &mdc);
		if (mdc == NULL) {
			PKCS7error(ERR_R_INTERNAL_ERROR);
			goto err;
		}
		if (EVP_MD_CTX_type(mdc) == md_type)
			break;
		/* Workaround for some broken clients that put the signature
		 * OID instead of the digest OID in digest_alg->algorithm
		 */
		if (EVP_MD_pkey_type(EVP_MD_CTX_md(mdc)) == md_type)
			break;
		btmp = BIO_next(btmp);
	}

	/* mdc is the digest ctx that we want, unless there are attributes,
	 * in which case the digest is the signed attributes */
	if (!EVP_MD_CTX_copy_ex(&mdc_tmp, mdc))
		goto err;

	sk = si->auth_attr;
	if ((sk != NULL) && (sk_X509_ATTRIBUTE_num(sk) != 0)) {
		unsigned char md_dat[EVP_MAX_MD_SIZE], *abuf = NULL;
		unsigned int md_len;
		int alen;
		ASN1_OCTET_STRING *message_digest;

		if (!EVP_DigestFinal_ex(&mdc_tmp, md_dat, &md_len))
			goto err;
		message_digest = PKCS7_digest_from_attributes(sk);
		if (!message_digest) {
			PKCS7error(PKCS7_R_UNABLE_TO_FIND_MESSAGE_DIGEST);
			goto err;
		}
		if ((message_digest->length != (int)md_len) ||
		    (memcmp(message_digest->data, md_dat, md_len))) {
			PKCS7error(PKCS7_R_DIGEST_FAILURE);
			ret = -1;
			goto err;
		}

		if (!EVP_VerifyInit_ex(&mdc_tmp, EVP_get_digestbynid(md_type),
		    NULL))
			goto err;

		alen = ASN1_item_i2d((ASN1_VALUE *)sk, &abuf,
		    &PKCS7_ATTR_VERIFY_it);
		if (alen <= 0) {
			PKCS7error(ERR_R_ASN1_LIB);
			ret = -1;
			goto err;
		}
		if (!EVP_VerifyUpdate(&mdc_tmp, abuf, alen)) {
			free(abuf);
			goto err;
		}

		free(abuf);
	}

	os = si->enc_digest;
	pkey = X509_get_pubkey(x509);
	if (!pkey) {
		ret = -1;
		goto err;
	}

	i = EVP_VerifyFinal(&mdc_tmp, os->data, os->length, pkey);
	EVP_PKEY_free(pkey);
	if (i <= 0) {
		PKCS7error(PKCS7_R_SIGNATURE_FAILURE);
		ret = -1;
		goto err;
	} else
		ret = 1;
err:
	EVP_MD_CTX_cleanup(&mdc_tmp);
	return ret;
}
LCRYPTO_ALIAS(PKCS7_signatureVerify);

PKCS7_ISSUER_AND_SERIAL *
PKCS7_get_issuer_and_serial(PKCS7 *p7, int idx)
{
	STACK_OF(PKCS7_RECIP_INFO) *rsk;
	PKCS7_RECIP_INFO *ri;
	int i;

	i = OBJ_obj2nid(p7->type);
	if (i != NID_pkcs7_signedAndEnveloped)
		return NULL;
	if (p7->d.signed_and_enveloped == NULL)
		return NULL;
	rsk = p7->d.signed_and_enveloped->recipientinfo;
	if (rsk == NULL)
		return NULL;
	ri = sk_PKCS7_RECIP_INFO_value(rsk, 0);
	if (sk_PKCS7_RECIP_INFO_num(rsk) <= idx)
		return NULL;
	ri = sk_PKCS7_RECIP_INFO_value(rsk, idx);
	return ri->issuer_and_serial;
}
LCRYPTO_ALIAS(PKCS7_get_issuer_and_serial);

static ASN1_TYPE *
get_attribute(STACK_OF(X509_ATTRIBUTE) *sk, int nid)
{
	int i;
	X509_ATTRIBUTE *xa;
	ASN1_OBJECT *o;

	o = OBJ_nid2obj(nid);
	if (!o || !sk)
		return NULL;
	for (i = 0; i < sk_X509_ATTRIBUTE_num(sk); i++) {
		xa = sk_X509_ATTRIBUTE_value(sk, i);
		if (OBJ_cmp(xa->object, o) == 0)
			return sk_ASN1_TYPE_value(xa->set, 0);
	}
	return NULL;
}

ASN1_TYPE *
PKCS7_get_signed_attribute(PKCS7_SIGNER_INFO *si, int nid)
{
	return get_attribute(si->auth_attr, nid);
}
LCRYPTO_ALIAS(PKCS7_get_signed_attribute);

ASN1_TYPE *
PKCS7_get_attribute(PKCS7_SIGNER_INFO *si, int nid)
{
	return get_attribute(si->unauth_attr, nid);
}
LCRYPTO_ALIAS(PKCS7_get_attribute);

ASN1_OCTET_STRING *
PKCS7_digest_from_attributes(STACK_OF(X509_ATTRIBUTE) *sk)
{
	ASN1_TYPE *astype;

	if (!(astype = get_attribute(sk, NID_pkcs9_messageDigest)))
		return NULL;
	if (astype->type != V_ASN1_OCTET_STRING)
		return NULL;
	return astype->value.octet_string;
}
LCRYPTO_ALIAS(PKCS7_digest_from_attributes);

int
PKCS7_set_signed_attributes(PKCS7_SIGNER_INFO *p7si,
    STACK_OF(X509_ATTRIBUTE) *sk)
{
	int i;

	if (p7si->auth_attr != NULL)
		sk_X509_ATTRIBUTE_pop_free(p7si->auth_attr,
		    X509_ATTRIBUTE_free);
	p7si->auth_attr = sk_X509_ATTRIBUTE_dup(sk);
	if (p7si->auth_attr == NULL)
		return 0;
	for (i = 0; i < sk_X509_ATTRIBUTE_num(sk); i++) {
		if ((sk_X509_ATTRIBUTE_set(p7si->auth_attr, i,
		    X509_ATTRIBUTE_dup(sk_X509_ATTRIBUTE_value(sk, i))))
		    == NULL)
			return 0;
	}
	return 1;
}
LCRYPTO_ALIAS(PKCS7_set_signed_attributes);

int
PKCS7_set_attributes(PKCS7_SIGNER_INFO *p7si, STACK_OF(X509_ATTRIBUTE) *sk)
{
	int i;

	if (p7si->unauth_attr != NULL)
		sk_X509_ATTRIBUTE_pop_free(p7si->unauth_attr,
		    X509_ATTRIBUTE_free);
	p7si->unauth_attr = sk_X509_ATTRIBUTE_dup(sk);
	if (p7si->unauth_attr == NULL)
		return 0;
	for (i = 0; i < sk_X509_ATTRIBUTE_num(sk); i++) {
		if ((sk_X509_ATTRIBUTE_set(p7si->unauth_attr, i,
		    X509_ATTRIBUTE_dup(sk_X509_ATTRIBUTE_value(sk, i))))
		    == NULL)
			return 0;
	}
	return 1;
}
LCRYPTO_ALIAS(PKCS7_set_attributes);

static int
add_attribute(STACK_OF(X509_ATTRIBUTE) **in_sk, int nid, int atrtype, void *value)
{
	STACK_OF(X509_ATTRIBUTE) *sk;
	X509_ATTRIBUTE *old_attr = NULL, *new_attr = NULL;
	int need_pop = 0;
	int i;

	if ((sk = *in_sk) == NULL)
		sk = sk_X509_ATTRIBUTE_new_null();
	if (sk == NULL)
		goto err;

	/* Replace an already existing attribute with the given nid. */
	for (i = 0; i < sk_X509_ATTRIBUTE_num(sk); i++) {
		old_attr = sk_X509_ATTRIBUTE_value(sk, i);
		if(OBJ_obj2nid(old_attr->object) == nid)
			break;
	}

	/* If there is none, make room for the new one, so _set() succeeds. */
	if (i == sk_X509_ATTRIBUTE_num(sk)) {
		old_attr = NULL;
		if (sk_X509_ATTRIBUTE_push(sk, NULL) <= 0)
			goto err;
		need_pop = 1;
	}

	/* On success, new_attr owns value. */
	if ((new_attr = X509_ATTRIBUTE_create(nid, atrtype, value)) == NULL)
		goto err;

	X509_ATTRIBUTE_free(old_attr);
	(void)sk_X509_ATTRIBUTE_set(sk, i, new_attr);

	*in_sk = sk;

	return 1;

 err:
	if (need_pop)
		(void)sk_X509_ATTRIBUTE_pop(sk);
	if (*in_sk != sk)
		sk_X509_ATTRIBUTE_pop_free(sk, X509_ATTRIBUTE_free);

	return 0;
}

int
PKCS7_add_signed_attribute(PKCS7_SIGNER_INFO *p7si, int nid, int atrtype,
    void *value)
{
	return add_attribute(&(p7si->auth_attr), nid, atrtype, value);
}
LCRYPTO_ALIAS(PKCS7_add_signed_attribute);

int
PKCS7_add_attribute(PKCS7_SIGNER_INFO *p7si, int nid, int atrtype, void *value)
{
	return add_attribute(&(p7si->unauth_attr), nid, atrtype, value);
}
LCRYPTO_ALIAS(PKCS7_add_attribute);

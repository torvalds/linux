/* $OpenBSD: evp_pbe.c,v 1.51 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2006 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"
#include "hmac_local.h"
#include "pkcs12_local.h"
#include "x509_local.h"

/* Password based encryption (PBE) functions */
int PKCS5_PBE_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *cipher, const EVP_MD *md, int en_de);
int PKCS5_v2_PBKDF2_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *c, const EVP_MD *md, int en_de);
int PKCS12_PBE_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *cipher, const EVP_MD *md_type,
    int en_de);
int PKCS5_v2_PBE_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *c, const EVP_MD *md, int en_de);

static const struct pbe_config {
	int pbe_nid;
	int cipher_nid;
	int md_nid;
	EVP_PBE_KEYGEN *keygen;
} pbe_outer[] = {
	{
		.pbe_nid = NID_pbeWithMD2AndDES_CBC,
		.cipher_nid = NID_des_cbc,
		.md_nid = NID_md2,
		.keygen = PKCS5_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbeWithMD5AndDES_CBC,
		.cipher_nid = NID_des_cbc,
		.md_nid = NID_md5,
		.keygen = PKCS5_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbeWithSHA1AndRC2_CBC,
		.cipher_nid = NID_rc2_64_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS5_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_id_pbkdf2,
		.cipher_nid = -1,
		.md_nid = -1,
		.keygen = PKCS5_v2_PBKDF2_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And128BitRC4,
		.cipher_nid = NID_rc4,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And40BitRC4,
		.cipher_nid = NID_rc4_40,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And3_Key_TripleDES_CBC,
		.cipher_nid = NID_des_ede3_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And2_Key_TripleDES_CBC,
		.cipher_nid = NID_des_ede_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And128BitRC2_CBC,
		.cipher_nid = NID_rc2_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbe_WithSHA1And40BitRC2_CBC,
		.cipher_nid = NID_rc2_40_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS12_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbes2,
		.cipher_nid = -1,
		.md_nid = -1,
		.keygen = PKCS5_v2_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbeWithMD2AndRC2_CBC,
		.cipher_nid = NID_rc2_64_cbc,
		.md_nid = NID_md2,
		.keygen = PKCS5_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbeWithMD5AndRC2_CBC,
		.cipher_nid = NID_rc2_64_cbc,
		.md_nid = NID_md5,
		.keygen = PKCS5_PBE_keyivgen,
	},
	{
		.pbe_nid = NID_pbeWithSHA1AndDES_CBC,
		.cipher_nid = NID_des_cbc,
		.md_nid = NID_sha1,
		.keygen = PKCS5_PBE_keyivgen,
	},
};

#define N_PBE_OUTER (sizeof(pbe_outer) / sizeof(pbe_outer[0]))

int
EVP_PBE_CipherInit(ASN1_OBJECT *pbe_obj, const char *pass, int passlen,
    ASN1_TYPE *param, EVP_CIPHER_CTX *ctx, int en_de)
{
	const struct pbe_config *cfg = NULL;
	const EVP_CIPHER *cipher = NULL;
	const EVP_MD *md = NULL;
	int pbe_nid;
	size_t i;

	if ((pbe_nid = OBJ_obj2nid(pbe_obj)) == NID_undef) {
		EVPerror(EVP_R_UNKNOWN_PBE_ALGORITHM);
		return 0;
	}

	for (i = 0; i < N_PBE_OUTER; i++) {
		if (pbe_nid == pbe_outer[i].pbe_nid) {
			cfg = &pbe_outer[i];
			break;
		}
	}
	if (cfg == NULL) {
		EVPerror(EVP_R_UNKNOWN_PBE_ALGORITHM);
		ERR_asprintf_error_data("NID=%d", pbe_nid);
		return 0;
	}

	if (pass == NULL)
		passlen = 0;
	if (passlen == -1)
		passlen = strlen(pass);

	if (cfg->cipher_nid != -1) {
		if ((cipher = EVP_get_cipherbynid(cfg->cipher_nid)) == NULL) {
			EVPerror(EVP_R_UNKNOWN_CIPHER);
			return 0;
		}
	}
	if (cfg->md_nid != -1) {
		if ((md = EVP_get_digestbynid(cfg->md_nid)) == NULL) {
			EVPerror(EVP_R_UNKNOWN_DIGEST);
			return 0;
		}
	}

	if (!cfg->keygen(ctx, pass, passlen, param, cipher, md, en_de)) {
		EVPerror(EVP_R_KEYGEN_FAILURE);
		return 0;
	}

	return 1;
}

int
PKCS5_PBE_keyivgen(EVP_CIPHER_CTX *cctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *cipher, const EVP_MD *md, int en_de)
{
	EVP_MD_CTX *md_ctx;
	unsigned char md_tmp[EVP_MAX_MD_SIZE];
	unsigned char key[EVP_MAX_KEY_LENGTH], iv[EVP_MAX_IV_LENGTH];
	int i;
	PBEPARAM *pbe;
	int saltlen, iter;
	unsigned char *salt;
	const unsigned char *pbuf;
	int mdsize;
	int ret = 0;

	/* Extract useful info from parameter */
	if (param == NULL || param->type != V_ASN1_SEQUENCE ||
	    param->value.sequence == NULL) {
		EVPerror(EVP_R_DECODE_ERROR);
		return 0;
	}

	mdsize = EVP_MD_size(md);
	if (mdsize < 0)
		return 0;

	pbuf = param->value.sequence->data;
	if (!(pbe = d2i_PBEPARAM(NULL, &pbuf, param->value.sequence->length))) {
		EVPerror(EVP_R_DECODE_ERROR);
		return 0;
	}

	if (!pbe->iter)
		iter = 1;
	else if ((iter = ASN1_INTEGER_get(pbe->iter)) <= 0) {
		EVPerror(EVP_R_UNSUPORTED_NUMBER_OF_ROUNDS);
		PBEPARAM_free(pbe);
		return 0;
	}
	salt = pbe->salt->data;
	saltlen = pbe->salt->length;

	if (!pass)
		passlen = 0;
	else if (passlen == -1)
		passlen = strlen(pass);

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if (!EVP_DigestInit_ex(md_ctx, md, NULL))
		goto err;
	if (!EVP_DigestUpdate(md_ctx, pass, passlen))
		goto err;
	if (!EVP_DigestUpdate(md_ctx, salt, saltlen))
		goto err;
	if (!EVP_DigestFinal_ex(md_ctx, md_tmp, NULL))
		goto err;
	for (i = 1; i < iter; i++) {
		if (!EVP_DigestInit_ex(md_ctx, md, NULL))
			goto err;
		if (!EVP_DigestUpdate(md_ctx, md_tmp, mdsize))
			goto err;
		if (!EVP_DigestFinal_ex(md_ctx, md_tmp, NULL))
			goto err;
	}
	if ((size_t)EVP_CIPHER_key_length(cipher) > sizeof(md_tmp)) {
		EVPerror(EVP_R_BAD_KEY_LENGTH);
		goto err;
	}
	memcpy(key, md_tmp, EVP_CIPHER_key_length(cipher));
	if ((size_t)EVP_CIPHER_iv_length(cipher) > 16) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		goto err;
	}
	memcpy(iv, md_tmp + (16 - EVP_CIPHER_iv_length(cipher)),
	    EVP_CIPHER_iv_length(cipher));
	if (!EVP_CipherInit_ex(cctx, cipher, NULL, key, iv, en_de))
		goto err;
	explicit_bzero(md_tmp, EVP_MAX_MD_SIZE);
	explicit_bzero(key, EVP_MAX_KEY_LENGTH);
	explicit_bzero(iv, EVP_MAX_IV_LENGTH);

	ret = 1;

 err:
	EVP_MD_CTX_free(md_ctx);
	PBEPARAM_free(pbe);

	return ret;
}

/*
 * PKCS#5 v2.0 password based encryption key derivation function PBKDF2.
 */

int
PKCS5_PBKDF2_HMAC(const char *pass, int passlen, const unsigned char *salt,
    int saltlen, int iter, const EVP_MD *digest, int keylen, unsigned char *out)
{
	unsigned char digtmp[EVP_MAX_MD_SIZE], *p, itmp[4];
	int cplen, j, k, tkeylen, mdlen;
	unsigned long i = 1;
	HMAC_CTX hctx_tpl, hctx;

	mdlen = EVP_MD_size(digest);
	if (mdlen < 0)
		return 0;

	HMAC_CTX_init(&hctx_tpl);
	p = out;
	tkeylen = keylen;
	if (!pass)
		passlen = 0;
	else if (passlen == -1)
		passlen = strlen(pass);
	if (!HMAC_Init_ex(&hctx_tpl, pass, passlen, digest, NULL)) {
		HMAC_CTX_cleanup(&hctx_tpl);
		return 0;
	}
	while (tkeylen) {
		if (tkeylen > mdlen)
			cplen = mdlen;
		else
			cplen = tkeylen;
		/*
		 * We are unlikely to ever use more than 256 blocks (5120 bits!)
		 * but just in case...
		 */
		itmp[0] = (unsigned char)((i >> 24) & 0xff);
		itmp[1] = (unsigned char)((i >> 16) & 0xff);
		itmp[2] = (unsigned char)((i >> 8) & 0xff);
		itmp[3] = (unsigned char)(i & 0xff);
		if (!HMAC_CTX_copy(&hctx, &hctx_tpl)) {
			HMAC_CTX_cleanup(&hctx_tpl);
			return 0;
		}
		if (!HMAC_Update(&hctx, salt, saltlen) ||
		    !HMAC_Update(&hctx, itmp, 4) ||
		    !HMAC_Final(&hctx, digtmp, NULL)) {
			HMAC_CTX_cleanup(&hctx_tpl);
			HMAC_CTX_cleanup(&hctx);
			return 0;
		}
		HMAC_CTX_cleanup(&hctx);
		memcpy(p, digtmp, cplen);
		for (j = 1; j < iter; j++) {
			if (!HMAC_CTX_copy(&hctx, &hctx_tpl)) {
				HMAC_CTX_cleanup(&hctx_tpl);
				return 0;
			}
			if (!HMAC_Update(&hctx, digtmp, mdlen) ||
			    !HMAC_Final(&hctx, digtmp, NULL)) {
				HMAC_CTX_cleanup(&hctx_tpl);
				HMAC_CTX_cleanup(&hctx);
				return 0;
			}
			HMAC_CTX_cleanup(&hctx);
			for (k = 0; k < cplen; k++)
				p[k] ^= digtmp[k];
		}
		tkeylen -= cplen;
		i++;
		p += cplen;
	}
	HMAC_CTX_cleanup(&hctx_tpl);
	return 1;
}
LCRYPTO_ALIAS(PKCS5_PBKDF2_HMAC);

int
PKCS5_PBKDF2_HMAC_SHA1(const char *pass, int passlen, const unsigned char *salt,
    int saltlen, int iter, int keylen, unsigned char *out)
{
	return PKCS5_PBKDF2_HMAC(pass, passlen, salt, saltlen, iter,
	    EVP_sha1(), keylen, out);
}
LCRYPTO_ALIAS(PKCS5_PBKDF2_HMAC_SHA1);

/*
 * Now the key derivation function itself. This is a bit evil because
 * it has to check the ASN1 parameters are valid: and there are quite a
 * few of them...
 */

int
PKCS5_v2_PBE_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *c, const EVP_MD *md, int en_de)
{
	const unsigned char *pbuf;
	int plen;
	PBE2PARAM *pbe2 = NULL;
	const EVP_CIPHER *cipher;
	int ret = 0;

	if (param == NULL || param->type != V_ASN1_SEQUENCE ||
	    param->value.sequence == NULL) {
		EVPerror(EVP_R_DECODE_ERROR);
		goto err;
	}

	pbuf = param->value.sequence->data;
	plen = param->value.sequence->length;
	if (!(pbe2 = d2i_PBE2PARAM(NULL, &pbuf, plen))) {
		EVPerror(EVP_R_DECODE_ERROR);
		goto err;
	}

	/* See if we recognise the key derivation function */

	if (OBJ_obj2nid(pbe2->keyfunc->algorithm) != NID_id_pbkdf2) {
		EVPerror(EVP_R_UNSUPPORTED_KEY_DERIVATION_FUNCTION);
		goto err;
	}

	/* Let's see if we recognise the encryption algorithm.  */
	cipher = EVP_get_cipherbyobj(pbe2->encryption->algorithm);
	if (!cipher) {
		EVPerror(EVP_R_UNSUPPORTED_CIPHER);
		goto err;
	}

	/* Fixup cipher based on AlgorithmIdentifier */
	if (!EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, en_de))
		goto err;
	if (EVP_CIPHER_asn1_to_param(ctx, pbe2->encryption->parameter) < 0) {
		EVPerror(EVP_R_CIPHER_PARAMETER_ERROR);
		goto err;
	}

	ret = PKCS5_v2_PBKDF2_keyivgen(ctx, pass, passlen,
	    pbe2->keyfunc->parameter, c, md, en_de);

 err:
	PBE2PARAM_free(pbe2);

	return ret;
}

static int
md_nid_from_prf_nid(int nid)
{
	switch (nid) {
	case NID_hmacWithMD5:
		return NID_md5;
	case NID_hmacWithSHA1:
		return NID_sha1;
	case NID_hmacWithSHA224:
		return NID_sha224;
	case NID_hmacWithSHA256:
		return NID_sha256;
	case NID_hmacWithSHA384:
		return NID_sha384;
	case NID_hmacWithSHA512:
		return NID_sha512;
	case NID_hmacWithSHA512_224:
		return NID_sha512_224;
	case NID_hmacWithSHA512_256:
		return NID_sha512_256;
	case NID_hmac_sha3_224:
		return NID_sha3_224;
	case NID_hmac_sha3_256:
		return NID_sha3_256;
	case NID_hmac_sha3_384:
		return NID_sha3_384;
	case NID_hmac_sha3_512:
		return NID_sha3_512;
	default:
		return NID_undef;
	}
}

int
PKCS5_v2_PBKDF2_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *c, const EVP_MD *md, int en_de)
{
	unsigned char *salt, key[EVP_MAX_KEY_LENGTH];
	const unsigned char *pbuf;
	int saltlen, iter, plen;
	unsigned int keylen = 0;
	int prf_nid, hmac_md_nid;
	PBKDF2PARAM *kdf = NULL;
	const EVP_MD *prfmd;
	int ret = 0;

	if (EVP_CIPHER_CTX_cipher(ctx) == NULL) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}
	keylen = EVP_CIPHER_CTX_key_length(ctx);
	if (keylen > sizeof key) {
		EVPerror(EVP_R_BAD_KEY_LENGTH);
		return 0;
	}

	/* Decode parameter */

	if (!param || (param->type != V_ASN1_SEQUENCE)) {
		EVPerror(EVP_R_DECODE_ERROR);
		return 0;
	}

	pbuf = param->value.sequence->data;
	plen = param->value.sequence->length;

	if (!(kdf = d2i_PBKDF2PARAM(NULL, &pbuf, plen)) ) {
		EVPerror(EVP_R_DECODE_ERROR);
		return 0;
	}

	/* Now check the parameters of the kdf */

	if (kdf->keylength &&
	    (ASN1_INTEGER_get(kdf->keylength) != (int)keylen)){
		EVPerror(EVP_R_UNSUPPORTED_KEYLENGTH);
		goto err;
	}

	if (kdf->prf)
		prf_nid = OBJ_obj2nid(kdf->prf->algorithm);
	else
		prf_nid = NID_hmacWithSHA1;

	if ((hmac_md_nid = md_nid_from_prf_nid(prf_nid)) == NID_undef) {
		EVPerror(EVP_R_UNSUPPORTED_PRF);
		goto err;
	}

	prfmd = EVP_get_digestbynid(hmac_md_nid);
	if (prfmd == NULL) {
		EVPerror(EVP_R_UNSUPPORTED_PRF);
		goto err;
	}

	if (kdf->salt->type != V_ASN1_OCTET_STRING) {
		EVPerror(EVP_R_UNSUPPORTED_SALT_TYPE);
		goto err;
	}

	/* it seems that its all OK */
	salt = kdf->salt->value.octet_string->data;
	saltlen = kdf->salt->value.octet_string->length;
	if ((iter = ASN1_INTEGER_get(kdf->iter)) <= 0) {
		EVPerror(EVP_R_UNSUPORTED_NUMBER_OF_ROUNDS);
		goto err;
	}
	if (!PKCS5_PBKDF2_HMAC(pass, passlen, salt, saltlen, iter, prfmd,
	    keylen, key))
		goto err;

	ret = EVP_CipherInit_ex(ctx, NULL, NULL, key, NULL, en_de);

 err:
	explicit_bzero(key, keylen);
	PBKDF2PARAM_free(kdf);

	return ret;
}

void
PKCS12_PBE_add(void)
{
}
LCRYPTO_ALIAS(PKCS12_PBE_add);

int
PKCS12_PBE_keyivgen(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *cipher, const EVP_MD *md, int en_de)
{
	PBEPARAM *pbe;
	int saltlen, iter, ret;
	unsigned char *salt;
	const unsigned char *pbuf;
	unsigned char key[EVP_MAX_KEY_LENGTH], iv[EVP_MAX_IV_LENGTH];

	/* Extract useful info from parameter */
	if (param == NULL || param->type != V_ASN1_SEQUENCE ||
	    param->value.sequence == NULL) {
		PKCS12error(PKCS12_R_DECODE_ERROR);
		return 0;
	}

	pbuf = param->value.sequence->data;
	if (!(pbe = d2i_PBEPARAM(NULL, &pbuf, param->value.sequence->length))) {
		PKCS12error(PKCS12_R_DECODE_ERROR);
		return 0;
	}

	if (!pbe->iter)
		iter = 1;
	else if ((iter = ASN1_INTEGER_get(pbe->iter)) <= 0) {
		PKCS12error(PKCS12_R_DECODE_ERROR);
		PBEPARAM_free(pbe);
		return 0;
	}
	salt = pbe->salt->data;
	saltlen = pbe->salt->length;
	if (!PKCS12_key_gen(pass, passlen, salt, saltlen, PKCS12_KEY_ID,
	    iter, EVP_CIPHER_key_length(cipher), key, md)) {
		PKCS12error(PKCS12_R_KEY_GEN_ERROR);
		PBEPARAM_free(pbe);
		return 0;
	}
	if (!PKCS12_key_gen(pass, passlen, salt, saltlen, PKCS12_IV_ID,
	    iter, EVP_CIPHER_iv_length(cipher), iv, md)) {
		PKCS12error(PKCS12_R_IV_GEN_ERROR);
		PBEPARAM_free(pbe);
		return 0;
	}
	PBEPARAM_free(pbe);
	ret = EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, en_de);
	explicit_bzero(key, EVP_MAX_KEY_LENGTH);
	explicit_bzero(iv, EVP_MAX_IV_LENGTH);
	return ret;
}

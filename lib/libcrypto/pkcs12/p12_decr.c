/* $OpenBSD: p12_decr.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
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
#include <string.h>

#include <openssl/pkcs12.h>

#include "err_local.h"
#include "evp_local.h"

/* Encrypt/Decrypt a buffer based on password and algor, result in a
 * malloc'ed buffer
 */

unsigned char *
PKCS12_pbe_crypt(const X509_ALGOR *algor, const char *pass, int passlen,
    const unsigned char *in, int inlen, unsigned char **data, int *datalen,
    int en_de)
{
	unsigned char *out;
	int outlen, i;
	EVP_CIPHER_CTX ctx;

	EVP_CIPHER_CTX_legacy_clear(&ctx);
	/* Decrypt data */
	if (!EVP_PBE_CipherInit(algor->algorithm, pass, passlen,
	    algor->parameter, &ctx, en_de)) {
		out = NULL;
		PKCS12error(PKCS12_R_PKCS12_ALGOR_CIPHERINIT_ERROR);
		goto err;
	}

	if (!(out = malloc(inlen + EVP_CIPHER_CTX_block_size(&ctx)))) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EVP_CipherUpdate(&ctx, out, &i, in, inlen)) {
		free(out);
		out = NULL;
		PKCS12error(ERR_R_EVP_LIB);
		goto err;
	}

	outlen = i;
	if (!EVP_CipherFinal_ex(&ctx, out + i, &i)) {
		free(out);
		out = NULL;
		PKCS12error(PKCS12_R_PKCS12_CIPHERFINAL_ERROR);
		goto err;
	}
	outlen += i;
	if (datalen)
		*datalen = outlen;
	if (data)
		*data = out;

err:
	EVP_CIPHER_CTX_cleanup(&ctx);
	return out;

}

/* Decrypt an OCTET STRING and decode ASN1 structure
 * if zbuf set zero buffer after use.
 */

void *
PKCS12_item_decrypt_d2i(const X509_ALGOR *algor, const ASN1_ITEM *it,
    const char *pass, int passlen, const ASN1_OCTET_STRING *oct, int zbuf)
{
	unsigned char *out;
	const unsigned char *p;
	void *ret;
	int outlen;

	if (!PKCS12_pbe_crypt(algor, pass, passlen, oct->data, oct->length,
	    &out, &outlen, 0)) {
		PKCS12error(PKCS12_R_PKCS12_PBE_CRYPT_ERROR);
		return NULL;
	}
	p = out;
	ret = ASN1_item_d2i(NULL, &p, outlen, it);
	if (zbuf)
		explicit_bzero(out, outlen);
	if (!ret)
		PKCS12error(PKCS12_R_DECODE_ERROR);
	free(out);
	return ret;
}

/* Encode ASN1 structure and encrypt, return OCTET STRING
 * if zbuf set zero encoding.
 */

ASN1_OCTET_STRING *
PKCS12_item_i2d_encrypt(X509_ALGOR *algor, const ASN1_ITEM *it,
    const char *pass, int passlen,
    void *obj, int zbuf)
{
	ASN1_OCTET_STRING *oct;
	unsigned char *in = NULL;
	int inlen;

	if (!(oct = ASN1_OCTET_STRING_new())) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	inlen = ASN1_item_i2d(obj, &in, it);
	if (!in) {
		PKCS12error(PKCS12_R_ENCODE_ERROR);
		goto err;
	}
	if (!PKCS12_pbe_crypt(algor, pass, passlen, in, inlen, &oct->data,
	    &oct->length, 1)) {
		PKCS12error(PKCS12_R_ENCRYPT_ERROR);
		goto err;
	}
	if (zbuf)
		explicit_bzero(in, inlen);
	free(in);
	return oct;

err:
	free(in);
	ASN1_OCTET_STRING_free(oct);
	return NULL;
}

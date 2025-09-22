/* $OpenBSD: e_cast.c,v 1.19 2025/05/27 03:58:12 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_CAST

#include <openssl/cast.h>
#include <openssl/evp.h>
#include <openssl/objects.h>

#include "evp_local.h"

typedef struct {
	CAST_KEY ks;
} EVP_CAST_KEY;

#define data(ctx)	((EVP_CAST_KEY *)(ctx)->cipher_data)

static int
cast_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	CAST_set_key(&data(ctx)->ks, EVP_CIPHER_CTX_key_length(ctx), key);
	return 1;
}

static int
cast5_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		CAST_cbc_encrypt(in, out, (long)chunk, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		CAST_cbc_encrypt(in, out, (long)inl, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
cast5_cfb64_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		CAST_cfb64_encrypt(in, out, (long)chunk, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
cast5_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		CAST_ecb_encrypt(in + i, out + i, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
cast5_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		CAST_ofb64_encrypt(in, out, (long)chunk, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		CAST_ofb64_encrypt(in, out, (long)inl, &((EVP_CAST_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER cast5_cbc = {
	.nid = NID_cast5_cbc,
	.block_size = 8,
	.key_len = CAST_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CBC_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = cast_init_key,
	.do_cipher = cast5_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAST_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_cast5_cbc(void)
{
	return &cast5_cbc;
}
LCRYPTO_ALIAS(EVP_cast5_cbc);

static const EVP_CIPHER cast5_cfb64 = {
	.nid = NID_cast5_cfb64,
	.block_size = 1,
	.key_len = CAST_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = cast_init_key,
	.do_cipher = cast5_cfb64_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAST_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_cast5_cfb64(void)
{
	return &cast5_cfb64;
}
LCRYPTO_ALIAS(EVP_cast5_cfb64);

static const EVP_CIPHER cast5_ofb = {
	.nid = NID_cast5_ofb64,
	.block_size = 1,
	.key_len = CAST_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_OFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = cast_init_key,
	.do_cipher = cast5_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAST_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_cast5_ofb(void)
{
	return &cast5_ofb;
}
LCRYPTO_ALIAS(EVP_cast5_ofb);

static const EVP_CIPHER cast5_ecb = {
	.nid = NID_cast5_ecb,
	.block_size = 8,
	.key_len = CAST_KEY_LENGTH,
	.iv_len = 0,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_ECB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = cast_init_key,
	.do_cipher = cast5_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAST_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_cast5_ecb(void)
{
	return &cast5_ecb;
}
LCRYPTO_ALIAS(EVP_cast5_ecb);
#endif

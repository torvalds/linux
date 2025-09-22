/* $OpenBSD: e_idea.c,v 1.23 2025/05/27 03:58:12 tb Exp $ */
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
#include <string.h>

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_IDEA

#include <openssl/evp.h>
#include <openssl/idea.h>
#include <openssl/objects.h>

#include "evp_local.h"

/* NB idea_ecb_encrypt doesn't take an 'encrypt' argument so we treat it as a special
 * case
 */

static int
idea_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	if (!enc) {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE)
			enc = 1;
		else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE)
			enc = 1;
	}
	if (enc)
		idea_set_encrypt_key(key, ctx->cipher_data);
	else {
		IDEA_KEY_SCHEDULE tmp;

		idea_set_encrypt_key(key, &tmp);
		idea_set_decrypt_key(&tmp, ctx->cipher_data);
		explicit_bzero((unsigned char *)&tmp,
		    sizeof(IDEA_KEY_SCHEDULE));
	}
	return 1;
}

static int
idea_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		idea_ecb_encrypt(in + i, out + i, ctx->cipher_data);

	return 1;
}

typedef struct {
	IDEA_KEY_SCHEDULE ks;
} EVP_IDEA_KEY;

static int
idea_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		idea_cbc_encrypt(in, out, (long)chunk, &((EVP_IDEA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		idea_cbc_encrypt(in, out, (long)inl, &((EVP_IDEA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
idea_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		idea_ofb64_encrypt(in, out, (long)chunk, &((EVP_IDEA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		idea_ofb64_encrypt(in, out, (long)inl, &((EVP_IDEA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static int
idea_cfb64_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		idea_cfb64_encrypt(in, out, (long)chunk, &((EVP_IDEA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER idea_cbc = {
	.nid = NID_idea_cbc,
	.block_size = 8,
	.key_len = 16,
	.iv_len = 8,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = idea_init_key,
	.do_cipher = idea_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(IDEA_KEY_SCHEDULE),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_idea_cbc(void)
{
	return &idea_cbc;
}
LCRYPTO_ALIAS(EVP_idea_cbc);

static const EVP_CIPHER idea_cfb64 = {
	.nid = NID_idea_cfb64,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 8,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = idea_init_key,
	.do_cipher = idea_cfb64_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(IDEA_KEY_SCHEDULE),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_idea_cfb64(void)
{
	return &idea_cfb64;
}
LCRYPTO_ALIAS(EVP_idea_cfb64);

static const EVP_CIPHER idea_ofb = {
	.nid = NID_idea_ofb64,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 8,
	.flags = EVP_CIPH_OFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = idea_init_key,
	.do_cipher = idea_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(IDEA_KEY_SCHEDULE),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_idea_ofb(void)
{
	return &idea_ofb;
}
LCRYPTO_ALIAS(EVP_idea_ofb);

static const EVP_CIPHER idea_ecb = {
	.nid = NID_idea_ecb,
	.block_size = 8,
	.key_len = 16,
	.iv_len = 0,
	.flags = EVP_CIPH_ECB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = idea_init_key,
	.do_cipher = idea_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(IDEA_KEY_SCHEDULE),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_idea_ecb(void)
{
	return &idea_ecb;
}
LCRYPTO_ALIAS(EVP_idea_ecb);
#endif

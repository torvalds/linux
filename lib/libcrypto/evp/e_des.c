/* $OpenBSD: e_des.c,v 1.25 2025/05/27 03:58:12 tb Exp $ */
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

#ifndef OPENSSL_NO_DES

#include <openssl/evp.h>
#include <openssl/des.h>
#include <openssl/objects.h>

#include "evp_local.h"

static int
des_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	DES_cblock *deskey = (DES_cblock *)key;

	DES_set_key_unchecked(deskey, ctx->cipher_data);
	return 1;
}

static int
des_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	switch (type) {
	case EVP_CTRL_RAND_KEY:
		if (DES_random_key((DES_cblock *)ptr) == 0)
			return 0;
		return 1;

	default:
		return -1;
	}
}

static int
des_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		DES_ecb_encrypt((DES_cblock *)(in + i), (DES_cblock *)(out + i),
		    ctx->cipher_data, ctx->encrypt);

	return 1;
}

static int
des_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		DES_ofb64_encrypt(in, out, (long)chunk, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, &ctx->num);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}
	if (inl)
		DES_ofb64_encrypt(in, out, (long)inl, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, &ctx->num);
	return 1;
}

static int
des_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		DES_ncbc_encrypt(in, out, (long)chunk, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}
	if (inl)
		DES_ncbc_encrypt(in, out, (long)inl, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, ctx->encrypt);
	return 1;
}

static int
des_cfb64_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		DES_cfb64_encrypt(in, out, (long)chunk, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}
	if (inl)
		DES_cfb64_encrypt(in, out, (long)inl, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, &ctx->num, ctx->encrypt);
	return 1;
}

/* Although we have a CFB-r implementation for DES, it doesn't pack the right
   way, so wrap it here */
static int
des_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	unsigned char c[1], d[1];
	size_t chunk = LONG_MAX / 8;
	size_t n;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		for (n = 0; n < chunk*8; ++n) {
			c[0] = (in[n / 8] & (1 << (7 - n % 8))) ? 0x80 : 0;
			DES_cfb_encrypt(c, d, 1, 1, ctx->cipher_data,
			    (DES_cblock *)ctx->iv, ctx->encrypt);
			out[n / 8] = (out[n / 8] &
			    ~(0x80 >> (unsigned int)(n % 8))) |
			    ((d[0] & 0x80) >> (unsigned int)(n % 8));
		}
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
des_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		DES_cfb_encrypt(in, out, 8, (long)chunk,
		    ctx->cipher_data, (DES_cblock *)ctx->iv, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}
	if (inl)
		DES_cfb_encrypt(in, out, 8, (long)inl, ctx->cipher_data,
		    (DES_cblock *)ctx->iv, ctx->encrypt);
	return 1;
}

static const EVP_CIPHER des_cbc = {
	.nid = NID_des_cbc,
	.block_size = 8,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_CBC_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_cbc(void)
{
	return &des_cbc;
}
LCRYPTO_ALIAS(EVP_des_cbc);

static const EVP_CIPHER des_cfb64 = {
	.nid = NID_des_cfb64,
	.block_size = 1,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_CFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_cfb64_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_cfb64(void)
{
	return &des_cfb64;
}
LCRYPTO_ALIAS(EVP_des_cfb64);

static const EVP_CIPHER des_ofb = {
	.nid = NID_des_ofb64,
	.block_size = 1,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_OFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_ofb(void)
{
	return &des_ofb;
}
LCRYPTO_ALIAS(EVP_des_ofb);

static const EVP_CIPHER des_ecb = {
	.nid = NID_des_ecb,
	.block_size = 8,
	.key_len = 8,
	.iv_len = 0,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_ECB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_ecb(void)
{
	return &des_ecb;
}
LCRYPTO_ALIAS(EVP_des_ecb);

static const EVP_CIPHER des_cfb1 = {
	.nid = NID_des_cfb1,
	.block_size = 1,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_CFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_cfb1_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_cfb1(void)
{
	return &des_cfb1;
}
LCRYPTO_ALIAS(EVP_des_cfb1);

static const EVP_CIPHER des_cfb8 = {
	.nid = NID_des_cfb8,
	.block_size = 1,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_RAND_KEY | EVP_CIPH_CFB_MODE |
	    EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = des_init_key,
	.do_cipher = des_cfb8_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(DES_key_schedule),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = des_ctrl,
};

const EVP_CIPHER *
EVP_des_cfb8(void)
{
	return &des_cfb8;
}
LCRYPTO_ALIAS(EVP_des_cfb8);
#endif

/*	$OpenBSD: e_sm4.c,v 1.13 2024/04/09 13:52:41 beck Exp $	*/
/*
 * Copyright (c) 2017, 2019 Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_SM4
#include <openssl/evp.h>
#include <openssl/modes.h>
#include <openssl/sm4.h>

#include "evp_local.h"

typedef struct {
	SM4_KEY ks;
} EVP_SM4_KEY;

static int
sm4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	SM4_set_key(key, ctx->cipher_data);
	return 1;
}

static void
sm4_cbc_encrypt(const unsigned char *in, unsigned char *out, size_t len,
    const SM4_KEY *key, unsigned char *ivec, const int enc)
{
	if (enc)
		CRYPTO_cbc128_encrypt(in, out, len, key, ivec,
		    (block128_f)SM4_encrypt);
	else
		CRYPTO_cbc128_decrypt(in, out, len, key, ivec,
		    (block128_f)SM4_decrypt);
}

static void
sm4_cfb128_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const SM4_KEY *key, unsigned char *ivec, int *num, const int enc)
{
	CRYPTO_cfb128_encrypt(in, out, length, key, ivec, num, enc,
	    (block128_f)SM4_encrypt);
}

static void
sm4_ecb_encrypt(const unsigned char *in, unsigned char *out, const SM4_KEY *key,
    const int enc)
{
	if (enc)
		SM4_encrypt(in, out, key);
	else
		SM4_decrypt(in, out, key);
}

static void
sm4_ofb128_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const SM4_KEY *key, unsigned char *ivec, int *num)
{
	CRYPTO_ofb128_encrypt(in, out, length, key, ivec, num,
	    (block128_f)SM4_encrypt);
}

static int
sm4_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		sm4_cbc_encrypt(in, out, EVP_MAXCHUNK, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		sm4_cbc_encrypt(in, out, inl, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
sm4_cfb128_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		sm4_cfb128_encrypt(in, out, chunk, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
sm4_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		sm4_ecb_encrypt(in + i, out + i, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
sm4_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		sm4_ofb128_encrypt(in, out, EVP_MAXCHUNK, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		sm4_ofb128_encrypt(in, out, inl, &((EVP_SM4_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER sm4_cbc = {
	.nid = NID_sm4_cbc,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CBC_MODE,
	.init = sm4_init_key,
	.do_cipher = sm4_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_SM4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_sm4_cbc(void)
{
	return &sm4_cbc;
}
LCRYPTO_ALIAS(EVP_sm4_cbc);

static const EVP_CIPHER sm4_cfb128 = {
	.nid = NID_sm4_cfb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CFB_MODE,
	.init = sm4_init_key,
	.do_cipher = sm4_cfb128_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_SM4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_sm4_cfb128(void)
{
	return &sm4_cfb128;
}
LCRYPTO_ALIAS(EVP_sm4_cfb128);

static const EVP_CIPHER sm4_ofb = {
	.nid = NID_sm4_ofb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_OFB_MODE,
	.init = sm4_init_key,
	.do_cipher = sm4_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_SM4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_sm4_ofb(void)
{
	return &sm4_ofb;
}
LCRYPTO_ALIAS(EVP_sm4_ofb);

static const EVP_CIPHER sm4_ecb = {
	.nid = NID_sm4_ecb,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 0,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_ECB_MODE,
	.init = sm4_init_key,
	.do_cipher = sm4_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_SM4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_sm4_ecb(void)
{
	return &sm4_ecb;
}
LCRYPTO_ALIAS(EVP_sm4_ecb);

static int
sm4_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in,
    size_t len)
{
	EVP_SM4_KEY *key = ((EVP_SM4_KEY *)(ctx)->cipher_data);

	CRYPTO_ctr128_encrypt(in, out, len, &key->ks, ctx->iv, ctx->buf,
	    &ctx->num, (block128_f)SM4_encrypt);
	return 1;
}

static const EVP_CIPHER sm4_ctr_mode = {
	.nid = NID_sm4_ctr,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CTR_MODE,
	.init = sm4_init_key,
	.do_cipher = sm4_ctr_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_SM4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_sm4_ctr(void)
{
	return &sm4_ctr_mode;
}
LCRYPTO_ALIAS(EVP_sm4_ctr);
#endif

/* $OpenBSD: e_camellia.c,v 1.22 2025/05/27 03:58:12 tb Exp $ */
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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

#include <string.h>

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_CAMELLIA
#include <openssl/evp.h>
#include <openssl/camellia.h>

#include "err_local.h"
#include "evp_local.h"

/* Camellia subkey Structure */
typedef struct {
	CAMELLIA_KEY ks;
} EVP_CAMELLIA_KEY;

/* Attribute operation for Camellia */
#define data(ctx)	((EVP_CAMELLIA_KEY *)(ctx)->cipher_data)

static int
camellia_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	int ret;

	ret = Camellia_set_key(key, ctx->key_len * 8, ctx->cipher_data);

	if (ret < 0) {
		EVPerror(EVP_R_CAMELLIA_KEY_SETUP_FAILED);
		return 0;
	}

	return 1;
}

static int
camellia_128_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_cbc_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_cbc_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
camellia_128_cfb128_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb128_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
camellia_128_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		Camellia_ecb_encrypt(in + i, out + i, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
camellia_128_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_ofb128_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_ofb128_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER camellia_128_cbc = {
	.nid = NID_camellia_128_cbc,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_cbc(void)
{
	return &camellia_128_cbc;
}
LCRYPTO_ALIAS(EVP_camellia_128_cbc);

static const EVP_CIPHER camellia_128_cfb128 = {
	.nid = NID_camellia_128_cfb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_cfb128_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_cfb128(void)
{
	return &camellia_128_cfb128;
}
LCRYPTO_ALIAS(EVP_camellia_128_cfb128);

static const EVP_CIPHER camellia_128_ofb = {
	.nid = NID_camellia_128_ofb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_OFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_ofb(void)
{
	return &camellia_128_ofb;
}
LCRYPTO_ALIAS(EVP_camellia_128_ofb);

static const EVP_CIPHER camellia_128_ecb = {
	.nid = NID_camellia_128_ecb,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 0,
	.flags = EVP_CIPH_ECB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_ecb(void)
{
	return &camellia_128_ecb;
}
LCRYPTO_ALIAS(EVP_camellia_128_ecb);

static int
camellia_192_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_cbc_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_cbc_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
camellia_192_cfb128_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb128_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
camellia_192_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		Camellia_ecb_encrypt(in + i, out + i, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
camellia_192_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_ofb128_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_ofb128_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER camellia_192_cbc = {
	.nid = NID_camellia_192_cbc,
	.block_size = 16,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_cbc(void)
{
	return &camellia_192_cbc;
}
LCRYPTO_ALIAS(EVP_camellia_192_cbc);

static const EVP_CIPHER camellia_192_cfb128 = {
	.nid = NID_camellia_192_cfb128,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_cfb128_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_cfb128(void)
{
	return &camellia_192_cfb128;
}
LCRYPTO_ALIAS(EVP_camellia_192_cfb128);

static const EVP_CIPHER camellia_192_ofb = {
	.nid = NID_camellia_192_ofb128,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_OFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_ofb(void)
{
	return &camellia_192_ofb;
}
LCRYPTO_ALIAS(EVP_camellia_192_ofb);

static const EVP_CIPHER camellia_192_ecb = {
	.nid = NID_camellia_192_ecb,
	.block_size = 16,
	.key_len = 24,
	.iv_len = 0,
	.flags = EVP_CIPH_ECB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_ecb(void)
{
	return &camellia_192_ecb;
}
LCRYPTO_ALIAS(EVP_camellia_192_ecb);

static int
camellia_256_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_cbc_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_cbc_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
camellia_256_cfb128_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb128_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
camellia_256_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		Camellia_ecb_encrypt(in + i, out + i, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
camellia_256_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	while (inl >= EVP_MAXCHUNK) {
		Camellia_ofb128_encrypt(in, out, EVP_MAXCHUNK, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= EVP_MAXCHUNK;
		in += EVP_MAXCHUNK;
		out += EVP_MAXCHUNK;
	}

	if (inl)
		Camellia_ofb128_encrypt(in, out, inl, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER camellia_256_cbc = {
	.nid = NID_camellia_256_cbc,
	.block_size = 16,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_cbc(void)
{
	return &camellia_256_cbc;
}
LCRYPTO_ALIAS(EVP_camellia_256_cbc);

static const EVP_CIPHER camellia_256_cfb128 = {
	.nid = NID_camellia_256_cfb128,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_cfb128_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_cfb128(void)
{
	return &camellia_256_cfb128;
}
LCRYPTO_ALIAS(EVP_camellia_256_cfb128);

static const EVP_CIPHER camellia_256_ofb = {
	.nid = NID_camellia_256_ofb128,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_OFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_ofb(void)
{
	return &camellia_256_ofb;
}
LCRYPTO_ALIAS(EVP_camellia_256_ofb);

static const EVP_CIPHER camellia_256_ecb = {
	.nid = NID_camellia_256_ecb,
	.block_size = 16,
	.key_len = 32,
	.iv_len = 0,
	.flags = EVP_CIPH_ECB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_ecb(void)
{
	return &camellia_256_ecb;
}
LCRYPTO_ALIAS(EVP_camellia_256_ecb);

static int
camellia_128_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	chunk >>= 3;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb1_encrypt(in, out, ((1 == 1) && !(ctx->flags & EVP_CIPH_FLAG_LENGTH_BITS) ? chunk * 8 : chunk), &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_128_cfb1 = {
	.nid = NID_camellia_128_cfb1,
	.block_size = 1,
	.key_len = 128/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_cfb1_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_cfb1(void)
{
	return &camellia_128_cfb1;
}
LCRYPTO_ALIAS(EVP_camellia_128_cfb1);

static int
camellia_192_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	chunk >>= 3;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb1_encrypt(in, out, ((1 == 1) && !(ctx->flags & EVP_CIPH_FLAG_LENGTH_BITS) ? chunk * 8 : chunk), &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_192_cfb1 = {
	.nid = NID_camellia_192_cfb1,
	.block_size = 1,
	.key_len = 192/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_cfb1_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_cfb1(void)
{
	return &camellia_192_cfb1;
}
LCRYPTO_ALIAS(EVP_camellia_192_cfb1);

static int
camellia_256_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	chunk >>= 3;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb1_encrypt(in, out, ((1 == 1) && !(ctx->flags & EVP_CIPH_FLAG_LENGTH_BITS) ? chunk * 8 : chunk), &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_256_cfb1 = {
	.nid = NID_camellia_256_cfb1,
	.block_size = 1,
	.key_len = 256/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_cfb1_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_cfb1(void)
{
	return &camellia_256_cfb1;
}
LCRYPTO_ALIAS(EVP_camellia_256_cfb1);


static int
camellia_128_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb8_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_128_cfb8 = {
	.nid = NID_camellia_128_cfb8,
	.block_size = 1,
	.key_len = 128/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_128_cfb8_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_128_cfb8(void)
{
	return &camellia_128_cfb8;
}
LCRYPTO_ALIAS(EVP_camellia_128_cfb8);

static int
camellia_192_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb8_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_192_cfb8 = {
	.nid = NID_camellia_192_cfb8,
	.block_size = 1,
	.key_len = 192/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_192_cfb8_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_192_cfb8(void)
{
	return &camellia_192_cfb8;
}
LCRYPTO_ALIAS(EVP_camellia_192_cfb8);

static int
camellia_256_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = EVP_MAXCHUNK;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		Camellia_cfb8_encrypt(in, out, chunk, &((EVP_CAMELLIA_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static const EVP_CIPHER camellia_256_cfb8 = {
	.nid = NID_camellia_256_cfb8,
	.block_size = 1,
	.key_len = 256/8,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1,
	.init = camellia_init_key,
	.do_cipher = camellia_256_cfb8_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_CAMELLIA_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_camellia_256_cfb8(void)
{
	return &camellia_256_cfb8;
}
LCRYPTO_ALIAS(EVP_camellia_256_cfb8);
#endif

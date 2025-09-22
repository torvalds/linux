/* $OpenBSD: e_rc2.c,v 1.30 2025/05/10 05:54:38 tb Exp $ */
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

#ifndef OPENSSL_NO_RC2

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rc2.h>

#include "err_local.h"
#include "evp_local.h"

static int rc2_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc);
static int rc2_meth_to_magic(EVP_CIPHER_CTX *ctx);
static int rc2_magic_to_meth(int i);
static int rc2_set_asn1_type_and_iv(EVP_CIPHER_CTX *c, ASN1_TYPE *type);
static int rc2_get_asn1_type_and_iv(EVP_CIPHER_CTX *c, ASN1_TYPE *type);
static int rc2_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr);

typedef struct {
	int key_bits;	/* effective key bits */
	RC2_KEY ks;	/* key schedule */
} EVP_RC2_KEY;

#define data(ctx)	((EVP_RC2_KEY *)(ctx)->cipher_data)

static int
rc2_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		RC2_cbc_encrypt(in, out, (long)chunk, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		RC2_cbc_encrypt(in, out, (long)inl, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
rc2_cfb64_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	if (inl < chunk)
		chunk = inl;

	while (inl && inl >= chunk) {
		RC2_cfb64_encrypt(in, out, (long)chunk, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num, ctx->encrypt);
		inl -= chunk;
		in += chunk;
		out += chunk;
		if (inl < chunk)
			chunk = inl;
	}

	return 1;
}

static int
rc2_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t i, bl;

	bl = ctx->cipher->block_size;

	if (inl < bl)
		return 1;

	inl -= bl;

	for (i = 0; i <= inl; i += bl)
		RC2_ecb_encrypt(in + i, out + i, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->encrypt);

	return 1;
}

static int
rc2_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t inl)
{
	size_t chunk = LONG_MAX & ~0xff;

	while (inl >= chunk) {
		RC2_ofb64_encrypt(in, out, (long)chunk, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);
		inl -= chunk;
		in += chunk;
		out += chunk;
	}

	if (inl)
		RC2_ofb64_encrypt(in, out, (long)inl, &((EVP_RC2_KEY *)ctx->cipher_data)->ks, ctx->iv, &ctx->num);

	return 1;
}

static const EVP_CIPHER rc2_cbc = {
	.nid = NID_rc2_cbc,
	.block_size = 8,
	.key_len = RC2_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT | EVP_CIPH_CBC_MODE,
	.init = rc2_init_key,
	.do_cipher = rc2_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

const EVP_CIPHER *
EVP_rc2_cbc(void)
{
	return &rc2_cbc;
}
LCRYPTO_ALIAS(EVP_rc2_cbc);

static const EVP_CIPHER rc2_cfb64 = {
	.nid = NID_rc2_cfb64,
	.block_size = 1,
	.key_len = RC2_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT | EVP_CIPH_CFB_MODE,
	.init = rc2_init_key,
	.do_cipher = rc2_cfb64_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

const EVP_CIPHER *
EVP_rc2_cfb64(void)
{
	return &rc2_cfb64;
}
LCRYPTO_ALIAS(EVP_rc2_cfb64);

static const EVP_CIPHER rc2_ofb = {
	.nid = NID_rc2_ofb64,
	.block_size = 1,
	.key_len = RC2_KEY_LENGTH,
	.iv_len = 8,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT | EVP_CIPH_OFB_MODE,
	.init = rc2_init_key,
	.do_cipher = rc2_ofb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

const EVP_CIPHER *
EVP_rc2_ofb(void)
{
	return &rc2_ofb;
}
LCRYPTO_ALIAS(EVP_rc2_ofb);

static const EVP_CIPHER rc2_ecb = {
	.nid = NID_rc2_ecb,
	.block_size = 8,
	.key_len = RC2_KEY_LENGTH,
	.iv_len = 0,
	.flags = EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT | EVP_CIPH_ECB_MODE,
	.init = rc2_init_key,
	.do_cipher = rc2_ecb_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

const EVP_CIPHER *
EVP_rc2_ecb(void)
{
	return &rc2_ecb;
}
LCRYPTO_ALIAS(EVP_rc2_ecb);

#define RC2_40_MAGIC	0xa0
#define RC2_64_MAGIC	0x78
#define RC2_128_MAGIC	0x3a

static const EVP_CIPHER r2_64_cbc_cipher = {
	.nid = NID_rc2_64_cbc,
	.block_size = 8,
	.key_len = 8,
	.iv_len = 8,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT,
	.init = rc2_init_key,
	.do_cipher = rc2_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

static const EVP_CIPHER r2_40_cbc_cipher = {
	.nid = NID_rc2_40_cbc,
	.block_size = 8,
	.key_len = 5,
	.iv_len = 8,
	.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_VARIABLE_LENGTH | EVP_CIPH_CTRL_INIT,
	.init = rc2_init_key,
	.do_cipher = rc2_cbc_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC2_KEY),
	.set_asn1_parameters = rc2_set_asn1_type_and_iv,
	.get_asn1_parameters = rc2_get_asn1_type_and_iv,
	.ctrl = rc2_ctrl,
};

const EVP_CIPHER *
EVP_rc2_64_cbc(void)
{
	return (&r2_64_cbc_cipher);
}
LCRYPTO_ALIAS(EVP_rc2_64_cbc);

const EVP_CIPHER *
EVP_rc2_40_cbc(void)
{
	return (&r2_40_cbc_cipher);
}
LCRYPTO_ALIAS(EVP_rc2_40_cbc);

static int
rc2_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	RC2_set_key(&data(ctx)->ks, EVP_CIPHER_CTX_key_length(ctx),
	    key, data(ctx)->key_bits);
	return 1;
}

static int
rc2_meth_to_magic(EVP_CIPHER_CTX *e)
{
	int i;

	if (EVP_CIPHER_CTX_ctrl(e, EVP_CTRL_GET_RC2_KEY_BITS, 0, &i) <= 0)
		return (0);
	if (i == 128)
		return (RC2_128_MAGIC);
	else if (i == 64)
		return (RC2_64_MAGIC);
	else if (i == 40)
		return (RC2_40_MAGIC);
	else
		return (0);
}

static int
rc2_magic_to_meth(int i)
{
	if (i == RC2_128_MAGIC)
		return 128;
	else if (i == RC2_64_MAGIC)
		return 64;
	else if (i == RC2_40_MAGIC)
		return 40;
	else {
		EVPerror(EVP_R_UNSUPPORTED_KEY_SIZE);
		return (0);
	}
}

static int
rc2_get_asn1_type_and_iv(EVP_CIPHER_CTX *c, ASN1_TYPE *type)
{
	long num = 0;
	int i = 0;
	int key_bits;
	int l;
	unsigned char iv[EVP_MAX_IV_LENGTH];

	if (type != NULL) {
		l = EVP_CIPHER_CTX_iv_length(c);
		if (l < 0 || l > sizeof(iv)) {
			EVPerror(EVP_R_IV_TOO_LARGE);
			return -1;
		}
		i = ASN1_TYPE_get_int_octetstring(type, &num, iv, l);
		if (i != l)
			return (-1);
		key_bits = rc2_magic_to_meth((int)num);
		if (!key_bits)
			return (-1);
		if (i > 0 && !EVP_CipherInit_ex(c, NULL, NULL, NULL, iv, -1))
			return -1;
		if (EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_SET_RC2_KEY_BITS,
		    key_bits, NULL) <= 0)
			return -1;
		if (!EVP_CIPHER_CTX_set_key_length(c, key_bits / 8))
			return -1;
	}
	return (i);
}

static int
rc2_set_asn1_type_and_iv(EVP_CIPHER_CTX *c, ASN1_TYPE *type)
{
	long num;
	int i = 0, j;

	if (type != NULL) {
		num = rc2_meth_to_magic(c);
		j = EVP_CIPHER_CTX_iv_length(c);
		if (j < 0 || j > sizeof(c->oiv))
			return 0;
		i = ASN1_TYPE_set_int_octetstring(type, num, c->oiv, j);
	}
	return (i);
}

static int
rc2_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	switch (type) {
	case EVP_CTRL_INIT:
		data(c)->key_bits = EVP_CIPHER_CTX_key_length(c) * 8;
		return 1;

	case EVP_CTRL_GET_RC2_KEY_BITS:
		*(int *)ptr = data(c)->key_bits;
		return 1;

	case EVP_CTRL_SET_RC2_KEY_BITS:
		if (arg > 0) {
			data(c)->key_bits = arg;
			return 1;
		}
		return 0;

	default:
		return -1;
	}
}

#endif

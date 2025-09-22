/* $OpenBSD: e_aes.c,v 1.83 2025/07/22 09:31:09 jsing Exp $ */
/* ====================================================================
 * Copyright (c) 2001-2011 The OpenSSL Project.  All rights reserved.
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
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include "crypto_internal.h"

#ifndef OPENSSL_NO_AES
#include <openssl/aes.h>
#include <openssl/evp.h>

#include "aes_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "modes_local.h"

typedef struct {
	AES_KEY ks;
} EVP_AES_KEY;

typedef struct {
	AES_KEY ks;		/* AES key schedule to use */
	int key_set;		/* Set if key initialised */
	int iv_set;		/* Set if an iv is set */
	GCM128_CONTEXT gcm;
	unsigned char *iv;	/* Temporary IV store */
	int ivlen;		/* IV length */
	int taglen;
	int iv_gen;		/* It is OK to generate IVs */
	int tls_aad_len;	/* TLS AAD length */
} EVP_AES_GCM_CTX;

typedef struct {
	AES_KEY ks1, ks2;	/* AES key schedules to use */
	XTS128_CONTEXT xts;	/* XXX - replace with flags. */
} EVP_AES_XTS_CTX;

typedef struct {
	AES_KEY ks;		/* AES key schedule to use */
	int key_set;		/* Set if key initialised */
	int iv_set;		/* Set if an iv is set */
	int tag_set;		/* Set if tag is valid */
	int len_set;		/* Set if message length set */
	int L, M;		/* L and M parameters from RFC3610 */
	CCM128_CONTEXT ccm;
} EVP_AES_CCM_CTX;

#define MAXBITCHUNK	((size_t)1<<(sizeof(size_t)*8-4))

static int
aes_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	if (AES_set_encrypt_key(key, ctx->key_len * 8, &eak->ks) < 0) {
		EVPerror(EVP_R_AES_KEY_SETUP_FAILED);
		return 0;
	}

	return 1;
}

static int
aes_cbc_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int encrypt)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	if (encrypt) {
		if (AES_set_encrypt_key(key, ctx->key_len * 8, &eak->ks) < 0) {
			EVPerror(EVP_R_AES_KEY_SETUP_FAILED);
			return 0;
		}
	} else {
		if (AES_set_decrypt_key(key, ctx->key_len * 8, &eak->ks) < 0) {
			EVPerror(EVP_R_AES_KEY_SETUP_FAILED);
			return 0;
		}
	}

	return 1;
}

static int
aes_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	AES_cbc_encrypt(in, out, len, &eak->ks, ctx->iv, ctx->encrypt);

	return 1;
}

static int
aes_ecb_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int encrypt)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	if (encrypt) {
		if (AES_set_encrypt_key(key, ctx->key_len * 8, &eak->ks) < 0) {
			EVPerror(EVP_R_AES_KEY_SETUP_FAILED);
			return 0;
		}
	} else {
		if (AES_set_decrypt_key(key, ctx->key_len * 8, &eak->ks) < 0) {
			EVPerror(EVP_R_AES_KEY_SETUP_FAILED);
			return 0;
		}
	}

	return 1;
}

static int
aes_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	aes_ecb_encrypt_internal(in, out, len, &eak->ks, ctx->encrypt);

	return 1;
}

static int
aes_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	AES_ofb128_encrypt(in, out, len, &eak->ks, ctx->iv, &ctx->num);

	return 1;
}

static int
aes_cfb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	AES_cfb128_encrypt(in, out, len, &eak->ks, ctx->iv, &ctx->num,
	    ctx->encrypt);

	return 1;
}

static int
aes_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	AES_cfb8_encrypt(in, out, len, &eak->ks, ctx->iv, &ctx->num,
	    ctx->encrypt);

	return 1;
}

static int
aes_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;

	if ((ctx->flags & EVP_CIPH_FLAG_LENGTH_BITS) != 0) {
		AES_cfb1_encrypt(in, out, len, &eak->ks, ctx->iv, &ctx->num,
		    ctx->encrypt);
		return 1;
	}

	while (len >= MAXBITCHUNK) {
		AES_cfb1_encrypt(in, out, MAXBITCHUNK * 8, &eak->ks, ctx->iv,
		    &ctx->num, ctx->encrypt);
		len -= MAXBITCHUNK;
		in += MAXBITCHUNK;
		out += MAXBITCHUNK;
	}
	if (len > 0) {
		AES_cfb1_encrypt(in, out, len * 8, &eak->ks, ctx->iv, &ctx->num,
		    ctx->encrypt);
	}

	return 1;
}

static int
aes_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_KEY *eak = ctx->cipher_data;
	unsigned int num = ctx->num;

	AES_ctr128_encrypt(in, out, len, &eak->ks, ctx->iv, ctx->buf, &num);

	ctx->num = (size_t)num;

	return 1;
}

static const EVP_CIPHER aes_128_cbc = {
	.nid = NID_aes_128_cbc,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CBC_MODE,
	.init = aes_cbc_init_key,
	.do_cipher = aes_cbc_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_cbc(void)
{
	return &aes_128_cbc;
}
LCRYPTO_ALIAS(EVP_aes_128_cbc);

static const EVP_CIPHER aes_128_ecb = {
	.nid = NID_aes_128_ecb,
	.block_size = 16,
	.key_len = 16,
	.iv_len = 0,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_ECB_MODE,
	.init = aes_ecb_init_key,
	.do_cipher = aes_ecb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_ecb(void)
{
	return &aes_128_ecb;
}
LCRYPTO_ALIAS(EVP_aes_128_ecb);

static const EVP_CIPHER aes_128_ofb = {
	.nid = NID_aes_128_ofb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_OFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ofb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_ofb(void)
{
	return &aes_128_ofb;
}
LCRYPTO_ALIAS(EVP_aes_128_ofb);

static const EVP_CIPHER aes_128_cfb = {
	.nid = NID_aes_128_cfb128,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_cfb128(void)
{
	return &aes_128_cfb;
}
LCRYPTO_ALIAS(EVP_aes_128_cfb128);

static const EVP_CIPHER aes_128_cfb1 = {
	.nid = NID_aes_128_cfb1,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb1_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_cfb1(void)
{
	return &aes_128_cfb1;
}
LCRYPTO_ALIAS(EVP_aes_128_cfb1);

static const EVP_CIPHER aes_128_cfb8 = {
	.nid = NID_aes_128_cfb8,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb8_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_cfb8(void)
{
	return &aes_128_cfb8;
}
LCRYPTO_ALIAS(EVP_aes_128_cfb8);

static const EVP_CIPHER aes_128_ctr = {
	.nid = NID_aes_128_ctr,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 16,
	.flags = EVP_CIPH_CTR_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ctr_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_128_ctr(void)
{
	return &aes_128_ctr;
}
LCRYPTO_ALIAS(EVP_aes_128_ctr);

static const EVP_CIPHER aes_192_cbc = {
	.nid = NID_aes_192_cbc,
	.block_size = 16,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CBC_MODE,
	.init = aes_cbc_init_key,
	.do_cipher = aes_cbc_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_cbc(void)
{
	return &aes_192_cbc;
}
LCRYPTO_ALIAS(EVP_aes_192_cbc);

static const EVP_CIPHER aes_192_ecb = {
	.nid = NID_aes_192_ecb,
	.block_size = 16,
	.key_len = 24,
	.iv_len = 0,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_ECB_MODE,
	.init = aes_ecb_init_key,
	.do_cipher = aes_ecb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_ecb(void)
{
	return &aes_192_ecb;
}
LCRYPTO_ALIAS(EVP_aes_192_ecb);

static const EVP_CIPHER aes_192_ofb = {
	.nid = NID_aes_192_ofb128,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_OFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ofb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_ofb(void)
{
	return &aes_192_ofb;
}
LCRYPTO_ALIAS(EVP_aes_192_ofb);

static const EVP_CIPHER aes_192_cfb = {
	.nid = NID_aes_192_cfb128,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_cfb128(void)
{
	return &aes_192_cfb;
}
LCRYPTO_ALIAS(EVP_aes_192_cfb128);

static const EVP_CIPHER aes_192_cfb1 = {
	.nid = NID_aes_192_cfb1,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb1_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_cfb1(void)
{
	return &aes_192_cfb1;
}
LCRYPTO_ALIAS(EVP_aes_192_cfb1);

static const EVP_CIPHER aes_192_cfb8 = {
	.nid = NID_aes_192_cfb8,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb8_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_cfb8(void)
{
	return &aes_192_cfb8;
}
LCRYPTO_ALIAS(EVP_aes_192_cfb8);

static const EVP_CIPHER aes_192_ctr = {
	.nid = NID_aes_192_ctr,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 16,
	.flags = EVP_CIPH_CTR_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ctr_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_192_ctr(void)
{
	return &aes_192_ctr;
}
LCRYPTO_ALIAS(EVP_aes_192_ctr);

static const EVP_CIPHER aes_256_cbc = {
	.nid = NID_aes_256_cbc,
	.block_size = 16,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CBC_MODE,
	.init = aes_cbc_init_key,
	.do_cipher = aes_cbc_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_cbc(void)
{
	return &aes_256_cbc;
}
LCRYPTO_ALIAS(EVP_aes_256_cbc);

static const EVP_CIPHER aes_256_ecb = {
	.nid = NID_aes_256_ecb,
	.block_size = 16,
	.key_len = 32,
	.iv_len = 0,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_ECB_MODE,
	.init = aes_ecb_init_key,
	.do_cipher = aes_ecb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_ecb(void)
{
	return &aes_256_ecb;
}
LCRYPTO_ALIAS(EVP_aes_256_ecb);

static const EVP_CIPHER aes_256_ofb = {
	.nid = NID_aes_256_ofb128,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_OFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ofb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_ofb(void)
{
	return &aes_256_ofb;
}
LCRYPTO_ALIAS(EVP_aes_256_ofb);

static const EVP_CIPHER aes_256_cfb = {
	.nid = NID_aes_256_cfb128,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_cfb128(void)
{
	return &aes_256_cfb;
}
LCRYPTO_ALIAS(EVP_aes_256_cfb128);

static const EVP_CIPHER aes_256_cfb1 = {
	.nid = NID_aes_256_cfb1,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb1_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_cfb1(void)
{
	return &aes_256_cfb1;
}
LCRYPTO_ALIAS(EVP_aes_256_cfb1);

static const EVP_CIPHER aes_256_cfb8 = {
	.nid = NID_aes_256_cfb8,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_CFB_MODE,
	.init = aes_init_key,
	.do_cipher = aes_cfb8_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_cfb8(void)
{
	return &aes_256_cfb8;
}
LCRYPTO_ALIAS(EVP_aes_256_cfb8);

static const EVP_CIPHER aes_256_ctr = {
	.nid = NID_aes_256_ctr,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 16,
	.flags = EVP_CIPH_CTR_MODE,
	.init = aes_init_key,
	.do_cipher = aes_ctr_cipher,
	.ctx_size = sizeof(EVP_AES_KEY),
};

const EVP_CIPHER *
EVP_aes_256_ctr(void)
{
	return &aes_256_ctr;
}
LCRYPTO_ALIAS(EVP_aes_256_ctr);

static int
aes_gcm_cleanup(EVP_CIPHER_CTX *c)
{
	EVP_AES_GCM_CTX *gctx = c->cipher_data;

	if (gctx->iv != c->iv)
		free(gctx->iv);

	explicit_bzero(gctx, sizeof(*gctx));

	return 1;
}

/* increment counter (64-bit int) by 1 */
static void
ctr64_inc(unsigned char *counter)
{
	int n = 8;
	unsigned char  c;

	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

static int
aes_gcm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	EVP_AES_GCM_CTX *gctx = c->cipher_data;

	switch (type) {
	case EVP_CTRL_INIT:
		gctx->key_set = 0;
		gctx->iv_set = 0;
		if (c->cipher->iv_len == 0) {
			EVPerror(EVP_R_INVALID_IV_LENGTH);
			return 0;
		}
		gctx->ivlen = c->cipher->iv_len;
		gctx->iv = c->iv;
		gctx->taglen = -1;
		gctx->iv_gen = 0;
		gctx->tls_aad_len = -1;
		return 1;

	case EVP_CTRL_AEAD_GET_IVLEN:
		*(int *)ptr = gctx->ivlen;
		return 1;

	case EVP_CTRL_AEAD_SET_IVLEN:
		if (arg <= 0)
			return 0;
		/* Allocate memory for IV if needed */
		if ((arg > EVP_MAX_IV_LENGTH) && (arg > gctx->ivlen)) {
			if (gctx->iv != c->iv)
				free(gctx->iv);
			gctx->iv = malloc(arg);
			if (!gctx->iv)
				return 0;
		}
		gctx->ivlen = arg;
		return 1;

	case EVP_CTRL_GCM_SET_TAG:
		if (arg <= 0 || arg > 16 || c->encrypt)
			return 0;
		memcpy(c->buf, ptr, arg);
		gctx->taglen = arg;
		return 1;

	case EVP_CTRL_GCM_GET_TAG:
		if (arg <= 0 || arg > 16 || !c->encrypt || gctx->taglen < 0)
			return 0;
		memcpy(ptr, c->buf, arg);
		return 1;

	case EVP_CTRL_GCM_SET_IV_FIXED:
		/* Special case: -1 length restores whole IV */
		if (arg == -1) {
			memcpy(gctx->iv, ptr, gctx->ivlen);
			gctx->iv_gen = 1;
			return 1;
		}
		/* Fixed field must be at least 4 bytes and invocation field
		 * at least 8.
		 */
		if ((arg < 4) || (gctx->ivlen - arg) < 8)
			return 0;
		if (arg)
			memcpy(gctx->iv, ptr, arg);
		if (c->encrypt)
			arc4random_buf(gctx->iv + arg, gctx->ivlen - arg);
		gctx->iv_gen = 1;
		return 1;

	case EVP_CTRL_GCM_IV_GEN:
		if (gctx->iv_gen == 0 || gctx->key_set == 0)
			return 0;
		CRYPTO_gcm128_setiv(&gctx->gcm, gctx->iv, gctx->ivlen);
		if (arg <= 0 || arg > gctx->ivlen)
			arg = gctx->ivlen;
		memcpy(ptr, gctx->iv + gctx->ivlen - arg, arg);
		/* Invocation field will be at least 8 bytes in size and
		 * so no need to check wrap around or increment more than
		 * last 8 bytes.
		 */
		ctr64_inc(gctx->iv + gctx->ivlen - 8);
		gctx->iv_set = 1;
		return 1;

	case EVP_CTRL_GCM_SET_IV_INV:
		if (gctx->iv_gen == 0 || gctx->key_set == 0 || c->encrypt)
			return 0;
		memcpy(gctx->iv + gctx->ivlen - arg, ptr, arg);
		CRYPTO_gcm128_setiv(&gctx->gcm, gctx->iv, gctx->ivlen);
		gctx->iv_set = 1;
		return 1;

	case EVP_CTRL_AEAD_TLS1_AAD:
		/* Save the AAD for later use */
		if (arg != 13)
			return 0;
		memcpy(c->buf, ptr, arg);
		gctx->tls_aad_len = arg;
		{
			unsigned int len = c->buf[arg - 2] << 8 |
			    c->buf[arg - 1];

			/* Correct length for explicit IV */
			if (len < EVP_GCM_TLS_EXPLICIT_IV_LEN)
				return 0;
			len -= EVP_GCM_TLS_EXPLICIT_IV_LEN;

			/* If decrypting correct for tag too */
			if (!c->encrypt) {
				if (len < EVP_GCM_TLS_TAG_LEN)
					return 0;
				len -= EVP_GCM_TLS_TAG_LEN;
			}
			c->buf[arg - 2] = len >> 8;
			c->buf[arg - 1] = len & 0xff;
		}
		/* Extra padding: tag appended to record */
		return EVP_GCM_TLS_TAG_LEN;

	case EVP_CTRL_COPY:
	    {
		EVP_CIPHER_CTX *out = ptr;
		EVP_AES_GCM_CTX *gctx_out = out->cipher_data;

		if (gctx->gcm.key) {
			if (gctx->gcm.key != &gctx->ks)
				return 0;
			gctx_out->gcm.key = &gctx_out->ks;
		}

		if (gctx->iv == c->iv) {
			gctx_out->iv = out->iv;
		} else {
			if ((gctx_out->iv = calloc(1, gctx->ivlen)) == NULL)
				return 0;
			memcpy(gctx_out->iv, gctx->iv, gctx->ivlen);
		}
		return 1;
	    }

	default:
		return -1;

	}
}

static int
aes_gcm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	EVP_AES_GCM_CTX *gctx = ctx->cipher_data;

	if (!iv && !key)
		return 1;
	if (key) {
		AES_set_encrypt_key(key, ctx->key_len * 8, &gctx->ks);
		CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks, aes_encrypt_block128);

		/* If we have an iv can set it directly, otherwise use
		 * saved IV.
		 */
		if (iv == NULL && gctx->iv_set)
			iv = gctx->iv;
		if (iv) {
			CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
			gctx->iv_set = 1;
		}
		gctx->key_set = 1;
	} else {
		/* If key set use IV, otherwise copy */
		if (gctx->key_set)
			CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
		else
			memcpy(gctx->iv, iv, gctx->ivlen);
		gctx->iv_set = 1;
		gctx->iv_gen = 0;
	}
	return 1;
}

/* Handle TLS GCM packet format. This consists of the last portion of the IV
 * followed by the payload and finally the tag. On encrypt generate IV,
 * encrypt payload and write the tag. On verify retrieve IV, decrypt payload
 * and verify tag.
 */

static int
aes_gcm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_GCM_CTX *gctx = ctx->cipher_data;
	int rv = -1;

	/* Encrypt/decrypt must be performed in place */
	if (out != in ||
	    len < (EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN))
		return -1;

	/* Set IV from start of buffer or generate IV and write to start
	 * of buffer.
	 */
	if (EVP_CIPHER_CTX_ctrl(ctx, ctx->encrypt ?
	    EVP_CTRL_GCM_IV_GEN : EVP_CTRL_GCM_SET_IV_INV,
	    EVP_GCM_TLS_EXPLICIT_IV_LEN, out) <= 0)
		goto err;

	/* Use saved AAD */
	if (CRYPTO_gcm128_aad(&gctx->gcm, ctx->buf, gctx->tls_aad_len))
		goto err;

	/* Fix buffer and length to point to payload */
	in += EVP_GCM_TLS_EXPLICIT_IV_LEN;
	out += EVP_GCM_TLS_EXPLICIT_IV_LEN;
	len -= EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
	if (ctx->encrypt) {
		/* Encrypt payload */
		if (CRYPTO_gcm128_encrypt_ctr32(&gctx->gcm, in, out, len,
		    aes_ctr32_encrypt_ctr128f))
			goto err;
		out += len;

		/* Finally write tag */
		CRYPTO_gcm128_tag(&gctx->gcm, out, EVP_GCM_TLS_TAG_LEN);
		rv = len + EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
	} else {
		/* Decrypt */
		if (CRYPTO_gcm128_decrypt_ctr32(&gctx->gcm, in, out, len,
		    aes_ctr32_encrypt_ctr128f))
			goto err;

		/* Retrieve tag */
		CRYPTO_gcm128_tag(&gctx->gcm, ctx->buf, EVP_GCM_TLS_TAG_LEN);

		/* If tag mismatch wipe buffer */
		if (timingsafe_memcmp(ctx->buf, in + len, EVP_GCM_TLS_TAG_LEN) != 0) {
			explicit_bzero(out, len);
			goto err;
		}
		rv = len;
	}

err:
	gctx->iv_set = 0;
	gctx->tls_aad_len = -1;
	return rv;
}

static int
aes_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_GCM_CTX *gctx = ctx->cipher_data;

	/* If not set up, return error */
	if (!gctx->key_set)
		return -1;

	if (gctx->tls_aad_len >= 0)
		return aes_gcm_tls_cipher(ctx, out, in, len);

	if (!gctx->iv_set)
		return -1;

	if (in) {
		if (out == NULL) {
			if (CRYPTO_gcm128_aad(&gctx->gcm, in, len))
				return -1;
		} else if (ctx->encrypt) {
			if (CRYPTO_gcm128_encrypt_ctr32(&gctx->gcm,
			    in, out, len, aes_ctr32_encrypt_ctr128f))
				return -1;
		} else {
			if (CRYPTO_gcm128_decrypt_ctr32(&gctx->gcm,
			    in, out, len, aes_ctr32_encrypt_ctr128f))
				return -1;
		}
		return len;
	} else {
		if (!ctx->encrypt) {
			if (gctx->taglen < 0)
				return -1;
			if (CRYPTO_gcm128_finish(&gctx->gcm, ctx->buf,
			    gctx->taglen) != 0)
				return -1;
			gctx->iv_set = 0;
			return 0;
		}
		CRYPTO_gcm128_tag(&gctx->gcm, ctx->buf, 16);
		gctx->taglen = 16;

		/* Don't reuse the IV */
		gctx->iv_set = 0;
		return 0;
	}

}

#define CUSTOM_FLAGS \
    ( EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CUSTOM_IV | \
      EVP_CIPH_FLAG_CUSTOM_IV_LENGTH | \
      EVP_CIPH_FLAG_CUSTOM_CIPHER | EVP_CIPH_ALWAYS_CALL_INIT | \
      EVP_CIPH_CTRL_INIT | EVP_CIPH_CUSTOM_COPY )

static const EVP_CIPHER aes_128_gcm = {
	.nid = NID_aes_128_gcm,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 12,
	.flags = EVP_CIPH_FLAG_AEAD_CIPHER|CUSTOM_FLAGS | EVP_CIPH_GCM_MODE,
	.init = aes_gcm_init_key,
	.do_cipher = aes_gcm_cipher,
	.cleanup = aes_gcm_cleanup,
	.ctx_size = sizeof(EVP_AES_GCM_CTX),
	.ctrl = aes_gcm_ctrl,
};

const EVP_CIPHER *
EVP_aes_128_gcm(void)
{
	return &aes_128_gcm;
}
LCRYPTO_ALIAS(EVP_aes_128_gcm);

static const EVP_CIPHER aes_192_gcm = {
	.nid = NID_aes_192_gcm,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 12,
	.flags = EVP_CIPH_FLAG_AEAD_CIPHER|CUSTOM_FLAGS | EVP_CIPH_GCM_MODE,
	.init = aes_gcm_init_key,
	.do_cipher = aes_gcm_cipher,
	.cleanup = aes_gcm_cleanup,
	.ctx_size = sizeof(EVP_AES_GCM_CTX),
	.ctrl = aes_gcm_ctrl,
};

const EVP_CIPHER *
EVP_aes_192_gcm(void)
{
	return &aes_192_gcm;
}
LCRYPTO_ALIAS(EVP_aes_192_gcm);

static const EVP_CIPHER aes_256_gcm = {
	.nid = NID_aes_256_gcm,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 12,
	.flags = EVP_CIPH_FLAG_AEAD_CIPHER|CUSTOM_FLAGS | EVP_CIPH_GCM_MODE,
	.init = aes_gcm_init_key,
	.do_cipher = aes_gcm_cipher,
	.cleanup = aes_gcm_cleanup,
	.ctx_size = sizeof(EVP_AES_GCM_CTX),
	.ctrl = aes_gcm_ctrl,
};

const EVP_CIPHER *
EVP_aes_256_gcm(void)
{
	return &aes_256_gcm;
}
LCRYPTO_ALIAS(EVP_aes_256_gcm);

static int
aes_xts_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	EVP_AES_XTS_CTX *xctx = c->cipher_data;

	switch (type) {
	case EVP_CTRL_INIT:
		/*
		 * key1 and key2 are used as an indicator both key and IV
		 * are set
		 */
		xctx->xts.key1 = NULL;
		xctx->xts.key2 = NULL;
		return 1;

	case EVP_CTRL_COPY:
	    {
		EVP_CIPHER_CTX *out = ptr;
		EVP_AES_XTS_CTX *xctx_out = out->cipher_data;

		if (xctx->xts.key1) {
			if (xctx->xts.key1 != &xctx->ks1)
				return 0;
			xctx_out->xts.key1 = &xctx_out->ks1;
		}
		if (xctx->xts.key2) {
			if (xctx->xts.key2 != &xctx->ks2)
				return 0;
			xctx_out->xts.key2 = &xctx_out->ks2;
		}
		return 1;
	    }
	}
	return -1;
}

static int
aes_xts_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int encrypt)
{
	EVP_AES_XTS_CTX *xctx = ctx->cipher_data;

	if (key != NULL) {
		/* key_len is two AES keys */
		if (encrypt)
			AES_set_encrypt_key(key, ctx->key_len * 4, &xctx->ks1);
		else
			AES_set_decrypt_key(key, ctx->key_len * 4, &xctx->ks1);

		AES_set_encrypt_key(key + ctx->key_len / 2, ctx->key_len * 4,
		    &xctx->ks2);

		xctx->xts.key1 = &xctx->ks1;
	}

	if (iv != NULL) {
		xctx->xts.key2 = &xctx->ks2;
		memcpy(ctx->iv, iv, 16);
	}

	return 1;
}

static int
aes_xts_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_XTS_CTX *xctx = ctx->cipher_data;

	if (xctx->xts.key1 == NULL || xctx->xts.key2 == NULL)
		return 0;

	if (out == NULL || in == NULL || len < AES_BLOCK_SIZE)
		return 0;

	aes_xts_encrypt_internal(in, out, len, xctx->xts.key1, xctx->xts.key2,
	    ctx->iv, ctx->encrypt);

	return 1;
}

#define XTS_FLAGS \
    ( EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CUSTOM_IV | \
      EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT | EVP_CIPH_CUSTOM_COPY )

static const EVP_CIPHER aes_128_xts = {
	.nid = NID_aes_128_xts,
	.block_size = 1,
	.key_len = 2 * 16,
	.iv_len = 16,
	.flags = XTS_FLAGS | EVP_CIPH_XTS_MODE,
	.init = aes_xts_init_key,
	.do_cipher = aes_xts_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_XTS_CTX),
	.ctrl = aes_xts_ctrl,
};

const EVP_CIPHER *
EVP_aes_128_xts(void)
{
	return &aes_128_xts;
}
LCRYPTO_ALIAS(EVP_aes_128_xts);

static const EVP_CIPHER aes_256_xts = {
	.nid = NID_aes_256_xts,
	.block_size = 1,
	.key_len = 2 * 32,
	.iv_len = 16,
	.flags = XTS_FLAGS | EVP_CIPH_XTS_MODE,
	.init = aes_xts_init_key,
	.do_cipher = aes_xts_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_XTS_CTX),
	.ctrl = aes_xts_ctrl,
};

const EVP_CIPHER *
EVP_aes_256_xts(void)
{
	return &aes_256_xts;
}
LCRYPTO_ALIAS(EVP_aes_256_xts);

static int
aes_ccm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	EVP_AES_CCM_CTX *cctx = c->cipher_data;

	switch (type) {
	case EVP_CTRL_INIT:
		cctx->key_set = 0;
		cctx->iv_set = 0;
		cctx->L = 8;
		cctx->M = 12;
		cctx->tag_set = 0;
		cctx->len_set = 0;
		return 1;

	case EVP_CTRL_AEAD_GET_IVLEN:
		*(int *)ptr = 15 - cctx->L;
		return 1;

	case EVP_CTRL_AEAD_SET_IVLEN:
		arg = 15 - arg;

	case EVP_CTRL_CCM_SET_L:
		if (arg < 2 || arg > 8)
			return 0;
		cctx->L = arg;
		return 1;

	case EVP_CTRL_CCM_SET_TAG:
		if ((arg & 1) || arg < 4 || arg > 16)
			return 0;
		if ((c->encrypt && ptr) || (!c->encrypt && !ptr))
			return 0;
		if (ptr) {
			cctx->tag_set = 1;
			memcpy(c->buf, ptr, arg);
		}
		cctx->M = arg;
		return 1;

	case EVP_CTRL_CCM_GET_TAG:
		if (!c->encrypt || !cctx->tag_set)
			return 0;
		if (!CRYPTO_ccm128_tag(&cctx->ccm, ptr, (size_t)arg))
			return 0;
		cctx->tag_set = 0;
		cctx->iv_set = 0;
		cctx->len_set = 0;
		return 1;

	case EVP_CTRL_COPY:
	    {
		EVP_CIPHER_CTX *out = ptr;
		EVP_AES_CCM_CTX *cctx_out = out->cipher_data;

		if (cctx->ccm.key) {
			if (cctx->ccm.key != &cctx->ks)
				return 0;
			cctx_out->ccm.key = &cctx_out->ks;
		}
		return 1;
	    }

	default:
		return -1;
	}
}

static int
aes_ccm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	EVP_AES_CCM_CTX *cctx = ctx->cipher_data;

	if (!iv && !key)
		return 1;
	if (key) {
		AES_set_encrypt_key(key, ctx->key_len * 8, &cctx->ks);
		CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
		    &cctx->ks, aes_encrypt_block128);
		cctx->key_set = 1;
	}
	if (iv) {
		memcpy(ctx->iv, iv, 15 - cctx->L);
		cctx->iv_set = 1;
	}
	return 1;
}

static int
aes_ccm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t len)
{
	EVP_AES_CCM_CTX *cctx = ctx->cipher_data;
	CCM128_CONTEXT *ccm = &cctx->ccm;

	/* If not set up, return error */
	if (!cctx->key_set)
		return -1;

	/* EVP_*Final() doesn't return any data */
	if (in == NULL && out != NULL)
		return 0;

	if (!cctx->iv_set)
		return -1;
	if (!ctx->encrypt && !cctx->tag_set)
		return -1;

	if (!out) {
		if (!in) {
			if (CRYPTO_ccm128_setiv(ccm, ctx->iv, 15 - cctx->L,
			    len))
				return -1;
			cctx->len_set = 1;
			return len;
		}
		/* If have AAD need message length */
		if (!cctx->len_set && len)
			return -1;
		CRYPTO_ccm128_aad(ccm, in, len);
		return len;
	}

	/* If not set length yet do it */
	if (!cctx->len_set) {
		if (CRYPTO_ccm128_setiv(ccm, ctx->iv, 15 - cctx->L, len))
			return -1;
		cctx->len_set = 1;
	}
	if (ctx->encrypt) {
		if (CRYPTO_ccm128_encrypt_ccm64(ccm, in, out, len,
		    aes_ccm64_encrypt_ccm128f) != 0)
			return -1;
		cctx->tag_set = 1;
		return len;
	} else {
		int rv = -1;
		if (CRYPTO_ccm128_decrypt_ccm64(ccm, in, out, len,
		    aes_ccm64_decrypt_ccm128f) == 0) {
			unsigned char tag[16];
			if (CRYPTO_ccm128_tag(ccm, tag, cctx->M)) {
				if (timingsafe_memcmp(tag, ctx->buf, cctx->M) == 0)
					rv = len;
			}
		}
		if (rv == -1)
			explicit_bzero(out, len);
		cctx->iv_set = 0;
		cctx->tag_set = 0;
		cctx->len_set = 0;
		return rv;
	}
}

static const EVP_CIPHER aes_128_ccm = {
	.nid = NID_aes_128_ccm,
	.block_size = 1,
	.key_len = 16,
	.iv_len = 12,
	.flags = CUSTOM_FLAGS | EVP_CIPH_CCM_MODE,
	.init = aes_ccm_init_key,
	.do_cipher = aes_ccm_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_CCM_CTX),
	.ctrl = aes_ccm_ctrl,
};

const EVP_CIPHER *
EVP_aes_128_ccm(void)
{
	return &aes_128_ccm;
}
LCRYPTO_ALIAS(EVP_aes_128_ccm);

static const EVP_CIPHER aes_192_ccm = {
	.nid = NID_aes_192_ccm,
	.block_size = 1,
	.key_len = 24,
	.iv_len = 12,
	.flags = CUSTOM_FLAGS | EVP_CIPH_CCM_MODE,
	.init = aes_ccm_init_key,
	.do_cipher = aes_ccm_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_CCM_CTX),
	.ctrl = aes_ccm_ctrl,
};

const EVP_CIPHER *
EVP_aes_192_ccm(void)
{
	return &aes_192_ccm;
}
LCRYPTO_ALIAS(EVP_aes_192_ccm);

static const EVP_CIPHER aes_256_ccm = {
	.nid = NID_aes_256_ccm,
	.block_size = 1,
	.key_len = 32,
	.iv_len = 12,
	.flags = CUSTOM_FLAGS | EVP_CIPH_CCM_MODE,
	.init = aes_ccm_init_key,
	.do_cipher = aes_ccm_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_CCM_CTX),
	.ctrl = aes_ccm_ctrl,
};

const EVP_CIPHER *
EVP_aes_256_ccm(void)
{
	return &aes_256_ccm;
}
LCRYPTO_ALIAS(EVP_aes_256_ccm);

#define EVP_AEAD_AES_GCM_TAG_LEN 16

struct aead_aes_gcm_ctx {
	union {
		double align;
		AES_KEY ks;
	} ks;
	GCM128_CONTEXT gcm;
	unsigned char tag_len;
};

static int
aead_aes_gcm_init(EVP_AEAD_CTX *ctx, const unsigned char *key, size_t key_len,
    size_t tag_len)
{
	struct aead_aes_gcm_ctx *gcm_ctx;
	const size_t key_bits = key_len * 8;

	/* EVP_AEAD_CTX_init should catch this. */
	if (key_bits != 128 && key_bits != 256) {
		EVPerror(EVP_R_BAD_KEY_LENGTH);
		return 0;
	}

	if (tag_len == EVP_AEAD_DEFAULT_TAG_LENGTH)
		tag_len = EVP_AEAD_AES_GCM_TAG_LEN;

	if (tag_len > EVP_AEAD_AES_GCM_TAG_LEN) {
		EVPerror(EVP_R_TAG_TOO_LARGE);
		return 0;
	}

	if ((gcm_ctx = calloc(1, sizeof(struct aead_aes_gcm_ctx))) == NULL)
		return 0;

	AES_set_encrypt_key(key, key_bits, &gcm_ctx->ks.ks);
	CRYPTO_gcm128_init(&gcm_ctx->gcm, &gcm_ctx->ks.ks, aes_encrypt_block128);
	gcm_ctx->tag_len = tag_len;
	ctx->aead_state = gcm_ctx;

	return 1;
}

static void
aead_aes_gcm_cleanup(EVP_AEAD_CTX *ctx)
{
	struct aead_aes_gcm_ctx *gcm_ctx = ctx->aead_state;

	freezero(gcm_ctx, sizeof(*gcm_ctx));
}

static int
aead_aes_gcm_seal(const EVP_AEAD_CTX *ctx, unsigned char *out, size_t *out_len,
    size_t max_out_len, const unsigned char *nonce, size_t nonce_len,
    const unsigned char *in, size_t in_len, const unsigned char *ad,
    size_t ad_len)
{
	const struct aead_aes_gcm_ctx *gcm_ctx = ctx->aead_state;
	GCM128_CONTEXT gcm;
	size_t bulk = 0;

	if (max_out_len < in_len + gcm_ctx->tag_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	memcpy(&gcm, &gcm_ctx->gcm, sizeof(gcm));

	if (nonce_len == 0) {
		EVPerror(EVP_R_INVALID_IV_LENGTH);
		return 0;
	}
	CRYPTO_gcm128_setiv(&gcm, nonce, nonce_len);

	if (ad_len > 0 && CRYPTO_gcm128_aad(&gcm, ad, ad_len))
		return 0;

	if (CRYPTO_gcm128_encrypt_ctr32(&gcm, in + bulk, out + bulk,
	    in_len - bulk, aes_ctr32_encrypt_ctr128f))
		return 0;

	CRYPTO_gcm128_tag(&gcm, out + in_len, gcm_ctx->tag_len);
	*out_len = in_len + gcm_ctx->tag_len;

	return 1;
}

static int
aead_aes_gcm_open(const EVP_AEAD_CTX *ctx, unsigned char *out, size_t *out_len,
    size_t max_out_len, const unsigned char *nonce, size_t nonce_len,
    const unsigned char *in, size_t in_len, const unsigned char *ad,
    size_t ad_len)
{
	const struct aead_aes_gcm_ctx *gcm_ctx = ctx->aead_state;
	unsigned char tag[EVP_AEAD_AES_GCM_TAG_LEN];
	GCM128_CONTEXT gcm;
	size_t plaintext_len;
	size_t bulk = 0;

	if (in_len < gcm_ctx->tag_len) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	plaintext_len = in_len - gcm_ctx->tag_len;

	if (max_out_len < plaintext_len) {
		EVPerror(EVP_R_BUFFER_TOO_SMALL);
		return 0;
	}

	memcpy(&gcm, &gcm_ctx->gcm, sizeof(gcm));

	if (nonce_len == 0) {
		EVPerror(EVP_R_INVALID_IV_LENGTH);
		return 0;
	}
	CRYPTO_gcm128_setiv(&gcm, nonce, nonce_len);

	if (CRYPTO_gcm128_aad(&gcm, ad, ad_len))
		return 0;

	if (CRYPTO_gcm128_decrypt_ctr32(&gcm, in + bulk, out + bulk,
	    in_len - bulk - gcm_ctx->tag_len, aes_ctr32_encrypt_ctr128f))
		return 0;

	CRYPTO_gcm128_tag(&gcm, tag, gcm_ctx->tag_len);
	if (timingsafe_memcmp(tag, in + plaintext_len, gcm_ctx->tag_len) != 0) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}

	*out_len = plaintext_len;

	return 1;
}

static const EVP_AEAD aead_aes_128_gcm = {
	.key_len = 16,
	.nonce_len = 12,
	.overhead = EVP_AEAD_AES_GCM_TAG_LEN,
	.max_tag_len = EVP_AEAD_AES_GCM_TAG_LEN,

	.init = aead_aes_gcm_init,
	.cleanup = aead_aes_gcm_cleanup,
	.seal = aead_aes_gcm_seal,
	.open = aead_aes_gcm_open,
};

static const EVP_AEAD aead_aes_256_gcm = {
	.key_len = 32,
	.nonce_len = 12,
	.overhead = EVP_AEAD_AES_GCM_TAG_LEN,
	.max_tag_len = EVP_AEAD_AES_GCM_TAG_LEN,

	.init = aead_aes_gcm_init,
	.cleanup = aead_aes_gcm_cleanup,
	.seal = aead_aes_gcm_seal,
	.open = aead_aes_gcm_open,
};

const EVP_AEAD *
EVP_aead_aes_128_gcm(void)
{
	return &aead_aes_128_gcm;
}
LCRYPTO_ALIAS(EVP_aead_aes_128_gcm);

const EVP_AEAD *
EVP_aead_aes_256_gcm(void)
{
	return &aead_aes_256_gcm;
}
LCRYPTO_ALIAS(EVP_aead_aes_256_gcm);

typedef struct {
	union {
		double align;
		AES_KEY ks;
	} ks;
	unsigned char *iv;
} EVP_AES_WRAP_CTX;

static int
aes_wrap_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	EVP_AES_WRAP_CTX *wctx = (EVP_AES_WRAP_CTX *)ctx->cipher_data;

	if (iv == NULL && key == NULL)
		return 1;

	if (key != NULL) {
		if (ctx->encrypt)
			AES_set_encrypt_key(key, 8 * ctx->key_len,
			    &wctx->ks.ks);
		else
			AES_set_decrypt_key(key, 8 * ctx->key_len,
			    &wctx->ks.ks);

		if (iv == NULL)
			wctx->iv = NULL;
	}

	if (iv != NULL) {
		int iv_len = EVP_CIPHER_CTX_iv_length(ctx);

		if (iv_len < 0 || iv_len > sizeof(ctx->iv))
			return 0;
		memcpy(ctx->iv, iv, iv_len);
		wctx->iv = ctx->iv;
	}

	return 1;
}

static int
aes_wrap_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inlen)
{
	EVP_AES_WRAP_CTX *wctx = ctx->cipher_data;
	int ret;

	if (in == NULL)
		return 0;

	if (inlen % 8 != 0)
		return -1;
	if (ctx->encrypt && inlen < 8)
		return -1;
	if (!ctx->encrypt && inlen < 16)
		return -1;
	if (inlen > INT_MAX)
		return -1;

	if (out == NULL) {
		if (ctx->encrypt)
			return inlen + 8;
		else
			return inlen - 8;
	}

	if (ctx->encrypt)
		ret = AES_wrap_key(&wctx->ks.ks, wctx->iv, out, in,
		    (unsigned int)inlen);
	else
		ret = AES_unwrap_key(&wctx->ks.ks, wctx->iv, out, in,
		    (unsigned int)inlen);

	return ret != 0 ? ret : -1;
}

static int
aes_wrap_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
	EVP_AES_WRAP_CTX *wctx = c->cipher_data;

	switch (type) {
	case EVP_CTRL_COPY:
	    {
		EVP_CIPHER_CTX *out = ptr;
		EVP_AES_WRAP_CTX *wctx_out = out->cipher_data;

		if (wctx->iv != NULL) {
			if (c->iv != wctx->iv)
				return 0;

			wctx_out->iv = out->iv;
		}

		return 1;
	    }
	}

	return -1;
}

#define WRAP_FLAGS \
    ( EVP_CIPH_WRAP_MODE | EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER | \
      EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_FLAG_DEFAULT_ASN1 | \
      EVP_CIPH_CUSTOM_COPY )

static const EVP_CIPHER aes_128_wrap = {
	.nid = NID_id_aes128_wrap,
	.block_size = 8,
	.key_len = 16,
	.iv_len = 8,
	.flags = WRAP_FLAGS,
	.init = aes_wrap_init_key,
	.do_cipher = aes_wrap_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_WRAP_CTX),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = aes_wrap_ctrl,
};

const EVP_CIPHER *
EVP_aes_128_wrap(void)
{
	return &aes_128_wrap;
}
LCRYPTO_ALIAS(EVP_aes_128_wrap);

static const EVP_CIPHER aes_192_wrap = {
	.nid = NID_id_aes192_wrap,
	.block_size = 8,
	.key_len = 24,
	.iv_len = 8,
	.flags = WRAP_FLAGS,
	.init = aes_wrap_init_key,
	.do_cipher = aes_wrap_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_WRAP_CTX),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = aes_wrap_ctrl,
};

const EVP_CIPHER *
EVP_aes_192_wrap(void)
{
	return &aes_192_wrap;
}
LCRYPTO_ALIAS(EVP_aes_192_wrap);

static const EVP_CIPHER aes_256_wrap = {
	.nid = NID_id_aes256_wrap,
	.block_size = 8,
	.key_len = 32,
	.iv_len = 8,
	.flags = WRAP_FLAGS,
	.init = aes_wrap_init_key,
	.do_cipher = aes_wrap_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_AES_WRAP_CTX),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = aes_wrap_ctrl,
};

const EVP_CIPHER *
EVP_aes_256_wrap(void)
{
	return &aes_256_wrap;
}
LCRYPTO_ALIAS(EVP_aes_256_wrap);

#endif

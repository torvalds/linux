/*	$OpenBSD: tls1_prf.c,v 1.42 2025/05/21 03:53:20 kenjiro Exp $ */
/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL project
 * 2016.
 */
/* ====================================================================
 * Copyright (c) 2015 The OpenSSL Project.  All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include "err_local.h"
#include "evp_local.h"

#define TLS1_PRF_MAXBUF 1024

struct tls1_prf_ctx {
	const EVP_MD *md;
	unsigned char *secret;
	size_t secret_len;
	unsigned char seed[TLS1_PRF_MAXBUF];
	size_t seed_len;
};

static int
pkey_tls1_prf_init(EVP_PKEY_CTX *ctx)
{
	struct tls1_prf_ctx *kctx;

	if ((kctx = calloc(1, sizeof(*kctx))) == NULL) {
		KDFerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ctx->data = kctx;

	return 1;
}

static void
pkey_tls1_prf_cleanup(EVP_PKEY_CTX *ctx)
{
	struct tls1_prf_ctx *kctx = ctx->data;

	if (kctx == NULL)
		return;

	freezero(kctx->secret, kctx->secret_len);
	freezero(kctx, sizeof(*kctx));
}

static int
pkey_tls1_prf_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	struct tls1_prf_ctx *kctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_TLS_MD:
		kctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_TLS_SECRET:
		if (p1 < 0)
			return 0;

		freezero(kctx->secret, kctx->secret_len);
		kctx->secret = NULL;
		kctx->secret_len = 0;

		explicit_bzero(kctx->seed, kctx->seed_len);
		kctx->seed_len = 0;

		if (p1 == 0 || p2 == NULL)
			return 0;

		if ((kctx->secret = calloc(1, p1)) == NULL)
			return 0;
		memcpy(kctx->secret, p2, p1);
		kctx->secret_len = p1;

		return 1;

	case EVP_PKEY_CTRL_TLS_SEED:
		if (p1 == 0 || p2 == NULL)
			return 1;
		if (p1 < 0 || p1 > (int)(TLS1_PRF_MAXBUF - kctx->seed_len))
			return 0;
		memcpy(kctx->seed + kctx->seed_len, p2, p1);
		kctx->seed_len += p1;
		return 1;

	default:
		return -2;
	}
}

static int
pkey_tls1_prf_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	if (value == NULL) {
		KDFerror(KDF_R_VALUE_MISSING);
		return 0;
	}
	if (strcmp(type, "md") == 0) {
		struct tls1_prf_ctx *kctx = ctx->data;

		const EVP_MD *md = EVP_get_digestbyname(value);
		if (md == NULL) {
			KDFerror(KDF_R_INVALID_DIGEST);
			return 0;
		}
		kctx->md = md;
		return 1;
	}
	if (strcmp(type, "secret") == 0)
		return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_TLS_SECRET, value);
	if (strcmp(type, "hexsecret") == 0)
		return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_TLS_SECRET, value);
	if (strcmp(type, "seed") == 0)
		return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_TLS_SEED, value);
	if (strcmp(type, "hexseed") == 0)
		return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_TLS_SEED, value);

	KDFerror(KDF_R_UNKNOWN_PARAMETER_TYPE);
	return -2;
}

static int
tls1_prf_P_hash(const EVP_MD *md, const unsigned char *secret, size_t secret_len,
    const unsigned char *seed, size_t seed_len, unsigned char *out, size_t out_len)
{
	int chunk;
	EVP_MD_CTX *ctx = NULL, *ctx_tmp = NULL, *ctx_init = NULL;
	EVP_PKEY *mac_key = NULL;
	unsigned char A1[EVP_MAX_MD_SIZE];
	size_t A1_len;
	int ret = 0;

	if ((chunk = EVP_MD_size(md)) < 0)
		goto err;

	if ((ctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if ((ctx_tmp = EVP_MD_CTX_new()) == NULL)
		goto err;
	if ((ctx_init = EVP_MD_CTX_new()) == NULL)
		goto err;

	EVP_MD_CTX_set_flags(ctx_init, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);

	if ((mac_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL,
	    secret, secret_len)) == NULL)
		goto err;

	if (!EVP_DigestSignInit(ctx_init, NULL, md, NULL, mac_key))
		goto err;
	if (!EVP_MD_CTX_copy_ex(ctx, ctx_init))
		goto err;
	if (seed != NULL && !EVP_DigestSignUpdate(ctx, seed, seed_len))
		goto err;
	if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
		goto err;

	for (;;) {
		/* Reinit mac contexts */
		if (!EVP_MD_CTX_copy_ex(ctx, ctx_init))
			goto err;
		if (!EVP_DigestSignUpdate(ctx, A1, A1_len))
			goto err;
		if (out_len > (size_t)chunk && !EVP_MD_CTX_copy_ex(ctx_tmp, ctx))
			goto err;
		if (seed != NULL && !EVP_DigestSignUpdate(ctx, seed, seed_len))
			goto err;

		if (out_len > (size_t)chunk) {
			size_t mac_len;
			if (!EVP_DigestSignFinal(ctx, out, &mac_len))
				goto err;
			out += mac_len;
			out_len -= mac_len;
			if (!EVP_DigestSignFinal(ctx_tmp, A1, &A1_len))
				goto err;
		} else {
			if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
				goto err;
			memcpy(out, A1, out_len);
			break;
		}
	}

	ret = 1;

 err:
	EVP_PKEY_free(mac_key);
	EVP_MD_CTX_free(ctx);
	EVP_MD_CTX_free(ctx_tmp);
	EVP_MD_CTX_free(ctx_init);
	explicit_bzero(A1, sizeof(A1));

	return ret;
}

static int
tls1_prf_alg(const EVP_MD *md, const unsigned char *secret, size_t secret_len,
    const unsigned char *seed, size_t seed_len, unsigned char *out, size_t out_len)
{
	unsigned char *tmp = NULL;
	size_t half_len;
	size_t i;
	int ret = 0;

	if (EVP_MD_type(md) != NID_md5_sha1)
		return tls1_prf_P_hash(md, secret, secret_len, seed, seed_len,
		    out, out_len);

	half_len = secret_len - secret_len / 2;
	if (!tls1_prf_P_hash(EVP_md5(), secret, half_len, seed, seed_len,
	    out, out_len))
		goto err;

	if ((tmp = calloc(1, out_len)) == NULL) {
		KDFerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	secret += secret_len - half_len;
	if (!tls1_prf_P_hash(EVP_sha1(), secret, half_len, seed, seed_len,
	    tmp, out_len))
		goto err;
	for (i = 0; i < out_len; i++)
		out[i] ^= tmp[i];

	ret = 1;

 err:
	freezero(tmp, out_len);

	return ret;
}

static int
pkey_tls1_prf_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *key_len)
{
	struct tls1_prf_ctx *kctx = ctx->data;

	if (kctx->md == NULL) {
		KDFerror(KDF_R_MISSING_MESSAGE_DIGEST);
		return 0;
	}
	if (kctx->secret == NULL) {
		KDFerror(KDF_R_MISSING_SECRET);
		return 0;
	}
	if (kctx->seed_len == 0) {
		KDFerror(KDF_R_MISSING_SEED);
		return 0;
	}

	return tls1_prf_alg(kctx->md, kctx->secret, kctx->secret_len,
	    kctx->seed, kctx->seed_len, key, *key_len);
}

const EVP_PKEY_METHOD tls1_prf_pkey_meth = {
	.pkey_id = EVP_PKEY_TLS1_PRF,
	.flags = 0,

	.init = pkey_tls1_prf_init,
	.copy = NULL,
	.cleanup = pkey_tls1_prf_cleanup,

	.paramgen = NULL,

	.keygen = NULL,

	.sign_init = NULL,
	.sign = NULL,

	.verify_init = NULL,
	.verify = NULL,

	.verify_recover = NULL,

	.signctx_init = NULL,
	.signctx = NULL,

	.encrypt = NULL,

	.decrypt = NULL,

	.derive_init = NULL,
	.derive = pkey_tls1_prf_derive,

	.ctrl = pkey_tls1_prf_ctrl,
	.ctrl_str = pkey_tls1_prf_ctrl_str,
};

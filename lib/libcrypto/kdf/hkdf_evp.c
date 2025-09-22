/*	$OpenBSD: hkdf_evp.c,v 1.22 2025/05/21 03:53:20 kenjiro Exp $ */
/* ====================================================================
 * Copyright (c) 2016-2018 The OpenSSL Project.  All rights reserved.
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
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/hmac.h>
#include <openssl/hkdf.h>
#include <openssl/kdf.h>

#include "err_local.h"
#include "evp_local.h"

#define HKDF_MAXBUF 1024

typedef struct {
	int mode;
	const EVP_MD *md;
	unsigned char *salt;
	size_t salt_len;
	unsigned char *key;
	size_t key_len;
	unsigned char info[HKDF_MAXBUF];
	size_t info_len;
} HKDF_PKEY_CTX;

static int
pkey_hkdf_init(EVP_PKEY_CTX *ctx)
{
	HKDF_PKEY_CTX *kctx;

	if ((kctx = calloc(1, sizeof(*kctx))) == NULL) {
		KDFerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	ctx->data = kctx;

	return 1;
}

static void
pkey_hkdf_cleanup(EVP_PKEY_CTX *ctx)
{
	HKDF_PKEY_CTX *kctx = ctx->data;

	if (kctx == NULL)
		return;

	freezero(kctx->salt, kctx->salt_len);
	freezero(kctx->key, kctx->key_len);
	freezero(kctx, sizeof(*kctx));
}

static int
pkey_hkdf_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	HKDF_PKEY_CTX *kctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_HKDF_MD:
		if (p2 == NULL)
			return 0;

		kctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_HKDF_MODE:
		kctx->mode = p1;
		return 1;

	case EVP_PKEY_CTRL_HKDF_SALT:
		if (p1 == 0 || p2 == NULL)
			return 1;

		if (p1 < 0)
			return 0;

		freezero(kctx->salt, kctx->salt_len);
		if ((kctx->salt = malloc(p1)) == NULL)
			return 0;
		memcpy(kctx->salt, p2, p1);

		kctx->salt_len = p1;
		return 1;

	case EVP_PKEY_CTRL_HKDF_KEY:
		if (p1 < 0)
			return 0;

		freezero(kctx->key, kctx->key_len);
		kctx->key = NULL;
		kctx->key_len = 0;

		/* Match OpenSSL's behavior. */
		if (p1 == 0 || p2 == NULL)
			return 0;

		if ((kctx->key = malloc(p1)) == NULL)
			return 0;
		memcpy(kctx->key, p2, p1);

		kctx->key_len = p1;
		return 1;

	case EVP_PKEY_CTRL_HKDF_INFO:
		if (p1 == 0 || p2 == NULL)
			return 1;

		if (p1 < 0 || p1 > (int)(HKDF_MAXBUF - kctx->info_len))
			return 0;

		memcpy(kctx->info + kctx->info_len, p2, p1);
		kctx->info_len += p1;
		return 1;

	default:
		return -2;
	}
}

static int
pkey_hkdf_ctrl_str(EVP_PKEY_CTX *ctx, const char *type,
    const char *value)
{
	if (strcmp(type, "mode") == 0) {
		int mode;

		if (strcmp(value, "EXTRACT_AND_EXPAND") == 0)
			mode = EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND;
		else if (strcmp(value, "EXTRACT_ONLY") == 0)
			mode = EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY;
		else if (strcmp(value, "EXPAND_ONLY") == 0)
			mode = EVP_PKEY_HKDEF_MODE_EXPAND_ONLY;
		else
			return 0;

		return EVP_PKEY_CTX_hkdf_mode(ctx, mode);
	}

	if (strcmp(type, "md") == 0)
		return EVP_PKEY_CTX_md(ctx, EVP_PKEY_OP_DERIVE,
		    EVP_PKEY_CTRL_HKDF_MD, value);

	if (strcmp(type, "salt") == 0)
		return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_HKDF_SALT,
		    value);

	if (strcmp(type, "hexsalt") == 0)
		return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_HKDF_SALT,
		    value);

	if (strcmp(type, "key") == 0)
		return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_HKDF_KEY, value);

	if (strcmp(type, "hexkey") == 0)
		return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_HKDF_KEY, value);

	if (strcmp(type, "info") == 0)
		return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_HKDF_INFO,
		    value);

	if (strcmp(type, "hexinfo") == 0)
		return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_HKDF_INFO,
		    value);

	KDFerror(KDF_R_UNKNOWN_PARAMETER_TYPE);
	return -2;
}

static int
pkey_hkdf_derive_init(EVP_PKEY_CTX *ctx)
{
	HKDF_PKEY_CTX *kctx = ctx->data;

	freezero(kctx->key, kctx->key_len);
	freezero(kctx->salt, kctx->salt_len);
	explicit_bzero(kctx, sizeof(*kctx));

	return 1;
}

static int
pkey_hkdf_derive(EVP_PKEY_CTX *ctx, unsigned char *key,
    size_t *keylen)
{
	HKDF_PKEY_CTX *kctx = ctx->data;

	if (kctx->md == NULL) {
		KDFerror(KDF_R_MISSING_MESSAGE_DIGEST);
		return 0;
	}
	if (kctx->key == NULL) {
		KDFerror(KDF_R_MISSING_KEY);
		return 0;
	}

	switch (kctx->mode) {
	case EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND:
		return HKDF(key, *keylen, kctx->md, kctx->key, kctx->key_len,
		    kctx->salt, kctx->salt_len, kctx->info, kctx->info_len);

	case EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY:
		if (key == NULL) {
			*keylen = EVP_MD_size(kctx->md);
			return 1;
		}
		return HKDF_extract(key, keylen, kctx->md, kctx->key,
		    kctx->key_len, kctx->salt, kctx->salt_len);

	case EVP_PKEY_HKDEF_MODE_EXPAND_ONLY:
		return HKDF_expand(key, *keylen, kctx->md, kctx->key,
		    kctx->key_len, kctx->info, kctx->info_len);

	default:
		return 0;
	}
}

const EVP_PKEY_METHOD hkdf_pkey_meth = {
	.pkey_id = EVP_PKEY_HKDF,
	.flags = 0,

	.init = pkey_hkdf_init,
	.copy = NULL,
	.cleanup = pkey_hkdf_cleanup,

	.derive_init = pkey_hkdf_derive_init,
	.derive = pkey_hkdf_derive,
	.ctrl = pkey_hkdf_ctrl,
	.ctrl_str = pkey_hkdf_ctrl_str,
};

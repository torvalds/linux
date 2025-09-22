/* $OpenBSD: cm_pmeth.c,v 1.12 2023/12/28 21:56:12 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2010.
 */
/* ====================================================================
 * Copyright (c) 2010 The OpenSSL Project.  All rights reserved.
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
 */

#include <stdio.h>
#include <string.h>

#include <openssl/cmac.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "evp_local.h"

/* The context structure and "key" is simply a CMAC_CTX */

static int
pkey_cmac_init(EVP_PKEY_CTX *ctx)
{
	ctx->data = CMAC_CTX_new();
	if (!ctx->data)
		return 0;
	ctx->keygen_info_count = 0;
	return 1;
}

static int
pkey_cmac_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	if (!pkey_cmac_init(dst))
		return 0;
	if (!CMAC_CTX_copy(dst->data, src->data))
		return 0;
	return 1;
}

static void
pkey_cmac_cleanup(EVP_PKEY_CTX *ctx)
{
	CMAC_CTX_free(ctx->data);
}

static int
pkey_cmac_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	CMAC_CTX *cmkey;
	int ret = 0;

	if ((cmkey = CMAC_CTX_new()) == NULL)
		goto err;
	if (!CMAC_CTX_copy(cmkey, ctx->data))
		goto err;
	if (!EVP_PKEY_assign(pkey, EVP_PKEY_CMAC, cmkey))
		goto err;
	cmkey = NULL;

	ret = 1;

 err:
	CMAC_CTX_free(cmkey);

	return ret;
}

static int
int_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	if (!CMAC_Update(ctx->pctx->data, data, count))
		return 0;
	return 1;
}

static int
cmac_signctx_init(EVP_PKEY_CTX *ctx, EVP_MD_CTX *mctx)
{
	EVP_MD_CTX_set_flags(mctx, EVP_MD_CTX_FLAG_NO_INIT);
	mctx->update = int_update;
	return 1;
}

static int
cmac_signctx(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    EVP_MD_CTX *mctx)
{
	return CMAC_Final(ctx->data, sig, siglen);
}

static int
pkey_cmac_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	CMAC_CTX *cmctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_SET_MAC_KEY:
		if (!p2 || p1 < 0)
			return 0;
		if (!CMAC_Init(cmctx, p2, p1, NULL, NULL))
			return 0;
		break;

	case EVP_PKEY_CTRL_CIPHER:
		if (!CMAC_Init(cmctx, NULL, 0, p2, NULL))
			return 0;
		break;

	case EVP_PKEY_CTRL_MD:
		if (ctx->pkey && !CMAC_CTX_copy(ctx->data, ctx->pkey->pkey.ptr))
			return 0;
		if (!CMAC_Init(cmctx, NULL, 0, NULL, NULL))
			return 0;
		break;

	default:
		return -2;
	}
	return 1;
}

static int
pkey_cmac_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	if (!value)
		return 0;
	if (!strcmp(type, "key")) {
		void *p = (void *)value;
		return pkey_cmac_ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY,
		    strlen(p), p);
	}
	if (!strcmp(type, "cipher")) {
		const EVP_CIPHER *c;

		c = EVP_get_cipherbyname(value);
		if (!c)
			return 0;
		return pkey_cmac_ctrl(ctx, EVP_PKEY_CTRL_CIPHER, -1, (void *)c);
	}
	if (!strcmp(type, "hexkey")) {
		unsigned char *key;
		int r;
		long keylen;

		key = string_to_hex(value, &keylen);
		if (!key)
			return 0;
		r = pkey_cmac_ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY, keylen, key);
		free(key);
		return r;
	}

	return -2;
}

const EVP_PKEY_METHOD cmac_pkey_meth = {
	.pkey_id = EVP_PKEY_CMAC,
	.flags = EVP_PKEY_FLAG_SIGCTX_CUSTOM,

	.init = pkey_cmac_init,
	.copy = pkey_cmac_copy,
	.cleanup = pkey_cmac_cleanup,

	.keygen = pkey_cmac_keygen,

	.signctx_init = cmac_signctx_init,
	.signctx = cmac_signctx,

	.ctrl = pkey_cmac_ctrl,
	.ctrl_str = pkey_cmac_ctrl_str
};

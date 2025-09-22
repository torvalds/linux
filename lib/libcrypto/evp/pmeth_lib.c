/* $OpenBSD: pmeth_lib.c,v 1.43 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"

extern const EVP_PKEY_METHOD cmac_pkey_meth;
extern const EVP_PKEY_METHOD dh_pkey_meth;
extern const EVP_PKEY_METHOD dsa_pkey_meth;
extern const EVP_PKEY_METHOD ec_pkey_meth;
extern const EVP_PKEY_METHOD ed25519_pkey_meth;
extern const EVP_PKEY_METHOD hkdf_pkey_meth;
extern const EVP_PKEY_METHOD hmac_pkey_meth;
extern const EVP_PKEY_METHOD rsa_pkey_meth;
extern const EVP_PKEY_METHOD rsa_pss_pkey_meth;
extern const EVP_PKEY_METHOD tls1_prf_pkey_meth;
extern const EVP_PKEY_METHOD x25519_pkey_meth;

static const EVP_PKEY_METHOD *pkey_methods[] = {
	&cmac_pkey_meth,
	&dh_pkey_meth,
	&dsa_pkey_meth,
	&ec_pkey_meth,
	&ed25519_pkey_meth,
	&hkdf_pkey_meth,
	&hmac_pkey_meth,
	&rsa_pkey_meth,
	&rsa_pss_pkey_meth,
	&tls1_prf_pkey_meth,
	&x25519_pkey_meth,
};

#define N_PKEY_METHODS (sizeof(pkey_methods) / sizeof(pkey_methods[0]))

static const EVP_PKEY_METHOD *
evp_pkey_method_find(int nid)
{
	size_t i;

	for (i = 0; i < N_PKEY_METHODS; i++) {
		const EVP_PKEY_METHOD *pmeth = pkey_methods[i];
		if (pmeth->pkey_id == nid)
			return pmeth;
	}

	return NULL;
}

static EVP_PKEY_CTX *
evp_pkey_ctx_new(EVP_PKEY *pkey, int nid)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	const EVP_PKEY_METHOD *pmeth;

	if (nid == -1) {
		if (pkey == NULL || pkey->ameth == NULL)
			return NULL;
		nid = pkey->ameth->pkey_id;
	}

	if ((pmeth = evp_pkey_method_find(nid)) == NULL) {
		EVPerror(EVP_R_UNSUPPORTED_ALGORITHM);
		goto err;
	}

	if ((pkey_ctx = calloc(1, sizeof(*pkey_ctx))) == NULL) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	pkey_ctx->pmeth = pmeth;
	pkey_ctx->operation = EVP_PKEY_OP_UNDEFINED;
	if ((pkey_ctx->pkey = pkey) != NULL)
		EVP_PKEY_up_ref(pkey_ctx->pkey);

	if (pmeth->init != NULL) {
		if (pmeth->init(pkey_ctx) <= 0)
			goto err;
	}

	return pkey_ctx;

 err:
	EVP_PKEY_CTX_free(pkey_ctx);

	return NULL;
}

EVP_PKEY_CTX *
EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *engine)
{
	return evp_pkey_ctx_new(pkey, -1);
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_new);

EVP_PKEY_CTX *
EVP_PKEY_CTX_new_id(int nid, ENGINE *engine)
{
	return evp_pkey_ctx_new(NULL, nid);
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_new_id);

EVP_PKEY_CTX *
EVP_PKEY_CTX_dup(EVP_PKEY_CTX *pctx)
{
	EVP_PKEY_CTX *rctx = NULL;

	if (pctx->pmeth == NULL || pctx->pmeth->copy == NULL)
		goto err;
	if ((rctx = calloc(1, sizeof(*rctx))) == NULL) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	rctx->pmeth = pctx->pmeth;

	if ((rctx->pkey = pctx->pkey) != NULL)
		EVP_PKEY_up_ref(rctx->pkey);
	if ((rctx->peerkey = pctx->peerkey) != NULL)
		EVP_PKEY_up_ref(rctx->peerkey);

	rctx->operation = pctx->operation;

	if (pctx->pmeth->copy(rctx, pctx) <= 0)
		goto err;

	return rctx;

 err:
	EVP_PKEY_CTX_free(rctx);
	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_dup);

void
EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->pmeth && ctx->pmeth->cleanup)
		ctx->pmeth->cleanup(ctx);
	EVP_PKEY_free(ctx->pkey);
	EVP_PKEY_free(ctx->peerkey);
	free(ctx);
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_free);

int
EVP_PKEY_CTX_ctrl(EVP_PKEY_CTX *ctx, int keytype, int optype, int cmd,
    int p1, void *p2)
{
	int ret;

	if (!ctx || !ctx->pmeth || !ctx->pmeth->ctrl) {
		EVPerror(EVP_R_COMMAND_NOT_SUPPORTED);
		return -2;
	}
	if ((keytype != -1) && (ctx->pmeth->pkey_id != keytype))
		return -1;

	if (ctx->operation == EVP_PKEY_OP_UNDEFINED) {
		EVPerror(EVP_R_NO_OPERATION_SET);
		return -1;
	}

	if ((optype != -1) && !(ctx->operation & optype)) {
		EVPerror(EVP_R_INVALID_OPERATION);
		return -1;
	}

	ret = ctx->pmeth->ctrl(ctx, cmd, p1, p2);

	if (ret == -2)
		EVPerror(EVP_R_COMMAND_NOT_SUPPORTED);

	return ret;

}
LCRYPTO_ALIAS(EVP_PKEY_CTX_ctrl);

/*
 * This is practically unused and would best be a part of the openssl(1) code,
 * but, unfortunately, openssl-ruby exposes this directly in an interface and
 * it's currently the only way to do RSA-PSS in Ruby.
 */
int
EVP_PKEY_CTX_ctrl_str(EVP_PKEY_CTX *ctx, const char *name, const char *value)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->ctrl_str) {
		EVPerror(EVP_R_COMMAND_NOT_SUPPORTED);
		return -2;
	}
	if (!strcmp(name, "digest")) {
		return EVP_PKEY_CTX_md(ctx, EVP_PKEY_OP_TYPE_SIG,
		    EVP_PKEY_CTRL_MD, value);
	}
	return ctx->pmeth->ctrl_str(ctx, name, value);
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_ctrl_str);

int
EVP_PKEY_CTX_str2ctrl(EVP_PKEY_CTX *ctx, int cmd, const char *str)
{
	size_t len;

	if ((len = strlen(str)) > INT_MAX)
		return -1;

	return ctx->pmeth->ctrl(ctx, cmd, len, (void *)str);
}

int
EVP_PKEY_CTX_hex2ctrl(EVP_PKEY_CTX *ctx, int cmd, const char *hexstr)
{
	unsigned char *hex = NULL;
	long length;
	int ret = 0;

	if ((hex = string_to_hex(hexstr, &length)) == NULL)
		goto err;
	if (length < 0 || length > INT_MAX) {
		ret = -1;
		goto err;
	}

	ret = ctx->pmeth->ctrl(ctx, cmd, length, hex);

 err:
	free(hex);
	return ret;
}

int
EVP_PKEY_CTX_md(EVP_PKEY_CTX *ctx, int optype, int cmd, const char *md_name)
{
	const EVP_MD *md;

	if ((md = EVP_get_digestbyname(md_name)) == NULL) {
		EVPerror(EVP_R_INVALID_DIGEST);
		return 0;
	}
	return EVP_PKEY_CTX_ctrl(ctx, -1, optype, cmd, 0, (void *)md);
}

int
EVP_PKEY_CTX_get_operation(EVP_PKEY_CTX *ctx)
{
	return ctx->operation;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get_operation);

void
EVP_PKEY_CTX_set0_keygen_info(EVP_PKEY_CTX *ctx, int *dat, int datlen)
{
	ctx->keygen_info = dat;
	ctx->keygen_info_count = datlen;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_set0_keygen_info);

void
EVP_PKEY_CTX_set_data(EVP_PKEY_CTX *ctx, void *data)
{
	ctx->data = data;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_set_data);

void *
EVP_PKEY_CTX_get_data(EVP_PKEY_CTX *ctx)
{
	return ctx->data;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get_data);

EVP_PKEY *
EVP_PKEY_CTX_get0_pkey(EVP_PKEY_CTX *ctx)
{
	return ctx->pkey;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get0_pkey);

EVP_PKEY *
EVP_PKEY_CTX_get0_peerkey(EVP_PKEY_CTX *ctx)
{
	return ctx->peerkey;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get0_peerkey);

void
EVP_PKEY_CTX_set_app_data(EVP_PKEY_CTX *ctx, void *data)
{
	ctx->app_data = data;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_set_app_data);

void *
EVP_PKEY_CTX_get_app_data(EVP_PKEY_CTX *ctx)
{
	return ctx->app_data;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get_app_data);

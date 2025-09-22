/* $OpenBSD: pmeth_gn.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>
#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "bn_local.h"
#include "err_local.h"
#include "evp_local.h"

int
EVP_PKEY_paramgen_init(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->paramgen == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	ctx->operation = EVP_PKEY_OP_PARAMGEN;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_paramgen_init);

int
EVP_PKEY_paramgen(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey)
{
	int ret;

	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->paramgen == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	if (ctx->operation != EVP_PKEY_OP_PARAMGEN) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}

	if (ppkey == NULL)
		return -1;

	if (*ppkey == NULL)
		*ppkey = EVP_PKEY_new();
	if (*ppkey == NULL)
		return -1;

	if ((ret = ctx->pmeth->paramgen(ctx, *ppkey)) <= 0) {
		EVP_PKEY_free(*ppkey);
		*ppkey = NULL;
	}

	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_paramgen);

int
EVP_PKEY_keygen_init(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->keygen == NULL)  {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	ctx->operation = EVP_PKEY_OP_KEYGEN;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_keygen_init);

int
EVP_PKEY_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey)
{
	int ret;

	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->keygen == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_KEYGEN) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}

	if (ppkey == NULL)
		return -1;

	if (*ppkey == NULL)
		*ppkey = EVP_PKEY_new();
	if (*ppkey == NULL)
		return -1;

	if ((ret = ctx->pmeth->keygen(ctx, *ppkey)) <= 0) {
		EVP_PKEY_free(*ppkey);
		*ppkey = NULL;
	}

	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_keygen);

void
EVP_PKEY_CTX_set_cb(EVP_PKEY_CTX *ctx, EVP_PKEY_gen_cb *cb)
{
	ctx->pkey_gencb = cb;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_set_cb);

EVP_PKEY_gen_cb *
EVP_PKEY_CTX_get_cb(EVP_PKEY_CTX *ctx)
{
	return ctx->pkey_gencb;
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get_cb);

/* "translation callback" to call EVP_PKEY_CTX callbacks using BN_GENCB
 * style callbacks.
 */

static int
trans_cb(int a, int b, BN_GENCB *gcb)
{
	EVP_PKEY_CTX *ctx = gcb->arg;
	ctx->keygen_info[0] = a;
	ctx->keygen_info[1] = b;
	return ctx->pkey_gencb(ctx);
}

void
evp_pkey_set_cb_translate(BN_GENCB *cb, EVP_PKEY_CTX *ctx)
{
	BN_GENCB_set(cb, trans_cb, ctx);
}

int
EVP_PKEY_CTX_get_keygen_info(EVP_PKEY_CTX *ctx, int idx)
{
	if (idx == -1)
		return ctx->keygen_info_count;
	if (idx < 0 || idx >= ctx->keygen_info_count)
		return 0;
	return ctx->keygen_info[idx];
}
LCRYPTO_ALIAS(EVP_PKEY_CTX_get_keygen_info);

EVP_PKEY *
EVP_PKEY_new_mac_key(int type, ENGINE *e, const unsigned char *key, int keylen)
{
	EVP_PKEY_CTX *mac_ctx = NULL;
	EVP_PKEY *mac_key = NULL;

	mac_ctx = EVP_PKEY_CTX_new_id(type, NULL);
	if (!mac_ctx)
		return NULL;
	if (EVP_PKEY_keygen_init(mac_ctx) <= 0)
		goto merr;
	if (EVP_PKEY_CTX_ctrl(mac_ctx, -1, EVP_PKEY_OP_KEYGEN,
	    EVP_PKEY_CTRL_SET_MAC_KEY, keylen, (void *)key) <= 0)
		goto merr;
	if (EVP_PKEY_keygen(mac_ctx, &mac_key) <= 0)
		goto merr;

merr:
	EVP_PKEY_CTX_free(mac_ctx);
	return mac_key;
}
LCRYPTO_ALIAS(EVP_PKEY_new_mac_key);

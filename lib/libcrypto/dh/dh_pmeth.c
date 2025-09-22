/* $OpenBSD: dh_pmeth.c,v 1.18 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "bn_local.h"
#include "dh_local.h"
#include "err_local.h"
#include "evp_local.h"

/* DH pkey context structure */

typedef struct {
	/* Parameter gen parameters */
	int prime_len;
	int generator;
	int use_dsa;
	/* Keygen callback info */
	int gentmp[2];
	/* message digest */
} DH_PKEY_CTX;

static int
pkey_dh_init(EVP_PKEY_CTX *ctx)
{
	DH_PKEY_CTX *dctx;

	dctx = malloc(sizeof(DH_PKEY_CTX));
	if (!dctx)
		return 0;
	dctx->prime_len = 1024;
	dctx->generator = 2;
	dctx->use_dsa = 0;

	ctx->data = dctx;
	ctx->keygen_info = dctx->gentmp;
	ctx->keygen_info_count = 2;

	return 1;
}

static int
pkey_dh_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	DH_PKEY_CTX *dctx, *sctx;

	if (!pkey_dh_init(dst))
		return 0;
	sctx = src->data;
	dctx = dst->data;
	dctx->prime_len = sctx->prime_len;
	dctx->generator = sctx->generator;
	dctx->use_dsa = sctx->use_dsa;
	return 1;
}

static void
pkey_dh_cleanup(EVP_PKEY_CTX *ctx)
{
	DH_PKEY_CTX *dctx = ctx->data;

	free(dctx);
}

static int
pkey_dh_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	DH_PKEY_CTX *dctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_DH_PARAMGEN_PRIME_LEN:
		if (p1 < 256)
			return -2;
		dctx->prime_len = p1;
		return 1;

	case EVP_PKEY_CTRL_DH_PARAMGEN_GENERATOR:
		dctx->generator = p1;
		return 1;

	case EVP_PKEY_CTRL_PEER_KEY:
		/* Default behaviour is OK */
		return 1;

	default:
		return -2;
	}
}

static int
pkey_dh_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	const char *errstr;
	int len;

	if (!strcmp(type, "dh_paramgen_prime_len")) {
		len = strtonum(value, INT_MIN, INT_MAX, &errstr);
		if (errstr != NULL)
			return -2;
		return EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, len);
	} else if (!strcmp(type, "dh_paramgen_generator")) {
		len = strtonum(value, INT_MIN, INT_MAX, &errstr);
		if (errstr != NULL)
			return -2;
		return EVP_PKEY_CTX_set_dh_paramgen_generator(ctx, len);
	}

	return -2;
}

static int
pkey_dh_paramgen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	DH *dh;
	DH_PKEY_CTX *dctx = ctx->data;
	BN_GENCB *pcb = NULL;
	BN_GENCB cb = {0};
	int ret = 0;

	if ((dh = DH_new()) == NULL)
		goto err;
	if (ctx->pkey_gencb != NULL) {
		pcb = &cb;
		evp_pkey_set_cb_translate(pcb, ctx);
	}
	if (!DH_generate_parameters_ex(dh, dctx->prime_len, dctx->generator, pcb))
		goto err;
	if (!EVP_PKEY_assign_DH(pkey, dh))
		goto err;
	dh = NULL;

	ret = 1;
 err:
	DH_free(dh);

	return ret;
}

static int
pkey_dh_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	DH *dh = NULL;
	int ret = 0;

	if (ctx->pkey == NULL) {
		DHerror(DH_R_NO_PARAMETERS_SET);
		goto err;
	}

	if ((dh = DH_new()) == NULL)
		goto err;
	if (!EVP_PKEY_set1_DH(pkey, dh))
		goto err;

	if (!EVP_PKEY_copy_parameters(pkey, ctx->pkey))
		goto err;
	if (!DH_generate_key(dh))
		goto err;

	ret = 1;

 err:
	DH_free(dh);

	return ret;
}

static int
pkey_dh_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *keylen)
{
	int ret;

	if (!ctx->pkey || !ctx->peerkey) {
		DHerror(DH_R_KEYS_NOT_SET);
		return 0;
	}
	ret = DH_compute_key(key, ctx->peerkey->pkey.dh->pub_key,
	    ctx->pkey->pkey.dh);
	if (ret < 0)
		return ret;
	*keylen = ret;
	return 1;
}

const EVP_PKEY_METHOD dh_pkey_meth = {
	.pkey_id = EVP_PKEY_DH,
	.flags = EVP_PKEY_FLAG_AUTOARGLEN,

	.init = pkey_dh_init,
	.copy = pkey_dh_copy,
	.cleanup = pkey_dh_cleanup,

	.paramgen = pkey_dh_paramgen,

	.keygen = pkey_dh_keygen,

	.derive = pkey_dh_derive,

	.ctrl = pkey_dh_ctrl,
	.ctrl_str = pkey_dh_ctrl_str
};

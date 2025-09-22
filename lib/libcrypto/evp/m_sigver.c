/* $OpenBSD: m_sigver.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
/* ====================================================================
 * Copyright (c) 2006,2007 The OpenSSL Project.  All rights reserved.
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

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"

static int
update_oneshot_only(EVP_MD_CTX *ctx, const void *data, size_t datalen)
{
	EVPerror(EVP_R_ONLY_ONESHOT_SUPPORTED);
	return 0;
}

static int
do_sigver_init(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type,
    EVP_PKEY *pkey, int ver)
{
	if (ctx->pctx == NULL)
		ctx->pctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (ctx->pctx == NULL)
		return 0;

	if (!(ctx->pctx->pmeth->flags & EVP_PKEY_FLAG_SIGCTX_CUSTOM)) {
		if (type == NULL) {
			int def_nid;
			if (EVP_PKEY_get_default_digest_nid(pkey, &def_nid) > 0)
				type = EVP_get_digestbynid(def_nid);
		}

		if (type == NULL) {
			EVPerror(EVP_R_NO_DEFAULT_DIGEST);
			return 0;
		}
	}

	if (ver) {
		if (ctx->pctx->pmeth->digestverify != NULL) {
			ctx->pctx->operation = EVP_PKEY_OP_VERIFY;
			ctx->update = update_oneshot_only;
		} else if (EVP_PKEY_verify_init(ctx->pctx) <= 0)
			return 0;
	} else {
		if (ctx->pctx->pmeth->signctx_init) {
			if (ctx->pctx->pmeth->signctx_init(ctx->pctx, ctx) <= 0)
				return 0;
			ctx->pctx->operation = EVP_PKEY_OP_SIGNCTX;
		} else if (ctx->pctx->pmeth->digestsign != NULL) {
			ctx->pctx->operation = EVP_PKEY_OP_SIGN;
			ctx->update = update_oneshot_only;
		} else if (EVP_PKEY_sign_init(ctx->pctx) <= 0)
			return 0;
	}
	if (EVP_PKEY_CTX_set_signature_md(ctx->pctx, type) <= 0)
		return 0;
	if (pctx)
		*pctx = ctx->pctx;
	if (ctx->pctx->pmeth->flags & EVP_PKEY_FLAG_SIGCTX_CUSTOM)
		return 1;
	if (!EVP_DigestInit_ex(ctx, type, NULL))
		return 0;
	return 1;
}

int
EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type,
    ENGINE *e, EVP_PKEY *pkey)
{
	return do_sigver_init(ctx, pctx, type, pkey, 0);
}
LCRYPTO_ALIAS(EVP_DigestSignInit);

int
EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type,
    ENGINE *e, EVP_PKEY *pkey)
{
	return do_sigver_init(ctx, pctx, type, pkey, 1);
}
LCRYPTO_ALIAS(EVP_DigestVerifyInit);

static int
evp_digestsignfinal_sigctx_custom(EVP_MD_CTX *ctx, unsigned char *sigret,
    size_t *siglen)
{
	EVP_PKEY_CTX *pctx = ctx->pctx;
	EVP_PKEY_CTX *dctx = NULL;
	int ret = 0;

	if (sigret == NULL)
		return pctx->pmeth->signctx(pctx, sigret, siglen, ctx);

	/* XXX - support EVP_MD_CTX_FLAG_FINALISE? */
	if ((dctx = EVP_PKEY_CTX_dup(pctx)) == NULL)
		goto err;

	if (!dctx->pmeth->signctx(dctx, sigret, siglen, ctx))
		goto err;

	ret = 1;

 err:
	EVP_PKEY_CTX_free(dctx);

	return ret;
}

int
EVP_DigestSignFinal(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen)
{
	EVP_PKEY_CTX *pctx = ctx->pctx;
	EVP_MD_CTX *md_ctx = NULL;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int mdlen = 0;
	int s;
	int ret = 0;

	if (pctx->pmeth->flags & EVP_PKEY_FLAG_SIGCTX_CUSTOM)
		return evp_digestsignfinal_sigctx_custom(ctx, sigret, siglen);

	if (sigret == NULL) {
		if (ctx->pctx->pmeth->signctx != NULL) {
			if (ctx->pctx->pmeth->signctx(ctx->pctx, NULL,
			    siglen, ctx) <= 0)
				return 0;
			return 1;
		}

		if ((s = EVP_MD_size(ctx->digest)) < 0)
			return 0;
		if (EVP_PKEY_sign(ctx->pctx, NULL, siglen, NULL, s) <= 0)
			return 0;

		return 1;
	}

	/* Use a copy since EVP_DigestFinal_ex() clears secrets. */
	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_MD_CTX_copy_ex(md_ctx, ctx))
		goto err;
	if (md_ctx->pctx->pmeth->signctx != NULL) {
		if (md_ctx->pctx->pmeth->signctx(md_ctx->pctx,
		    sigret, siglen, md_ctx) <= 0)
			goto err;
	} else {
		if (!EVP_DigestFinal_ex(md_ctx, md, &mdlen))
			goto err;
		/* Use the original ctx since secrets were cleared. */
		if (EVP_PKEY_sign(ctx->pctx, sigret, siglen, md, mdlen) <= 0)
			goto err;
	}

	ret = 1;

 err:
	EVP_MD_CTX_free(md_ctx);

	return ret;
}
LCRYPTO_ALIAS(EVP_DigestSignFinal);

int
EVP_DigestSign(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen,
    const unsigned char *tbs, size_t tbslen)
{
	if (ctx->pctx->pmeth->digestsign != NULL)
		return ctx->pctx->pmeth->digestsign(ctx, sigret, siglen,
		    tbs, tbslen);

	if (sigret != NULL) {
		if (EVP_DigestSignUpdate(ctx, tbs, tbslen) <= 0)
			return 0;
	}

	return EVP_DigestSignFinal(ctx, sigret, siglen);
}
LCRYPTO_ALIAS(EVP_DigestSign);

int
EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const unsigned char *sig, size_t siglen)
{
	EVP_MD_CTX tmp_ctx;
	unsigned char md[EVP_MAX_MD_SIZE];
	int r;
	unsigned int mdlen = 0;

	EVP_MD_CTX_legacy_clear(&tmp_ctx);
	if (!EVP_MD_CTX_copy_ex(&tmp_ctx, ctx))
		return -1;
	r = EVP_DigestFinal_ex(&tmp_ctx, md, &mdlen);
	EVP_MD_CTX_cleanup(&tmp_ctx);
	if (!r)
		return r;
	return EVP_PKEY_verify(ctx->pctx, sig, siglen, md, mdlen);
}
LCRYPTO_ALIAS(EVP_DigestVerifyFinal);

int
EVP_DigestVerify(EVP_MD_CTX *ctx, const unsigned char *sigret, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	if (ctx->pctx->pmeth->digestverify != NULL)
		return ctx->pctx->pmeth->digestverify(ctx, sigret, siglen,
		    tbs, tbslen);

	if (EVP_DigestVerifyUpdate(ctx, tbs, tbslen) <= 0)
		return -1;

	return EVP_DigestVerifyFinal(ctx, sigret, siglen);
}
LCRYPTO_ALIAS(EVP_DigestVerify);

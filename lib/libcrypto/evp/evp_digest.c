/* $OpenBSD: evp_digest.c,v 1.15 2025/05/10 05:54:38 tb Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2001 The OpenSSL Project.  All rights reserved.
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

#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/evp.h>
#include <openssl/objects.h>

#include "err_local.h"
#include "evp_local.h"

int
EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type)
{
	EVP_MD_CTX_legacy_clear(ctx);
	return EVP_DigestInit_ex(ctx, type, NULL);
}
LCRYPTO_ALIAS(EVP_DigestInit);

int
EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl)
{
	EVP_MD_CTX_clear_flags(ctx, EVP_MD_CTX_FLAG_CLEANED);

	if (ctx->digest != type) {
		if (ctx->digest && ctx->digest->ctx_size && ctx->md_data &&
		    !EVP_MD_CTX_test_flags(ctx, EVP_MD_CTX_FLAG_REUSE)) {
			freezero(ctx->md_data, ctx->digest->ctx_size);
			ctx->md_data = NULL;
		}
		ctx->digest = type;
		if (!(ctx->flags & EVP_MD_CTX_FLAG_NO_INIT) && type->ctx_size) {
			ctx->update = type->update;
			ctx->md_data = calloc(1, type->ctx_size);
			if (ctx->md_data == NULL) {
				EVP_PKEY_CTX_free(ctx->pctx);
				ctx->pctx = NULL;
				EVPerror(ERR_R_MALLOC_FAILURE);
				return 0;
			}
		}
	}
	if (ctx->pctx) {
		int r;
		r = EVP_PKEY_CTX_ctrl(ctx->pctx, -1, EVP_PKEY_OP_TYPE_SIG,
		    EVP_PKEY_CTRL_DIGESTINIT, 0, ctx);
		if (r <= 0 && (r != -2))
			return 0;
	}
	if (ctx->flags & EVP_MD_CTX_FLAG_NO_INIT)
		return 1;
	return ctx->digest->init(ctx);
}
LCRYPTO_ALIAS(EVP_DigestInit_ex);

int
EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return ctx->update(ctx, data, count);
}
LCRYPTO_ALIAS(EVP_DigestUpdate);

/* The caller can assume that this removes any secret data from the context */
int
EVP_DigestFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *size)
{
	int ret;

	ret = EVP_DigestFinal_ex(ctx, md, size);
	EVP_MD_CTX_cleanup(ctx);
	return ret;
}
LCRYPTO_ALIAS(EVP_DigestFinal);

/* The caller can assume that this removes any secret data from the context */
int
EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *size)
{
	int ret;

	if ((size_t)ctx->digest->md_size > EVP_MAX_MD_SIZE) {
		EVPerror(EVP_R_TOO_LARGE);
		return 0;
	}
	ret = ctx->digest->final(ctx, md);
	if (size != NULL)
		*size = ctx->digest->md_size;
	if (ctx->digest->cleanup) {
		ctx->digest->cleanup(ctx);
		EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_CLEANED);
	}
	memset(ctx->md_data, 0, ctx->digest->ctx_size);
	return ret;
}
LCRYPTO_ALIAS(EVP_DigestFinal_ex);

int
EVP_Digest(const void *data, size_t count,
    unsigned char *md, unsigned int *size, const EVP_MD *type, ENGINE *impl)
{
	EVP_MD_CTX ctx;
	int ret;

	EVP_MD_CTX_legacy_clear(&ctx);
	EVP_MD_CTX_set_flags(&ctx, EVP_MD_CTX_FLAG_ONESHOT);
	ret = EVP_DigestInit_ex(&ctx, type, NULL) &&
	    EVP_DigestUpdate(&ctx, data, count) &&
	    EVP_DigestFinal_ex(&ctx, md, size);
	EVP_MD_CTX_cleanup(&ctx);

	return ret;
}
LCRYPTO_ALIAS(EVP_Digest);

EVP_MD_CTX *
EVP_MD_CTX_new(void)
{
	return calloc(1, sizeof(EVP_MD_CTX));
}
LCRYPTO_ALIAS(EVP_MD_CTX_new);

void
EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
	if (ctx == NULL)
		return;

	EVP_MD_CTX_cleanup(ctx);

	free(ctx);
}
LCRYPTO_ALIAS(EVP_MD_CTX_free);

EVP_MD_CTX *
EVP_MD_CTX_create(void)
{
	return EVP_MD_CTX_new();
}
LCRYPTO_ALIAS(EVP_MD_CTX_create);

void
EVP_MD_CTX_destroy(EVP_MD_CTX *ctx)
{
	EVP_MD_CTX_free(ctx);
}
LCRYPTO_ALIAS(EVP_MD_CTX_destroy);

void
EVP_MD_CTX_legacy_clear(EVP_MD_CTX *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

int
EVP_MD_CTX_init(EVP_MD_CTX *ctx)
{
	return EVP_MD_CTX_cleanup(ctx);
}
LCRYPTO_ALIAS(EVP_MD_CTX_init);

int
EVP_MD_CTX_reset(EVP_MD_CTX *ctx)
{
	return EVP_MD_CTX_cleanup(ctx);
}
LCRYPTO_ALIAS(EVP_MD_CTX_reset);

int
EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx)
{
	if (ctx == NULL)
		return 1;

	/*
	 * Don't assume ctx->md_data was cleaned in EVP_Digest_Final,
	 * because sometimes only copies of the context are ever finalised.
	 */
	if (ctx->digest && ctx->digest->cleanup &&
	    !EVP_MD_CTX_test_flags(ctx, EVP_MD_CTX_FLAG_CLEANED))
		ctx->digest->cleanup(ctx);
	if (ctx->digest && ctx->digest->ctx_size && ctx->md_data &&
	    !EVP_MD_CTX_test_flags(ctx, EVP_MD_CTX_FLAG_REUSE))
		freezero(ctx->md_data, ctx->digest->ctx_size);
	/*
	 * If EVP_MD_CTX_FLAG_KEEP_PKEY_CTX is set, EVP_MD_CTX_set_pkey() was
	 * called and its strange API contract implies we don't own ctx->pctx.
	 */
	if (!EVP_MD_CTX_test_flags(ctx, EVP_MD_CTX_FLAG_KEEP_PKEY_CTX))
		EVP_PKEY_CTX_free(ctx->pctx);
	memset(ctx, 0, sizeof(*ctx));

	return 1;
}
LCRYPTO_ALIAS(EVP_MD_CTX_cleanup);

int
EVP_MD_CTX_copy(EVP_MD_CTX *out, const EVP_MD_CTX *in)
{
	EVP_MD_CTX_legacy_clear(out);
	return EVP_MD_CTX_copy_ex(out, in);
}
LCRYPTO_ALIAS(EVP_MD_CTX_copy);

int
EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in)
{
	unsigned char *tmp_buf;

	if ((in == NULL) || (in->digest == NULL)) {
		EVPerror(EVP_R_INPUT_NOT_INITIALIZED);
		return 0;
	}

	if (out->digest == in->digest) {
		tmp_buf = out->md_data;
		EVP_MD_CTX_set_flags(out, EVP_MD_CTX_FLAG_REUSE);
	} else
		tmp_buf = NULL;
	EVP_MD_CTX_cleanup(out);
	memcpy(out, in, sizeof *out);
	out->md_data = NULL;
	out->pctx = NULL;

	/*
	 * Because of the EVP_PKEY_CTX_dup() below, EVP_MD_CTX_cleanup() needs
	 * to free out->pctx in all cases (even if this flag is set on in).
	 */
	EVP_MD_CTX_clear_flags(out, EVP_MD_CTX_FLAG_KEEP_PKEY_CTX);

	if (in->md_data && out->digest->ctx_size) {
		if (tmp_buf) {
			out->md_data = tmp_buf;
		} else {
			out->md_data = calloc(1, out->digest->ctx_size);
			if (out->md_data == NULL) {
				EVPerror(ERR_R_MALLOC_FAILURE);
				return 0;
			}
		}
		memcpy(out->md_data, in->md_data, out->digest->ctx_size);
	}

	out->update = in->update;

	if (in->pctx) {
		out->pctx = EVP_PKEY_CTX_dup(in->pctx);
		if (!out->pctx) {
			EVP_MD_CTX_cleanup(out);
			return 0;
		}
	}

	if (out->digest->copy)
		return out->digest->copy(out, in);

	return 1;
}
LCRYPTO_ALIAS(EVP_MD_CTX_copy_ex);

int
EVP_MD_CTX_ctrl(EVP_MD_CTX *ctx, int type, int arg, void *ptr)
{
	int ret;

	if (!ctx->digest) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}

	if (!ctx->digest->md_ctrl) {
		EVPerror(EVP_R_CTRL_NOT_IMPLEMENTED);
		return 0;
	}

	ret = ctx->digest->md_ctrl(ctx, type, arg, ptr);
	if (ret == -1) {
		EVPerror(EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED);
		return 0;
	}
	return ret;
}
LCRYPTO_ALIAS(EVP_MD_CTX_ctrl);

const EVP_MD *
EVP_MD_CTX_md(const EVP_MD_CTX *ctx)
{
	if (!ctx)
		return NULL;
	return ctx->digest;
}
LCRYPTO_ALIAS(EVP_MD_CTX_md);

void
EVP_MD_CTX_clear_flags(EVP_MD_CTX *ctx, int flags)
{
	ctx->flags &= ~flags;
}
LCRYPTO_ALIAS(EVP_MD_CTX_clear_flags);

void
EVP_MD_CTX_set_flags(EVP_MD_CTX *ctx, int flags)
{
	ctx->flags |= flags;
}
LCRYPTO_ALIAS(EVP_MD_CTX_set_flags);

int
EVP_MD_CTX_test_flags(const EVP_MD_CTX *ctx, int flags)
{
	return (ctx->flags & flags);
}
LCRYPTO_ALIAS(EVP_MD_CTX_test_flags);

void *
EVP_MD_CTX_md_data(const EVP_MD_CTX *ctx)
{
	return ctx->md_data;
}
LCRYPTO_ALIAS(EVP_MD_CTX_md_data);

EVP_PKEY_CTX *
EVP_MD_CTX_pkey_ctx(const EVP_MD_CTX *ctx)
{
	return ctx->pctx;
}
LCRYPTO_ALIAS(EVP_MD_CTX_pkey_ctx);

void
EVP_MD_CTX_set_pkey_ctx(EVP_MD_CTX *ctx, EVP_PKEY_CTX *pctx)
{
	if (EVP_MD_CTX_test_flags(ctx, EVP_MD_CTX_FLAG_KEEP_PKEY_CTX)) {
		EVP_MD_CTX_clear_flags(ctx, EVP_MD_CTX_FLAG_KEEP_PKEY_CTX);
	} else {
		EVP_PKEY_CTX_free(ctx->pctx);
	}

	ctx->pctx = pctx;

	if (pctx != NULL) {
		/*
		 * For unclear reasons it was decided that the caller keeps
		 * ownership of pctx. So a flag was invented to make sure we
		 * don't free it in EVP_MD_CTX_cleanup(). We also need to
		 * unset it in EVP_MD_CTX_copy_ex(). Fortunately, the flag
		 * isn't public...
		 */
		EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_KEEP_PKEY_CTX);
	}
}
LCRYPTO_ALIAS(EVP_MD_CTX_set_pkey_ctx);

int
EVP_MD_type(const EVP_MD *md)
{
	return md->type;
}
LCRYPTO_ALIAS(EVP_MD_type);

int
EVP_MD_pkey_type(const EVP_MD *md)
{
	return md->pkey_type;
}
LCRYPTO_ALIAS(EVP_MD_pkey_type);

int
EVP_MD_size(const EVP_MD *md)
{
	if (!md) {
		EVPerror(EVP_R_MESSAGE_DIGEST_IS_NULL);
		return -1;
	}
	return md->md_size;
}
LCRYPTO_ALIAS(EVP_MD_size);

unsigned long
EVP_MD_flags(const EVP_MD *md)
{
	return md->flags;
}
LCRYPTO_ALIAS(EVP_MD_flags);

int
EVP_MD_block_size(const EVP_MD *md)
{
	return md->block_size;
}
LCRYPTO_ALIAS(EVP_MD_block_size);

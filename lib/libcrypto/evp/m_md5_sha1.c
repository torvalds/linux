/* $OpenBSD: m_md5_sha1.c,v 1.9 2024/04/09 13:52:41 beck Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/objects.h>
#include <openssl/sha.h>

#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "evp_local.h"

struct md5_sha1_ctx {
	MD5_CTX md5;
	SHA_CTX sha1;
};

static int
md5_sha1_init(EVP_MD_CTX *ctx)
{
	struct md5_sha1_ctx *mdctx = ctx->md_data;

	if (!MD5_Init(&mdctx->md5))
		return 0;
	if (!SHA1_Init(&mdctx->sha1))
		return 0;

	return 1;
}

static int
md5_sha1_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	struct md5_sha1_ctx *mdctx = ctx->md_data;

	if (!MD5_Update(&mdctx->md5, data, count))
		return 0;
	if (!SHA1_Update(&mdctx->sha1, data, count))
		return 0;

	return 1;
}

static int
md5_sha1_final(EVP_MD_CTX *ctx, unsigned char *out)
{
	struct md5_sha1_ctx *mdctx = ctx->md_data;

	if (!MD5_Final(out, &mdctx->md5))
		return 0;
	if (!SHA1_Final(out + MD5_DIGEST_LENGTH, &mdctx->sha1))
		return 0;

	return 1;
}

static const EVP_MD md5_sha1_md = {
        .type = NID_md5_sha1,
        .pkey_type = NID_md5_sha1,
        .md_size = MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH,
        .flags = 0,
        .init = md5_sha1_init,
        .update = md5_sha1_update,
        .final = md5_sha1_final,
        .block_size = MD5_CBLOCK, /* MD5_CBLOCK == SHA_CBLOCK */
        .ctx_size = sizeof(EVP_MD *) + sizeof(struct md5_sha1_ctx),
};

const EVP_MD *
EVP_md5_sha1(void)
{
	return &md5_sha1_md;
}
LCRYPTO_ALIAS(EVP_md5_sha1);

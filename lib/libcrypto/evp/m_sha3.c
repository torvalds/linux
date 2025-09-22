/* $OpenBSD: m_sha3.c,v 1.4 2024/04/09 13:52:41 beck Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include "evp_local.h"
#include "sha3_internal.h"

static int
sha3_224_init(EVP_MD_CTX *ctx)
{
	return sha3_init(ctx->md_data, SHA3_224_DIGEST_LENGTH);
}

static int
sha3_224_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return sha3_update(ctx->md_data, data, count);
}

static int
sha3_224_final(EVP_MD_CTX *ctx, unsigned char *md)
{
	return sha3_final(md, ctx->md_data);
}

static const EVP_MD sha3_224_md = {
	.type = NID_sha3_224,
	.pkey_type = NID_RSA_SHA3_224,
	.md_size = SHA3_224_DIGEST_LENGTH,
	.flags = EVP_MD_FLAG_DIGALGID_ABSENT,
	.init = sha3_224_init,
	.update = sha3_224_update,
	.final = sha3_224_final,
	.copy = NULL,
	.cleanup = NULL,
	.block_size = SHA3_224_BLOCK_SIZE,
	.ctx_size = sizeof(EVP_MD *) + sizeof(sha3_ctx),
};

const EVP_MD *
EVP_sha3_224(void)
{
	return &sha3_224_md;
}
LCRYPTO_ALIAS(EVP_sha3_224);

static int
sha3_256_init(EVP_MD_CTX *ctx)
{
	return sha3_init(ctx->md_data, SHA3_256_DIGEST_LENGTH);
}

static int
sha3_256_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return sha3_update(ctx->md_data, data, count);
}

static int
sha3_256_final(EVP_MD_CTX *ctx, unsigned char *md)
{
	return sha3_final(md, ctx->md_data);
}

static const EVP_MD sha3_256_md = {
	.type = NID_sha3_256,
	.pkey_type = NID_RSA_SHA3_256,
	.md_size = SHA3_256_DIGEST_LENGTH,
	.flags = EVP_MD_FLAG_DIGALGID_ABSENT,
	.init = sha3_256_init,
	.update = sha3_256_update,
	.final = sha3_256_final,
	.copy = NULL,
	.cleanup = NULL,
	.block_size = SHA3_256_BLOCK_SIZE,
	.ctx_size = sizeof(EVP_MD *) + sizeof(sha3_ctx),
};

const EVP_MD *
EVP_sha3_256(void)
{
	return &sha3_256_md;
}
LCRYPTO_ALIAS(EVP_sha3_256);

static int
sha3_384_init(EVP_MD_CTX *ctx)
{
	return sha3_init(ctx->md_data, SHA3_384_DIGEST_LENGTH);
}

static int
sha3_384_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return sha3_update(ctx->md_data, data, count);
}

static int
sha3_384_final(EVP_MD_CTX *ctx, unsigned char *md)
{
	return sha3_final(md, ctx->md_data);
}

static const EVP_MD sha3_384_md = {
	.type = NID_sha3_384,
	.pkey_type = NID_RSA_SHA3_384,
	.md_size = SHA3_384_DIGEST_LENGTH,
	.flags = EVP_MD_FLAG_DIGALGID_ABSENT,
	.init = sha3_384_init,
	.update = sha3_384_update,
	.final = sha3_384_final,
	.copy = NULL,
	.cleanup = NULL,
	.block_size = SHA3_384_BLOCK_SIZE,
	.ctx_size = sizeof(EVP_MD *) + sizeof(sha3_ctx),
};

const EVP_MD *
EVP_sha3_384(void)
{
	return &sha3_384_md;
}
LCRYPTO_ALIAS(EVP_sha3_384);

static int
sha3_512_init(EVP_MD_CTX *ctx)
{
	return sha3_init(ctx->md_data, SHA3_512_DIGEST_LENGTH);
}

static int
sha3_512_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return sha3_update(ctx->md_data, data, count);
}

static int
sha3_512_final(EVP_MD_CTX *ctx, unsigned char *md)
{
	return sha3_final(md, ctx->md_data);
}

static const EVP_MD sha3_512_md = {
	.type = NID_sha3_512,
	.pkey_type = NID_RSA_SHA3_512,
	.md_size = SHA3_512_DIGEST_LENGTH,
	.flags = EVP_MD_FLAG_DIGALGID_ABSENT,
	.init = sha3_512_init,
	.update = sha3_512_update,
	.final = sha3_512_final,
	.copy = NULL,
	.cleanup = NULL,
	.block_size = SHA3_512_BLOCK_SIZE,
	.ctx_size = sizeof(EVP_MD *) + sizeof(sha3_ctx),
};

const EVP_MD *
EVP_sha3_512(void)
{
	return &sha3_512_md;
}
LCRYPTO_ALIAS(EVP_sha3_512);

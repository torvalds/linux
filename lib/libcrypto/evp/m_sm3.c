/*	$OpenBSD: m_sm3.c,v 1.7 2024/04/09 13:52:41 beck Exp $	*/
/*
 * Copyright (c) 2018, Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_SM3
#include <openssl/evp.h>
#include <openssl/sm3.h>

#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "evp_local.h"

static int
sm3_init(EVP_MD_CTX *ctx)
{
	return SM3_Init(ctx->md_data);
}

static int
sm3_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
	return SM3_Update(ctx->md_data, data, count);
}

static int
sm3_final(EVP_MD_CTX *ctx, unsigned char *md)
{
	return SM3_Final(md, ctx->md_data);
}

static const EVP_MD sm3_md = {
	.type = NID_sm3,
	.pkey_type = NID_sm3WithRSAEncryption,
	.md_size = SM3_DIGEST_LENGTH,
	.flags = EVP_MD_FLAG_DIGALGID_ABSENT,
	.init = sm3_init,
	.update = sm3_update,
	.final = sm3_final,
	.copy = NULL,
	.cleanup = NULL,
	.block_size = SM3_CBLOCK,
	.ctx_size = sizeof(EVP_MD *) + sizeof(SM3_CTX),
};

const EVP_MD *
EVP_sm3(void)
{
	return &sm3_md;
}
LCRYPTO_ALIAS(EVP_sm3);

#endif /* OPENSSL_NO_SM3 */

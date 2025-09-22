/* $OpenBSD: hash.c,v 1.26 2025/07/18 03:16:28 tb Exp $	 */
/* $EOM: hash.c,v 1.10 1999/04/17 23:20:34 niklas Exp $	 */

/*
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
 * Copyright (c) 1999 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <string.h>
#include <md5.h>
#include <sha1.h>
#include <sha2.h>

#include "hash.h"
#include "log.h"

void	hmac_init(struct hash *, unsigned char *, unsigned int);
void	hmac_final(unsigned char *, struct hash *);

/* Temporary hash contexts.  */
static union ANY_CTX {
	MD5_CTX		md5ctx;
	SHA1_CTX        sha1ctx;
	SHA2_CTX	sha2ctx;
} Ctx, Ctx2;

/* Temporary hash digest.  */
static unsigned char digest[HASH_MAX];

/* Encapsulation of hash functions.  */

static void
md5_init(union ANY_CTX *ctx)
{
	MD5Init(&ctx->md5ctx);
}

static void
md5_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	MD5Update(&ctx->md5ctx, data, len);
}

static void
md5_final(unsigned char *digest, union ANY_CTX *ctx)
{
	MD5Final(digest, &ctx->md5ctx);
}

static void
sha1_init(union ANY_CTX *ctx)
{
	SHA1Init(&ctx->sha1ctx);
}

static void
sha1_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA1Update(&ctx->sha1ctx, data, len);
}

static void
sha1_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA1Final(digest, &ctx->sha1ctx);
}

static void
sha256_init(union ANY_CTX *ctx)
{
	SHA256Init(&ctx->sha2ctx);
}

static void
sha256_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA256Update(&ctx->sha2ctx, data, len);
}

static void
sha256_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA256Final(digest, &ctx->sha2ctx);
}

static void
sha384_init(union ANY_CTX *ctx)
{
	SHA384Init(&ctx->sha2ctx);
}

static void
sha384_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA384Update(&ctx->sha2ctx, data, len);
}

static void
sha384_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA384Final(digest, &ctx->sha2ctx);
}

static void
sha512_init(union ANY_CTX *ctx)
{
	SHA512Init(&ctx->sha2ctx);
}

static void
sha512_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA512Update(&ctx->sha2ctx, data, len);
}

static void
sha512_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA512Final(digest, &ctx->sha2ctx);
}

static struct hash hashes[] = {
    {
	HASH_MD5, 5, MD5_SIZE, MD5_BLOCK_LENGTH, (void *)&Ctx.md5ctx, digest,
	sizeof(MD5_CTX), (void *)&Ctx2.md5ctx,
	md5_init,
	md5_update,
	md5_final,
	hmac_init,
	hmac_final
    }, {
	HASH_SHA1, 6, SHA1_SIZE, SHA1_BLOCK_LENGTH, (void *)&Ctx.sha1ctx,
	digest, sizeof(SHA1_CTX), (void *)&Ctx2.sha1ctx,
	sha1_init,
	sha1_update,
	sha1_final,
	hmac_init,
	hmac_final
    }, {
	HASH_SHA2_256, 7, SHA2_256_SIZE, SHA256_BLOCK_LENGTH,
	(void *)&Ctx.sha2ctx, digest, sizeof(SHA2_CTX), (void *)&Ctx2.sha2ctx,
	sha256_init,
	sha256_update,
	sha256_final,
	hmac_init,
	hmac_final
    }, {
	HASH_SHA2_384, 8, SHA2_384_SIZE, SHA384_BLOCK_LENGTH,
	(void *)&Ctx.sha2ctx, digest, sizeof(SHA2_CTX), (void *)&Ctx2.sha2ctx,
	sha384_init,
	sha384_update,
	sha384_final,
	hmac_init,
	hmac_final
    }, {
	HASH_SHA2_512, 9, SHA2_512_SIZE, SHA512_BLOCK_LENGTH,
	(void *)&Ctx.sha2ctx, digest, sizeof(SHA2_CTX), (void *)&Ctx2.sha2ctx,
	sha512_init,
	sha512_update,
	sha512_final,
	hmac_init,
	hmac_final
    }
};

struct hash *
hash_get(enum hashes hashtype)
{
	size_t	i;

	LOG_DBG((LOG_CRYPTO, 60, "hash_get: requested algorithm %d",
	    hashtype));

	for (i = 0; i < sizeof hashes / sizeof hashes[0]; i++)
		if (hashtype == hashes[i].type)
			return &hashes[i];

	return 0;
}

/*
 * Initial a hash for HMAC usage this requires a special init function.
 * ctx, ctx2 hold the contexts, if you want to use the hash object for
 * something else in the meantime, be sure to store the contexts somewhere.
 */

void
hmac_init(struct hash *hash, unsigned char *okey, unsigned int len)
{
	unsigned int    i;
	unsigned char   key[128];

	bzero(key, sizeof(key));
	if (len > hash->blocklen) {
		/* Truncate key down to blocklen */
		hash->Init(hash->ctx);
		hash->Update(hash->ctx, okey, len);
		hash->Final(key, hash->ctx);
	} else {
		memcpy(key, okey, len);
	}

	/* HMAC I and O pad computation */
	for (i = 0; i < hash->blocklen; i++)
		key[i] ^= HMAC_IPAD_VAL;

	hash->Init(hash->ctx);
	hash->Update(hash->ctx, key, hash->blocklen);

	for (i = 0; i < hash->blocklen; i++)
		key[i] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

	hash->Init(hash->ctx2);
	hash->Update(hash->ctx2, key, hash->blocklen);

	explicit_bzero(key, sizeof(key));
}

/*
 * HMAC Final function
 */

void
hmac_final(unsigned char *dgst, struct hash *hash)
{
	hash->Final(dgst, hash->ctx);
	hash->Update(hash->ctx2, dgst, hash->hashsize);
	hash->Final(dgst, hash->ctx2);
}

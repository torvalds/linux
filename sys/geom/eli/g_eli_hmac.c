/*-
 * Copyright (c) 2005-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#elif defined(_STANDALONE)
#include "stand.h"
#else
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <openssl/evp.h>
#define	_OpenSSL_
#endif
#include <geom/eli/g_eli.h>

void
g_eli_crypto_hmac_init(struct hmac_ctx *ctx, const uint8_t *hkey,
    size_t hkeylen)
{
	u_char k_ipad[128], k_opad[128], key[128];
	SHA512_CTX lctx;
	u_int i;

	bzero(key, sizeof(key));
	if (hkeylen == 0)
		; /* do nothing */
	else if (hkeylen <= 128)
		bcopy(hkey, key, hkeylen);
	else {
		/* If key is longer than 128 bytes reset it to key = SHA512(key). */
		SHA512_Init(&lctx);
		SHA512_Update(&lctx, hkey, hkeylen);
		SHA512_Final(key, &lctx);
	}

	/* XOR key with ipad and opad values. */
	for (i = 0; i < sizeof(key); i++) {
		k_ipad[i] = key[i] ^ 0x36;
		k_opad[i] = key[i] ^ 0x5c;
	}
	explicit_bzero(key, sizeof(key));
	/* Start inner SHA512. */
	SHA512_Init(&ctx->innerctx);
	SHA512_Update(&ctx->innerctx, k_ipad, sizeof(k_ipad));
	explicit_bzero(k_ipad, sizeof(k_ipad));
	/* Start outer SHA512. */
	SHA512_Init(&ctx->outerctx);
	SHA512_Update(&ctx->outerctx, k_opad, sizeof(k_opad));
	explicit_bzero(k_opad, sizeof(k_opad));
}

void
g_eli_crypto_hmac_update(struct hmac_ctx *ctx, const uint8_t *data,
    size_t datasize)
{

	SHA512_Update(&ctx->innerctx, data, datasize);
}

void
g_eli_crypto_hmac_final(struct hmac_ctx *ctx, uint8_t *md, size_t mdsize)
{
	u_char digest[SHA512_MDLEN];

	/* Complete inner hash */
	SHA512_Final(digest, &ctx->innerctx);
	
	/* Complete outer hash */
	SHA512_Update(&ctx->outerctx, digest, sizeof(digest));
	SHA512_Final(digest, &ctx->outerctx);
	
	explicit_bzero(ctx, sizeof(*ctx));
	/* mdsize == 0 means "Give me the whole hash!" */
	if (mdsize == 0)
		mdsize = SHA512_MDLEN;
	bcopy(digest, md, mdsize);
	explicit_bzero(digest, sizeof(digest));
}

void
g_eli_crypto_hmac(const uint8_t *hkey, size_t hkeysize, const uint8_t *data,
    size_t datasize, uint8_t *md, size_t mdsize)
{
	struct hmac_ctx ctx;

	g_eli_crypto_hmac_init(&ctx, hkey, hkeysize);
	g_eli_crypto_hmac_update(&ctx, data, datasize);
	g_eli_crypto_hmac_final(&ctx, md, mdsize);
}

/*
 * Here we generate IV. It is unique for every sector.
 */
void
g_eli_crypto_ivgen(struct g_eli_softc *sc, off_t offset, u_char *iv,
    size_t size)
{
	uint8_t off[8];

	if ((sc->sc_flags & G_ELI_FLAG_NATIVE_BYTE_ORDER) != 0)
		bcopy(&offset, off, sizeof(off));
	else
		le64enc(off, (uint64_t)offset);

	switch (sc->sc_ealgo) {
	case CRYPTO_AES_XTS:
		bcopy(off, iv, sizeof(off));
		bzero(iv + sizeof(off), size - sizeof(off));
		break;
	default:
	    {
		u_char hash[SHA256_DIGEST_LENGTH];
		SHA256_CTX ctx;

		/* Copy precalculated SHA256 context for IV-Key. */
		bcopy(&sc->sc_ivctx, &ctx, sizeof(ctx));
		SHA256_Update(&ctx, off, sizeof(off));
		SHA256_Final(hash, &ctx);
		bcopy(hash, iv, MIN(sizeof(hash), size));
		break;
	    }
	}
}

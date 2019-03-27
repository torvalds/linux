/* This file is in the public domain. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <opencrypto/xform_auth.h>
#include <opencrypto/xform_poly1305.h>

#include <sodium/crypto_onetimeauth_poly1305.h>

struct poly1305_xform_ctx {
	struct crypto_onetimeauth_poly1305_state state;
};
CTASSERT(sizeof(union authctx) >= sizeof(struct poly1305_xform_ctx));

CTASSERT(POLY1305_KEY_LEN == crypto_onetimeauth_poly1305_KEYBYTES);
CTASSERT(POLY1305_HASH_LEN == crypto_onetimeauth_poly1305_BYTES);

void
Poly1305_Init(struct poly1305_xform_ctx *polyctx)
{
	/* Nop */
}

void
Poly1305_Setkey(struct poly1305_xform_ctx *polyctx,
    const uint8_t key[__min_size(POLY1305_KEY_LEN)], size_t klen)
{
	int rc;

	if (klen != POLY1305_KEY_LEN)
		panic("%s: Bogus keylen: %u bytes", __func__, (unsigned)klen);

	rc = crypto_onetimeauth_poly1305_init(&polyctx->state, key);
	if (rc != 0)
		panic("%s: Invariant violated: %d", __func__, rc);
}

static void
xform_Poly1305_Setkey(void *ctx, const uint8_t *key, uint16_t klen)
{
	Poly1305_Setkey(ctx, key, klen);
}

int
Poly1305_Update(struct poly1305_xform_ctx *polyctx, const void *data,
    size_t len)
{
	int rc;

	rc = crypto_onetimeauth_poly1305_update(&polyctx->state, data, len);
	if (rc != 0)
		panic("%s: Invariant violated: %d", __func__, rc);
	return (0);
}

static int
xform_Poly1305_Update(void *ctx, const uint8_t *data, uint16_t len)
{
	return (Poly1305_Update(ctx, data, len));
}

void
Poly1305_Final(uint8_t digest[__min_size(POLY1305_HASH_LEN)],
    struct poly1305_xform_ctx *polyctx)
{
	int rc;

	rc = crypto_onetimeauth_poly1305_final(&polyctx->state, digest);
	if (rc != 0)
		panic("%s: Invariant violated: %d", __func__, rc);
}

static void
xform_Poly1305_Final(uint8_t *digest, void *ctx)
{
	Poly1305_Final(digest, ctx);
}

struct auth_hash auth_hash_poly1305 = {
	.type = CRYPTO_POLY1305,
	.name = "Poly-1305",
	.keysize = POLY1305_KEY_LEN,
	.hashsize = POLY1305_HASH_LEN,
	.ctxsize = sizeof(struct poly1305_xform_ctx),
	.blocksize = crypto_onetimeauth_poly1305_BYTES,
	.Init = (void *)Poly1305_Init,
	.Setkey = xform_Poly1305_Setkey,
	.Update = xform_Poly1305_Update,
	.Final = xform_Poly1305_Final,
};

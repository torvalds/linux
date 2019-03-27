/* This file is in the public domain. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <contrib/libb2/blake2.h>
#include <opencrypto/xform_auth.h>

extern int blake2b_init_ref(blake2b_state *S, size_t outlen);
extern int blake2b_init_param_ref(blake2b_state *S, const blake2b_param *P);
extern int blake2b_init_key_ref(blake2b_state *S, size_t outlen,
    const void *key, size_t keylen);
extern int blake2b_update_ref(blake2b_state *S, const uint8_t *in,
    size_t inlen);
extern int blake2b_final_ref(blake2b_state *S, uint8_t *out, size_t outlen);
extern int blake2b_ref(uint8_t *out, const void *in, const void *key,
    size_t outlen, size_t inlen, size_t keylen);

extern int blake2s_init_ref(blake2s_state *S, size_t outlen);
extern int blake2s_init_param_ref(blake2s_state *S, const blake2s_param *P);
extern int blake2s_init_key_ref(blake2s_state *S, size_t outlen,
    const void *key, size_t keylen);
extern int blake2s_update_ref(blake2s_state *S, const uint8_t *in,
    size_t inlen);
extern int blake2s_final_ref(blake2s_state *S, uint8_t *out, size_t outlen);
extern int blake2s_ref(uint8_t *out, const void *in, const void *key,
    size_t outlen, size_t inlen, size_t keylen);

struct blake2b_xform_ctx {
	blake2b_state state;
	uint8_t key[BLAKE2B_KEYBYTES];
	uint16_t klen;
};
CTASSERT(sizeof(union authctx) >= sizeof(struct blake2b_xform_ctx));

static void
blake2b_xform_init(void *vctx)
{
	struct blake2b_xform_ctx *ctx = vctx;
	int rc;

	if (ctx->klen > 0)
		rc = blake2b_init_key_ref(&ctx->state, BLAKE2B_OUTBYTES,
		    ctx->key, ctx->klen);
	else
		rc = blake2b_init_ref(&ctx->state, BLAKE2B_OUTBYTES);
	if (rc != 0)
		panic("blake2b_init_key: invalid arguments");
}

static void
blake2b_xform_setkey(void *vctx, const uint8_t *key, uint16_t klen)
{
	struct blake2b_xform_ctx *ctx = vctx;

	if (klen > sizeof(ctx->key))
		panic("invalid klen %u", (unsigned)klen);
	memcpy(ctx->key, key, klen);
	ctx->klen = klen;
}

static int
blake2b_xform_update(void *vctx, const uint8_t *data, uint16_t len)
{
	struct blake2b_xform_ctx *ctx = vctx;
	int rc;

	rc = blake2b_update_ref(&ctx->state, data, len);
	if (rc != 0)
		return (EINVAL);
	return (0);
}

static void
blake2b_xform_final(uint8_t *out, void *vctx)
{
	struct blake2b_xform_ctx *ctx = vctx;
	int rc;

	rc = blake2b_final_ref(&ctx->state, out, BLAKE2B_OUTBYTES);
	if (rc != 0)
		panic("blake2b_final: invalid");
}

struct auth_hash auth_hash_blake2b = {
	.type = CRYPTO_BLAKE2B,
	.name = "Blake2b",
	.keysize = BLAKE2B_KEYBYTES,
	.hashsize = BLAKE2B_OUTBYTES,
	.ctxsize = sizeof(struct blake2b_xform_ctx),
	.Setkey = blake2b_xform_setkey,
	.Init = blake2b_xform_init,
	.Update = blake2b_xform_update,
	.Final = blake2b_xform_final,
};

struct blake2s_xform_ctx {
	blake2s_state state;
	uint8_t key[BLAKE2S_KEYBYTES];
	uint16_t klen;
};
CTASSERT(sizeof(union authctx) >= sizeof(struct blake2s_xform_ctx));

static void
blake2s_xform_init(void *vctx)
{
	struct blake2s_xform_ctx *ctx = vctx;
	int rc;

	if (ctx->klen > 0)
		rc = blake2s_init_key_ref(&ctx->state, BLAKE2S_OUTBYTES,
		    ctx->key, ctx->klen);
	else
		rc = blake2s_init_ref(&ctx->state, BLAKE2S_OUTBYTES);
	if (rc != 0)
		panic("blake2s_init_key: invalid arguments");
}

static void
blake2s_xform_setkey(void *vctx, const uint8_t *key, uint16_t klen)
{
	struct blake2s_xform_ctx *ctx = vctx;

	if (klen > sizeof(ctx->key))
		panic("invalid klen %u", (unsigned)klen);
	memcpy(ctx->key, key, klen);
	ctx->klen = klen;
}

static int
blake2s_xform_update(void *vctx, const uint8_t *data, uint16_t len)
{
	struct blake2s_xform_ctx *ctx = vctx;
	int rc;

	rc = blake2s_update_ref(&ctx->state, data, len);
	if (rc != 0)
		return (EINVAL);
	return (0);
}

static void
blake2s_xform_final(uint8_t *out, void *vctx)
{
	struct blake2s_xform_ctx *ctx = vctx;
	int rc;

	rc = blake2s_final_ref(&ctx->state, out, BLAKE2S_OUTBYTES);
	if (rc != 0)
		panic("blake2s_final: invalid");
}

struct auth_hash auth_hash_blake2s = {
	.type = CRYPTO_BLAKE2S,
	.name = "Blake2s",
	.keysize = BLAKE2S_KEYBYTES,
	.hashsize = BLAKE2S_OUTBYTES,
	.ctxsize = sizeof(struct blake2s_xform_ctx),
	.Setkey = blake2s_xform_setkey,
	.Init = blake2s_xform_init,
	.Update = blake2s_xform_update,
	.Final = blake2s_xform_final,
};

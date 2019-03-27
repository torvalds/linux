/* This file is in the public domain. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <crypto/chacha20/chacha.h>
#include <opencrypto/xform_enc.h>

static int
chacha20_xform_setkey(u_int8_t **sched, u_int8_t *key, int len)
{
	struct chacha_ctx *ctx;

	if (len != CHACHA_MINKEYLEN && len != 32)
		return (EINVAL);

	ctx = malloc(sizeof(*ctx), M_CRYPTO_DATA, M_NOWAIT | M_ZERO);
	*sched = (void *)ctx;
	if (ctx == NULL)
		return (ENOMEM);

	chacha_keysetup(ctx, key, len * 8);
	return (0);
}

static void
chacha20_xform_reinit(caddr_t key, u_int8_t *iv)
{
	struct chacha_ctx *ctx;

	ctx = (void *)key;
	chacha_ivsetup(ctx, iv + 8, iv);
}

static void
chacha20_xform_zerokey(u_int8_t **sched)
{
	struct chacha_ctx *ctx;

	ctx = (void *)*sched;
	explicit_bzero(ctx, sizeof(*ctx));
	free(ctx, M_CRYPTO_DATA);
	*sched = NULL;
}

static void
chacha20_xform_crypt(caddr_t cctx, u_int8_t *bytes)
{
	struct chacha_ctx *ctx;

	ctx = (void *)cctx;
	chacha_encrypt_bytes(ctx, bytes, bytes, 1);
}

static void
chacha20_xform_crypt_multi(void *vctx, uint8_t *bytes, size_t len)
{
	struct chacha_ctx *ctx;

	ctx = vctx;
	chacha_encrypt_bytes(ctx, bytes, bytes, len);
}

struct enc_xform enc_xform_chacha20 = {
	.type = CRYPTO_CHACHA20,
	.name = "chacha20",
	.blocksize = 1,
	.ivsize = CHACHA_NONCELEN + CHACHA_CTRLEN,
	.minkey = CHACHA_MINKEYLEN,
	.maxkey = 32,
	.encrypt = chacha20_xform_crypt,
	.decrypt = chacha20_xform_crypt,
	.setkey = chacha20_xform_setkey,
	.zerokey = chacha20_xform_zerokey,
	.reinit = chacha20_xform_reinit,
	.encrypt_multi = chacha20_xform_crypt_multi,
	.decrypt_multi = chacha20_xform_crypt_multi,
};

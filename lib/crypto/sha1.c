// SPDX-License-Identifier: GPL-2.0
/*
 * SHA-1 and HMAC-SHA1 library functions
 */

#include <crypto/hmac.h>
#include <crypto/sha1.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/wordpart.h>

static const struct sha1_block_state sha1_iv = {
	.h = { SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 },
};

/*
 * If you have 32 registers or more, the compiler can (and should)
 * try to change the array[] accesses into registers. However, on
 * machines with less than ~25 registers, that won't really work,
 * and at least gcc will make an unholy mess of it.
 *
 * So to avoid that mess which just slows things down, we force
 * the stores to memory to actually happen (we might be better off
 * with a 'W(t)=(val);asm("":"+m" (W(t))' there instead, as
 * suggested by Artur Skawina - that will also make gcc unable to
 * try to do the silly "optimize away loads" part because it won't
 * see what the value will be).
 *
 * Ben Herrenschmidt reports that on PPC, the C version comes close
 * to the optimized asm with this (ie on PPC you don't want that
 * 'volatile', since there are lots of registers).
 *
 * On ARM we get the best code generation by forcing a full memory barrier
 * between each SHA_ROUND, otherwise gcc happily get wild with spilling and
 * the stack frame size simply explode and performance goes down the drain.
 */

#ifdef CONFIG_X86
  #define setW(x, val) (*(volatile __u32 *)&W(x) = (val))
#elif defined(CONFIG_ARM)
  #define setW(x, val) do { W(x) = (val); __asm__("":::"memory"); } while (0)
#else
  #define setW(x, val) (W(x) = (val))
#endif

/* This "rolls" over the 512-bit array */
#define W(x) (array[(x)&15])

/*
 * Where do we get the source from? The first 16 iterations get it from
 * the input data, the next mix it from the 512-bit array.
 */
#define SHA_SRC(t) get_unaligned_be32((__u32 *)data + t)
#define SHA_MIX(t) rol32(W(t+13) ^ W(t+8) ^ W(t+2) ^ W(t), 1)

#define SHA_ROUND(t, input, fn, constant, A, B, C, D, E) do { \
	__u32 TEMP = input(t); setW(t, TEMP); \
	E += TEMP + rol32(A,5) + (fn) + (constant); \
	B = ror32(B, 2); \
	TEMP = E; E = D; D = C; C = B; B = A; A = TEMP; } while (0)

#define T_0_15(t, A, B, C, D, E)  SHA_ROUND(t, SHA_SRC, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_16_19(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_20_39(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) , 0x6ed9eba1, A, B, C, D, E )
#define T_40_59(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, ((B&C)+(D&(B^C))) , 0x8f1bbcdc, A, B, C, D, E )
#define T_60_79(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) ,  0xca62c1d6, A, B, C, D, E )

/**
 * sha1_transform - single block SHA1 transform (deprecated)
 *
 * @digest: 160 bit digest to update
 * @data:   512 bits of data to hash
 * @array:  16 words of workspace (see note)
 *
 * This function executes SHA-1's internal compression function.  It updates the
 * 160-bit internal state (@digest) with a single 512-bit data block (@data).
 *
 * Don't use this function.  SHA-1 is no longer considered secure.  And even if
 * you do have to use SHA-1, this isn't the correct way to hash something with
 * SHA-1 as this doesn't handle padding and finalization.
 *
 * Note: If the hash is security sensitive, the caller should be sure
 * to clear the workspace. This is left to the caller to avoid
 * unnecessary clears between chained hashing operations.
 */
void sha1_transform(__u32 *digest, const char *data, __u32 *array)
{
	__u32 A, B, C, D, E;
	unsigned int i = 0;

	A = digest[0];
	B = digest[1];
	C = digest[2];
	D = digest[3];
	E = digest[4];

	/* Round 1 - iterations 0-16 take their input from 'data' */
	for (; i < 16; ++i)
		T_0_15(i, A, B, C, D, E);

	/* Round 1 - tail. Input from 512-bit mixing array */
	for (; i < 20; ++i)
		T_16_19(i, A, B, C, D, E);

	/* Round 2 */
	for (; i < 40; ++i)
		T_20_39(i, A, B, C, D, E);

	/* Round 3 */
	for (; i < 60; ++i)
		T_40_59(i, A, B, C, D, E);

	/* Round 4 */
	for (; i < 80; ++i)
		T_60_79(i, A, B, C, D, E);

	digest[0] += A;
	digest[1] += B;
	digest[2] += C;
	digest[3] += D;
	digest[4] += E;
}
EXPORT_SYMBOL(sha1_transform);

/**
 * sha1_init_raw - initialize the vectors for a SHA1 digest
 * @buf: vector to initialize
 */
void sha1_init_raw(__u32 *buf)
{
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;
	buf[4] = 0xc3d2e1f0;
}
EXPORT_SYMBOL(sha1_init_raw);

static void __maybe_unused sha1_blocks_generic(struct sha1_block_state *state,
					       const u8 *data, size_t nblocks)
{
	u32 workspace[SHA1_WORKSPACE_WORDS];

	do {
		sha1_transform(state->h, data, workspace);
		data += SHA1_BLOCK_SIZE;
	} while (--nblocks);

	memzero_explicit(workspace, sizeof(workspace));
}

#ifdef CONFIG_CRYPTO_LIB_SHA1_ARCH
#include "sha1.h" /* $(SRCARCH)/sha1.h */
#else
#define sha1_blocks sha1_blocks_generic
#endif

void sha1_init(struct sha1_ctx *ctx)
{
	ctx->state = sha1_iv;
	ctx->bytecount = 0;
}
EXPORT_SYMBOL_GPL(sha1_init);

void sha1_update(struct sha1_ctx *ctx, const u8 *data, size_t len)
{
	size_t partial = ctx->bytecount % SHA1_BLOCK_SIZE;

	ctx->bytecount += len;

	if (partial + len >= SHA1_BLOCK_SIZE) {
		size_t nblocks;

		if (partial) {
			size_t l = SHA1_BLOCK_SIZE - partial;

			memcpy(&ctx->buf[partial], data, l);
			data += l;
			len -= l;

			sha1_blocks(&ctx->state, ctx->buf, 1);
		}

		nblocks = len / SHA1_BLOCK_SIZE;
		len %= SHA1_BLOCK_SIZE;

		if (nblocks) {
			sha1_blocks(&ctx->state, data, nblocks);
			data += nblocks * SHA1_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(&ctx->buf[partial], data, len);
}
EXPORT_SYMBOL_GPL(sha1_update);

static void __sha1_final(struct sha1_ctx *ctx, u8 out[SHA1_DIGEST_SIZE])
{
	u64 bitcount = ctx->bytecount << 3;
	size_t partial = ctx->bytecount % SHA1_BLOCK_SIZE;

	ctx->buf[partial++] = 0x80;
	if (partial > SHA1_BLOCK_SIZE - 8) {
		memset(&ctx->buf[partial], 0, SHA1_BLOCK_SIZE - partial);
		sha1_blocks(&ctx->state, ctx->buf, 1);
		partial = 0;
	}
	memset(&ctx->buf[partial], 0, SHA1_BLOCK_SIZE - 8 - partial);
	*(__be64 *)&ctx->buf[SHA1_BLOCK_SIZE - 8] = cpu_to_be64(bitcount);
	sha1_blocks(&ctx->state, ctx->buf, 1);

	for (size_t i = 0; i < SHA1_DIGEST_SIZE; i += 4)
		put_unaligned_be32(ctx->state.h[i / 4], out + i);
}

void sha1_final(struct sha1_ctx *ctx, u8 out[SHA1_DIGEST_SIZE])
{
	__sha1_final(ctx, out);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(sha1_final);

void sha1(const u8 *data, size_t len, u8 out[SHA1_DIGEST_SIZE])
{
	struct sha1_ctx ctx;

	sha1_init(&ctx);
	sha1_update(&ctx, data, len);
	sha1_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(sha1);

static void __hmac_sha1_preparekey(struct sha1_block_state *istate,
				   struct sha1_block_state *ostate,
				   const u8 *raw_key, size_t raw_key_len)
{
	union {
		u8 b[SHA1_BLOCK_SIZE];
		unsigned long w[SHA1_BLOCK_SIZE / sizeof(unsigned long)];
	} derived_key = { 0 };

	if (unlikely(raw_key_len > SHA1_BLOCK_SIZE))
		sha1(raw_key, raw_key_len, derived_key.b);
	else
		memcpy(derived_key.b, raw_key, raw_key_len);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_IPAD_VALUE);
	*istate = sha1_iv;
	sha1_blocks(istate, derived_key.b, 1);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_OPAD_VALUE ^
						HMAC_IPAD_VALUE);
	*ostate = sha1_iv;
	sha1_blocks(ostate, derived_key.b, 1);

	memzero_explicit(&derived_key, sizeof(derived_key));
}

void hmac_sha1_preparekey(struct hmac_sha1_key *key,
			  const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha1_preparekey(&key->istate, &key->ostate,
			       raw_key, raw_key_len);
}
EXPORT_SYMBOL_GPL(hmac_sha1_preparekey);

void hmac_sha1_init(struct hmac_sha1_ctx *ctx, const struct hmac_sha1_key *key)
{
	ctx->sha_ctx.state = key->istate;
	ctx->sha_ctx.bytecount = SHA1_BLOCK_SIZE;
	ctx->ostate = key->ostate;
}
EXPORT_SYMBOL_GPL(hmac_sha1_init);

void hmac_sha1_init_usingrawkey(struct hmac_sha1_ctx *ctx,
				const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha1_preparekey(&ctx->sha_ctx.state, &ctx->ostate,
			       raw_key, raw_key_len);
	ctx->sha_ctx.bytecount = SHA1_BLOCK_SIZE;
}
EXPORT_SYMBOL_GPL(hmac_sha1_init_usingrawkey);

void hmac_sha1_final(struct hmac_sha1_ctx *ctx, u8 out[SHA1_DIGEST_SIZE])
{
	/* Generate the padded input for the outer hash in ctx->sha_ctx.buf. */
	__sha1_final(&ctx->sha_ctx, ctx->sha_ctx.buf);
	memset(&ctx->sha_ctx.buf[SHA1_DIGEST_SIZE], 0,
	       SHA1_BLOCK_SIZE - SHA1_DIGEST_SIZE);
	ctx->sha_ctx.buf[SHA1_DIGEST_SIZE] = 0x80;
	*(__be32 *)&ctx->sha_ctx.buf[SHA1_BLOCK_SIZE - 4] =
		cpu_to_be32(8 * (SHA1_BLOCK_SIZE + SHA1_DIGEST_SIZE));

	/* Compute the outer hash, which gives the HMAC value. */
	sha1_blocks(&ctx->ostate, ctx->sha_ctx.buf, 1);
	for (size_t i = 0; i < SHA1_DIGEST_SIZE; i += 4)
		put_unaligned_be32(ctx->ostate.h[i / 4], out + i);

	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(hmac_sha1_final);

void hmac_sha1(const struct hmac_sha1_key *key,
	       const u8 *data, size_t data_len, u8 out[SHA1_DIGEST_SIZE])
{
	struct hmac_sha1_ctx ctx;

	hmac_sha1_init(&ctx, key);
	hmac_sha1_update(&ctx, data, data_len);
	hmac_sha1_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha1);

void hmac_sha1_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			   const u8 *data, size_t data_len,
			   u8 out[SHA1_DIGEST_SIZE])
{
	struct hmac_sha1_ctx ctx;

	hmac_sha1_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_sha1_update(&ctx, data, data_len);
	hmac_sha1_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha1_usingrawkey);

#ifdef sha1_mod_init_arch
static int __init sha1_mod_init(void)
{
	sha1_mod_init_arch();
	return 0;
}
subsys_initcall(sha1_mod_init);

static void __exit sha1_mod_exit(void)
{
}
module_exit(sha1_mod_exit);
#endif

MODULE_DESCRIPTION("SHA-1 and HMAC-SHA1 library functions");
MODULE_LICENSE("GPL");

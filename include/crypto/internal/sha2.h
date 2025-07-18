/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _CRYPTO_INTERNAL_SHA2_H
#define _CRYPTO_INTERNAL_SHA2_H

#include <crypto/internal/simd.h>
#include <crypto/sha2.h>
#include <linux/compiler_attributes.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#if IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_SHA256)
bool sha256_is_arch_optimized(void);
#else
static inline bool sha256_is_arch_optimized(void)
{
	return false;
}
#endif
void sha256_blocks_generic(u32 state[SHA256_STATE_WORDS],
			   const u8 *data, size_t nblocks);
void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks);
void sha256_blocks_simd(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks);

static __always_inline void sha256_choose_blocks(
	u32 state[SHA256_STATE_WORDS], const u8 *data, size_t nblocks,
	bool force_generic, bool force_simd)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_SHA256) || force_generic)
		sha256_blocks_generic(state, data, nblocks);
	else if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_SHA256_SIMD) &&
		 (force_simd || crypto_simd_usable()))
		sha256_blocks_simd(state, data, nblocks);
	else
		sha256_blocks_arch(state, data, nblocks);
}

static __always_inline void sha256_finup(
	struct crypto_sha256_state *sctx, u8 buf[SHA256_BLOCK_SIZE],
	size_t len, u8 out[SHA256_DIGEST_SIZE], size_t digest_size,
	bool force_generic, bool force_simd)
{
	const size_t bit_offset = SHA256_BLOCK_SIZE - 8;
	__be64 *bits = (__be64 *)&buf[bit_offset];
	int i;

	buf[len++] = 0x80;
	if (len > bit_offset) {
		memset(&buf[len], 0, SHA256_BLOCK_SIZE - len);
		sha256_choose_blocks(sctx->state, buf, 1, force_generic,
				     force_simd);
		len = 0;
	}

	memset(&buf[len], 0, bit_offset - len);
	*bits = cpu_to_be64(sctx->count << 3);
	sha256_choose_blocks(sctx->state, buf, 1, force_generic, force_simd);

	for (i = 0; i < digest_size; i += 4)
		put_unaligned_be32(sctx->state[i / 4], out + i);
}

#endif /* _CRYPTO_INTERNAL_SHA2_H */

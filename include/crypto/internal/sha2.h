/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _CRYPTO_INTERNAL_SHA2_H
#define _CRYPTO_INTERNAL_SHA2_H

#include <crypto/sha2.h>

void sha256_update_generic(struct sha256_state *sctx,
			   const u8 *data, size_t len);
void sha256_final_generic(struct sha256_state *sctx,
			  u8 out[SHA256_DIGEST_SIZE]);
void sha224_final_generic(struct sha256_state *sctx,
			  u8 out[SHA224_DIGEST_SIZE]);

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

#endif /* _CRYPTO_INTERNAL_SHA2_H */

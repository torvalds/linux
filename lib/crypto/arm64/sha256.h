/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for ARM64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

asmlinkage void sha256_block_data_order(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage void sha256_block_neon(struct sha256_block_state *state,
				  const u8 *data, size_t nblocks);
asmlinkage void sha256_ce_transform(struct sha256_block_state *state,
				    const u8 *data, size_t nblocks);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_neon) && likely(may_use_simd())) {
		scoped_ksimd() {
			if (static_branch_likely(&have_ce))
				sha256_ce_transform(state, data, nblocks);
			else
				sha256_block_neon(state, data, nblocks);
		}
	} else {
		sha256_block_data_order(state, data, nblocks);
	}
}

static_assert(offsetof(struct __sha256_ctx, state) == 0);
static_assert(offsetof(struct __sha256_ctx, bytecount) == 32);
static_assert(offsetof(struct __sha256_ctx, buf) == 40);
asmlinkage void sha256_ce_finup2x(const struct __sha256_ctx *ctx,
				  const u8 *data1, const u8 *data2, int len,
				  u8 out1[SHA256_DIGEST_SIZE],
				  u8 out2[SHA256_DIGEST_SIZE]);

#define sha256_finup_2x_arch sha256_finup_2x_arch
static bool sha256_finup_2x_arch(const struct __sha256_ctx *ctx,
				 const u8 *data1, const u8 *data2, size_t len,
				 u8 out1[SHA256_DIGEST_SIZE],
				 u8 out2[SHA256_DIGEST_SIZE])
{
	/* The assembly requires len >= SHA256_BLOCK_SIZE && len <= INT_MAX. */
	if (static_branch_likely(&have_ce) && len >= SHA256_BLOCK_SIZE &&
	    len <= INT_MAX && likely(may_use_simd())) {
		scoped_ksimd()
			sha256_ce_finup2x(ctx, data1, data2, len, out1, out2);
		kmsan_unpoison_memory(out1, SHA256_DIGEST_SIZE);
		kmsan_unpoison_memory(out2, SHA256_DIGEST_SIZE);
		return true;
	}
	return false;
}

static bool sha256_finup_2x_is_optimized_arch(void)
{
	return static_key_enabled(&have_ce);
}

#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_neon);
		if (cpu_have_named_feature(SHA2))
			static_branch_enable(&have_ce);
	}
}

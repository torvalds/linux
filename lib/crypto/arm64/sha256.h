/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for ARM64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

asmlinkage void sha256_block_data_order(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage void sha256_block_neon(struct sha256_block_state *state,
				  const u8 *data, size_t nblocks);
asmlinkage size_t __sha256_ce_transform(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon) && likely(may_use_simd())) {
		if (static_branch_likely(&have_ce)) {
			do {
				size_t rem;

				kernel_neon_begin();
				rem = __sha256_ce_transform(state,
							    data, nblocks);
				kernel_neon_end();
				data += (nblocks - rem) * SHA256_BLOCK_SIZE;
				nblocks = rem;
			} while (nblocks);
		} else {
			kernel_neon_begin();
			sha256_block_neon(state, data, nblocks);
			kernel_neon_end();
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
	/*
	 * The assembly requires len >= SHA256_BLOCK_SIZE && len <= INT_MAX.
	 * Further limit len to 65536 to avoid spending too long with preemption
	 * disabled.  (Of course, in practice len is nearly always 4096 anyway.)
	 */
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_ce) && len >= SHA256_BLOCK_SIZE &&
	    len <= 65536 && likely(may_use_simd())) {
		kernel_neon_begin();
		sha256_ce_finup2x(ctx, data1, data2, len, out1, out2);
		kernel_neon_end();
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

#ifdef CONFIG_KERNEL_MODE_NEON
#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_neon);
		if (cpu_have_named_feature(SHA2))
			static_branch_enable(&have_ce);
	}
}
#endif /* CONFIG_KERNEL_MODE_NEON */

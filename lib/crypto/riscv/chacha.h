/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ChaCha stream cipher (RISC-V optimized)
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/internal/simd.h>
#include <linux/linkage.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(use_zvkb);

asmlinkage void chacha_zvkb(struct chacha_state *state, const u8 *in, u8 *out,
			    size_t nblocks, int nrounds);

#define hchacha_block_arch hchacha_block_generic /* not implemented yet */

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	u8 block_buffer[CHACHA_BLOCK_SIZE];
	unsigned int full_blocks = bytes / CHACHA_BLOCK_SIZE;
	unsigned int tail_bytes = bytes % CHACHA_BLOCK_SIZE;

	if (!static_branch_likely(&use_zvkb) || !crypto_simd_usable())
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	kernel_vector_begin();
	if (full_blocks) {
		chacha_zvkb(state, src, dst, full_blocks, nrounds);
		src += full_blocks * CHACHA_BLOCK_SIZE;
		dst += full_blocks * CHACHA_BLOCK_SIZE;
	}
	if (tail_bytes) {
		memcpy(block_buffer, src, tail_bytes);
		chacha_zvkb(state, block_buffer, block_buffer, 1, nrounds);
		memcpy(dst, block_buffer, tail_bytes);
	}
	kernel_vector_end();
}

#define chacha_mod_init_arch chacha_mod_init_arch
static void chacha_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&use_zvkb);
}

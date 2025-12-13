/*
 * ChaCha and HChaCha functions (ARM64 optimized)
 *
 * Copyright (C) 2016 - 2017 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on:
 * ChaCha20 256-bit cipher algorithm, RFC7539, SIMD glue code
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/internal/simd.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

asmlinkage void chacha_block_xor_neon(const struct chacha_state *state,
				      u8 *dst, const u8 *src, int nrounds);
asmlinkage void chacha_4block_xor_neon(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       int nrounds, int bytes);
asmlinkage void hchacha_block_neon(const struct chacha_state *state,
				   u32 out[HCHACHA_OUT_WORDS], int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

static void chacha_doneon(struct chacha_state *state, u8 *dst, const u8 *src,
			  int bytes, int nrounds)
{
	while (bytes > 0) {
		int l = min(bytes, CHACHA_BLOCK_SIZE * 5);

		if (l <= CHACHA_BLOCK_SIZE) {
			u8 buf[CHACHA_BLOCK_SIZE];

			memcpy(buf, src, l);
			chacha_block_xor_neon(state, buf, buf, nrounds);
			memcpy(dst, buf, l);
			state->x[12] += 1;
			break;
		}
		chacha_4block_xor_neon(state, dst, src, nrounds, l);
		bytes -= l;
		src += l;
		dst += l;
		state->x[12] += DIV_ROUND_UP(l, CHACHA_BLOCK_SIZE);
	}
}

static void hchacha_block_arch(const struct chacha_state *state,
			       u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	if (!static_branch_likely(&have_neon) || !crypto_simd_usable()) {
		hchacha_block_generic(state, out, nrounds);
	} else {
		kernel_neon_begin();
		hchacha_block_neon(state, out, nrounds);
		kernel_neon_end();
	}
}

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	if (!static_branch_likely(&have_neon) || bytes <= CHACHA_BLOCK_SIZE ||
	    !crypto_simd_usable())
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	do {
		unsigned int todo = min_t(unsigned int, bytes, SZ_4K);

		kernel_neon_begin();
		chacha_doneon(state, dst, src, todo, nrounds);
		kernel_neon_end();

		bytes -= todo;
		src += todo;
		dst += todo;
	} while (bytes);
}

#define chacha_mod_init_arch chacha_mod_init_arch
static void chacha_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD))
		static_branch_enable(&have_neon);
}

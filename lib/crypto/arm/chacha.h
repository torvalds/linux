/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ChaCha and HChaCha functions (ARM optimized)
 *
 * Copyright (C) 2016-2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2015 Martin Willi
 */

#include <crypto/internal/simd.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>

#include <asm/cputype.h>
#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

asmlinkage void chacha_block_xor_neon(const struct chacha_state *state,
				      u8 *dst, const u8 *src, int nrounds);
asmlinkage void chacha_4block_xor_neon(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       int nrounds, unsigned int nbytes);
asmlinkage void hchacha_block_arm(const struct chacha_state *state,
				  u32 out[HCHACHA_OUT_WORDS], int nrounds);
asmlinkage void hchacha_block_neon(const struct chacha_state *state,
				   u32 out[HCHACHA_OUT_WORDS], int nrounds);

asmlinkage void chacha_doarm(u8 *dst, const u8 *src, unsigned int bytes,
			     const struct chacha_state *state, int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(use_neon);

static inline bool neon_usable(void)
{
	return static_branch_likely(&use_neon) && crypto_simd_usable();
}

static void chacha_doneon(struct chacha_state *state, u8 *dst, const u8 *src,
			  unsigned int bytes, int nrounds)
{
	u8 buf[CHACHA_BLOCK_SIZE];

	while (bytes > CHACHA_BLOCK_SIZE) {
		unsigned int l = min(bytes, CHACHA_BLOCK_SIZE * 4U);

		chacha_4block_xor_neon(state, dst, src, nrounds, l);
		bytes -= l;
		src += l;
		dst += l;
		state->x[12] += DIV_ROUND_UP(l, CHACHA_BLOCK_SIZE);
	}
	if (bytes) {
		const u8 *s = src;
		u8 *d = dst;

		if (bytes != CHACHA_BLOCK_SIZE)
			s = d = memcpy(buf, src, bytes);
		chacha_block_xor_neon(state, d, s, nrounds);
		if (d != dst)
			memcpy(dst, buf, bytes);
		state->x[12]++;
	}
}

static void hchacha_block_arch(const struct chacha_state *state,
			       u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	if (!IS_ENABLED(CONFIG_KERNEL_MODE_NEON) || !neon_usable()) {
		hchacha_block_arm(state, out, nrounds);
	} else {
		kernel_neon_begin();
		hchacha_block_neon(state, out, nrounds);
		kernel_neon_end();
	}
}

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	if (!IS_ENABLED(CONFIG_KERNEL_MODE_NEON) || !neon_usable() ||
	    bytes <= CHACHA_BLOCK_SIZE) {
		chacha_doarm(dst, src, bytes, state, nrounds);
		state->x[12] += DIV_ROUND_UP(bytes, CHACHA_BLOCK_SIZE);
		return;
	}

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
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && (elf_hwcap & HWCAP_NEON)) {
		switch (read_cpuid_part()) {
		case ARM_CPU_PART_CORTEX_A7:
		case ARM_CPU_PART_CORTEX_A5:
			/*
			 * The Cortex-A7 and Cortex-A5 do not perform well with
			 * the NEON implementation but do incredibly with the
			 * scalar one and use less power.
			 */
			break;
		default:
			static_branch_enable(&use_neon);
		}
	}
}

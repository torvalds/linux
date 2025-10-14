/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ChaCha and HChaCha functions (x86_64 optimized)
 *
 * Copyright (C) 2015 Martin Willi
 */

#include <asm/simd.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/sizes.h>

asmlinkage void chacha_block_xor_ssse3(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_ssse3(const struct chacha_state *state,
					u8 *dst, const u8 *src,
					unsigned int len, int nrounds);
asmlinkage void hchacha_block_ssse3(const struct chacha_state *state,
				    u32 out[HCHACHA_OUT_WORDS], int nrounds);

asmlinkage void chacha_2block_xor_avx2(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx2(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx2(const struct chacha_state *state,
				       u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);

asmlinkage void chacha_2block_xor_avx512vl(const struct chacha_state *state,
					   u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx512vl(const struct chacha_state *state,
					   u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx512vl(const struct chacha_state *state,
					   u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_simd);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_avx2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_avx512vl);

static unsigned int chacha_advance(unsigned int len, unsigned int maxblocks)
{
	len = min(len, maxblocks * CHACHA_BLOCK_SIZE);
	return round_up(len, CHACHA_BLOCK_SIZE) / CHACHA_BLOCK_SIZE;
}

static void chacha_dosimd(struct chacha_state *state, u8 *dst, const u8 *src,
			  unsigned int bytes, int nrounds)
{
	if (static_branch_likely(&chacha_use_avx512vl)) {
		while (bytes >= CHACHA_BLOCK_SIZE * 8) {
			chacha_8block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			bytes -= CHACHA_BLOCK_SIZE * 8;
			src += CHACHA_BLOCK_SIZE * 8;
			dst += CHACHA_BLOCK_SIZE * 8;
			state->x[12] += 8;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 4) {
			chacha_8block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state->x[12] += chacha_advance(bytes, 8);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 2) {
			chacha_4block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state->x[12] += chacha_advance(bytes, 4);
			return;
		}
		if (bytes) {
			chacha_2block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state->x[12] += chacha_advance(bytes, 2);
			return;
		}
	}

	if (static_branch_likely(&chacha_use_avx2)) {
		while (bytes >= CHACHA_BLOCK_SIZE * 8) {
			chacha_8block_xor_avx2(state, dst, src, bytes, nrounds);
			bytes -= CHACHA_BLOCK_SIZE * 8;
			src += CHACHA_BLOCK_SIZE * 8;
			dst += CHACHA_BLOCK_SIZE * 8;
			state->x[12] += 8;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 4) {
			chacha_8block_xor_avx2(state, dst, src, bytes, nrounds);
			state->x[12] += chacha_advance(bytes, 8);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 2) {
			chacha_4block_xor_avx2(state, dst, src, bytes, nrounds);
			state->x[12] += chacha_advance(bytes, 4);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE) {
			chacha_2block_xor_avx2(state, dst, src, bytes, nrounds);
			state->x[12] += chacha_advance(bytes, 2);
			return;
		}
	}

	while (bytes >= CHACHA_BLOCK_SIZE * 4) {
		chacha_4block_xor_ssse3(state, dst, src, bytes, nrounds);
		bytes -= CHACHA_BLOCK_SIZE * 4;
		src += CHACHA_BLOCK_SIZE * 4;
		dst += CHACHA_BLOCK_SIZE * 4;
		state->x[12] += 4;
	}
	if (bytes > CHACHA_BLOCK_SIZE) {
		chacha_4block_xor_ssse3(state, dst, src, bytes, nrounds);
		state->x[12] += chacha_advance(bytes, 4);
		return;
	}
	if (bytes) {
		chacha_block_xor_ssse3(state, dst, src, bytes, nrounds);
		state->x[12]++;
	}
}

static void hchacha_block_arch(const struct chacha_state *state,
			       u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	if (!static_branch_likely(&chacha_use_simd)) {
		hchacha_block_generic(state, out, nrounds);
	} else {
		kernel_fpu_begin();
		hchacha_block_ssse3(state, out, nrounds);
		kernel_fpu_end();
	}
}

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	if (!static_branch_likely(&chacha_use_simd) ||
	    bytes <= CHACHA_BLOCK_SIZE)
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	do {
		unsigned int todo = min_t(unsigned int, bytes, SZ_4K);

		kernel_fpu_begin();
		chacha_dosimd(state, dst, src, todo, nrounds);
		kernel_fpu_end();

		bytes -= todo;
		src += todo;
		dst += todo;
	} while (bytes);
}

#define chacha_mod_init_arch chacha_mod_init_arch
static void chacha_mod_init_arch(void)
{
	if (!boot_cpu_has(X86_FEATURE_SSSE3))
		return;

	static_branch_enable(&chacha_use_simd);

	if (boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL)) {
		static_branch_enable(&chacha_use_avx2);

		if (boot_cpu_has(X86_FEATURE_AVX512VL) &&
		    boot_cpu_has(X86_FEATURE_AVX512BW)) /* kmovq */
			static_branch_enable(&chacha_use_avx512vl);
	}
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ChaCha stream cipher (P10 accelerated)
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */

#include <crypto/internal/simd.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <linux/sizes.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

asmlinkage void chacha_p10le_8x(const struct chacha_state *state, u8 *dst,
				const u8 *src, unsigned int len, int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_p10);

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

static void chacha_p10_do_8x(struct chacha_state *state, u8 *dst, const u8 *src,
			     unsigned int bytes, int nrounds)
{
	unsigned int l = bytes & ~0x0FF;

	if (l > 0) {
		chacha_p10le_8x(state, dst, src, l, nrounds);
		bytes -= l;
		src += l;
		dst += l;
		state->x[12] += l / CHACHA_BLOCK_SIZE;
	}

	if (bytes > 0)
		chacha_crypt_generic(state, dst, src, bytes, nrounds);
}

#define hchacha_block_arch hchacha_block_generic /* not implemented yet */

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	if (!static_branch_likely(&have_p10) || bytes <= CHACHA_BLOCK_SIZE ||
	    !crypto_simd_usable())
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	do {
		unsigned int todo = min_t(unsigned int, bytes, SZ_4K);

		vsx_begin();
		chacha_p10_do_8x(state, dst, src, todo, nrounds);
		vsx_end();

		bytes -= todo;
		src += todo;
		dst += todo;
	} while (bytes);
}

#define chacha_mod_init_arch chacha_mod_init_arch
static void chacha_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_31))
		static_branch_enable(&have_p10);
}

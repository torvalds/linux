/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ChaCha stream cipher (s390 optimized)
 *
 * Copyright IBM Corp. 2021
 */

#include <linux/cpufeature.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <asm/fpu.h>
#include "chacha-s390.h"

#define hchacha_block_arch hchacha_block_generic /* not implemented yet */

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, unsigned int bytes, int nrounds)
{
	/* s390 chacha20 implementation has 20 rounds hard-coded,
	 * it cannot handle a block of data or less, but otherwise
	 * it can handle data of arbitrary size
	 */
	if (bytes <= CHACHA_BLOCK_SIZE || nrounds != 20 || !cpu_has_vx()) {
		chacha_crypt_generic(state, dst, src, bytes, nrounds);
	} else {
		DECLARE_KERNEL_FPU_ONSTACK32(vxstate);

		kernel_fpu_begin(&vxstate, KERNEL_VXR);
		chacha20_vx(dst, src, bytes, &state->x[4], &state->x[12]);
		kernel_fpu_end(&vxstate, KERNEL_VXR);

		state->x[12] += round_up(bytes, CHACHA_BLOCK_SIZE) /
				CHACHA_BLOCK_SIZE;
	}
}

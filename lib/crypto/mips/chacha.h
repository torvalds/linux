/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ChaCha and HChaCha functions (MIPS optimized)
 *
 * Copyright (C) 2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/kernel.h>
#include <linux/sizes.h>

asmlinkage void chacha_crypt_mips(struct chacha_state *state, u8 *dst,
				  const u8 *src, size_t bytes, int nrounds);
asmlinkage void hchacha_block_arch(const struct chacha_state *state,
				   u32 out[HCHACHA_OUT_WORDS], int nrounds);

static void chacha_crypt_arch(struct chacha_state *state, u8 *dst,
			      const u8 *src, size_t bytes, int nrounds)
{
	/*
	 * chacha_crypt_mips() compares the length as a signed value, so pass
	 * it at most 1 GiB at a time.
	 */
	do {
		size_t todo = min(bytes, SZ_1G);

		chacha_crypt_mips(state, dst, src, todo, nrounds);
		bytes -= todo;
		src += todo;
		dst += todo;
	} while (bytes);
}

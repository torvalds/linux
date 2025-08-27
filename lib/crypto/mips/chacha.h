/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ChaCha and HChaCha functions (MIPS optimized)
 *
 * Copyright (C) 2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/kernel.h>

asmlinkage void chacha_crypt_arch(struct chacha_state *state,
				  u8 *dst, const u8 *src,
				  unsigned int bytes, int nrounds);
asmlinkage void hchacha_block_arch(const struct chacha_state *state,
				   u32 out[HCHACHA_OUT_WORDS], int nrounds);

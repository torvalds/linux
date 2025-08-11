// SPDX-License-Identifier: GPL-2.0
/*
 * ChaCha and HChaCha functions (MIPS optimized)
 *
 * Copyright (C) 2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#include <crypto/chacha.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void chacha_crypt_arch(struct chacha_state *state,
				  u8 *dst, const u8 *src,
				  unsigned int bytes, int nrounds);
EXPORT_SYMBOL(chacha_crypt_arch);

asmlinkage void hchacha_block_arch(const struct chacha_state *state,
				   u32 out[HCHACHA_OUT_WORDS], int nrounds);
EXPORT_SYMBOL(hchacha_block_arch);

bool chacha_is_arch_optimized(void)
{
	return true;
}
EXPORT_SYMBOL(chacha_is_arch_optimized);

MODULE_DESCRIPTION("ChaCha and HChaCha functions (MIPS optimized)");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

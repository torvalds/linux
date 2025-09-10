// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for MIPS
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <crypto/internal/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_block_init_arch(
	struct poly1305_block_state *state,
	const u8 raw_key[POLY1305_BLOCK_SIZE]);
EXPORT_SYMBOL_GPL(poly1305_block_init_arch);
asmlinkage void poly1305_blocks_arch(struct poly1305_block_state *state,
				     const u8 *src, u32 len, u32 hibit);
EXPORT_SYMBOL_GPL(poly1305_blocks_arch);
asmlinkage void poly1305_emit_arch(const struct poly1305_state *state,
				   u8 digest[POLY1305_DIGEST_SIZE],
				   const u32 nonce[4]);
EXPORT_SYMBOL_GPL(poly1305_emit_arch);

bool poly1305_is_arch_optimized(void)
{
	return true;
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

MODULE_DESCRIPTION("Poly1305 transform (MIPS accelerated");
MODULE_LICENSE("GPL v2");

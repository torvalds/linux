/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for MIPS
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

asmlinkage void poly1305_block_init(struct poly1305_block_state *state,
				    const u8 raw_key[POLY1305_BLOCK_SIZE]);
asmlinkage void poly1305_blocks(struct poly1305_block_state *state,
				const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_emit(const struct poly1305_state *state,
			      u8 digest[POLY1305_DIGEST_SIZE],
			      const u32 nonce[4]);

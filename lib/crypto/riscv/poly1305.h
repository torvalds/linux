/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for riscv
 *
 * Copyright (C) 2025 Institute of Software, CAS.
 */

asmlinkage void poly1305_block_init(struct poly1305_block_state *state,
				    const u8 raw_key[POLY1305_BLOCK_SIZE]);
asmlinkage void poly1305_blocks(struct poly1305_block_state *state,
				const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_emit(const struct poly1305_state *state,
			      u8 digest[POLY1305_DIGEST_SIZE],
			      const u32 nonce[4]);

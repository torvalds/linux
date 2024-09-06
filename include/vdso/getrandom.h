/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _VDSO_GETRANDOM_H
#define _VDSO_GETRANDOM_H

#include <linux/types.h>

#define CHACHA_KEY_SIZE         32
#define CHACHA_BLOCK_SIZE       64

/**
 * struct vgetrandom_state - State used by vDSO getrandom().
 *
 * @batch:	One and a half ChaCha20 blocks of buffered RNG output.
 *
 * @key:	Key to be used for generating next batch.
 *
 * @batch_key:	Union of the prior two members, which is exactly two full
 * 		ChaCha20 blocks in size, so that @batch and @key can be filled
 * 		together.
 *
 * @generation:	Snapshot of @rng_info->generation in the vDSO data page at
 *		the time @key was generated.
 *
 * @pos:	Offset into @batch of the next available random byte.
 *
 * @in_use:	Reentrancy guard for reusing a state within the same thread
 *		due to signal handlers.
 */
struct vgetrandom_state {
	union {
		struct {
			u8	batch[CHACHA_BLOCK_SIZE * 3 / 2];
			u32	key[CHACHA_KEY_SIZE / sizeof(u32)];
		};
		u8		batch_key[CHACHA_BLOCK_SIZE * 2];
	};
	u64			generation;
	u8			pos;
	bool 			in_use;
};

#endif /* _VDSO_GETRANDOM_H */

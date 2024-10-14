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

/**
 * __arch_chacha20_blocks_nostack - Generate ChaCha20 stream without using the stack.
 * @dst_bytes:	Destination buffer to hold @nblocks * 64 bytes of output.
 * @key:	32-byte input key.
 * @counter:	8-byte counter, read on input and updated on return.
 * @nblocks:	Number of blocks to generate.
 *
 * Generates a given positive number of blocks of ChaCha20 output with nonce=0, and does not write
 * to any stack or memory outside of the parameters passed to it, in order to mitigate stack data
 * leaking into forked child processes.
 */
extern void __arch_chacha20_blocks_nostack(u8 *dst_bytes, const u32 *key, u32 *counter, size_t nblocks);

/**
 * __vdso_getrandom - Architecture-specific vDSO implementation of getrandom() syscall.
 * @buffer:		Passed to __cvdso_getrandom().
 * @len:		Passed to __cvdso_getrandom().
 * @flags:		Passed to __cvdso_getrandom().
 * @opaque_state:	Passed to __cvdso_getrandom().
 * @opaque_len:		Passed to __cvdso_getrandom();
 *
 * This function is implemented by making a single call to to __cvdso_getrandom(), whose
 * documentation may be consulted for more information.
 *
 * Returns:	The return value of __cvdso_getrandom().
 */
extern ssize_t __vdso_getrandom(void *buffer, size_t len, unsigned int flags, void *opaque_state, size_t opaque_len);

#endif /* _VDSO_GETRANDOM_H */

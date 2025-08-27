// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The "hash function" used as the core of the ChaCha stream cipher (RFC7539)
 *
 * Copyright (C) 2015 Martin Willi
 */

#include <crypto/chacha.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/unaligned.h>

static void chacha_permute(struct chacha_state *state, int nrounds)
{
	u32 *x = state->x;
	int i;

	/* whitelist the allowed round counts */
	WARN_ON_ONCE(nrounds != 20 && nrounds != 12);

	for (i = 0; i < nrounds; i += 2) {
		x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],  16);
		x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],  16);
		x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],  16);
		x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],  16);

		x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],  12);
		x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],  12);
		x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10], 12);
		x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11], 12);

		x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],   8);
		x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],   8);
		x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],   8);
		x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],   8);

		x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],   7);
		x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],   7);
		x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10],  7);
		x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11],  7);

		x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],  16);
		x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],  16);
		x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],  16);
		x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],  16);

		x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10], 12);
		x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11], 12);
		x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],  12);
		x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],  12);

		x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],   8);
		x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],   8);
		x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],   8);
		x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],   8);

		x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10],  7);
		x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11],  7);
		x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],   7);
		x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],   7);
	}
}

/**
 * chacha_block_generic - generate one keystream block and increment block counter
 * @state: input state matrix
 * @out: output keystream block
 * @nrounds: number of rounds (20 or 12; 20 is recommended)
 *
 * This is the ChaCha core, a function from 64-byte strings to 64-byte strings.
 * The caller has already converted the endianness of the input.  This function
 * also handles incrementing the block counter in the input matrix.
 */
void chacha_block_generic(struct chacha_state *state,
			  u8 out[CHACHA_BLOCK_SIZE], int nrounds)
{
	struct chacha_state permuted_state = *state;
	int i;

	chacha_permute(&permuted_state, nrounds);

	for (i = 0; i < ARRAY_SIZE(state->x); i++)
		put_unaligned_le32(permuted_state.x[i] + state->x[i],
				   &out[i * sizeof(u32)]);

	state->x[12]++;
}
EXPORT_SYMBOL(chacha_block_generic);

/**
 * hchacha_block_generic - abbreviated ChaCha core, for XChaCha
 * @state: input state matrix
 * @out: the output words
 * @nrounds: number of rounds (20 or 12; 20 is recommended)
 *
 * HChaCha is the ChaCha equivalent of HSalsa and is an intermediate step
 * towards XChaCha (see https://cr.yp.to/snuffle/xsalsa-20081128.pdf).  HChaCha
 * skips the final addition of the initial state, and outputs only certain words
 * of the state.  It should not be used for streaming directly.
 */
void hchacha_block_generic(const struct chacha_state *state,
			   u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	struct chacha_state permuted_state = *state;

	chacha_permute(&permuted_state, nrounds);

	memcpy(&out[0], &permuted_state.x[0], 16);
	memcpy(&out[4], &permuted_state.x[12], 16);
}
EXPORT_SYMBOL(hchacha_block_generic);

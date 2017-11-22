/*
 * ChaCha20 256-bit cipher algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/cryptohash.h>
#include <asm/unaligned.h>
#include <crypto/chacha20.h>

static inline u32 rotl32(u32 v, u8 n)
{
	return (v << n) | (v >> (sizeof(v) * 8 - n));
}

void chacha20_block(u32 *state, u32 *stream)
{
	u32 x[16], *out = stream;
	int i;

	for (i = 0; i < ARRAY_SIZE(x); i++)
		x[i] = state[i];

	for (i = 0; i < 20; i += 2) {
		x[0]  += x[4];    x[12] = rotl32(x[12] ^ x[0],  16);
		x[1]  += x[5];    x[13] = rotl32(x[13] ^ x[1],  16);
		x[2]  += x[6];    x[14] = rotl32(x[14] ^ x[2],  16);
		x[3]  += x[7];    x[15] = rotl32(x[15] ^ x[3],  16);

		x[8]  += x[12];   x[4]  = rotl32(x[4]  ^ x[8],  12);
		x[9]  += x[13];   x[5]  = rotl32(x[5]  ^ x[9],  12);
		x[10] += x[14];   x[6]  = rotl32(x[6]  ^ x[10], 12);
		x[11] += x[15];   x[7]  = rotl32(x[7]  ^ x[11], 12);

		x[0]  += x[4];    x[12] = rotl32(x[12] ^ x[0],   8);
		x[1]  += x[5];    x[13] = rotl32(x[13] ^ x[1],   8);
		x[2]  += x[6];    x[14] = rotl32(x[14] ^ x[2],   8);
		x[3]  += x[7];    x[15] = rotl32(x[15] ^ x[3],   8);

		x[8]  += x[12];   x[4]  = rotl32(x[4]  ^ x[8],   7);
		x[9]  += x[13];   x[5]  = rotl32(x[5]  ^ x[9],   7);
		x[10] += x[14];   x[6]  = rotl32(x[6]  ^ x[10],  7);
		x[11] += x[15];   x[7]  = rotl32(x[7]  ^ x[11],  7);

		x[0]  += x[5];    x[15] = rotl32(x[15] ^ x[0],  16);
		x[1]  += x[6];    x[12] = rotl32(x[12] ^ x[1],  16);
		x[2]  += x[7];    x[13] = rotl32(x[13] ^ x[2],  16);
		x[3]  += x[4];    x[14] = rotl32(x[14] ^ x[3],  16);

		x[10] += x[15];   x[5]  = rotl32(x[5]  ^ x[10], 12);
		x[11] += x[12];   x[6]  = rotl32(x[6]  ^ x[11], 12);
		x[8]  += x[13];   x[7]  = rotl32(x[7]  ^ x[8],  12);
		x[9]  += x[14];   x[4]  = rotl32(x[4]  ^ x[9],  12);

		x[0]  += x[5];    x[15] = rotl32(x[15] ^ x[0],   8);
		x[1]  += x[6];    x[12] = rotl32(x[12] ^ x[1],   8);
		x[2]  += x[7];    x[13] = rotl32(x[13] ^ x[2],   8);
		x[3]  += x[4];    x[14] = rotl32(x[14] ^ x[3],   8);

		x[10] += x[15];   x[5]  = rotl32(x[5]  ^ x[10],  7);
		x[11] += x[12];   x[6]  = rotl32(x[6]  ^ x[11],  7);
		x[8]  += x[13];   x[7]  = rotl32(x[7]  ^ x[8],   7);
		x[9]  += x[14];   x[4]  = rotl32(x[4]  ^ x[9],   7);
	}

	for (i = 0; i < ARRAY_SIZE(x); i++)
		out[i] = cpu_to_le32(x[i] + state[i]);

	state[12]++;
}
EXPORT_SYMBOL(chacha20_block);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * SHA-1 message digest algorithm
 *
 * Copyright 2025 Google LLC
 */
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/unaligned.h>
#include <string.h>

#include "sha1.h"

#define SHA1_BLOCK_SIZE 64

static const u32 sha1_K[4] = { 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6 };

#define SHA1_ROUND(i, a, b, c, d, e)                                          \
	do {                                                                  \
		if ((i) >= 16)                                                \
			w[i] = rol32(w[(i) - 16] ^ w[(i) - 14] ^ w[(i) - 8] ^ \
					     w[(i) - 3],                      \
				     1);                                      \
		e += w[i] + rol32(a, 5) + sha1_K[(i) / 20];                   \
		if ((i) < 20)                                                 \
			e += (b & (c ^ d)) ^ d;                               \
		else if ((i) < 40 || (i) >= 60)                               \
			e += b ^ c ^ d;                                       \
		else                                                          \
			e += (c & d) ^ (b & (c ^ d));                         \
		b = rol32(b, 30);                                             \
		/* The new (a, b, c, d, e) is the old (e, a, b, c, d). */     \
	} while (0)

#define SHA1_5ROUNDS(i)                             \
	do {                                        \
		SHA1_ROUND((i) + 0, a, b, c, d, e); \
		SHA1_ROUND((i) + 1, e, a, b, c, d); \
		SHA1_ROUND((i) + 2, d, e, a, b, c); \
		SHA1_ROUND((i) + 3, c, d, e, a, b); \
		SHA1_ROUND((i) + 4, b, c, d, e, a); \
	} while (0)

#define SHA1_20ROUNDS(i)                \
	do {                            \
		SHA1_5ROUNDS((i) + 0);  \
		SHA1_5ROUNDS((i) + 5);  \
		SHA1_5ROUNDS((i) + 10); \
		SHA1_5ROUNDS((i) + 15); \
	} while (0)

static void sha1_blocks(u32 h[5], const u8 *data, size_t nblocks)
{
	while (nblocks--) {
		u32 a = h[0];
		u32 b = h[1];
		u32 c = h[2];
		u32 d = h[3];
		u32 e = h[4];
		u32 w[80];

		for (int i = 0; i < 16; i++)
			w[i] = get_unaligned_be32(&data[i * 4]);
		SHA1_20ROUNDS(0);
		SHA1_20ROUNDS(20);
		SHA1_20ROUNDS(40);
		SHA1_20ROUNDS(60);

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
		data += SHA1_BLOCK_SIZE;
	}
}

/* Calculate the SHA-1 message digest of the given data. */
void sha1(const void *data, size_t len, u8 out[SHA1_DIGEST_SIZE])
{
	u32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
		     0xC3D2E1F0 };
	u8 final_data[2 * SHA1_BLOCK_SIZE] = { 0 };
	size_t final_len = len % SHA1_BLOCK_SIZE;

	sha1_blocks(h, data, len / SHA1_BLOCK_SIZE);

	memcpy(final_data, data + len - final_len, final_len);
	final_data[final_len] = 0x80;
	final_len = round_up(final_len + 9, SHA1_BLOCK_SIZE);
	put_unaligned_be64((u64)len * 8, &final_data[final_len - 8]);

	sha1_blocks(h, final_data, final_len / SHA1_BLOCK_SIZE);

	for (int i = 0; i < 5; i++)
		put_unaligned_be32(h[i], &out[i * 4]);
}

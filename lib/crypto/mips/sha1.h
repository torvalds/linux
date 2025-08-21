/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * SHA1 Secure Hash Algorithm.
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/sha1_generic.c, which is:
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 */

#include <asm/octeon/crypto.h>
#include <asm/octeon/octeon.h>

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_sha1_store_hash(struct sha1_block_state *state)
{
	u64 *hash = (u64 *)&state->h[0];
	union {
		u32 word[2];
		u64 dword;
	} hash_tail = { { state->h[4], } };

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
	write_octeon_64bit_hash_dword(hash_tail.dword, 2);
	memzero_explicit(&hash_tail.word[0], sizeof(hash_tail.word[0]));
}

static void octeon_sha1_read_hash(struct sha1_block_state *state)
{
	u64 *hash = (u64 *)&state->h[0];
	union {
		u32 word[2];
		u64 dword;
	} hash_tail;

	hash[0]		= read_octeon_64bit_hash_dword(0);
	hash[1]		= read_octeon_64bit_hash_dword(1);
	hash_tail.dword	= read_octeon_64bit_hash_dword(2);
	state->h[4]	= hash_tail.word[0];
	memzero_explicit(&hash_tail.dword, sizeof(hash_tail.dword));
}

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	struct octeon_cop2_state cop2_state;
	unsigned long flags;

	if (!octeon_has_crypto())
		return sha1_blocks_generic(state, data, nblocks);

	flags = octeon_crypto_enable(&cop2_state);
	octeon_sha1_store_hash(state);

	do {
		const u64 *block = (const u64 *)data;

		write_octeon_64bit_block_dword(block[0], 0);
		write_octeon_64bit_block_dword(block[1], 1);
		write_octeon_64bit_block_dword(block[2], 2);
		write_octeon_64bit_block_dword(block[3], 3);
		write_octeon_64bit_block_dword(block[4], 4);
		write_octeon_64bit_block_dword(block[5], 5);
		write_octeon_64bit_block_dword(block[6], 6);
		octeon_sha1_start(block[7]);

		data += SHA1_BLOCK_SIZE;
	} while (--nblocks);

	octeon_sha1_read_hash(state);
	octeon_crypto_disable(&cop2_state, flags);
}

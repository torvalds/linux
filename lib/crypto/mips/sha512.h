/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * SHA-512 and SHA-384 Secure Hash Algorithm.
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/sha512_generic.c, which is:
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 */

#include <asm/octeon/crypto.h>
#include <asm/octeon/octeon.h>

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	struct octeon_cop2_state cop2_state;
	unsigned long flags;

	if (!octeon_has_crypto())
		return sha512_blocks_generic(state, data, nblocks);

	flags = octeon_crypto_enable(&cop2_state);
	write_octeon_64bit_hash_sha512(state->h[0], 0);
	write_octeon_64bit_hash_sha512(state->h[1], 1);
	write_octeon_64bit_hash_sha512(state->h[2], 2);
	write_octeon_64bit_hash_sha512(state->h[3], 3);
	write_octeon_64bit_hash_sha512(state->h[4], 4);
	write_octeon_64bit_hash_sha512(state->h[5], 5);
	write_octeon_64bit_hash_sha512(state->h[6], 6);
	write_octeon_64bit_hash_sha512(state->h[7], 7);

	do {
		const u64 *block = (const u64 *)data;

		write_octeon_64bit_block_sha512(block[0], 0);
		write_octeon_64bit_block_sha512(block[1], 1);
		write_octeon_64bit_block_sha512(block[2], 2);
		write_octeon_64bit_block_sha512(block[3], 3);
		write_octeon_64bit_block_sha512(block[4], 4);
		write_octeon_64bit_block_sha512(block[5], 5);
		write_octeon_64bit_block_sha512(block[6], 6);
		write_octeon_64bit_block_sha512(block[7], 7);
		write_octeon_64bit_block_sha512(block[8], 8);
		write_octeon_64bit_block_sha512(block[9], 9);
		write_octeon_64bit_block_sha512(block[10], 10);
		write_octeon_64bit_block_sha512(block[11], 11);
		write_octeon_64bit_block_sha512(block[12], 12);
		write_octeon_64bit_block_sha512(block[13], 13);
		write_octeon_64bit_block_sha512(block[14], 14);
		octeon_sha512_start(block[15]);

		data += SHA512_BLOCK_SIZE;
	} while (--nblocks);

	state->h[0] = read_octeon_64bit_hash_sha512(0);
	state->h[1] = read_octeon_64bit_hash_sha512(1);
	state->h[2] = read_octeon_64bit_hash_sha512(2);
	state->h[3] = read_octeon_64bit_hash_sha512(3);
	state->h[4] = read_octeon_64bit_hash_sha512(4);
	state->h[5] = read_octeon_64bit_hash_sha512(5);
	state->h[6] = read_octeon_64bit_hash_sha512(6);
	state->h[7] = read_octeon_64bit_hash_sha512(7);
	octeon_crypto_disable(&cop2_state, flags);
}

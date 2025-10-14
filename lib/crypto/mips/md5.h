/*
 * Cryptographic API.
 *
 * MD5 Message Digest Algorithm (RFC1321).
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/md5.c, which is:
 *
 * Derived from cryptoapi implementation, originally based on the
 * public domain implementation written by Colin Plumb in 1993.
 *
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <asm/octeon/crypto.h>
#include <asm/octeon/octeon.h>

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void md5_blocks(struct md5_block_state *state,
		       const u8 *data, size_t nblocks)
{
	struct octeon_cop2_state cop2_state;
	u64 *state64 = (u64 *)state;
	unsigned long flags;

	if (!octeon_has_crypto())
		return md5_blocks_generic(state, data, nblocks);

	cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));

	flags = octeon_crypto_enable(&cop2_state);
	write_octeon_64bit_hash_dword(state64[0], 0);
	write_octeon_64bit_hash_dword(state64[1], 1);

	do {
		const u64 *block = (const u64 *)data;

		write_octeon_64bit_block_dword(block[0], 0);
		write_octeon_64bit_block_dword(block[1], 1);
		write_octeon_64bit_block_dword(block[2], 2);
		write_octeon_64bit_block_dword(block[3], 3);
		write_octeon_64bit_block_dword(block[4], 4);
		write_octeon_64bit_block_dword(block[5], 5);
		write_octeon_64bit_block_dword(block[6], 6);
		octeon_md5_start(block[7]);

		data += MD5_BLOCK_SIZE;
	} while (--nblocks);

	state64[0] = read_octeon_64bit_hash_dword(0);
	state64[1] = read_octeon_64bit_hash_dword(1);
	octeon_crypto_disable(&cop2_state, flags);

	le32_to_cpu_array(state->h, ARRAY_SIZE(state->h));
}

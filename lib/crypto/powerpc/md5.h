/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MD5 optimized for PowerPC
 */

void ppc_md5_transform(u32 *state, const u8 *data, size_t nblocks);

static void md5_blocks(struct md5_block_state *state,
		       const u8 *data, size_t nblocks)
{
	ppc_md5_transform(state->h, data, nblocks);
}

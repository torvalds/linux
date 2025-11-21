/* SPDX-License-Identifier: GPL-2.0-or-later */

/* defined in blake2s-core.S */
void blake2s_compress(struct blake2s_state *state, const u8 *block,
		      size_t nblocks, u32 inc);

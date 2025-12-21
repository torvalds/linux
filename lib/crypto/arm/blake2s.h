/* SPDX-License-Identifier: GPL-2.0-or-later */

/* defined in blake2s-core.S */
void blake2s_compress(struct blake2s_ctx *ctx,
		      const u8 *data, size_t nblocks, u32 inc);

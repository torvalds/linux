/* SPDX-License-Identifier: GPL-2.0-only */

extern struct xor_block_template xor_block_arm4regs;
extern struct xor_block_template xor_block_neon;

void xor_gen_neon_inner(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes);

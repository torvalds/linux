/* SPDX-License-Identifier: GPL-2.0-only */

void xor_gen_neon_inner(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes);
void xor_gen_eor3_inner(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes);

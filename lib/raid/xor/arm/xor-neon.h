/* SPDX-License-Identifier: GPL-2.0-only */

extern struct xor_block_template xor_block_arm4regs;
extern struct xor_block_template xor_block_neon;

void __xor_neon_2(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2);
void __xor_neon_3(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3);
void __xor_neon_4(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4);
void __xor_neon_5(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4,
		const unsigned long * __restrict p5);

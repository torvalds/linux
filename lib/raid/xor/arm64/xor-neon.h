/* SPDX-License-Identifier: GPL-2.0-only */

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

#define __xor_eor3_2	__xor_neon_2
void __xor_eor3_3(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3);
void __xor_eor3_4(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4);
void __xor_eor3_5(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4,
		const unsigned long * __restrict p5);

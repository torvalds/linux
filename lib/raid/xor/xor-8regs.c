// SPDX-License-Identifier: GPL-2.0-or-later
#include "xor_impl.h"

static void
xor_8regs_2(unsigned long bytes, unsigned long * __restrict p1,
	    const unsigned long * __restrict p2)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		p1[0] ^= p2[0];
		p1[1] ^= p2[1];
		p1[2] ^= p2[2];
		p1[3] ^= p2[3];
		p1[4] ^= p2[4];
		p1[5] ^= p2[5];
		p1[6] ^= p2[6];
		p1[7] ^= p2[7];
		p1 += 8;
		p2 += 8;
	} while (--lines > 0);
}

static void
xor_8regs_3(unsigned long bytes, unsigned long * __restrict p1,
	    const unsigned long * __restrict p2,
	    const unsigned long * __restrict p3)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		p1[0] ^= p2[0] ^ p3[0];
		p1[1] ^= p2[1] ^ p3[1];
		p1[2] ^= p2[2] ^ p3[2];
		p1[3] ^= p2[3] ^ p3[3];
		p1[4] ^= p2[4] ^ p3[4];
		p1[5] ^= p2[5] ^ p3[5];
		p1[6] ^= p2[6] ^ p3[6];
		p1[7] ^= p2[7] ^ p3[7];
		p1 += 8;
		p2 += 8;
		p3 += 8;
	} while (--lines > 0);
}

static void
xor_8regs_4(unsigned long bytes, unsigned long * __restrict p1,
	    const unsigned long * __restrict p2,
	    const unsigned long * __restrict p3,
	    const unsigned long * __restrict p4)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		p1[0] ^= p2[0] ^ p3[0] ^ p4[0];
		p1[1] ^= p2[1] ^ p3[1] ^ p4[1];
		p1[2] ^= p2[2] ^ p3[2] ^ p4[2];
		p1[3] ^= p2[3] ^ p3[3] ^ p4[3];
		p1[4] ^= p2[4] ^ p3[4] ^ p4[4];
		p1[5] ^= p2[5] ^ p3[5] ^ p4[5];
		p1[6] ^= p2[6] ^ p3[6] ^ p4[6];
		p1[7] ^= p2[7] ^ p3[7] ^ p4[7];
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
	} while (--lines > 0);
}

static void
xor_8regs_5(unsigned long bytes, unsigned long * __restrict p1,
	    const unsigned long * __restrict p2,
	    const unsigned long * __restrict p3,
	    const unsigned long * __restrict p4,
	    const unsigned long * __restrict p5)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		p1[0] ^= p2[0] ^ p3[0] ^ p4[0] ^ p5[0];
		p1[1] ^= p2[1] ^ p3[1] ^ p4[1] ^ p5[1];
		p1[2] ^= p2[2] ^ p3[2] ^ p4[2] ^ p5[2];
		p1[3] ^= p2[3] ^ p3[3] ^ p4[3] ^ p5[3];
		p1[4] ^= p2[4] ^ p3[4] ^ p4[4] ^ p5[4];
		p1[5] ^= p2[5] ^ p3[5] ^ p4[5] ^ p5[5];
		p1[6] ^= p2[6] ^ p3[6] ^ p4[6] ^ p5[6];
		p1[7] ^= p2[7] ^ p3[7] ^ p4[7] ^ p5[7];
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
		p5 += 8;
	} while (--lines > 0);
}

#ifndef NO_TEMPLATE
DO_XOR_BLOCKS(8regs, xor_8regs_2, xor_8regs_3, xor_8regs_4, xor_8regs_5);

struct xor_block_template xor_block_8regs = {
	.name		= "8regs",
	.xor_gen	= xor_gen_8regs,
};
#endif /* NO_TEMPLATE */

/*
 * include/asm-generic/xor.h
 *
 * Generic optimized RAID-5 checksumming functions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/processor.h>

static void
xor_8regs_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
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
xor_8regs_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3)
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
xor_8regs_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4)
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
xor_8regs_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4, unsigned long *p5)
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

static void
xor_32regs_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
	} while (--lines > 0);
}

static void
xor_32regs_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
	} while (--lines > 0);
}

static void
xor_32regs_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		d0 ^= p4[0];
		d1 ^= p4[1];
		d2 ^= p4[2];
		d3 ^= p4[3];
		d4 ^= p4[4];
		d5 ^= p4[5];
		d6 ^= p4[6];
		d7 ^= p4[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
	} while (--lines > 0);
}

static void
xor_32regs_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof (long)) / 8;

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		d0 ^= p4[0];
		d1 ^= p4[1];
		d2 ^= p4[2];
		d3 ^= p4[3];
		d4 ^= p4[4];
		d5 ^= p4[5];
		d6 ^= p4[6];
		d7 ^= p4[7];
		d0 ^= p5[0];
		d1 ^= p5[1];
		d2 ^= p5[2];
		d3 ^= p5[3];
		d4 ^= p5[4];
		d5 ^= p5[5];
		d6 ^= p5[6];
		d7 ^= p5[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
		p5 += 8;
	} while (--lines > 0);
}

static void
xor_8regs_p_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;
	prefetchw(p1);
	prefetch(p2);

	do {
		prefetchw(p1+8);
		prefetch(p2+8);
 once_more:
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
	if (lines == 0)
		goto once_more;
}

static void
xor_8regs_p_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;
	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);

	do {
		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
 once_more:
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
	if (lines == 0)
		goto once_more;
}

static void
xor_8regs_p_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);
	prefetch(p4);

	do {
		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
		prefetch(p4+8);
 once_more:
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
	if (lines == 0)
		goto once_more;
}

static void
xor_8regs_p_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);
	prefetch(p4);
	prefetch(p5);

	do {
		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
		prefetch(p4+8);
		prefetch(p5+8);
 once_more:
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
	if (lines == 0)
		goto once_more;
}

static void
xor_32regs_p_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;

		prefetchw(p1+8);
		prefetch(p2+8);
 once_more:
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
	} while (--lines > 0);
	if (lines == 0)
		goto once_more;
}

static void
xor_32regs_p_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;

		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
 once_more:
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
	} while (--lines > 0);
	if (lines == 0)
		goto once_more;
}

static void
xor_32regs_p_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);
	prefetch(p4);

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;

		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
		prefetch(p4+8);
 once_more:
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		d0 ^= p4[0];
		d1 ^= p4[1];
		d2 ^= p4[2];
		d3 ^= p4[3];
		d4 ^= p4[4];
		d5 ^= p4[5];
		d6 ^= p4[6];
		d7 ^= p4[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
	} while (--lines > 0);
	if (lines == 0)
		goto once_more;
}

static void
xor_32regs_p_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	    unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof (long)) / 8 - 1;

	prefetchw(p1);
	prefetch(p2);
	prefetch(p3);
	prefetch(p4);
	prefetch(p5);

	do {
		register long d0, d1, d2, d3, d4, d5, d6, d7;

		prefetchw(p1+8);
		prefetch(p2+8);
		prefetch(p3+8);
		prefetch(p4+8);
		prefetch(p5+8);
 once_more:
		d0 = p1[0];	/* Pull the stuff into registers	*/
		d1 = p1[1];	/*  ... in bursts, if possible.		*/
		d2 = p1[2];
		d3 = p1[3];
		d4 = p1[4];
		d5 = p1[5];
		d6 = p1[6];
		d7 = p1[7];
		d0 ^= p2[0];
		d1 ^= p2[1];
		d2 ^= p2[2];
		d3 ^= p2[3];
		d4 ^= p2[4];
		d5 ^= p2[5];
		d6 ^= p2[6];
		d7 ^= p2[7];
		d0 ^= p3[0];
		d1 ^= p3[1];
		d2 ^= p3[2];
		d3 ^= p3[3];
		d4 ^= p3[4];
		d5 ^= p3[5];
		d6 ^= p3[6];
		d7 ^= p3[7];
		d0 ^= p4[0];
		d1 ^= p4[1];
		d2 ^= p4[2];
		d3 ^= p4[3];
		d4 ^= p4[4];
		d5 ^= p4[5];
		d6 ^= p4[6];
		d7 ^= p4[7];
		d0 ^= p5[0];
		d1 ^= p5[1];
		d2 ^= p5[2];
		d3 ^= p5[3];
		d4 ^= p5[4];
		d5 ^= p5[5];
		d6 ^= p5[6];
		d7 ^= p5[7];
		p1[0] = d0;	/* Store the result (in bursts)		*/
		p1[1] = d1;
		p1[2] = d2;
		p1[3] = d3;
		p1[4] = d4;
		p1[5] = d5;
		p1[6] = d6;
		p1[7] = d7;
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
		p5 += 8;
	} while (--lines > 0);
	if (lines == 0)
		goto once_more;
}

static struct xor_block_template xor_block_8regs = {
	.name = "8regs",
	.do_2 = xor_8regs_2,
	.do_3 = xor_8regs_3,
	.do_4 = xor_8regs_4,
	.do_5 = xor_8regs_5,
};

static struct xor_block_template xor_block_32regs = {
	.name = "32regs",
	.do_2 = xor_32regs_2,
	.do_3 = xor_32regs_3,
	.do_4 = xor_32regs_4,
	.do_5 = xor_32regs_5,
};

static struct xor_block_template xor_block_8regs_p = {
	.name = "8regs_prefetch",
	.do_2 = xor_8regs_p_2,
	.do_3 = xor_8regs_p_3,
	.do_4 = xor_8regs_p_4,
	.do_5 = xor_8regs_p_5,
};

static struct xor_block_template xor_block_32regs_p = {
	.name = "32regs_prefetch",
	.do_2 = xor_32regs_p_2,
	.do_3 = xor_32regs_p_3,
	.do_4 = xor_32regs_p_4,
	.do_5 = xor_32regs_p_5,
};

#define XOR_TRY_TEMPLATES			\
	do {					\
		xor_speed(&xor_block_8regs);	\
		xor_speed(&xor_block_8regs_p);	\
		xor_speed(&xor_block_32regs);	\
		xor_speed(&xor_block_32regs_p);	\
	} while (0)

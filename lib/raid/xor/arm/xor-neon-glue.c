// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2001 Russell King
 */
#include <linux/raid/xor_impl.h>
#include <asm/xor.h>

extern struct xor_block_template const xor_block_neon_inner;

static void
xor_neon_2(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2)
{
	kernel_neon_begin();
	xor_block_neon_inner.do_2(bytes, p1, p2);
	kernel_neon_end();
}

static void
xor_neon_3(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3)
{
	kernel_neon_begin();
	xor_block_neon_inner.do_3(bytes, p1, p2, p3);
	kernel_neon_end();
}

static void
xor_neon_4(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4)
{
	kernel_neon_begin();
	xor_block_neon_inner.do_4(bytes, p1, p2, p3, p4);
	kernel_neon_end();
}

static void
xor_neon_5(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4,
	   const unsigned long * __restrict p5)
{
	kernel_neon_begin();
	xor_block_neon_inner.do_5(bytes, p1, p2, p3, p4, p5);
	kernel_neon_end();
}

struct xor_block_template xor_block_neon = {
	.name	= "neon",
	.do_2	= xor_neon_2,
	.do_3	= xor_neon_3,
	.do_4	= xor_neon_4,
	.do_5	= xor_neon_5
};

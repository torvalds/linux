// SPDX-License-Identifier: GPL-2.0-only
/*
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */

#include <linux/raid/xor_impl.h>
#include <asm/simd.h>
#include <asm/xor.h>

extern struct xor_block_template const xor_block_inner_neon;

static void
xor_neon_2(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2)
{
	scoped_ksimd()
		xor_block_inner_neon.do_2(bytes, p1, p2);
}

static void
xor_neon_3(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3)
{
	scoped_ksimd()
		xor_block_inner_neon.do_3(bytes, p1, p2, p3);
}

static void
xor_neon_4(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4)
{
	scoped_ksimd()
		xor_block_inner_neon.do_4(bytes, p1, p2, p3, p4);
}

static void
xor_neon_5(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4,
	   const unsigned long * __restrict p5)
{
	scoped_ksimd()
		xor_block_inner_neon.do_5(bytes, p1, p2, p3, p4, p5);
}

struct xor_block_template xor_block_arm64 = {
	.name   = "arm64_neon",
	.do_2   = xor_neon_2,
	.do_3   = xor_neon_3,
	.do_4   = xor_neon_4,
	.do_5	= xor_neon_5
};

// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2001 Russell King
 */
#include "xor_impl.h"
#include "xor_arch.h"

static void xor_gen_neon(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes)
{
	kernel_neon_begin();
	xor_gen_neon_inner(dest, srcs, src_cnt, bytes);
	kernel_neon_end();
}

struct xor_block_template xor_block_neon = {
	.name		= "neon",
	.xor_gen	= xor_gen_neon,
};

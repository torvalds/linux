// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LoongArch SIMD XOR operations
 *
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 */

#include <linux/sched.h>
#include <asm/fpu.h>
#include "xor_impl.h"
#include "xor_arch.h"
#include "xor_simd.h"

#define MAKE_XOR_GLUES(flavor)							\
DO_XOR_BLOCKS(flavor##_inner, __xor_##flavor##_2, __xor_##flavor##_3,		\
		__xor_##flavor##_4, __xor_##flavor##_5);			\
										\
static void xor_gen_##flavor(void *dest, void **srcs, unsigned int src_cnt,	\
		unsigned int bytes)						\
{										\
	kernel_fpu_begin();							\
	xor_gen_##flavor##_inner(dest, srcs, src_cnt, bytes);			\
	kernel_fpu_end();							\
}										\
										\
struct xor_block_template xor_block_##flavor = {				\
	.name		= __stringify(flavor),					\
	.xor_gen	= xor_gen_##flavor					\
}

#ifdef CONFIG_CPU_HAS_LSX
MAKE_XOR_GLUES(lsx);
#endif /* CONFIG_CPU_HAS_LSX */

#ifdef CONFIG_CPU_HAS_LASX
MAKE_XOR_GLUES(lasx);
#endif /* CONFIG_CPU_HAS_LASX */

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LoongArch SIMD XOR operations
 *
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 */

#include <linux/sched.h>
#include <linux/raid/xor_impl.h>
#include <asm/fpu.h>
#include <asm/xor.h>
#include "xor_simd.h"

#define MAKE_XOR_GLUE_2(flavor)							\
static void xor_##flavor##_2(unsigned long bytes, unsigned long * __restrict p1,\
		      const unsigned long * __restrict p2)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_2(bytes, p1, p2);					\
	kernel_fpu_end();							\
}										\

#define MAKE_XOR_GLUE_3(flavor)							\
static void xor_##flavor##_3(unsigned long bytes, unsigned long * __restrict p1,\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_3(bytes, p1, p2, p3);					\
	kernel_fpu_end();							\
}										\

#define MAKE_XOR_GLUE_4(flavor)							\
static void xor_##flavor##_4(unsigned long bytes, unsigned long * __restrict p1,\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3,			\
		      const unsigned long * __restrict p4)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_4(bytes, p1, p2, p3, p4);				\
	kernel_fpu_end();							\
}										\

#define MAKE_XOR_GLUE_5(flavor)							\
static void xor_##flavor##_5(unsigned long bytes, unsigned long * __restrict p1,\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3,			\
		      const unsigned long * __restrict p4,			\
		      const unsigned long * __restrict p5)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_5(bytes, p1, p2, p3, p4, p5);				\
	kernel_fpu_end();							\
}										\

#define MAKE_XOR_GLUES(flavor)				\
	MAKE_XOR_GLUE_2(flavor);			\
	MAKE_XOR_GLUE_3(flavor);			\
	MAKE_XOR_GLUE_4(flavor);			\
	MAKE_XOR_GLUE_5(flavor);			\
							\
struct xor_block_template xor_block_##flavor = {	\
	.name = __stringify(flavor),			\
	.do_2 = xor_##flavor##_2,			\
	.do_3 = xor_##flavor##_3,			\
	.do_4 = xor_##flavor##_4,			\
	.do_5 = xor_##flavor##_5,			\
}


#ifdef CONFIG_CPU_HAS_LSX
MAKE_XOR_GLUES(lsx);
#endif /* CONFIG_CPU_HAS_LSX */

#ifdef CONFIG_CPU_HAS_LASX
MAKE_XOR_GLUES(lasx);
#endif /* CONFIG_CPU_HAS_LASX */

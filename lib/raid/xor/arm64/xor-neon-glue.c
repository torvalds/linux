// SPDX-License-Identifier: GPL-2.0-only
/*
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */

#include <asm/simd.h>
#include "xor_impl.h"
#include "xor_arch.h"
#include "xor-neon.h"

#define XOR_TEMPLATE(_name)						\
static void								\
xor_##_name##_2(unsigned long bytes, unsigned long * __restrict p1,	\
	   const unsigned long * __restrict p2)				\
{									\
	scoped_ksimd()							\
		__xor_##_name##_2(bytes, p1, p2);			\
}									\
									\
static void								\
xor_##_name##_3(unsigned long bytes, unsigned long * __restrict p1,	\
	   const unsigned long * __restrict p2,				\
	   const unsigned long * __restrict p3)				\
{									\
	scoped_ksimd()							\
		__xor_##_name##_3(bytes, p1, p2, p3);			\
}									\
									\
static void								\
xor_##_name##_4(unsigned long bytes, unsigned long * __restrict p1,	\
	   const unsigned long * __restrict p2,				\
	   const unsigned long * __restrict p3,				\
	   const unsigned long * __restrict p4)				\
{									\
	scoped_ksimd()							\
		__xor_##_name##_4(bytes, p1, p2, p3, p4);		\
}									\
									\
static void								\
xor_##_name##_5(unsigned long bytes, unsigned long * __restrict p1,	\
	   const unsigned long * __restrict p2,				\
	   const unsigned long * __restrict p3,				\
	   const unsigned long * __restrict p4,				\
	   const unsigned long * __restrict p5)				\
{									\
	scoped_ksimd()							\
		__xor_##_name##_5(bytes, p1, p2, p3, p4, p5);		\
}									\
									\
struct xor_block_template xor_block_##_name = {				\
	.name   = __stringify(_name),					\
	.do_2   = xor_##_name##_2,					\
	.do_3   = xor_##_name##_3,					\
	.do_4   = xor_##_name##_4,					\
	.do_5	= xor_##_name##_5					\
};

XOR_TEMPLATE(neon);
XOR_TEMPLATE(eor3);

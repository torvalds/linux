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
static void xor_gen_##_name(void *dest, void **srcs, unsigned int src_cnt, \
		unsigned int bytes)					\
{									\
	scoped_ksimd()							\
		xor_gen_##_name##_inner(dest, srcs, src_cnt, bytes);	\
}									\
									\
struct xor_block_template xor_block_##_name = {				\
	.name   	= __stringify(_name),				\
	.xor_gen	= xor_gen_##_name,				\
};

XOR_TEMPLATE(neon);
XOR_TEMPLATE(eor3);

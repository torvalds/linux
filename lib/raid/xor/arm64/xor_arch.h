/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */
#include <asm/simd.h>

extern struct xor_block_template xor_block_neon;
extern struct xor_block_template xor_block_eor3;

static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
	if (cpu_has_neon()) {
		if (cpu_have_named_feature(SHA3))
			xor_register(&xor_block_eor3);
		else
			xor_register(&xor_block_neon);
	}
}

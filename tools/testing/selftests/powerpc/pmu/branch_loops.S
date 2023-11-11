/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <ppc-asm.h>

	.text

#define ITER_SHIFT	31

FUNC_START(indirect_branch_loop)
	li	r3, 1
	sldi	r3, r3, ITER_SHIFT

1:	cmpdi	r3, 0
	beqlr

	addi	r3, r3, -1

	ld	r4, 2f@got(%r2)
	mtctr	r4
	bctr

	.balign 32
2:	b	1b

FUNC_END(indirect_branch_loop)

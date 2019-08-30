/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 */

#include <ppc-asm.h>

	.text

FUNC_START(thirty_two_instruction_loop)
	cmpdi	r3,0
	beqlr
	addi	r4,r3,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1
	addi	r4,r4,1	# 28 addi's
	subi	r3,r3,1
	b	FUNC_NAME(thirty_two_instruction_loop)
FUNC_END(thirty_two_instruction_loop)

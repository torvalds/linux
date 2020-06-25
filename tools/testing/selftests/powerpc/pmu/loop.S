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

FUNC_START(thirty_two_instruction_loop_with_ll_sc)
	cmpdi	r3,0
	beqlr
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1		# 5
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
1:	ldarx	r6,0,r4		# 10
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1		# 15
	addi	r5,r5,1
	addi	r5,r5,1
	stdcx.	r6,0,r4
	bne-	1b
	addi	r5,r5,1		# 20
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1		# 25
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1
	addi	r5,r5,1		# 30
	subi	r3,r3,1
	b	FUNC_NAME(thirty_two_instruction_loop_with_ll_sc)
FUNC_END(thirty_two_instruction_loop_with_ll_sc)

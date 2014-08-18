/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <ppc-asm.h>

	.text

FUNC_START(thirty_two_instruction_loop)
	cmpwi	r3,0
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

/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

	.text

	.global thirty_two_instruction_loop
	.type .thirty_two_instruction_loop,@function
	.section ".opd","aw",@progbits
thirty_two_instruction_loop:
	.quad .thirty_two_instruction_loop, .TOC.@tocbase, 0
	.previous
.thirty_two_instruction_loop:
	cmpwi	%r3,0
	beqlr
	addi	%r4,%r3,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1
	addi	%r4,%r4,1	# 28 addi's
	subi	%r3,%r3,1
	b	.thirty_two_instruction_loop

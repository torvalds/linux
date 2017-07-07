/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "basic_asm.h"
#include "vsx_asm.h"

#long check_vsx(vector int *r3);
#This function wraps storeing VSX regs to the end of an array and a
#call to a comparison function in C which boils down to a memcmp()
FUNC_START(check_vsx)
	PUSH_BASIC_STACK(32)
	std	r3,STACK_FRAME_PARAM(0)(sp)
	addi r3, r3, 16 * 12 #Second half of array
	bl store_vsx
	ld r3,STACK_FRAME_PARAM(0)(sp)
	bl vsx_memcmp
	POP_BASIC_STACK(32)
	blr
FUNC_END(check_vsx)

# int preempt_vmx(vector int *varray, int *threads_starting,
#                 int *running);
# On starting will (atomically) decrement threads_starting as a signal
# that the VMX have been loaded with varray. Will proceed to check the
# validity of the VMX registers while running is not zero.
FUNC_START(preempt_vsx)
	PUSH_BASIC_STACK(512)
	std r3,STACK_FRAME_PARAM(0)(sp) # vector int *varray
	std r4,STACK_FRAME_PARAM(1)(sp) # int *threads_starting
	std r5,STACK_FRAME_PARAM(2)(sp) # int *running

	bl load_vsx
	nop

	sync
	# Atomic DEC
	ld r3,STACK_FRAME_PARAM(1)(sp)
1:	lwarx r4,0,r3
	addi r4,r4,-1
	stwcx. r4,0,r3
	bne- 1b

2:	ld r3,STACK_FRAME_PARAM(0)(sp)
	bl check_vsx
	nop
	cmpdi r3,0
	bne 3f
	ld r4,STACK_FRAME_PARAM(2)(sp)
	ld r5,0(r4)
	cmpwi r5,0
	bne 2b

3:	POP_BASIC_STACK(512)
	blr
FUNC_END(preempt_vsx)

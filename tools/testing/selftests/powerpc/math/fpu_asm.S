/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 */

#include "basic_asm.h"
#include "fpu_asm.h"

FUNC_START(check_fpu)
	mr r4,r3
	li	r3,1 # assume a bad result
	lfd	f0,0(r4)
	fcmpu	cr1,f0,f14
	bne	cr1,1f
	lfd	f0,8(r4)
	fcmpu	cr1,f0,f15
	bne	cr1,1f
	lfd	f0,16(r4)
	fcmpu	cr1,f0,f16
	bne	cr1,1f
	lfd	f0,24(r4)
	fcmpu	cr1,f0,f17
	bne	cr1,1f
	lfd	f0,32(r4)
	fcmpu	cr1,f0,f18
	bne	cr1,1f
	lfd	f0,40(r4)
	fcmpu	cr1,f0,f19
	bne	cr1,1f
	lfd	f0,48(r4)
	fcmpu	cr1,f0,f20
	bne	cr1,1f
	lfd	f0,56(r4)
	fcmpu	cr1,f0,f21
	bne	cr1,1f
	lfd	f0,64(r4)
	fcmpu	cr1,f0,f22
	bne	cr1,1f
	lfd	f0,72(r4)
	fcmpu	cr1,f0,f23
	bne	cr1,1f
	lfd	f0,80(r4)
	fcmpu	cr1,f0,f24
	bne	cr1,1f
	lfd	f0,88(r4)
	fcmpu	cr1,f0,f25
	bne	cr1,1f
	lfd	f0,96(r4)
	fcmpu	cr1,f0,f26
	bne	cr1,1f
	lfd	f0,104(r4)
	fcmpu	cr1,f0,f27
	bne	cr1,1f
	lfd	f0,112(r4)
	fcmpu	cr1,f0,f28
	bne	cr1,1f
	lfd	f0,120(r4)
	fcmpu	cr1,f0,f29
	bne	cr1,1f
	lfd	f0,128(r4)
	fcmpu	cr1,f0,f30
	bne	cr1,1f
	lfd	f0,136(r4)
	fcmpu	cr1,f0,f31
	bne	cr1,1f
	li	r3,0 # Success!!!
1:	blr

FUNC_START(test_fpu)
	# r3 holds pointer to where to put the result of fork
	# r4 holds pointer to the pid
	# f14-f31 are non volatiles
	PUSH_BASIC_STACK(256)
	PUSH_FPU(256)
	std	r3,STACK_FRAME_PARAM(0)(sp) # Address of darray
	std r4,STACK_FRAME_PARAM(1)(sp) # Address of pid

	bl load_fpu
	nop
	li	r0,__NR_fork
	sc

	# pass the result of the fork to the caller
	ld	r9,STACK_FRAME_PARAM(1)(sp)
	std	r3,0(r9)

	ld r3,STACK_FRAME_PARAM(0)(sp)
	bl check_fpu
	nop

	POP_FPU(256)
	POP_BASIC_STACK(256)
	blr
FUNC_END(test_fpu)

# int preempt_fpu(double *darray, int *threads_running, int *running)
# On starting will (atomically) decrement not_ready as a signal that the FPU
# has been loaded with darray. Will proceed to check the validity of the FPU
# registers while running is not zero.
FUNC_START(preempt_fpu)
	PUSH_BASIC_STACK(256)
	PUSH_FPU(256)
	std r3,STACK_FRAME_PARAM(0)(sp) # double *darray
	std r4,STACK_FRAME_PARAM(1)(sp) # int *threads_starting
	std r5,STACK_FRAME_PARAM(2)(sp) # int *running

	bl load_fpu
	nop

	sync
	# Atomic DEC
	ld r3,STACK_FRAME_PARAM(1)(sp)
1:	lwarx r4,0,r3
	addi r4,r4,-1
	stwcx. r4,0,r3
	bne- 1b

2:	ld r3,STACK_FRAME_PARAM(0)(sp)
	bl check_fpu
	nop
	cmpdi r3,0
	bne 3f
	ld r4,STACK_FRAME_PARAM(2)(sp)
	ld r5,0(r4)
	cmpwi r5,0
	bne 2b

3:	POP_FPU(256)
	POP_BASIC_STACK(256)
	blr
FUNC_END(preempt_fpu)

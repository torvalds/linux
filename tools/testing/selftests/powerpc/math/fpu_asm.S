/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "../basic_asm.h"

#define PUSH_FPU(pos) \
	stfd	f14,pos(sp); \
	stfd	f15,pos+8(sp); \
	stfd	f16,pos+16(sp); \
	stfd	f17,pos+24(sp); \
	stfd	f18,pos+32(sp); \
	stfd	f19,pos+40(sp); \
	stfd	f20,pos+48(sp); \
	stfd	f21,pos+56(sp); \
	stfd	f22,pos+64(sp); \
	stfd	f23,pos+72(sp); \
	stfd	f24,pos+80(sp); \
	stfd	f25,pos+88(sp); \
	stfd	f26,pos+96(sp); \
	stfd	f27,pos+104(sp); \
	stfd	f28,pos+112(sp); \
	stfd	f29,pos+120(sp); \
	stfd	f30,pos+128(sp); \
	stfd	f31,pos+136(sp);

#define POP_FPU(pos) \
	lfd	f14,pos(sp); \
	lfd	f15,pos+8(sp); \
	lfd	f16,pos+16(sp); \
	lfd	f17,pos+24(sp); \
	lfd	f18,pos+32(sp); \
	lfd	f19,pos+40(sp); \
	lfd	f20,pos+48(sp); \
	lfd	f21,pos+56(sp); \
	lfd	f22,pos+64(sp); \
	lfd	f23,pos+72(sp); \
	lfd	f24,pos+80(sp); \
	lfd	f25,pos+88(sp); \
	lfd	f26,pos+96(sp); \
	lfd	f27,pos+104(sp); \
	lfd	f28,pos+112(sp); \
	lfd	f29,pos+120(sp); \
	lfd	f30,pos+128(sp); \
	lfd	f31,pos+136(sp);

# Careful calling this, it will 'clobber' fpu (by design)
# Don't call this from C
FUNC_START(load_fpu)
	lfd	f14,0(r3)
	lfd	f15,8(r3)
	lfd	f16,16(r3)
	lfd	f17,24(r3)
	lfd	f18,32(r3)
	lfd	f19,40(r3)
	lfd	f20,48(r3)
	lfd	f21,56(r3)
	lfd	f22,64(r3)
	lfd	f23,72(r3)
	lfd	f24,80(r3)
	lfd	f25,88(r3)
	lfd	f26,96(r3)
	lfd	f27,104(r3)
	lfd	f28,112(r3)
	lfd	f29,120(r3)
	lfd	f30,128(r3)
	lfd	f31,136(r3)
	blr
FUNC_END(load_fpu)

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
	std	r3,STACK_FRAME_PARAM(0)(sp) # Address of darray
	std r4,STACK_FRAME_PARAM(1)(sp) # Address of pid
	PUSH_FPU(STACK_FRAME_LOCAL(2,0))

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

	POP_FPU(STACK_FRAME_LOCAL(2,0))
	POP_BASIC_STACK(256)
	blr
FUNC_END(test_fpu)

# int preempt_fpu(double *darray, int *threads_running, int *running)
# On starting will (atomically) decrement not_ready as a signal that the FPU
# has been loaded with darray. Will proceed to check the validity of the FPU
# registers while running is not zero.
FUNC_START(preempt_fpu)
	PUSH_BASIC_STACK(256)
	std r3,STACK_FRAME_PARAM(0)(sp) # double *darray
	std r4,STACK_FRAME_PARAM(1)(sp) # int *threads_starting
	std r5,STACK_FRAME_PARAM(2)(sp) # int *running
	PUSH_FPU(STACK_FRAME_LOCAL(3,0))

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

3:	POP_FPU(STACK_FRAME_LOCAL(3,0))
	POP_BASIC_STACK(256)
	blr
FUNC_END(preempt_fpu)

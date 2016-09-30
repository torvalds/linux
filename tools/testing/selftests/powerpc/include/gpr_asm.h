/*
 * Copyright 2016, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _SELFTESTS_POWERPC_GPR_ASM_H
#define _SELFTESTS_POWERPC_GPR_ASM_H

#include "basic_asm.h"

#define __PUSH_NVREGS(top_pos); \
	std r31,(top_pos)(%r1); \
	std r30,(top_pos - 8)(%r1); \
	std r29,(top_pos - 16)(%r1); \
	std r28,(top_pos - 24)(%r1); \
	std r27,(top_pos - 32)(%r1); \
	std r26,(top_pos - 40)(%r1); \
	std r25,(top_pos - 48)(%r1); \
	std r24,(top_pos - 56)(%r1); \
	std r23,(top_pos - 64)(%r1); \
	std r22,(top_pos - 72)(%r1); \
	std r21,(top_pos - 80)(%r1); \
	std r20,(top_pos - 88)(%r1); \
	std r19,(top_pos - 96)(%r1); \
	std r18,(top_pos - 104)(%r1); \
	std r17,(top_pos - 112)(%r1); \
	std r16,(top_pos - 120)(%r1); \
	std r15,(top_pos - 128)(%r1); \
	std r14,(top_pos - 136)(%r1)

#define __POP_NVREGS(top_pos); \
	ld r31,(top_pos)(%r1); \
	ld r30,(top_pos - 8)(%r1); \
	ld r29,(top_pos - 16)(%r1); \
	ld r28,(top_pos - 24)(%r1); \
	ld r27,(top_pos - 32)(%r1); \
	ld r26,(top_pos - 40)(%r1); \
	ld r25,(top_pos - 48)(%r1); \
	ld r24,(top_pos - 56)(%r1); \
	ld r23,(top_pos - 64)(%r1); \
	ld r22,(top_pos - 72)(%r1); \
	ld r21,(top_pos - 80)(%r1); \
	ld r20,(top_pos - 88)(%r1); \
	ld r19,(top_pos - 96)(%r1); \
	ld r18,(top_pos - 104)(%r1); \
	ld r17,(top_pos - 112)(%r1); \
	ld r16,(top_pos - 120)(%r1); \
	ld r15,(top_pos - 128)(%r1); \
	ld r14,(top_pos - 136)(%r1)

#define PUSH_NVREGS(stack_size) \
	__PUSH_NVREGS(stack_size + STACK_FRAME_MIN_SIZE)

/* 18 NV FPU REGS */
#define PUSH_NVREGS_BELOW_FPU(stack_size) \
	__PUSH_NVREGS(stack_size + STACK_FRAME_MIN_SIZE - (18 * 8))

#define POP_NVREGS(stack_size) \
	__POP_NVREGS(stack_size + STACK_FRAME_MIN_SIZE)

/* 18 NV FPU REGS */
#define POP_NVREGS_BELOW_FPU(stack_size) \
	__POP_NVREGS(stack_size + STACK_FRAME_MIN_SIZE - (18 * 8))

/*
 * Careful calling this, it will 'clobber' NVGPRs (by design)
 * Don't call this from C
 */
FUNC_START(load_gpr)
	ld	r14,0(r3)
	ld	r15,8(r3)
	ld	r16,16(r3)
	ld	r17,24(r3)
	ld	r18,32(r3)
	ld	r19,40(r3)
	ld	r20,48(r3)
	ld	r21,56(r3)
	ld	r22,64(r3)
	ld	r23,72(r3)
	ld	r24,80(r3)
	ld	r25,88(r3)
	ld	r26,96(r3)
	ld	r27,104(r3)
	ld	r28,112(r3)
	ld	r29,120(r3)
	ld	r30,128(r3)
	ld	r31,136(r3)
	blr
FUNC_END(load_gpr)


#endif /* _SELFTESTS_POWERPC_GPR_ASM_H */

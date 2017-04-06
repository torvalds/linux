/*
 * Copyright 2016, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _SELFTESTS_POWERPC_FPU_ASM_H
#define _SELFTESTS_POWERPC_FPU_ASM_H
#include "basic_asm.h"

#define PUSH_FPU(stack_size) \
	stfd	f31,(stack_size + STACK_FRAME_MIN_SIZE)(%r1); \
	stfd	f30,(stack_size + STACK_FRAME_MIN_SIZE - 8)(%r1); \
	stfd	f29,(stack_size + STACK_FRAME_MIN_SIZE - 16)(%r1); \
	stfd	f28,(stack_size + STACK_FRAME_MIN_SIZE - 24)(%r1); \
	stfd	f27,(stack_size + STACK_FRAME_MIN_SIZE - 32)(%r1); \
	stfd	f26,(stack_size + STACK_FRAME_MIN_SIZE - 40)(%r1); \
	stfd	f25,(stack_size + STACK_FRAME_MIN_SIZE - 48)(%r1); \
	stfd	f24,(stack_size + STACK_FRAME_MIN_SIZE - 56)(%r1); \
	stfd	f23,(stack_size + STACK_FRAME_MIN_SIZE - 64)(%r1); \
	stfd	f22,(stack_size + STACK_FRAME_MIN_SIZE - 72)(%r1); \
	stfd	f21,(stack_size + STACK_FRAME_MIN_SIZE - 80)(%r1); \
	stfd	f20,(stack_size + STACK_FRAME_MIN_SIZE - 88)(%r1); \
	stfd	f19,(stack_size + STACK_FRAME_MIN_SIZE - 96)(%r1); \
	stfd	f18,(stack_size + STACK_FRAME_MIN_SIZE - 104)(%r1); \
	stfd	f17,(stack_size + STACK_FRAME_MIN_SIZE - 112)(%r1); \
	stfd	f16,(stack_size + STACK_FRAME_MIN_SIZE - 120)(%r1); \
	stfd	f15,(stack_size + STACK_FRAME_MIN_SIZE - 128)(%r1); \
	stfd	f14,(stack_size + STACK_FRAME_MIN_SIZE - 136)(%r1);

#define POP_FPU(stack_size) \
	lfd	f31,(stack_size + STACK_FRAME_MIN_SIZE)(%r1); \
	lfd	f30,(stack_size + STACK_FRAME_MIN_SIZE - 8)(%r1); \
	lfd	f29,(stack_size + STACK_FRAME_MIN_SIZE - 16)(%r1); \
	lfd	f28,(stack_size + STACK_FRAME_MIN_SIZE - 24)(%r1); \
	lfd	f27,(stack_size + STACK_FRAME_MIN_SIZE - 32)(%r1); \
	lfd	f26,(stack_size + STACK_FRAME_MIN_SIZE - 40)(%r1); \
	lfd	f25,(stack_size + STACK_FRAME_MIN_SIZE - 48)(%r1); \
	lfd	f24,(stack_size + STACK_FRAME_MIN_SIZE - 56)(%r1); \
	lfd	f23,(stack_size + STACK_FRAME_MIN_SIZE - 64)(%r1); \
	lfd	f22,(stack_size + STACK_FRAME_MIN_SIZE - 72)(%r1); \
	lfd	f21,(stack_size + STACK_FRAME_MIN_SIZE - 80)(%r1); \
	lfd	f20,(stack_size + STACK_FRAME_MIN_SIZE - 88)(%r1); \
	lfd	f19,(stack_size + STACK_FRAME_MIN_SIZE - 96)(%r1); \
	lfd	f18,(stack_size + STACK_FRAME_MIN_SIZE - 104)(%r1); \
	lfd	f17,(stack_size + STACK_FRAME_MIN_SIZE - 112)(%r1); \
	lfd	f16,(stack_size + STACK_FRAME_MIN_SIZE - 120)(%r1); \
	lfd	f15,(stack_size + STACK_FRAME_MIN_SIZE - 128)(%r1); \
	lfd	f14,(stack_size + STACK_FRAME_MIN_SIZE - 136)(%r1);

/*
 * Careful calling this, it will 'clobber' fpu (by design)
 * Don't call this from C
 */
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

#endif /* _SELFTESTS_POWERPC_FPU_ASM_H */

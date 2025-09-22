//===-- udivsi3.S - 32-bit unsigned integer divide ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the __udivsi3 (32-bit unsigned integer divide)
// function for the ARM 32-bit architecture.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

	.syntax unified
	.text

DEFINE_CODE_STATE

	.p2align 2
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_uidiv, __udivsi3)

@ unsigned int __udivsi3(unsigned int divident, unsigned int divisor)
@   Calculate and return the quotient of the (unsigned) division.

DEFINE_COMPILERRT_FUNCTION(__udivsi3)
#if __ARM_ARCH_EXT_IDIV__
	tst     r1, r1
	beq     LOCAL_LABEL(divby0)
	udiv	r0, r0, r1
	bx  	lr

LOCAL_LABEL(divby0):
	// Use movs for compatibility with v8-m.base.
	movs    r0, #0
#  ifdef __ARM_EABI__
	b       __aeabi_idiv0
#  else
	JMP(lr)
#  endif

#else // ! __ARM_ARCH_EXT_IDIV__
	cmp	r1, #1
	bcc	LOCAL_LABEL(divby0)
#if defined(USE_THUMB_1)
	bne LOCAL_LABEL(num_neq_denom)
	JMP(lr)
LOCAL_LABEL(num_neq_denom):
#else
	IT(eq)
	JMPc(lr, eq)
#endif
	cmp	r0, r1
#if defined(USE_THUMB_1)
	bhs LOCAL_LABEL(num_ge_denom)
	movs r0, #0
	JMP(lr)
LOCAL_LABEL(num_ge_denom):
#else
	ITT(cc)
	movcc	r0, #0
	JMPc(lr, cc)
#endif

	// Implement division using binary long division algorithm.
	//
	// r0 is the numerator, r1 the denominator.
	//
	// The code before JMP computes the correct shift I, so that
	// r0 and (r1 << I) have the highest bit set in the same position.
	// At the time of JMP, ip := .Ldiv0block - 12 * I.
	// This depends on the fixed instruction size of block.
	// For ARM mode, this is 12 Bytes, for THUMB mode 14 Bytes.
	//
	// block(shift) implements the test-and-update-quotient core.
	// It assumes (r0 << shift) can be computed without overflow and
	// that (r0 << shift) < 2 * r1. The quotient is stored in r3.

#  if defined(__ARM_FEATURE_CLZ)
	clz	ip, r0
	clz	r3, r1
	// r0 >= r1 implies clz(r0) <= clz(r1), so ip <= r3.
	sub	r3, r3, ip
#    if defined(USE_THUMB_2)
	adr	ip, LOCAL_LABEL(div0block) + 1
	sub	ip, ip, r3, lsl #1
#    else
	adr	ip, LOCAL_LABEL(div0block)
#    endif
	sub	ip, ip, r3, lsl #2
	sub	ip, ip, r3, lsl #3
	mov	r3, #0
	bx	ip
#  else // No CLZ Feature
#    if defined(USE_THUMB_2)
#    error THUMB mode requires CLZ or UDIV
#    endif
#    if defined(USE_THUMB_1)
#      define BLOCK_SIZE 10
#    else
#      define BLOCK_SIZE 12
#    endif

	mov	r2, r0
#    if defined(USE_THUMB_1)
	mov ip, r0
	adr r0, LOCAL_LABEL(div0block)
	adds r0, #1
#    else
	adr	ip, LOCAL_LABEL(div0block)
#    endif
	lsrs	r3, r2, #16
	cmp	r3, r1
#    if defined(USE_THUMB_1)
	blo LOCAL_LABEL(skip_16)
	movs r2, r3
	subs r0, r0, #(16 * BLOCK_SIZE)
LOCAL_LABEL(skip_16):
#    else
	movhs	r2, r3
	subhs	ip, ip, #(16 * BLOCK_SIZE)
#    endif

	lsrs	r3, r2, #8
	cmp	r3, r1
#    if defined(USE_THUMB_1)
	blo LOCAL_LABEL(skip_8)
	movs r2, r3
	subs r0, r0, #(8 * BLOCK_SIZE)
LOCAL_LABEL(skip_8):
#    else
	movhs	r2, r3
	subhs	ip, ip, #(8 * BLOCK_SIZE)
#    endif

	lsrs	r3, r2, #4
	cmp	r3, r1
#    if defined(USE_THUMB_1)
	blo LOCAL_LABEL(skip_4)
	movs r2, r3
	subs r0, r0, #(4 * BLOCK_SIZE)
LOCAL_LABEL(skip_4):
#    else
	movhs	r2, r3
	subhs	ip, #(4 * BLOCK_SIZE)
#    endif

	lsrs	r3, r2, #2
	cmp	r3, r1
#    if defined(USE_THUMB_1)
	blo LOCAL_LABEL(skip_2)
	movs r2, r3
	subs r0, r0, #(2 * BLOCK_SIZE)
LOCAL_LABEL(skip_2):
#    else
	movhs	r2, r3
	subhs	ip, ip, #(2 * BLOCK_SIZE)
#    endif

	// Last block, no need to update r2 or r3.
#    if defined(USE_THUMB_1)
	lsrs r3, r2, #1
	cmp r3, r1
	blo LOCAL_LABEL(skip_1)
	subs r0, r0, #(1 * BLOCK_SIZE)
LOCAL_LABEL(skip_1):
	movs r2, r0
	mov r0, ip
	movs r3, #0
	JMP (r2)

#    else
	cmp	r1, r2, lsr #1
	subls	ip, ip, #(1 * BLOCK_SIZE)

	movs	r3, #0

	JMP(ip)
#    endif
#  endif // __ARM_FEATURE_CLZ


#define	IMM	#
	// due to the range limit of branch in Thumb1, we have to place the
	// block closer
LOCAL_LABEL(divby0):
	movs	r0, #0
#      if defined(__ARM_EABI__)
	push {r7, lr}
	bl	__aeabi_idiv0 // due to relocation limit, can't use b.
	pop  {r7, pc}
#      else
	JMP(lr)
#      endif


#if defined(USE_THUMB_1)
#define block(shift)                                                           \
	lsls r2, r1, IMM shift;                                                      \
	cmp r0, r2;                                                                  \
	blo LOCAL_LABEL(block_skip_##shift);                                         \
	subs r0, r0, r2;                                                             \
	LOCAL_LABEL(block_skip_##shift) :;                                           \
	adcs r3, r3 // same as ((r3 << 1) | Carry). Carry is set if r0 >= r2.

	// TODO: if current location counter is not word aligned, we don't
	// need the .p2align and nop
	// Label div0block must be word-aligned. First align block 31
	.p2align 2
	nop // Padding to align div0block as 31 blocks = 310 bytes

#else
#define block(shift)                                                           \
	cmp	r0, r1, lsl IMM shift;                                         \
	ITT(hs);                                                               \
	WIDE(addhs)	r3, r3, IMM (1 << shift);                              \
	WIDE(subhs)	r0, r0, r1, lsl IMM shift
#endif

	block(31)
	block(30)
	block(29)
	block(28)
	block(27)
	block(26)
	block(25)
	block(24)
	block(23)
	block(22)
	block(21)
	block(20)
	block(19)
	block(18)
	block(17)
	block(16)
	block(15)
	block(14)
	block(13)
	block(12)
	block(11)
	block(10)
	block(9)
	block(8)
	block(7)
	block(6)
	block(5)
	block(4)
	block(3)
	block(2)
	block(1)
LOCAL_LABEL(div0block):
	block(0)

	mov	r0, r3
	JMP(lr)
#endif // __ARM_ARCH_EXT_IDIV__

END_COMPILERRT_FUNCTION(__udivsi3)

NO_EXEC_STACK_DIRECTIVE


//===-- int_mul_impl.inc - Integer multiplication -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers used by __mulsi3, __muldi3.
//
//===----------------------------------------------------------------------===//

#ifndef __mulxi3
#error "__mulxi3 must be defined to use this generic implementation"
#endif

	.text
	.align 2

	.globl __mulxi3
	.type  __mulxi3, @function
__mulxi3:
	mv     a2, a0
	mv     a0, zero
.L1:
	andi   a3, a1, 1
	beqz   a3, .L2
	add    a0, a0, a2
.L2:
	srli   a1, a1, 1
	slli   a2, a2, 1
	bnez   a1, .L1
	ret

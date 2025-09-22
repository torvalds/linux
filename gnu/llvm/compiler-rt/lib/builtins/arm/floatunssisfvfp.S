//===-- floatunssisfvfp.S - Implement floatunssisfvfp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern float __floatunssisfvfp(unsigned int a);
//
// Converts single precision float to a 32-bit int rounding towards zero.
// Uses Darwin calling convention where a single precision result is
// return in a GPR..
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__floatunssisfvfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vmov s0, r0
	vcvt.f32.u32 s0, s0
#else
	vmov	s15, r0	       // move int to float register s15
	vcvt.f32.u32 s15, s15  // convert 32-bit int in s15 to float in s15
	vmov	r0, s15        // move s15 to result register
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__floatunssisfvfp)

NO_EXEC_STACK_DIRECTIVE


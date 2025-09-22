//===-- floatunssidfvfp.S - Implement floatunssidfvfp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __floatunssidfvfp(unsigned int a);
//
// Converts a 32-bit int to a double precision float.
// Uses Darwin calling convention where a double precision result is
// return in GPR register pair.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__floatunssidfvfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vmov s0, r0
	vcvt.f64.u32 d0, s0
#else
	vmov	s15, r0        // move int to float register s15
	vcvt.f64.u32 d7, s15   // convert 32-bit int in s15 to double in d7
	vmov	r0, r1, d7     // move d7 to result register pair r0/r1
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__floatunssidfvfp)

NO_EXEC_STACK_DIRECTIVE


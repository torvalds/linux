//===-- divdf3vfp.S - Implement divdf3vfp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __divdf3vfp(double a, double b);
//
// Divides two double precision floating point numbers using the Darwin
// calling convention where double arguments are passsed in GPR pairs
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__divdf3vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vdiv.f64 d0, d0, d1
#else
	vmov	d6, r0, r1		// move first param from r0/r1 pair into d6
	vmov	d7, r2, r3		// move second param from r2/r3 pair into d7
	vdiv.f64 d5, d6, d7
	vmov	r0, r1, d5		// move result back to r0/r1 pair
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__divdf3vfp)

NO_EXEC_STACK_DIRECTIVE


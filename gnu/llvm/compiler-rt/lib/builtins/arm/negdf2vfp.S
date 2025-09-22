//===-- negdf2vfp.S - Implement negdf2vfp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __negdf2vfp(double a, double b);
//
// Returns the negation a double precision floating point numbers using the
// Darwin calling convention where double arguments are passsed in GPR pairs.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__negdf2vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vneg.f64 d0, d0
#else
	eor	r1, r1, #-2147483648	// flip sign bit on double in r0/r1 pair
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__negdf2vfp)

NO_EXEC_STACK_DIRECTIVE


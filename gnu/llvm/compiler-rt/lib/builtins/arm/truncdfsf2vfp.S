//===-- truncdfsf2vfp.S - Implement truncdfsf2vfp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern float __truncdfsf2vfp(double a);
//
// Converts double precision float to single precision result.
// Uses Darwin calling convention where a double precision parameter is
// passed in a R0/R1 pair and a single precision result is returned in R0.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__truncdfsf2vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vcvt.f32.f64 s0, d0
#else
	vmov 	d7, r0, r1   // load double from r0/r1 pair
	vcvt.f32.f64 s15, d7 // convert double to single (trucate precision)
	vmov 	r0, s15      // return result in r0
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__truncdfsf2vfp)

NO_EXEC_STACK_DIRECTIVE


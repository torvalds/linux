//===----------------------Hexagon builtin routine ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


	.macro FUNCTION_BEGIN name
	.text
	.p2align 5
	.globl \name
	.type  \name, @function
\name:
	.endm

	.macro FUNCTION_END name
	.size  \name, . - \name
	.endm


FUNCTION_BEGIN __hexagon_umodsi3
	{
		r2 = cl0(r0)
		r3 = cl0(r1)
		p0 = cmp.gtu(r1,r0)
	}
	{
		r2 = sub(r3,r2)
		if (p0) jumpr r31
	}
	{
		loop0(1f,r2)
		p1 = cmp.eq(r2,#0)
		r2 = lsl(r1,r2)
	}
	.falign
1:
	{
		p0 = cmp.gtu(r2,r0)
		if (!p0.new) r0 = sub(r0,r2)
		r2 = lsr(r2,#1)
		if (p1) r1 = #0
	}:endloop0
	{
		p0 = cmp.gtu(r2,r0)
		if (!p0.new) r0 = sub(r0,r1)
		jumpr r31
	}
FUNCTION_END __hexagon_umodsi3

  .globl __qdsp_umodsi3
  .set   __qdsp_umodsi3, __hexagon_umodsi3

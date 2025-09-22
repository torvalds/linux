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


FUNCTION_BEGIN __hexagon_udivmodsi4
	{
		r2 = cl0(r0)
		r3 = cl0(r1)
		r5:4 = combine(#1,#0)
		p0 = cmp.gtu(r1,r0)
	}
	{
		r6 = sub(r3,r2)
		r4 = r1
		r1:0 = combine(r0,r4)
		if (p0) jumpr r31
	}
	{
		r3:2 = vlslw(r5:4,r6)
		loop0(1f,r6)
		p0 = cmp.eq(r6,#0)
		if (p0.new) r4 = #0
	}
	.falign
1:
	{
		p0 = cmp.gtu(r2,r1)
		if (!p0.new) r1 = sub(r1,r2)
		if (!p0.new) r0 = add(r0,r3)
		r3:2 = vlsrw(r3:2,#1)
	}:endloop0
	{
		p0 = cmp.gtu(r2,r1)
		if (!p0.new) r1 = sub(r1,r4)
		if (!p0.new) r0 = add(r0,r3)
		jumpr r31
	}
FUNCTION_END __hexagon_udivmodsi4

  .globl __qdsp_udivmodsi4
  .set   __qdsp_udivmodsi4, __hexagon_udivmodsi4

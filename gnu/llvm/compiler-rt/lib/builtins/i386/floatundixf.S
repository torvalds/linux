// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../assembly.h"

// xf_float __floatundixf(du_int a);16

#ifdef __i386__

CONST_SECTION

	.balign 16
twop52:
	.quad 0x4330000000000000

	.balign 16
twop84_plus_twop52_neg:
	.quad 0xc530000000100000

	.balign 16
twop84:
	.quad 0x4530000000000000

#define REL_ADDR(_a)	(_a)-0b(%eax)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatundixf)
	calll	0f
0:	popl	%eax
	movss	8(%esp),			%xmm0	// hi 32 bits of input
	movss	4(%esp),			%xmm1	// lo 32 bits of input
	orpd	REL_ADDR(twop84),	%xmm0	// 2^84 + hi (as a double)
	orpd	REL_ADDR(twop52),	%xmm1	// 2^52 + lo (as a double)
	addsd	REL_ADDR(twop84_plus_twop52_neg),	%xmm0	// hi - 2^52 (no rounding occurs)
	movsd	%xmm1,				4(%esp)
	fldl	4(%esp)
	movsd	%xmm0,				4(%esp)
	faddl	4(%esp)
	ret
END_COMPILERRT_FUNCTION(__floatundixf)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE


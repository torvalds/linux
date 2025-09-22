// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../assembly.h"

// di_int __muldi3(di_int a, di_int b);

#ifdef __i386__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__muldi3)
	pushl	%ebx
	movl  16(%esp),		%eax	// b.lo
	movl  12(%esp),		%ecx	// a.hi
	imull	%eax,		%ecx	// b.lo * a.hi

	movl   8(%esp),		%edx	// a.lo
	movl  20(%esp),		%ebx	// b.hi
	imull	%edx,		%ebx	// a.lo * b.hi

	mull	%edx				// EDX:EAX = a.lo * b.lo
	addl	%ecx,		%ebx	// EBX = (a.lo*b.hi + a.hi*b.lo)
	addl	%ebx,		%edx

	popl	%ebx
	retl
END_COMPILERRT_FUNCTION(__muldi3)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE


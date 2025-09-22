// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../assembly.h"

// di_int __divdi3(di_int a, di_int b);

// result = a / b.
// both inputs and the output are 64-bit signed integers.
// This will do whatever the underlying hardware is set to do on division by zero.
// No other exceptions are generated, as the divide cannot overflow.
//
// This is targeted at 32-bit x86 *only*, as this can be done directly in hardware
// on x86_64.  The performance goal is ~40 cycles per divide, which is faster than
// currently possible via simulation of integer divides on the x87 unit.
//
// Stephen Canon, December 2008

#ifdef __i386__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__divdi3)

// This is currently implemented by wrapping the unsigned divide up in an absolute
// value, then restoring the correct sign at the end of the computation.  This could
// certainly be improved upon.

	pushl		%esi
	movl	 20(%esp),			%edx	// high word of b
	movl	 16(%esp),			%eax	// low word of b
	movl		%edx,			%ecx
	sarl		$31,			%ecx	// (b < 0) ? -1 : 0
	xorl		%ecx,			%eax
	xorl		%ecx,			%edx	// EDX:EAX = (b < 0) ? not(b) : b
	subl		%ecx,			%eax
	sbbl		%ecx,			%edx	// EDX:EAX = abs(b)
	movl		%edx,		 20(%esp)
	movl		%eax,		 16(%esp)	// store abs(b) back to stack
	movl		%ecx,			%esi	// set aside sign of b

	movl	 12(%esp),			%edx	// high word of b
	movl	  8(%esp),			%eax	// low word of b
	movl		%edx,			%ecx
	sarl		$31,			%ecx	// (a < 0) ? -1 : 0
	xorl		%ecx,			%eax
	xorl		%ecx,			%edx	// EDX:EAX = (a < 0) ? not(a) : a
	subl		%ecx,			%eax
	sbbl		%ecx,			%edx	// EDX:EAX = abs(a)
	movl		%edx,		 12(%esp)
	movl		%eax,		  8(%esp)	// store abs(a) back to stack
	xorl		%ecx,			%esi	// sign of result = (sign of a) ^ (sign of b)

	pushl		%ebx
	movl	 24(%esp),			%ebx	// Find the index i of the leading bit in b.
	bsrl		%ebx,			%ecx	// If the high word of b is zero, jump to
	jz			9f						// the code to handle that special case [9].

	// High word of b is known to be non-zero on this branch

	movl	 20(%esp),			%eax	// Construct bhi, containing bits [1+i:32+i] of b

	shrl		%cl,			%eax	// Practically, this means that bhi is given by:
	shrl		%eax					//
	notl		%ecx					//		bhi = (high word of b) << (31 - i) |
	shll		%cl,			%ebx	//			  (low word of b) >> (1 + i)
	orl			%eax,			%ebx	//
	movl	 16(%esp),			%edx	// Load the high and low words of a, and jump
	movl	 12(%esp),			%eax	// to [1] if the high word is larger than bhi
	cmpl		%ebx,			%edx	// to avoid overflowing the upcoming divide.
	jae			1f

	// High word of a is greater than or equal to (b >> (1 + i)) on this branch

	divl		%ebx					// eax <-- qs, edx <-- r such that ahi:alo = bs*qs + r

	pushl		%edi
	notl		%ecx
	shrl		%eax
	shrl		%cl,			%eax	// q = qs >> (1 + i)
	movl		%eax,			%edi
	mull	 24(%esp)					// q*blo
	movl	 16(%esp),			%ebx
	movl	 20(%esp),			%ecx	// ECX:EBX = a
	subl		%eax,			%ebx
	sbbl		%edx,			%ecx	// ECX:EBX = a - q*blo
	movl	 28(%esp),			%eax
	imull		%edi,			%eax	// q*bhi
	subl		%eax,			%ecx	// ECX:EBX = a - q*b
	sbbl		$0,				%edi	// decrement q if remainder is negative
	xorl		%edx,			%edx
	movl		%edi,			%eax

	addl		%esi,			%eax	// Restore correct sign to result
	adcl		%esi,			%edx
	xorl		%esi,			%eax
	xorl		%esi,			%edx
	popl		%edi					// Restore callee-save registers
	popl		%ebx
	popl		%esi
	retl								// Return


1:	// High word of a is greater than or equal to (b >> (1 + i)) on this branch

	subl		%ebx,			%edx	// subtract bhi from ahi so that divide will not
	divl		%ebx					// overflow, and find q and r such that
										//
										//		ahi:alo = (1:q)*bhi + r
										//
										// Note that q is a number in (31-i).(1+i)
										// fix point.

	pushl		%edi
	notl		%ecx
	shrl		%eax
	orl			$0x80000000,	%eax
	shrl		%cl,			%eax	// q = (1:qs) >> (1 + i)
	movl		%eax,			%edi
	mull	 24(%esp)					// q*blo
	movl	 16(%esp),			%ebx
	movl	 20(%esp),			%ecx	// ECX:EBX = a
	subl		%eax,			%ebx
	sbbl		%edx,			%ecx	// ECX:EBX = a - q*blo
	movl	 28(%esp),			%eax
	imull		%edi,			%eax	// q*bhi
	subl		%eax,			%ecx	// ECX:EBX = a - q*b
	sbbl		$0,				%edi	// decrement q if remainder is negative
	xorl		%edx,			%edx
	movl		%edi,			%eax

	addl		%esi,			%eax	// Restore correct sign to result
	adcl		%esi,			%edx
	xorl		%esi,			%eax
	xorl		%esi,			%edx
	popl		%edi					// Restore callee-save registers
	popl		%ebx
	popl		%esi
	retl								// Return


9:	// High word of b is zero on this branch

	movl	 16(%esp),			%eax	// Find qhi and rhi such that
	movl	 20(%esp),			%ecx	//
	xorl		%edx,			%edx	//		ahi = qhi*b + rhi	with	0 ≤ rhi < b
	divl		%ecx					//
	movl		%eax,			%ebx	//
	movl	 12(%esp),			%eax	// Find qlo such that
	divl		%ecx					//
	movl		%ebx,			%edx	//		rhi:alo = qlo*b + rlo  with 0 ≤ rlo < b

	addl		%esi,			%eax	// Restore correct sign to result
	adcl		%esi,			%edx
	xorl		%esi,			%eax
	xorl		%esi,			%edx
	popl		%ebx					// Restore callee-save registers
	popl		%esi
	retl								// Return
END_COMPILERRT_FUNCTION(__divdi3)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE


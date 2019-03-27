/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
	.asciz "@(#)_setjmp.s	5.1 (Berkeley) 4/23/90"
#endif /* LIBC_SCCS and not lint */
#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the environment 'a'.
 * The previous signal state is NOT restored.
 */

ENTRY(_setjmp)
	movq	%rdi,%rax
	movq	0(%rsp),%rdx		/* retval */
	movq	%rdx, 0(%rax)		/* 0; retval */
	movq	%rbx, 8(%rax)		/* 1; rbx */
	movq	%rsp,16(%rax)		/* 2; rsp */
	movq	%rbp,24(%rax)		/* 3; rbp */
	movq	%r12,32(%rax)		/* 4; r12 */
	movq	%r13,40(%rax)		/* 5; r13 */
	movq	%r14,48(%rax)		/* 6; r14 */
	movq	%r15,56(%rax)		/* 7; r15 */
	fnstcw	64(%rax)		/* 8; fpu cw */
	stmxcsr	68(%rax)		/*    and mxcsr */
	xorq	%rax,%rax
	ret
END(_setjmp)

	.weak	CNAME(_longjmp)
ENTRY(_longjmp)
	movq	%rdi,%rdx
	/* Restore the mxcsr, but leave exception flags intact. */
	stmxcsr	-4(%rsp)
	movl	68(%rdx),%eax
	andl	$0xffffffc0,%eax
	movl	-4(%rsp),%edi
	andl	$0x3f,%edi
	xorl	%eax,%edi
	movl	%edi,-4(%rsp)
	ldmxcsr -4(%rsp)
	movq	%rsi,%rax		/* retval */
	movq	0(%rdx),%rcx
	movq	8(%rdx),%rbx
	movq	16(%rdx),%rsp
	movq	24(%rdx),%rbp
	movq	32(%rdx),%r12
	movq	40(%rdx),%r13
	movq	48(%rdx),%r14
	movq	56(%rdx),%r15
	fldcw	64(%rdx)
	testq	%rax,%rax
	jnz	1f
	incq	%rax
1:	movq	%rcx,0(%rsp)
	ret
END(_longjmp)

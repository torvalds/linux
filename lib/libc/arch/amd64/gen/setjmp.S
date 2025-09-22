/*	$OpenBSD: setjmp.S,v 1.10 2023/12/10 16:45:51 deraadt Exp $	*/
/*	$NetBSD: __setjmp14.S,v 1.1 2001/06/19 00:25:02 fvdl Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "SYS.h"
#include <machine/setjmp.h>

	.section        .openbsd.randomdata,"aw",@progbits
	.align 8
	.globl	__jmpxor
__jmpxor:
	.zero 8*3		# (rbp, rsp, pc)
	END(__jmpxor)
	.type	__jmpxor,@object

/*
 * C library -- _setjmp, _longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is restored.
 */

ENTRY(setjmp)
	RETGUARD_SETUP(setjmp, r10)
	movq	%rdi,%r8		/* save jmpbuf addr */
	movl	$1,%edi			/* how = SIG_BLOCK */
	xorl	%esi,%esi		/* set = empty */
	movl	$SYS_sigprocmask,%eax
99:	syscall
	PINSYSCALL(SYS_sigprocmask, 99b)
	movq	%r8,%rdi		/* restore jmpbuf addr */
	movq	%rax,(_JB_SIGMASK * 8)(%rdi)

	movq	(%rsp),%r11
	leaq	__jmpxor(%rip),%rcx
	movq	%rbx,(_JB_RBX * 8)(%rdi)
	movq	%rbp,%rax
	xorq	(0*8)(%rcx),%rax
	movq	%rax,(_JB_RBP * 8)(%rdi)
	movq	%r12,(_JB_R12 * 8)(%rdi)
	movq	%r13,(_JB_R13 * 8)(%rdi)
	movq	%r14,(_JB_R14 * 8)(%rdi)
	movq	%r15,(_JB_R15 * 8)(%rdi)
	movq	%rsp,%rax
	xorq	(1*8)(%rcx),%rax
	movq	%rax,(_JB_RSP * 8)(%rdi)
	movq	%r11,%rax
	xorq	(2*8)(%rcx),%rax
	movq	%rax,(_JB_PC  * 8)(%rdi)
	xorq	%rcx,%rcx
	stmxcsr	(_JB_MXCSR * 8)(%rdi)
	fnstcw	(_JB_MXCSR * 8 + 4)(%rdi)

	xorq	%rax,%rax
	RETGUARD_CHECK(setjmp, r10)
	ret
END(setjmp)

ENTRY(longjmp)
	movq	%rdi,%r12
	movl	%esi,%r8d

	movq	(_JB_SIGMASK * 8)(%rdi),%rsi	/* get set from sc_mask */
	movl	$3,%edi				/* how = SIG_SETMASK */
	movl	$SYS_sigprocmask,%eax
98:	syscall
	PINSYSCALL(SYS_sigprocmask, 98b)
	movl	%r8d,%eax

	leaq	 __jmpxor(%rip),%rcx
	movq	(_JB_RSP * 8)(%r12),%r11
	xorq	(1*8)(%rcx),%r11
	movq	%r11,%rsp
	movq	(_JB_PC  * 8)(%r12),%r11
	xorq	(2*8)(%rcx),%r11
	movq	%r11,0(%rsp)
	RETGUARD_SETUP(longjmp, r10)
	ldmxcsr	(_JB_MXCSR * 8)(%r12)
	fldcw	(_JB_MXCSR * 8 + 4)(%r12)
	movq	(_JB_RBX * 8)(%r12),%rbx
	movq	(_JB_RBP * 8)(%r12),%r11
	xorq	(0*8)(%rcx),%r11
	movq	%r11,%rbp
	movq	(_JB_R13 * 8)(%r12),%r13
	movq	(_JB_R14 * 8)(%r12),%r14
	movq	(_JB_R15 * 8)(%r12),%r15
	movq	(_JB_R12 * 8)(%r12),%r12
	xorq	%rcx,%rcx

	testl	%eax,%eax
	jnz	1f
	incl	%eax
1:	RETGUARD_CHECK(longjmp, r10)
	ret
END(longjmp)

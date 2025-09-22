/*	$OpenBSD: _setjmp.S,v 1.5 2020/10/21 17:22:59 kettenis Exp $	*/
/*	$NetBSD: _setjmp.S,v 1.1 2001/06/19 00:25:02 fvdl Exp $	*/

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


#include <machine/asm.h>
#include <machine/setjmp.h>

	.hidden __jmpxor

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is NOT restored.
 */

ENTRY(_setjmp)
	RETGUARD_SETUP(_setjmp, r10)
	movq    (%rsp),%r11
	leaq	__jmpxor(%rip),%rcx
	movq    %rbx,(_JB_RBX * 8)(%rdi)
	movq	%rbp,%rax
	xorq	(0*8)(%rcx),%rax
	movq    %rax,(_JB_RBP * 8)(%rdi)
	movq    %r12,(_JB_R12 * 8)(%rdi)
	movq    %r13,(_JB_R13 * 8)(%rdi)
	movq    %r14,(_JB_R14 * 8)(%rdi)
	movq    %r15,(_JB_R15 * 8)(%rdi)
	movq	%rsp,%rax
	xorq	(1*8)(%rcx),%rax
	movq    %rax,(_JB_RSP * 8)(%rdi)
	movq	%r11,%rax
	xorq	(2*8)(%rcx),%rax
	movq    %rax,(_JB_PC  * 8)(%rdi)
	xorq	%rcx,%rcx
	stmxcsr	(_JB_MXCSR * 8)(%rdi)
	fnstcw	(_JB_MXCSR * 8 + 4)(%rdi)

	xorq	%rax,%rax
	RETGUARD_CHECK(_setjmp, r10)
	ret
END(_setjmp)

ENTRY(_longjmp)
	leaq	 __jmpxor(%rip),%rcx
	movq    (_JB_RSP * 8)(%rdi),%r11
	xorq	(1*8)(%rcx),%r11
	movq	%r11,%rsp
	movq    (_JB_PC  * 8)(%rdi),%r11
	xorq	(2*8)(%rcx),%r11
	movq	%r11,0(%rsp)
	RETGUARD_SETUP(_longjmp, r10)
	ldmxcsr	(_JB_MXCSR * 8)(%rdi)
	fldcw	(_JB_MXCSR * 8 + 4)(%rdi)
	movq    (_JB_RBX * 8)(%rdi),%rbx
	movq    (_JB_RBP * 8)(%rdi),%r11
	xorq    (0*8)(%rcx),%r11
	movq	%r11,%rbp
	movq    (_JB_R12 * 8)(%rdi),%r12
	movq    (_JB_R13 * 8)(%rdi),%r13
	movq    (_JB_R14 * 8)(%rdi),%r14
	movq    (_JB_R15 * 8)(%rdi),%r15
	xorq	%rcx,%rcx

	movl	%esi,%eax
	testl	%eax,%eax
	jnz	1f
	incl	%eax
1:	RETGUARD_CHECK(_longjmp, r10)
	ret
END(_longjmp)

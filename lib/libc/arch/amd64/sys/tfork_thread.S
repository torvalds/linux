/*	$OpenBSD: tfork_thread.S,v 1.12 2023/12/10 16:45:51 deraadt Exp $ */
/*-
 * Copyright (c) 2000 Peter Wemm <peter@FreeBSD.org>
 * Copyright (c) 2003 Alan L. Cox <alc@cs.rice.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>

/*
 * With thanks to John Dyson for the original version of this.
 */

#include "SYS.h"

/*
 *                %rdi   %rsi   %rdx       %rcx
 * __tfork_thread(param, psize, start_fnc, start_arg);
 *
 * param:		Argument to pass to the actual kernel call.
 * psize:		Other argument to pass to the actual kernel call.
 * start_fnc:		Address of thread function to call in child.
 * start_arg:		Argument to pass to the thread function in child.
 */

ENTRY(__tfork_thread)
	.cfi_startproc
	RETGUARD_SETUP(__tfork_thread, r11);
	RETGUARD_PUSH(r11);
	movq	%rdx, %r8
	movq	%rcx, %r9

	/*
	 * Prepare and execute the thread creation syscall
	 */
	movl	$SYS___tfork, %eax
	.cfi_endproc
99:	syscall
	PINSYSCALL(SYS___tfork, 99b)
	jb 	2f

	/*
	 * Check to see if we are in the parent or child
	 */
	cmpl	$0, %eax
	jz	1f
	jmp	3f

	/* the retpoline we'll use to call the child's main */
	_ALIGN_TRAPS
	.cfi_startproc
0:	JMP_RETPOLINE(r8)

	/*
	 * If we are in the child (new thread), then
	 * set-up the call to the internal subroutine.  If it
	 * returns, then call __threxit.
	 */
	_ALIGN_TRAPS
1:
	/* Mark top frame of new thread in CFI and with zero FP */
	.cfi_undefined	rip
	.cfi_undefined	rsp
	xorl	%ebp, %ebp
	movq	%r9, %rdi
	call	0b

	/*
	 * Thread exit system call
	 */
	movl	$SYS___threxit, %eax
	xorl	%edi, %edi
98:	syscall
	PINSYSCALL(SYS___threxit, 98b)
	int3

	/*
	 * Branch here if the thread creation fails:
	 */
2:
	SET_ERRNO
3:
	RETGUARD_POP(r11);
	RETGUARD_CHECK(__tfork_thread, r11);
	ret
	.cfi_endproc
END(__tfork_thread)

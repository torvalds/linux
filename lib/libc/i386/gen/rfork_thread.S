/*-
 * Copyright (c) 2000 Peter Wemm <peter@FreeBSD.org>
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
__FBSDID("$FreeBSD$");

/*
 * With thanks to John Dyson for the original version of this.
 */

#include <SYS.h>

/*
 *                8      12          16         20
 * rfork_thread(flags, stack_addr, start_fnc, start_arg);
 *
 * flags:		Flags to rfork system call.  See rfork(2).
 * stack_addr:		Top of stack for thread.
 * start_fnc:		Address of thread function to call in child.
 * start_arg:		Argument to pass to the thread function in child.
 */

ENTRY(rfork_thread)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi

	/*
	 * Push thread info onto the new thread's stack
	 */
	movl	12(%ebp), %esi	# get stack addr

	subl	$4, %esi
	movl	20(%ebp), %eax	# get start argument
	movl	%eax, (%esi)

	subl	$4, %esi
	movl	16(%ebp), %eax	# get start thread address
	movl	%eax, (%esi)

	/*
	 * Prepare and execute the thread creation syscall
	 */
	pushl	8(%ebp)
	pushl	$0
	movl	$SYS_rfork, %eax
	KERNCALL
	jb 	2f

	/*
	 * Check to see if we are in the parent or child
	 */
	cmpl	$0, %edx
	jnz	1f
	addl	$8, %esp
	popl	%esi
	movl	%ebp, %esp
	popl	%ebp
	ret
	.p2align 2

	/*
	 * If we are in the child (new thread), then
	 * set-up the call to the internal subroutine.  If it
	 * returns, then call __exit.
	 */
1:
	movl	%esi,%esp
	popl	%eax 
	call	*%eax
	addl	$4, %esp

	/*
	 * Exit system call
	 */
	pushl	%eax
	pushl	$0
	movl	$SYS_exit, %eax
	KERNCALL

	/*
	 * Branch here if the thread creation fails:
	 */
2:
	addl	$8, %esp
	popl	%esi
	movl	%ebp, %esp
	popl	%ebp
	jmp	HIDENAME(cerror)
END(rfork_thread)

	.section .note.GNU-stack,"",%progbits

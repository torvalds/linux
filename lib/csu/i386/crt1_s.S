/*-
 * Copyright 2009 Konstantin Belousov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

	.text
	.align	4
	.globl	_start
	.type	_start, @function
_start:
	.cfi_startproc
	xorl	%ebp,%ebp
	pushl	%ebp
	.cfi_def_cfa_offset 4
	movl	%esp,%ebp
	.cfi_offset %ebp,-8
	.cfi_def_cfa_register %ebp
	andl	$0xfffffff0,%esp # align stack
	leal	8(%ebp),%eax
	subl	$4,%esp
	pushl	%eax		# argv
	pushl	4(%ebp)		# argc
	pushl	%edx		# rtld cleanup
	call	_start1
	int3
	.cfi_endproc
	.size	_start, . - _start

	.section .note.GNU-stack,"",%progbits

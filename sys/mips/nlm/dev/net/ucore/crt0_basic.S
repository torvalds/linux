/*-
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asm.h>

	.text
	.align	2
	.globl	_start
	.ent	_start
_start:
	.set	noreorder
	la	gp, _gp
	.end	_start

	.globl	__stack

	.ent	zerobss
zerobss:
	la	v0, _fbss
	la	v1, _end
3:
	sw	zero, 0(v0)
	bltu	v0,v1,3b
	addiu	v0,v0,4			# executed in delay slot
	la	sp, __stack		# set stack pointer
	.end	zerobss

	.ent	init
init:
	addiu	a1,sp,32		# argv = sp + 32
	addiu	a2,sp,40		# envp = sp + 40
	sw	zero,(a1)		# argv[argc] = 0
	sw	zero,(a2)		# envp[0] = 0
	jal	main			# call the program start function
	move	a0,zero			# set argc to 0
1:	b	1b
	nop
	.end	init


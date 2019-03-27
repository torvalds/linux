/*-
 * Copyright 2001 David E. O'Brien
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#ifdef _CALL_ELF
.abiversion _CALL_ELF
#endif

	.section .init,"ax",@progbits
	.p2align	2
	.globl	_init
#if !defined(_CALL_ELF) || _CALL_ELF == 1
	.section ".opd","aw"
	.p2align	3
_init:
	.quad	.L._init,.TOC.@tocbase,0
	.previous
	.type	_init,@function

	.p2align 4
.L._init:
#else
	.p2align 4
	.globl	_init
	.type	_init,@function
_init:
	addis	%r2, %r12, (.TOC.-_init)@ha
	addi	%r2, %r2, (.TOC.-_init)@l
	.localentry _init, .-_init
#endif
	stdu 1,-48(1)
	mflr 0
	std 0,64(1)

/* Fini */
	.section .fini,"ax",@progbits
	.p2align	2
	.globl	_fini
#if !defined(_CALL_ELF) || _CALL_ELF == 1
	.section ".opd","aw"
	.p2align	3
_fini:
	.quad	.L._fini,.TOC.@tocbase,0
	.previous
	.type	_fini,@function

	.p2align 4
.L._fini:
#else
	.p2align 4
	.globl	_fini
	.type	_fini,@function
_fini:
	addis	%r2, %r12, (.TOC.-_fini)@ha
	addi	%r2, %r2, (.TOC.-_fini)@l
	.localentry _fini, .-_fini
#endif
	stdu 1,-48(1)
	mflr 0
	std 0,64(1)

	.section .note.GNU-stack,"",%progbits

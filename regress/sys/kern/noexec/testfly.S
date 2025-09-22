/*	$OpenBSD: testfly.S,v 1.13 2024/03/26 19:12:34 miod Exp $	*/

/*
 * Copyright (c) 2002,2003 Michael Shalayeff
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

#if !defined(_ASM_TYPE_FUNCTION)
#define _ASM_TYPE_FUNCTION @function
#endif

	.space 16384

#if defined(__aarch64__)
	.section .rodata
	.globl	testfly
	.type	testfly,_ASM_TYPE_FUNCTION
testfly:
	bti c
	ret
END(testfly)
#endif

#if defined(__amd64__)
	.section .rodata
	.globl	testfly
	.type	testfly,_ASM_TYPE_FUNCTION
testfly:
	endbr64
	ret
END(testfly)
#endif

#if defined(__i386__)
ENTRY(testfly)
	ret
END(testfly)
#endif

#ifdef __alpha__
LEAF(testfly, 0)
	ret	zero,(ra),1
	nop
END(testfly)
#endif

#ifdef __hppa__
LEAF_ENTRY(testfly)
	bv	r0(rp)
	nop
EXIT(testfly)
#endif

#ifdef __arm__
ENTRY(testfly)
	mov	pc, lr
END(testfly)
#endif

#ifdef __mips64__
LEAF(testfly, 0)
	jr	ra
	nop
END(testfly)
#endif

#ifdef __riscv
ENTRY(testfly)
	jr	ra
END(testfly)
#endif

#ifdef __powerpc__
ENTRY(testfly)
	blr
END(testfly)
#endif

#if defined(__sparc64__)
ENTRY(testfly)
	retl
	nop
END(testfly)
#endif

	.space 16384

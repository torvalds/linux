/*-
 * Copyright (c) 2003 Tim J. Robbins.
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
 * wchar_t *
 * wcschr(const wchar_t *s, wchar_t c) --
 *	Return pointer to first occurrence of the character `c' in the wide
 *	character string `s', or NULL if not found.
 */
ENTRY(wcschr)
	movl	4(%esp),%ecx		/* String */
	movl	8(%esp),%eax		/* Character */
	pushl	%ebx
.p2align 4,0x90
L1:	movl	(%ecx),%ebx
	cmpl	%eax,%ebx
	je	found0
	testl	%ebx,%ebx
	jz	no
	movl	4(%ecx),%ebx
	cmpl	%eax,%ebx
	je	found1
	testl	%ebx,%ebx
	jz	no
	movl	8(%ecx),%ebx
	cmpl	%eax,%ebx
	je	found2
	testl	%ebx,%ebx
	jz	no
	movl	12(%ecx),%ebx
	cmpl	%eax,%ebx
	je	found3
	testl	%ebx,%ebx
	jz	no
	leal	16(%ecx),%ecx
	jmp	L1
.p2align 2,0x90
found3:	leal	4(%ecx),%ecx
.p2align 2,0x90
found2:	leal	4(%ecx),%ecx
.p2align 2,0x90
found1:	leal	4(%ecx),%ecx
.p2align 2,0x90
found0: popl	%ebx
	movl	%ecx,%eax
	ret
.p2align 2,0x90
no:	popl	%ebx
	xorl	%eax,%eax
	ret
END(wcschr)

	.section .note.GNU-stack,"",%progbits

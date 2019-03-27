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
 * int
 * wcscmp(const wchar_t *s1, const wchar_t *s2) --
 *	Return an integer greater than, equal to, or less than 0, when
 *	the string s1 is greater than, equal to, or less than the string s2.
 */
ENTRY(wcscmp)
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi		/* s1 */
	movl	16(%esp),%esi		/* s2 */
.p2align 4,0x90
top:	movl	(%edi),%eax
	cmpl	%eax,(%esi)
	jne	no0
	testl	%eax,%eax
	jz	same
	movl	4(%edi),%eax
	cmpl	%eax,4(%esi)
	jne	no4
	testl	%eax,%eax
	jz	same
	movl	8(%edi),%eax
	cmpl	%eax,8(%esi)
	jne	no8
	testl	%eax,%eax
	jz	same
	movl	12(%edi),%eax
	cmpl	%eax,12(%esi)
	jne	no12
	leal	16(%edi),%edi
	leal	16(%esi),%esi
	testl	%eax,%eax
	jnz	top
.p2align 2,0x90
same:	xorl	%eax,%eax
	popl	%esi
	popl	%edi
	ret
.p2align 2,0x90
no12:	leal	4(%esi),%esi
.p2align 2,0x90
no8:	leal	4(%esi),%esi
.p2align 2,0x90
no4:	leal	4(%esi),%esi
.p2align 2,0x90
no0:	subl	(%esi),%eax
	popl	%esi
	popl	%edi
	ret
END(wcscmp)

	.section .note.GNU-stack,"",%progbits

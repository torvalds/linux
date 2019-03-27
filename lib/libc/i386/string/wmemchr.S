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
 * wmemchr(const wchar_t *buf, wchar_t c, size_t n) --
 *	Search the wide character array `buf', which has length `n',
 *	the character `c', return a pointer to it if found, or NULL on
 *	failure.
 */
ENTRY(wmemchr)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp),%edi		/* Buffer */
	movl	16(%esp),%eax		/* Wide character */
	movl	20(%esp),%ecx		/* Length of buffer */

	/*
	 * Search in chunks of 8 wide characters (32 bytes).
	 */
	movl	%ecx,%ebx
	shrl	$3,%ecx
	jz	small
.p2align 4,0x90
bigloop:cmpl	%eax,(%edi)
	je	found
	cmpl	%eax,4(%edi)
	je	found4
	cmpl	%eax,8(%edi)
	je	found8
	cmpl	%eax,12(%edi)
	je	found12
	cmpl	%eax,16(%edi)
	je	found16
	cmpl	%eax,20(%edi)
	je	found20
	cmpl	%eax,24(%edi)
	je	found24
	cmpl	%eax,28(%edi)
	je	found28
	leal	32(%edi),%edi
	decl	%ecx
	jnz	bigloop
	jmp	small
found:	movl	%edi,%eax
	popl	%ebx
	popl	%edi
	ret
found4:	leal	4(%edi),%edi
	jmp	found
found8:	leal	8(%edi),%edi
	jmp	found
found12:leal	12(%edi),%edi
	jmp	found
found16:leal	16(%edi),%edi
	jmp	found
found20:leal	20(%edi),%edi
	jmp	found
found24:leal	24(%edi),%edi
	jmp	found
found28:leal	28(%edi),%edi
	jmp	found

	/*
	 * Search remaining part of string.
	 */
small:	movl	%ebx,%ecx
	andl	$7,%ecx
	jz	no
.p2align 2,0x90
smltop:	cmpl	%eax,(%edi)
	je	found
	leal	4(%edi),%edi
	decl	%ecx
	jnz	smltop
no:	xorl	%eax,%eax
	popl	%ebx
	popl	%edi
	ret
END(wmemchr)

	.section .note.GNU-stack,"",%progbits

/*
 * Copyright (c) 1993 Winning Strategies, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Winning Strategies, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

/*
 * strcat(s, append)
 *	append a copy of the null-terminated string "append" to the end
 *	of the null-terminated string s, then add a terminating `\0'.
 *
 * Written by:
 *	J.T. Conklin (jtc@wimsey.com), Winning Strategies, Inc.
 */

/*
 * I've unrolled the loop eight times: large enough to make a
 * significant difference, and small enough not to totally trash the
 * cache.
 */

ENTRY(strcat)
	pushl	%edi			/* save edi */
	movl	8(%esp),%edi		/* dst address */
	movl	12(%esp),%edx		/* src address */
	pushl	%edi			/* push destination address */

	cld				/* set search forward */
	xorl	%eax,%eax		/* set search for null terminator */
	movl	$-1,%ecx		/* set search for lots of characters */
	repne				/* search! */
	scasb

	leal	-1(%edi),%ecx		/* correct dst address */

	.align 2,0x90
L1:	movb	(%edx),%al		/* unroll loop, but not too much */
	movb	%al,(%ecx)
	testb	%al,%al
	je	L2
	movb	1(%edx),%al
	movb	%al,1(%ecx)
	testb	%al,%al
	je	L2
	movb	2(%edx),%al
	movb	%al,2(%ecx)
	testb	%al,%al
	je	L2
	movb	3(%edx),%al
	movb	%al,3(%ecx)
	testb	%al,%al
	je	L2
	movb	4(%edx),%al
	movb	%al,4(%ecx)
	testb	%al,%al
	je	L2
	movb	5(%edx),%al
	movb	%al,5(%ecx)
	testb	%al,%al
	je	L2
	movb	6(%edx),%al
	movb	%al,6(%ecx)
	testb	%al,%al
	je	L2
	movb	7(%edx),%al
	movb	%al,7(%ecx)
	addl	$8,%edx
	addl	$8,%ecx
	testb	%al,%al
	jne	L1
L2:	popl	%eax			/* pop destination address */
	popl	%edi			/* restore edi */
	ret
END(strcat)

	.section .note.GNU-stack,"",%progbits

/*
 * Copyright (c) 1993,94 Winning Strategies, Inc.
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
 * strncmp(s1, s2, n)
 *	return an integer greater than, equal to, or less than 0,
 *	according as the first n characters of string s1 is greater
 *	than, equal to, or less than the string s2.
 *
 * %eax - pointer to s1
 * %ecx - pointer to s2
 * %edx - length
 *
 * Written by:
 *	J.T. Conklin (jtc@wimsey.com), Winning Strategies, Inc.
 */

/*
 * I've unrolled the loop eight times: large enough to make a
 * significant difference, and small enough not to totally trash the
 * cache.
 *
 * TODO: change all the jz's back to je for consistency.
 */

ENTRY(strncmp)
	pushl	%ebx
	movl	8(%esp),%eax
	movl	12(%esp),%ecx
	movl	16(%esp),%edx
	testl	%edx,%edx
	jmp	L2			/* Jump into the loop! */

	.align 2,0x90
L1:	incl	%eax
	incl	%ecx
	decl	%edx
L2:	jz	L4			/* strings are equal */
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

/*
 * XXX it might be best to move the next 4 instructions to the end of the
 * unrolled part of the loop.  The unrolled part would then be
 *	movb n(%eax),%bl; testb %bl, %bl; je L3; cmpb n(%ecx); jne L3
 * or maybe better
 *	movb n(%eax),%bl; cmpb n(%ecx); jne L3; testb %bl,%bl; je return_0
 * for n = 0, 1, ..., 8.  The end of the loop would be
 *	L1: addl $8,%eax; addl $8,%ecx; subl $8,%edx; cmpl $8,%edx; jae Lx
 * where residual counts of 0 to 7 are handled at Lx.  However, this would
 * be slower for short strings.  Cache effects are probably not so
 * important because we are only handling a byte at a time.
 */
	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	je	L1

	.align 2,0x90
L3:	movzbl	(%eax),%eax		/* unsigned comparison */
	movzbl	(%ecx),%ecx
	subl	%ecx,%eax
	popl	%ebx
	ret
	.align 2,0x90
L4:	xorl	%eax,%eax
	popl	%ebx
	ret
END(strncmp)

	.section .note.GNU-stack,"",%progbits

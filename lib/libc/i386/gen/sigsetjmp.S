/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)setjmp.s	5.1 (Berkeley) 4/23/90"
 */

#if defined(LIBC_SCCS) && !defined(lint)
	.text
	.asciz "$Id: sigsetjmp.S,v 1.1 1993/12/05 13:01:05 ats Exp $"
#endif /* LIBC_SCCS and not lint */
#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "SYS.h"

/*-
 * TODO:
 *	Rename sigsetjmp to __sigsetjmp and siglongjmp to __siglongjmp,
 *	remove the other *jmp functions and define everything in terms
 *	of the renamed functions.  This requires compiler support for
 *	the renamed functions (introduced in gcc-2.5.3; previous versions
 *	only supported *jmp with 0 or 1 leading underscores).
 *
 *	Restore _all_ the registers and the signal mask atomically.  Can
 *	use sigreturn() if sigreturn() works.
 */

ENTRY(sigsetjmp)
	movl	8(%esp),%eax
	movl	4(%esp),%ecx
	movl	%eax,44(%ecx)
	testl	%eax,%eax
	jz	2f
	leal	28(%ecx), %eax
	pushl	%eax			/* (sigset_t*)oset */
	pushl	$0			/* (sigset_t*)set  */
	pushl	$1			/* SIG_BLOCK       */
	call	__libc_sigprocmask
	addl	$12,%esp
	movl	4(%esp),%ecx
2:	movl	0(%esp),%edx
	movl	%edx, 0(%ecx)
	movl	%ebx, 4(%ecx)
	movl	%esp, 8(%ecx)
	movl	%ebp,12(%ecx)
	movl	%esi,16(%ecx)
	movl	%edi,20(%ecx)
	fnstcw	24(%ecx)
	xorl	%eax,%eax
	ret
END(sigsetjmp)

	WEAK_REFERENCE(__siglongjmp, siglongjmp)
ENTRY(__siglongjmp)
	movl	4(%esp),%edx
	cmpl	$0,44(%edx)
	jz	2f
	pushl	$0			/* (sigset_t*)oset */
	leal	28(%edx), %eax
	pushl	%eax			/* (sigset_t*)set  */
	pushl	$3			/* SIG_SETMASK     */
	call	__libc_sigprocmask
	addl	$12,%esp
	movl	4(%esp),%edx
2:	movl	8(%esp),%eax
	movl	0(%edx),%ecx
	movl	4(%edx),%ebx
	movl	8(%edx),%esp
	movl	12(%edx),%ebp
	movl	16(%edx),%esi
	movl	20(%edx),%edi
	fldcw	24(%edx)
	testl	%eax,%eax
	jnz	1f
	incl	%eax
1:	movl	%ecx,0(%esp)
	ret
END(__siglongjmp)

	.section .note.GNU-stack,"",%progbits

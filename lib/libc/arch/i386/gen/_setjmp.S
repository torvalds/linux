/* $OpenBSD: _setjmp.S,v 1.9 2020/12/13 21:21:32 bluhm Exp $ */
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
 */

#include <machine/asm.h>
#include <machine/setjmp.h>

	.global	__jmpxor

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is NOT restored.
 */

ENTRY(_setjmp)
	call	1f
1:	popl	%ecx
	addl	$__jmpxor-1b,%ecx	# load cookie address
	movl	4(%esp),%eax		# parameter, pointer to env
	movl	0(%esp),%edx		# caller address
	xorl	0(%ecx),%edx		# use eip cookie
	movl	%edx,(_JB_EIP * 4)(%eax)
	movl	%ebx,(_JB_EBX * 4)(%eax)
	movl	%esp,%edx
	xorl	4(%ecx),%edx		# use esp cookie
	movl	%edx,(_JB_ESP * 4)(%eax)
	movl	8(%ecx),%ecx		# load ebp cookie over cookie address
	xorl	%ebp,%ecx
	movl	%ecx,(_JB_EBP * 4)(%eax)
	movl	%esi,(_JB_ESI * 4)(%eax)
	movl	%edi,(_JB_EDI * 4)(%eax)
	fnstcw	(_JB_FCW * 4)(%eax)
	xorl	%eax,%eax
	ret
END(_setjmp)

ENTRY(_longjmp)
	call	1f
1:	popl	%ecx
	addl	$__jmpxor-1b,%ecx	# load cookie address
	movl	4(%esp),%edx		# parameter, pointer to env
	movl	8(%esp),%eax		# parameter, val
	fldcw	(_JB_FCW * 4)(%edx)
	movl	(_JB_EBX * 4)(%edx),%ebx
	movl	(_JB_ESP * 4)(%edx),%esi
	xorl	4(%ecx),%esi		# use esp cookie
	movl	%esi,%esp		# un-xor'ed esp is safe to use
	movl	(_JB_EBP * 4)(%edx),%ebp
	xorl	8(%ecx),%ebp		# use ebp cookie
	movl	(_JB_ESI * 4)(%edx),%esi
	movl	(_JB_EDI * 4)(%edx),%edi
	movl	0(%ecx),%ecx		# load eip cookie over cookie address
	xorl	(_JB_EIP * 4)(%edx),%ecx
	testl	%eax,%eax
	jnz	1f
	incl	%eax
1:	movl	%ecx,0(%esp)
	ret
END(_longjmp)

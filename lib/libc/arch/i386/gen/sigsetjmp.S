/* $OpenBSD: sigsetjmp.S,v 1.14 2023/12/10 16:45:51 deraadt Exp $ */
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

#include "SYS.h"
#include <machine/setjmp.h>

	.global	__jmpxor

ENTRY(sigsetjmp)
	movl	4(%esp),%ecx		# parameter, pointer to env
	movl	8(%esp),%eax		# parameter, savemask
	movl	%eax,(_JB_SIGFLAG * 4)(%ecx)
	testl	%eax,%eax
	jz	1f

	pushl	$0			/* mask = empty */
	pushl	$1			/* how = SIG_BLOCK */
	subl	$4,%esp
	movl	$(SYS_sigprocmask),%eax
99:	int	$0x80			/* leave oset in %eax */
	PINSYSCALL(SYS_sigprocmask, 99b)
	addl	$12,%esp
	movl	%eax,(_JB_SIGMASK * 4)(%ecx)

1:	call	2f
2:	popl	%edx
	addl	$__jmpxor-2b,%edx	# load cookie address

	movl	%ebx,(_JB_EBX * 4)(%ecx)
	movl	%esp,%eax
	xorl	8(%edx),%eax		# use esp cookie
	movl	%eax,(_JB_ESP * 4)(%ecx)
	movl	%ebp,%eax
	xorl	0(%edx),%eax		# use ebp cookie
	movl	%eax,(_JB_EBP * 4)(%ecx)
	movl	%esi,(_JB_ESI * 4)(%ecx)
	movl	%edi,(_JB_EDI * 4)(%ecx)
	movl	4(%edx),%edx		# load eip cookie over cookie address
	xorl	0(%esp),%edx
	movl	%edx,(_JB_EIP * 4)(%ecx)
	fnstcw	(_JB_FCW * 4)(%ecx)
	xorl	%eax,%eax
	ret
END(sigsetjmp)

ENTRY(siglongjmp)
	movl	4(%esp),%edx		# parameter, pointer to env
	cmpl	$0,(_JB_SIGFLAG * 4)(%edx)
	jz	1f

	pushl	(_JB_SIGMASK * 4)(%edx)	/* mask from sc_mask */
	pushl	$3			/* how = SIG_SETMASK */
	subl	$4,%esp
	movl	$(SYS_sigprocmask),%eax
98:	int	$0x80
	PINSYSCALL(SYS_sigprocmask, 98b)
	addl	$12,%esp

1:	call	2f
2:	popl	%ecx
	addl	$__jmpxor-2b,%ecx	# load cookie address

	movl	4(%esp),%edx		# reload in case sigprocmask failed
	movl	8(%esp),%eax		# parameter, val
	fldcw	(_JB_FCW * 4)(%edx)
	movl	(_JB_EBX * 4)(%edx),%ebx
	movl	(_JB_ESP * 4)(%edx),%esi
	xorl	8(%ecx),%esi		# use esp cookie
	movl	%esi,%esp		# un-xor'ed esp is safe to use
	movl	(_JB_EBP * 4)(%edx),%ebp
	xorl	0(%ecx),%ebp		# use ebp cookie
	movl	(_JB_ESI * 4)(%edx),%esi
	movl	(_JB_EDI * 4)(%edx),%edi

	movl	4(%ecx),%ecx		# load eip cookie over cookie address
	xorl	(_JB_EIP * 4)(%edx),%ecx
	testl	%eax,%eax
	jnz	2f
	incl	%eax
2:	movl	%ecx,0(%esp)
	ret
END(siglongjmp)

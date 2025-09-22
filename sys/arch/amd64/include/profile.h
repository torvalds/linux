/*	$OpenBSD: profile.h,v 1.6 2023/06/27 10:11:15 cheloha Exp $	*/
/*	$NetBSD: profile.h,v 1.3 2003/11/28 23:22:45 fvdl Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)profile.h	8.1 (Berkeley) 6/11/93
 */

#define	_MCOUNT_DECL void _mcount

#ifdef __PIC__
#define __MCPLT	"@PLT"
#else
#define __MCPLT
#endif

#define	MCOUNT						\
__asm(" .globl __mcount		\n"			\
"	.type __mcount,@function\n"			\
"__mcount:			\n"			\
"	pushq	%rbp		\n"			\
"	movq	%rsp,%rbp	\n"			\
"	subq	$56,%rsp	\n"			\
"	movq	%rdi,0(%rsp)	\n"			\
"	movq	%rsi,8(%rsp)	\n"			\
"	movq	%rdx,16(%rsp)	\n"			\
"	movq	%rcx,24(%rsp)	\n"			\
"	movq	%r8,32(%rsp)	\n"			\
"	movq	%r9,40(%rsp)	\n"			\
"	movq	%rax,48(%rsp)	\n"			\
"	movq	0(%rbp),%r11	\n"			\
"	movq	8(%r11),%rdi	\n"			\
"	movq	8(%rbp),%rsi	\n"			\
"	call	_mcount"__MCPLT"\n"			\
"	movq	0(%rsp),%rdi	\n"			\
"	movq	8(%rsp),%rsi	\n"			\
"	movq	16(%rsp),%rdx	\n"			\
"	movq	24(%rsp),%rcx	\n"			\
"	movq	32(%rsp),%r8	\n"			\
"	movq	40(%rsp),%r9	\n"			\
"	movq	48(%rsp),%rax	\n"			\
"	leave			\n"			\
"	ret			\n"			\
"	lfence			\n"			\
"	.size __mcount,.-__mcount");


#ifdef _KERNEL
#define MCOUNT_ENTER	s = intr_disable()
#define MCOUNT_EXIT	intr_restore(s)
#endif /* _KERNEL */

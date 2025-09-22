/*	$OpenBSD: profile.h,v 1.3 2016/05/27 16:32:38 deraadt Exp $	*/
/*	$NetBSD: profile.h,v 1.5 2006/10/26 23:54:28 uwe Exp $	*/

/*-
 * Copyright (c) 2000 Tsubai Masanari.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define	_MCOUNT_DECL static void _mcount

#define	MCOUNT __asm ("				\n\
	.text					\n\
	.align	2				\n\
	.globl	__mcount			\n\
__mcount:					\n\
	mov.l	r4, @-r15			\n\
	mov.l	r5, @-r15			\n\
	mov.l	r6, @-r15			\n\
	mov.l	r7, @-r15			\n\
	mov.l	r0, @-r15			\n\
	mov.l	r14, @-r15			\n\
	sts.l	pr, @-r15			\n\
	mov	r15, r14			\n\
						\n\
	mov.l	1f, r1		! _mcount	\n\
	sts	pr, r4		! frompc	\n\
0:	bsrf	r1				\n\
	 mov	r0, r5		! selfpc	\n\
						\n\
	mov	r14, r15			\n\
	lds.l	@r15+, pr			\n\
	mov.l	@r15+, r14			\n\
	mov.l	@r15+, r0			\n\
	mov.l	@r15+, r7			\n\
	mov.l	@r15+, r6			\n\
	mov.l	@r15+, r5			\n\
	jmp	@r0		! real function	\n\
	 mov.l	@r15+, r4			\n\
						\n\
	.align	2				\n\
1:	.long	_mcount - ((0b) + 4)		\n\
						\n\
	.size	__mcount, . - __mcount		");

#ifdef _KERNEL
#define	MCOUNT_ENTER	s = splhigh()
#define	MCOUNT_EXIT	splx(s)
#endif

/*	$OpenBSD: sigprocmask.S,v 1.14 2015/10/23 04:39:24 guenther Exp $	*/

/*
 * Copyright (c) 1999 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "SYS.h"

SYSENTRY_HIDDEN(sigprocmask)
	/* just in case compiler forgot about this */
	stw	arg2, HPPA_FRAME_ARG(2)(sp)

	comb,<>,n r0, arg1, sigprocmask$nblock

	b	sigprocmask$call
	ldi	1, arg0

sigprocmask$nblock
	ldw	0(arg1), arg1
	stw	arg1, HPPA_FRAME_ARG(1)(sp)

sigprocmask$call
	SYSCALL(sigprocmask)

	ldw	HPPA_FRAME_ARG(2)(sp), arg2
	add,=	r0, arg2, r0
	stw	ret0, 0(arg2)
	bv	r0(rp)
	copy	r0, ret0
SYSEXIT_HIDDEN(sigprocmask)

	.end

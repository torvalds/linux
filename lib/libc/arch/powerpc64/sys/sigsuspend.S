/* $OpenBSD: sigsuspend.S,v 1.4 2023/12/10 16:45:52 deraadt Exp $ */

/*
 * Copyright (c) 1996 Dale Rahn <drahn@openbsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* int sigsuspend(int *mask) */

#include "SYS.h"

	.text
SYSENTRY(sigsuspend)
	RETGUARD_SETUP(sigsuspend, %r11)
	li	%r0, SYS_sigsuspend

	lwz %r3, 0(%r3)		/* load the mask */
99:	sc
	PINSYSCALL(SYS_sigsuspend, 99b)

	cmpwi	%r0, 0
	beq	.L_ret
	stw	%r0, R13_OFFSET_ERRNO(%r13)
	li	%r3, -1
.L_ret:
	RETGUARD_CHECK(sigsuspend, %r11);
	blr
SYSCALL_END_HIDDEN(sigsuspend)

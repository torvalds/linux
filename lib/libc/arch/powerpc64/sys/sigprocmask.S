/* $OpenBSD: sigprocmask.S,v 1.5 2023/12/10 16:45:52 deraadt Exp $ */

/*
 * Copyright (c) 1996 Dale Rahn
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

/* sigprocmask(int how, const sigset_t *set, sigset_t *oset); */

#include "SYS.h"

	.text
SYSENTRY_HIDDEN(sigprocmask)
	RETGUARD_SETUP(sigprocmask, %r11)
	li	%r0, SYS_sigprocmask

	stdu	%r1, -32(%r1)
	std	%r5, 16(%r1)
	
	/* check set (new mask value) for null, in which case
	   fiddle arguments */
	cmpdi	%r4, 0
	bne+	.L_load_set
	addi	%r3, 0, 1	/* how = SIG_BLOCK, new mask already 0 */
	b .L_do_call
.L_load_set:
	lwz	%r4, 0(%r4)	/* get new mask */
.L_do_call:
	
99:	sc
	PINSYSCALL(SYS_sigprocmask, 99b)
	
	/* didnt work? */
	cmpwi	%r0, 0
	beq+	.L_sigprocmask_ok
	stw	%r0, R13_OFFSET_ERRNO(%r13)
	li	%r3, -1
	b	.L_end

.L_sigprocmask_ok:
	ld	%r5, 16(%r1)
	cmpdi	%r5, 0
	beq+	.L_sigprocmask_done
	stw	%r3, 0(%r5)
.L_sigprocmask_done:
	li	%r3, 0
	addi	%r1, %r1, 32
.L_end:
	RETGUARD_CHECK(sigprocmask, %r11)
	blr
SYSCALL_END_HIDDEN(sigprocmask)

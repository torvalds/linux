/*	$OpenBSD: tfork_thread.S,v 1.5 2022/12/08 01:25:43 guenther Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "SYS.h"

/*
 * int __tfork_thread(const struct __tfork *param, size_t psize, void (*func)(void *), void *arg);
 */
FRAMESZ=4*REGSZ
GPOFF=FRAMESZ-2*REGSZ
LEAF(__tfork_thread, FRAMESZ)
	/* a0 = param, a1 = psize, a2 = func, a3 = arg */
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, __tfork_thread)
	.set	reorder

	move	t1, a3		/* arg */

	__DO_SYSCALL(__tfork)
	bnez	a3, 9f

	beqz	v0, 1f

	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ

	/*
	 * In parent process: just return.
	 */
	j	ra

1:
	/*
	 * In child process: invoke function, then exit.
	 */

	move	t9, a2		/* func */
	move	a0, t1		/* arg */
	move	v0, zero
	jal	ra, t9

	move	a0, zero
	__DO_SYSCALL(__threxit)
	teq	zero, zero, 0x52

9:
	/*
	 * System call failure.
	 */
	LA	t9, CERROR

	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr	t9
END(__tfork_thread)

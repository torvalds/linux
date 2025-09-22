/*	$OpenBSD: tfork_thread.S,v 1.3 2020/10/18 14:28:17 deraadt Exp $	*/

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

#include  "SYS.h"

/*
 * int __tfork_thread(const struct __tfork *param, size_t psize, void (*func)(void *), void *arg);
 */
LEAF(__tfork_thread,0)
	/* a0 = flags, a1 = psize, a2 = func, a3 = arg */
	mov	a3, a5
	CALLSYS_ERROR(__tfork)

	beq	v0, 1f

	/*
	 * In parent process: just return.
	 */
	RET

1:
	/*
	 * In child process: invoke function, then exit.
	 */
	mov	a5, a0		/* arg */
	mov	a2, pv		/* func */
	jsr	ra, (pv)

	mov	zero, a0
	CALLSYS_NOERROR(__threxit)
	halt
	
END(__tfork_thread)

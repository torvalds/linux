/*	$OpenBSD: sigprocmask.S,v 1.3 2018/08/12 17:15:10 mortimer Exp $	*/
/*	$NetBSD: sigprocmask.S,v 1.5 2003/08/07 16:42:05 agc Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	from: @(#)sigprocmask.s	5.2 (Berkeley) 12/17/90
 */

#include "SYS.h"

SYSENTRY_HIDDEN(sigprocmask)
	RETGUARD_SETUP(sigprocmask, x15)
	cbz	x1, 1f
	ldr	w1, [x1]
	b	2f
1:
	mov	w0, #0x00000001
	mov	w1, #0x00000000
2:

	SYSTRAP(sigprocmask)
	bcs	CERROR
	cbz	x2, 1f
	str	w0, [x2]
1:
	mov	x0, #0x00000000
	RETGUARD_CHECK(sigprocmask, x15)
	ret
SYSCALL_END_HIDDEN(sigprocmask)

/*	$OpenBSD: Ovfork.S,v 1.10 2023/05/18 04:26:06 guenther Exp $	*/
/*	$NetBSD: Ovfork.S,v 1.2 2002/06/03 18:30:33 fvdl Exp $	*/

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
 *
 *	from: @(#)Ovfork.s	5.1 (Berkeley) 4/23/90
 */

#include <machine/asm.h>

#include "SYS.h"

/*
 * This is written to support a potential vfork(2) that would share
 * the parent's vmspace to the child.  For that, the parent must
 * not rely on anything on the stack at the time of the syscall,
 * as the child will overwrite it.  So, keep both the return address
 * and retguard value in registers (r9 and r8) across the call.
 * This used to do an indirect jump on success, but that doesn't
 * work if indirect-branch-tracking is enabled as the _caller_ of
 * this vfork() stub won't know to place an endbr64 instruction
 * after the call.  So, just push it back on the stack and return.
 */
SYSENTRY_HIDDEN(vfork)
	RETGUARD_SETUP(_thread_sys_vfork, r8);
	popq	%r9		/* my rta into r9 */
	SYSTRAP(vfork)
	pushq	%r9
	jnc	1f
	SET_ERRNO
1:	RETGUARD_CHECK(_thread_sys_vfork, r8);
	ret
SYSCALL_END_HIDDEN(vfork)

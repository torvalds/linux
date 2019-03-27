/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998-1999 Andrew Gallatin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_COMPAT_IA32_IA32_UTIL_H
#define	_COMPAT_IA32_IA32_UTIL_H

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <sys/exec.h>
#include <sys/sysent.h>
#include <sys/cdefs.h>

#define	FREEBSD32_MAXUSER	((1ul << 32) - IA32_PAGE_SIZE)
#define	FREEBSD32_MINUSER	0
#define	FREEBSD32_SHAREDPAGE	(FREEBSD32_MAXUSER - IA32_PAGE_SIZE)
#define	FREEBSD32_USRSTACK	FREEBSD32_SHAREDPAGE

#define	IA32_PAGE_SIZE	4096
#define	IA32_MAXDSIZ	(512*1024*1024)		/* 512MB */
#define	IA32_MAXSSIZ	(64*1024*1024)		/* 64MB */
#define	IA32_MAXVMEM	0			/* Unlimited */

struct syscall_args;
int ia32_fetch_syscall_args(struct thread *td);
void ia32_set_syscall_retval(struct thread *, int);
void ia32_fixlimit(struct rlimit *rl, int which);

#endif	/* _COMPAT_IA32_IA32_UTIL_H */

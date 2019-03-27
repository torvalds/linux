/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: proc.h,v 1.2 1997/04/16 22:57:48 thorpej Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_PROC_H_
#define	_MACHINE_PROC_H_

/*
 * Machine-dependent part of the proc structure
 */
struct mdthread {
	int	md_spinlock_count;	/* (k) */
	register_t md_saved_msr;	/* (k) */
};

struct mdproc {
};

#ifdef __powerpc64__
#define	KINFO_PROC_SIZE 1088
#define	KINFO_PROC32_SIZE 816
#else
#define	KINFO_PROC_SIZE 816
#endif

struct syscall_args {
	u_int code;
	struct sysent *callp;
	register_t args[10];
	int narg;
};

#ifdef _KERNEL

#include <machine/pcb.h>

/* Get the current kernel thread stack usage. */
#define	GET_STACK_USAGE(total, used) do {				\
	struct thread *td = curthread;					\
	(total) = td->td_kstack_pages * PAGE_SIZE - sizeof(struct pcb);	\
	(used) = (char *)td->td_kstack +				\
	    td->td_kstack_pages * PAGE_SIZE -				\
	    (char *)&td;						\
} while (0)
#endif

#endif /* !_MACHINE_PROC_H_ */

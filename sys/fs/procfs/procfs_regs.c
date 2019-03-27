/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)procfs_regs.c	8.4 (Berkeley) 6/15/94
 *
 * From:
 *	$Id: procfs_regs.c,v 3.2 1993/12/15 09:40:17 jsp Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/sysent.h>
#include <sys/uio.h>

#include <machine/reg.h>

#include <fs/pseudofs/pseudofs.h>
#include <fs/procfs/procfs.h>

#ifdef COMPAT_FREEBSD32
#include <sys/procfs.h>
#include <machine/fpu.h>

/*
 * PROC(write, regs, td2, &r) becomes
 * proc_write_regs(td2, &r)   or
 * proc_write_regs32(td2, &r32)
 *
 * UIOMOVE_FROMBUF(r, uio) becomes
 * uiomove_frombuf(&r, sizeof(r), uio)  or
 * uiomove_frombuf(&r32, sizeof(r32), uio)
 */
#define	PROC(d, w, t, r)	wrap32 ? \
	proc_ ## d ## _ ## w ## 32(t, r ## 32) : \
	proc_ ## d ## _ ## w(t, r)
#define	UIOMOVE_FROMBUF(k, u)	wrap32 ? \
	uiomove_frombuf(& k ## 32, sizeof(k ## 32), u) : \
	uiomove_frombuf(& k, sizeof(k), u)
#else
#define	PROC(d, w, t, r)	proc_ ## d ## _ ## w(t, r)
#define	UIOMOVE_FROMBUF(k, u)	uiomove_frombuf(& k, sizeof(k), u)
#endif

int
procfs_doprocregs(PFS_FILL_ARGS)
{
	int error;
	struct reg r;
	struct thread *td2;
#ifdef COMPAT_FREEBSD32
	struct reg32 r32;
	int wrap32 = 0;
#endif

	if (uio->uio_offset != 0)
		return (0);

	PROC_LOCK(p);
	PROC_ASSERT_HELD(p);
	if (p_candebug(td, p)) {
		PROC_UNLOCK(p);
		return (EPERM);
	}
	if (!P_SHOULDSTOP(p)) {
		PROC_UNLOCK(p);
		return (EBUSY);
	}

	td2 = FIRST_THREAD_IN_PROC(p);
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32)) {
		if ((SV_PROC_FLAG(td2->td_proc, SV_ILP32)) == 0) {
			PROC_UNLOCK(p);
			return (EINVAL);
		}
		wrap32 = 1;
		memset(&r32, 0, sizeof(r32));
	} else
#endif
		memset(&r, 0, sizeof(r));
	error = PROC(read, regs, td2, &r);
	if (error == 0) {
		PROC_UNLOCK(p);
		error = UIOMOVE_FROMBUF(r, uio);
		PROC_LOCK(p);
	}
	if (error == 0 && uio->uio_rw == UIO_WRITE) {
		if (!P_SHOULDSTOP(p))
			error = EBUSY;
		else
			/* XXXKSE: */
			error = PROC(write, regs, td2, &r);
	}
	PROC_UNLOCK(p);

	return (error);
}

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Dag-Erling Coïdan Smørgrav
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
 *    derived from this software without specific prior written permission.
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
 *      $FreeBSD$
 */

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pioctl.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>

#include <fs/pseudofs/pseudofs.h>
#include <fs/procfs/procfs.h>

#ifdef COMPAT_FREEBSD32
struct procfs_status32 {
	int	state;	/* Running, stopped, something else? */
	int	flags;	/* Any flags */
	unsigned int	events;	/* Events to stop on */
	int	why;	/* What event, if any, proc stopped on */
	unsigned int	val;	/* Any extra data */
};

#define	PIOCWAIT32	_IOR('p', 4, struct procfs_status32)
#define	PIOCSTATUS32	_IOR('p', 6, struct procfs_status32)
#endif

/*
 * Process ioctls
 */
int
procfs_ioctl(PFS_IOCTL_ARGS)
{
	struct procfs_status *ps;
#ifdef COMPAT_FREEBSD32
	struct procfs_status32 *ps32;
#endif
	int error, flags, sig;
#ifdef COMPAT_FREEBSD6
	int ival;
#endif

	KASSERT(p != NULL,
	    ("%s() called without a process", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);

	error = 0;
	switch (cmd) {
#if defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IOC(IOC_IN, 'p', 1, 0):
#endif
#ifdef COMPAT_FREEBSD6
	case _IO('p', 1):
		ival = IOCPARM_IVAL(data);
		data = &ival;
#endif
	case PIOCBIS:
		p->p_stops |= *(unsigned int *)data;
		break;
#if defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IOC(IOC_IN, 'p', 2, 0):
#endif
#ifdef COMPAT_FREEBSD6
	case _IO('p', 2):
		ival = IOCPARM_IVAL(data);
		data = &ival;
#endif
	case PIOCBIC:
		p->p_stops &= ~*(unsigned int *)data;
		break;
#if defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IOC(IOC_IN, 'p', 3, 0):
#endif
#ifdef COMPAT_FREEBSD6
	case _IO('p', 3):
		ival = IOCPARM_IVAL(data);
		data = &ival;
#endif
	case PIOCSFL:
		flags = *(unsigned int *)data;
		if (flags & PF_ISUGID) {
			/*
			 * XXXRW: Is this specific check required here, as
			 * p_candebug() should implement it, or other checks
			 * are missing.
			 */
			error = priv_check(td, PRIV_DEBUG_SUGID);
			if (error)
				break;
		}
		p->p_pfsflags = flags;
		break;
	case PIOCGFL:
		*(unsigned int *)data = p->p_pfsflags;
		break;
	case PIOCWAIT:
		while (p->p_step == 0 && (p->p_flag & P_WEXIT) == 0) {
			/* sleep until p stops */
			_PHOLD(p);
			error = msleep(&p->p_stype, &p->p_mtx,
			    PWAIT|PCATCH, "pioctl", 0);
			_PRELE(p);
			if (error != 0)
				break;
		}
		/* fall through to PIOCSTATUS */
	case PIOCSTATUS:
		ps = (struct procfs_status *)data;
		ps->state = (p->p_step == 0);
		ps->flags = 0; /* nope */
		ps->events = p->p_stops;
		ps->why = p->p_step ? p->p_stype : 0;
		ps->val = p->p_step ? p->p_xsig : 0;
		break;
#ifdef COMPAT_FREEBSD32
	case PIOCWAIT32:
		while (p->p_step == 0 && (p->p_flag & P_WEXIT) == 0) {
			/* sleep until p stops */
			_PHOLD(p);
			error = msleep(&p->p_stype, &p->p_mtx,
			    PWAIT|PCATCH, "pioctl", 0);
			_PRELE(p);
			if (error != 0)
				break;
		}
		/* fall through to PIOCSTATUS32 */
	case PIOCSTATUS32:
		ps32 = (struct procfs_status32 *)data;
		ps32->state = (p->p_step == 0);
		ps32->flags = 0; /* nope */
		ps32->events = p->p_stops;
		ps32->why = p->p_step ? p->p_stype : 0;
		ps32->val = p->p_step ? p->p_xsig : 0;
		break;
#endif
#if defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	case _IOC(IOC_IN, 'p', 5, 0):
#endif
#ifdef COMPAT_FREEBSD6
	case _IO('p', 5):
		ival = IOCPARM_IVAL(data);
		data = &ival;
#endif
	case PIOCCONT:
		if (p->p_step == 0)
			break;
		sig = *(unsigned int *)data;
		if (sig != 0 && !_SIG_VALID(sig)) {
			error = EINVAL;
			break;
		}
#if 0
		p->p_step = 0;
		if (P_SHOULDSTOP(p)) {
			p->p_xsig = sig;
			p->p_flag &= ~(P_STOPPED_TRACE|P_STOPPED_SIG);
			PROC_SLOCK(p);
			thread_unsuspend(p);
			PROC_SUNLOCK(p);
		} else if (sig)
			kern_psignal(p, sig);
#else
		if (sig)
			kern_psignal(p, sig);
		p->p_step = 0;
		wakeup(&p->p_step);
#endif
		break;
	default:
		error = (ENOTTY);
	}

	return (error);
}

/*
 * Clean up on last close
 */
int
procfs_close(PFS_CLOSE_ARGS)
{
	if (p != NULL && (p->p_pfsflags & PF_LINGER) == 0) {
		PROC_LOCK_ASSERT(p, MA_OWNED);
		p->p_pfsflags = 0;
		p->p_stops = 0;
		p->p_step = 0;
		wakeup(&p->p_step);
	}
	return (0);
}

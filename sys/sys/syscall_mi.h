/*	$OpenBSD: syscall_mi.h,v 1.37 2024/12/27 11:57:16 mpi Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)kern_xxx.c	8.2 (Berkeley) 11/14/93
 */

#include <sys/param.h>
#include <sys/pledge.h>
#include <sys/acct.h>
#include <sys/tracepoint.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <uvm/uvm_extern.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include "dt.h"
#if NDT > 0
#include <dev/dt/dtvar.h>
#endif

/*
 * Check if a system call is entered from precisely correct location
 */
static inline int
pin_check(struct proc *p, register_t code)
{
	extern char sigcodecall[], sigcoderet[], sigcodecall[];
	struct pinsyscall *pin = NULL, *ppin, *plibcpin;
	struct process *pr = p->p_p;
	vaddr_t addr;
	int error = 0;

	/* point at start of syscall instruction */
	addr = (vaddr_t)PROC_PC(p) - (vaddr_t)(sigcoderet - sigcodecall);
	ppin = &pr->ps_pin;
	plibcpin = &pr->ps_libcpin;

	/*
	 * System calls come from the following places, checks are ordered
	 * by most common case:
	 * 1) dynamic binary: syscalls in libc.so (in the ps_libcpin region)
	 * 2a) static binary: syscalls in main program (in the ps_pin region)
	 * 2b) dynamic binary: syscalls in ld.so (in the ps_pin region)
	 * 3) sigtramp, containing only sigreturn(2)
	 */
	if (plibcpin->pn_pins &&
	    addr >= plibcpin->pn_start && addr < plibcpin->pn_end)
		pin = plibcpin;
	else if (ppin->pn_pins &&
	    addr >= ppin->pn_start && addr < ppin->pn_end)
		pin = ppin;
	else if (PROC_PC(p) == pr->ps_sigcoderet) {
		if (code == SYS_sigreturn)
			return (0);
		error = EPERM;
		goto die;
	}
	if (pin) {
		if (code >= pin->pn_npins || pin->pn_pins[code] == 0)
			error = ENOSYS;
		else if (pin->pn_pins[code] + pin->pn_start == addr)
			; /* correct location */
		else if (pin->pn_pins[code] == (u_int)-1)
			; /* multiple locations, hopefully a boring operation */
		else
			error = ENOSYS;
	} else
		error = ENOSYS;
	if (error == 0)
		return (0);
die:
#ifdef KTRACE
	if (KTRPOINT(p, KTR_PINSYSCALL))
		ktrpinsyscall(p, error, code, addr);
#endif
	KERNEL_LOCK();
	/* XXX remove or simplify this uprintf() call after OpenBSD 7.5 release */
	uprintf("%s[%d]: pinsyscalls addr %lx code %ld, pinoff 0x%x "
	    "(pin%s %d %lx-%lx %lx) (libcpin%s %d %lx-%lx %lx) error %d\n",
	    p->p_p->ps_comm, p->p_p->ps_pid, addr, code,
	    (pin && code < pin->pn_npins) ? pin->pn_pins[code] : -1,
	    pin == ppin ? "(Y)" : "", ppin->pn_npins,
	    ppin->pn_start, ppin->pn_end, ppin->pn_end - ppin->pn_start,
	    pin == plibcpin ? "(Y)" : "", plibcpin->pn_npins,
	    plibcpin->pn_start, plibcpin->pn_end, plibcpin->pn_end - plibcpin->pn_start,
	    error);
        p->p_p->ps_acflag |= APINSYS;

	/* Try to stop threads immediately, because this process is suspect */
	if (P_HASSIBLING(p))
		single_thread_set(p, SINGLE_UNWIND | SINGLE_DEEP);
	/* Send uncatchable SIGABRT for coredump */
	sigabort(p);
	KERNEL_UNLOCK();
	return (error);
}

/*
 * The MD setup for a system call has been done; here's the MI part.
 */
static inline int
mi_syscall(struct proc *p, register_t code, const struct sysent *callp,
    register_t *argp, register_t retval[2])
{
	uint64_t tval;
	int lock = !(callp->sy_flags & SY_NOLOCK);
	int error, pledged;

	/* refresh the thread's cache of the process's creds */
	refreshcreds(p);

#ifdef SYSCALL_DEBUG
	KERNEL_LOCK();
	scdebug_call(p, code, argp);
	KERNEL_UNLOCK();
#endif
	TRACEPOINT(raw_syscalls, sys_enter, code, NULL);
#if NDT > 0
	DT_ENTER(syscall, code, callp->sy_argsize, argp);
#endif
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSCALL)) {
		/* convert to mask, then include with code */
		ktrsyscall(p, code, callp->sy_argsize, argp);
	}
#endif

	/* SP must be within MAP_STACK space */
	if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
	    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
	    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
		return (EPERM);

	if ((error = pin_check(p, code)))
		return (error);

	pledged = (p->p_p->ps_flags & PS_PLEDGE);
	if (pledged && (error = pledge_syscall(p, code, &tval))) {
		KERNEL_LOCK();
		error = pledge_fail(p, error, tval);
		KERNEL_UNLOCK();
		return (error);
	}
	if (lock)
		KERNEL_LOCK();
	error = (*callp->sy_call)(p, argp, retval);
	if (lock)
		KERNEL_UNLOCK();

	return (error);
}

/*
 * Finish MI stuff on return, after the registers have been set
 */
static inline void
mi_syscall_return(struct proc *p, register_t code, int error,
    const register_t retval[2])
{
#ifdef SYSCALL_DEBUG
	KERNEL_LOCK();
	scdebug_ret(p, code, error, retval);
	KERNEL_UNLOCK();
#endif
#if NDT > 0
	DT_LEAVE(syscall, code, error, retval[0], retval[1]);
#endif
	TRACEPOINT(raw_syscalls, sys_exit, code, NULL);

	userret(p);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p, code, error, retval);
#endif
}

/*
 * Finish MI stuff for a new process/thread to return
 */
static inline void
mi_child_return(struct proc *p)
{
#if defined(SYSCALL_DEBUG) || defined(KTRACE) || NDT > 0
	int code = (p->p_flag & P_THREAD) ? SYS___tfork :
	    (p->p_p->ps_flags & PS_PPWAIT) ? SYS_vfork : SYS_fork;
	const register_t child_retval[2] = { 0, 1 };
#endif

	TRACEPOINT(sched, on__cpu, NULL);

#ifdef SYSCALL_DEBUG
	KERNEL_LOCK();
	scdebug_ret(p, code, 0, child_retval);
	KERNEL_UNLOCK();
#endif
#if NDT > 0
	DT_LEAVE(syscall, code, 0, child_retval[0], child_retval[1]);
#endif
	TRACEPOINT(raw_syscalls, sys_exit, code, NULL);

	userret(p);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p, code, 0, child_retval);
#endif
}

/* 
 * Do the specific processing necessary for an AST
 */
static inline void
mi_ast(struct proc *p, int resched)
{
	if (p->p_flag & P_OWEUPC) {
		KERNEL_LOCK();
		ADDUPROF(p);
		KERNEL_UNLOCK();
	}
	if (resched)
		preempt();

	/*
	 * XXX could move call to userret() here, but
	 * hppa calls ast() in syscall return and sh calls
	 * it after userret()
	 */
}

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2007 The FreeBSD Foundation
 *
 * This code is derived from software contributed to Berkeley by
 * the University of Utah, and William Jolitz.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)trap.c	7.4 (Berkeley) 5/13/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"
#include "opt_ktrace.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pmckern.h>
#include <sys/proc.h>
#include <sys/ktr.h>
#include <sys/pioctl.h>
#include <sys/ptrace.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>
#ifdef KTRACE
#include <sys/uio.h>
#include <sys/ktrace.h>
#endif
#include <security/audit/audit.h>

#include <machine/cpu.h>

#ifdef VIMAGE
#include <net/vnet.h>
#endif

#ifdef	HWPMC_HOOKS
#include <sys/pmckern.h>
#endif

#include <security/mac/mac_framework.h>

void (*softdep_ast_cleanup)(struct thread *);

/*
 * Define the code needed before returning to user mode, for trap and
 * syscall.
 */
void
userret(struct thread *td, struct trapframe *frame)
{
	struct proc *p = td->td_proc;

	CTR3(KTR_SYSC, "userret: thread %p (pid %d, %s)", td, p->p_pid,
            td->td_name);
	KASSERT((p->p_flag & P_WEXIT) == 0,
	    ("Exiting process returns to usermode"));
#ifdef DIAGNOSTIC
	/*
	 * Check that we called signotify() enough.  For
	 * multi-threaded processes, where signal distribution might
	 * change due to other threads changing sigmask, the check is
	 * racy and cannot be performed reliably.
	 * If current process is vfork child, indicated by P_PPWAIT, then
	 * issignal() ignores stops, so we block the check to avoid
	 * classifying pending signals.
	 */
	if (p->p_numthreads == 1) {
		PROC_LOCK(p);
		thread_lock(td);
		if ((p->p_flag & P_PPWAIT) == 0) {
			KASSERT(!SIGPENDING(td) || (td->td_flags &
			    (TDF_NEEDSIGCHK | TDF_ASTPENDING)) ==
			    (TDF_NEEDSIGCHK | TDF_ASTPENDING),
			    ("failed to set signal flags for ast p %p "
			    "td %p fl %x", p, td, td->td_flags));
		}
		thread_unlock(td);
		PROC_UNLOCK(p);
	}
#endif
#ifdef KTRACE
	KTRUSERRET(td);
#endif
	td_softdep_cleanup(td);
	MPASS(td->td_su == NULL);

	/*
	 * If this thread tickled GEOM, we need to wait for the giggling to
	 * stop before we return to userland
	 */
	if (td->td_pflags & TDP_GEOM)
		g_waitidle();

	/*
	 * Charge system time if profiling.
	 */
	if (p->p_flag & P_PROFIL)
		addupc_task(td, TRAPF_PC(frame), td->td_pticks * psratio);

#ifdef HWPMC_HOOKS
	if (PMC_THREAD_HAS_SAMPLES(td))
		PMC_CALL_HOOK(td, PMC_FN_THR_USERRET, NULL);
#endif
	/*
	 * Let the scheduler adjust our priority etc.
	 */
	sched_userret(td);

	/*
	 * Check for misbehavior.
	 *
	 * In case there is a callchain tracing ongoing because of
	 * hwpmc(4), skip the scheduler pinning check.
	 * hwpmc(4) subsystem, infact, will collect callchain informations
	 * at ast() checkpoint, which is past userret().
	 */
	WITNESS_WARN(WARN_PANIC, NULL, "userret: returning");
	KASSERT(td->td_critnest == 0,
	    ("userret: Returning in a critical section"));
	KASSERT(td->td_epochnest == 0,
	    ("userret: Returning in an epoch section"));
	KASSERT(td->td_locks == 0,
	    ("userret: Returning with %d locks held", td->td_locks));
	KASSERT(td->td_rw_rlocks == 0,
	    ("userret: Returning with %d rwlocks held in read mode",
	    td->td_rw_rlocks));
	KASSERT(td->td_sx_slocks == 0,
	    ("userret: Returning with %d sx locks held in shared mode",
	    td->td_sx_slocks));
	KASSERT((td->td_pflags & TDP_NOFAULTING) == 0,
	    ("userret: Returning with pagefaults disabled"));
	KASSERT(td->td_no_sleeping == 0,
	    ("userret: Returning with sleep disabled"));
	KASSERT(td->td_pinned == 0 || (td->td_pflags & TDP_CALLCHAIN) != 0,
	    ("userret: Returning with with pinned thread"));
	KASSERT(td->td_vp_reserv == 0,
	    ("userret: Returning while holding vnode reservation"));
	KASSERT((td->td_flags & (TDF_SBDRY | TDF_SEINTR | TDF_SERESTART)) == 0,
	    ("userret: Returning with stop signals deferred"));
	KASSERT(td->td_su == NULL,
	    ("userret: Returning with SU cleanup request not handled"));
	KASSERT(td->td_vslock_sz == 0,
	    ("userret: Returning with vslock-wired space"));
#ifdef VIMAGE
	/* Unfortunately td_vnet_lpush needs VNET_DEBUG. */
	VNET_ASSERT(curvnet == NULL,
	    ("%s: Returning on td %p (pid %d, %s) with vnet %p set in %s",
	    __func__, td, p->p_pid, td->td_name, curvnet,
	    (td->td_vnet_lpush != NULL) ? td->td_vnet_lpush : "N/A"));
#endif
#ifdef RACCT
	if (__predict_false(racct_enable && p->p_throttled != 0))
		racct_proc_throttled(p);
#endif
}

/*
 * Process an asynchronous software trap.
 * This is relatively easy.
 * This function will return with preemption disabled.
 */
void
ast(struct trapframe *framep)
{
	struct thread *td;
	struct proc *p;
	int flags;
	int sig;

	td = curthread;
	p = td->td_proc;

	CTR3(KTR_SYSC, "ast: thread %p (pid %d, %s)", td, p->p_pid,
            p->p_comm);
	KASSERT(TRAPF_USERMODE(framep), ("ast in kernel mode"));
	WITNESS_WARN(WARN_PANIC, NULL, "Returning to user mode");
	mtx_assert(&Giant, MA_NOTOWNED);
	THREAD_LOCK_ASSERT(td, MA_NOTOWNED);
	td->td_frame = framep;
	td->td_pticks = 0;

	/*
	 * This updates the td_flag's for the checks below in one
	 * "atomic" operation with turning off the astpending flag.
	 * If another AST is triggered while we are handling the
	 * AST's saved in flags, the astpending flag will be set and
	 * ast() will be called again.
	 */
	thread_lock(td);
	flags = td->td_flags;
	td->td_flags &= ~(TDF_ASTPENDING | TDF_NEEDSIGCHK | TDF_NEEDSUSPCHK |
	    TDF_NEEDRESCHED | TDF_ALRMPEND | TDF_PROFPEND | TDF_MACPEND);
	thread_unlock(td);
	VM_CNT_INC(v_trap);

	if (td->td_cowgen != p->p_cowgen)
		thread_cow_update(td);
	if (td->td_pflags & TDP_OWEUPC && p->p_flag & P_PROFIL) {
		addupc_task(td, td->td_profil_addr, td->td_profil_ticks);
		td->td_profil_ticks = 0;
		td->td_pflags &= ~TDP_OWEUPC;
	}
#ifdef HWPMC_HOOKS
	/* Handle Software PMC callchain capture. */
	if (PMC_IS_PENDING_CALLCHAIN(td))
		PMC_CALL_HOOK_UNLOCKED(td, PMC_FN_USER_CALLCHAIN_SOFT, (void *) framep);
#endif
	if (flags & TDF_ALRMPEND) {
		PROC_LOCK(p);
		kern_psignal(p, SIGVTALRM);
		PROC_UNLOCK(p);
	}
	if (flags & TDF_PROFPEND) {
		PROC_LOCK(p);
		kern_psignal(p, SIGPROF);
		PROC_UNLOCK(p);
	}
#ifdef MAC
	if (flags & TDF_MACPEND)
		mac_thread_userret(td);
#endif
	if (flags & TDF_NEEDRESCHED) {
#ifdef KTRACE
		if (KTRPOINT(td, KTR_CSW))
			ktrcsw(1, 1, __func__);
#endif
		thread_lock(td);
		sched_prio(td, td->td_user_pri);
		mi_switch(SW_INVOL | SWT_NEEDRESCHED, NULL);
		thread_unlock(td);
#ifdef KTRACE
		if (KTRPOINT(td, KTR_CSW))
			ktrcsw(0, 1, __func__);
#endif
	}

#ifdef DIAGNOSTIC
	if (p->p_numthreads == 1 && (flags & TDF_NEEDSIGCHK) == 0) {
		PROC_LOCK(p);
		thread_lock(td);
		/*
		 * Note that TDF_NEEDSIGCHK should be re-read from
		 * td_flags, since signal might have been delivered
		 * after we cleared td_flags above.  This is one of
		 * the reason for looping check for AST condition.
		 * See comment in userret() about P_PPWAIT.
		 */
		if ((p->p_flag & P_PPWAIT) == 0) {
			KASSERT(!SIGPENDING(td) || (td->td_flags &
			    (TDF_NEEDSIGCHK | TDF_ASTPENDING)) ==
			    (TDF_NEEDSIGCHK | TDF_ASTPENDING),
			    ("failed2 to set signal flags for ast p %p td %p "
			    "fl %x %x", p, td, flags, td->td_flags));
		}
		thread_unlock(td);
		PROC_UNLOCK(p);
	}
#endif

	/*
	 * Check for signals. Unlocked reads of p_pendingcnt or
	 * p_siglist might cause process-directed signal to be handled
	 * later.
	 */
	if (flags & TDF_NEEDSIGCHK || p->p_pendingcnt > 0 ||
	    !SIGISEMPTY(p->p_siglist)) {
		PROC_LOCK(p);
		mtx_lock(&p->p_sigacts->ps_mtx);
		while ((sig = cursig(td)) != 0) {
			KASSERT(sig >= 0, ("sig %d", sig));
			postsig(sig);
		}
		mtx_unlock(&p->p_sigacts->ps_mtx);
		PROC_UNLOCK(p);
	}
	/*
	 * We need to check to see if we have to exit or wait due to a
	 * single threading requirement or some other STOP condition.
	 */
	if (flags & TDF_NEEDSUSPCHK) {
		PROC_LOCK(p);
		thread_suspend_check(0);
		PROC_UNLOCK(p);
	}

	if (td->td_pflags & TDP_OLDMASK) {
		td->td_pflags &= ~TDP_OLDMASK;
		kern_sigprocmask(td, SIG_SETMASK, &td->td_oldsigmask, NULL, 0);
	}

	userret(td, framep);
}

const char *
syscallname(struct proc *p, u_int code)
{
	static const char unknown[] = "unknown";
	struct sysentvec *sv;

	sv = p->p_sysent;
	if (sv->sv_syscallnames == NULL || code >= sv->sv_size)
		return (unknown);
	return (sv->sv_syscallnames[code]);
}

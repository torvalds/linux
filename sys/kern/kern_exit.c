/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_exit.c	8.7 (Berkeley) 2/12/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/capsicum.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/procdesc.h>
#include <sys/pioctl.h>
#include <sys/jail.h>
#include <sys/tty.h>
#include <sys/wait.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sbuf.h>
#include <sys/signalvar.h>
#include <sys/sched.h>
#include <sys/sx.h>
#include <sys/syscallsubr.h>
#include <sys/syslog.h>
#include <sys/ptrace.h>
#include <sys/acct.h>		/* for acct_process() function prototype */
#include <sys/filedesc.h>
#include <sys/sdt.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/umtx.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/uma.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
dtrace_execexit_func_t	dtrace_fasttrap_exit;
#endif

SDT_PROVIDER_DECLARE(proc);
SDT_PROBE_DEFINE1(proc, , , exit, "int");

/* Hook for NFS teardown procedure. */
void (*nlminfo_release_p)(struct proc *p);

EVENTHANDLER_LIST_DECLARE(process_exit);

struct proc *
proc_realparent(struct proc *child)
{
	struct proc *p, *parent;

	sx_assert(&proctree_lock, SX_LOCKED);
	if ((child->p_treeflag & P_TREE_ORPHANED) == 0)
		return (child->p_pptr->p_pid == child->p_oppid ?
			    child->p_pptr : initproc);
	for (p = child; (p->p_treeflag & P_TREE_FIRST_ORPHAN) == 0;) {
		/* Cannot use LIST_PREV(), since the list head is not known. */
		p = __containerof(p->p_orphan.le_prev, struct proc,
		    p_orphan.le_next);
		KASSERT((p->p_treeflag & P_TREE_ORPHANED) != 0,
		    ("missing P_ORPHAN %p", p));
	}
	parent = __containerof(p->p_orphan.le_prev, struct proc,
	    p_orphans.lh_first);
	return (parent);
}

void
reaper_abandon_children(struct proc *p, bool exiting)
{
	struct proc *p1, *p2, *ptmp;

	sx_assert(&proctree_lock, SX_LOCKED);
	KASSERT(p != initproc, ("reaper_abandon_children for initproc"));
	if ((p->p_treeflag & P_TREE_REAPER) == 0)
		return;
	p1 = p->p_reaper;
	LIST_FOREACH_SAFE(p2, &p->p_reaplist, p_reapsibling, ptmp) {
		LIST_REMOVE(p2, p_reapsibling);
		p2->p_reaper = p1;
		p2->p_reapsubtree = p->p_reapsubtree;
		LIST_INSERT_HEAD(&p1->p_reaplist, p2, p_reapsibling);
		if (exiting && p2->p_pptr == p) {
			PROC_LOCK(p2);
			proc_reparent(p2, p1, true);
			PROC_UNLOCK(p2);
		}
	}
	KASSERT(LIST_EMPTY(&p->p_reaplist), ("p_reaplist not empty"));
	p->p_treeflag &= ~P_TREE_REAPER;
}

static void
reaper_clear(struct proc *p)
{
	struct proc *p1;
	bool clear;

	sx_assert(&proctree_lock, SX_LOCKED);
	LIST_REMOVE(p, p_reapsibling);
	if (p->p_reapsubtree == 1)
		return;
	clear = true;
	LIST_FOREACH(p1, &p->p_reaper->p_reaplist, p_reapsibling) {
		if (p1->p_reapsubtree == p->p_reapsubtree) {
			clear = false;
			break;
		}
	}
	if (clear)
		proc_id_clear(PROC_ID_REAP, p->p_reapsubtree);
}

static void
clear_orphan(struct proc *p)
{
	struct proc *p1;

	sx_assert(&proctree_lock, SA_XLOCKED);
	if ((p->p_treeflag & P_TREE_ORPHANED) == 0)
		return;
	if ((p->p_treeflag & P_TREE_FIRST_ORPHAN) != 0) {
		p1 = LIST_NEXT(p, p_orphan);
		if (p1 != NULL)
			p1->p_treeflag |= P_TREE_FIRST_ORPHAN;
		p->p_treeflag &= ~P_TREE_FIRST_ORPHAN;
	}
	LIST_REMOVE(p, p_orphan);
	p->p_treeflag &= ~P_TREE_ORPHANED;
}

/*
 * exit -- death of process.
 */
void
sys_sys_exit(struct thread *td, struct sys_exit_args *uap)
{

	exit1(td, uap->rval, 0);
	/* NOTREACHED */
}

/*
 * Exit: deallocate address space and other resources, change proc state to
 * zombie, and unlink proc from allproc and parent's lists.  Save exit status
 * and rusage for wait().  Check for child processes and orphan them.
 */
void
exit1(struct thread *td, int rval, int signo)
{
	struct proc *p, *nq, *q, *t;
	struct thread *tdt;
	ksiginfo_t *ksi, *ksi1;
	int signal_parent;

	mtx_assert(&Giant, MA_NOTOWNED);
	KASSERT(rval == 0 || signo == 0, ("exit1 rv %d sig %d", rval, signo));

	p = td->td_proc;
	/*
	 * XXX in case we're rebooting we just let init die in order to
	 * work around an unsolved stack overflow seen very late during
	 * shutdown on sparc64 when the gmirror worker process exists.
	 */
	if (p == initproc && rebooting == 0) {
		printf("init died (signal %d, exit %d)\n", signo, rval);
		panic("Going nowhere without my init!");
	}

	/*
	 * Deref SU mp, since the thread does not return to userspace.
	 */
	td_softdep_cleanup(td);

	/*
	 * MUST abort all other threads before proceeding past here.
	 */
	PROC_LOCK(p);
	/*
	 * First check if some other thread or external request got
	 * here before us.  If so, act appropriately: exit or suspend.
	 * We must ensure that stop requests are handled before we set
	 * P_WEXIT.
	 */
	thread_suspend_check(0);
	while (p->p_flag & P_HADTHREADS) {
		/*
		 * Kill off the other threads. This requires
		 * some co-operation from other parts of the kernel
		 * so it may not be instantaneous.  With this state set
		 * any thread entering the kernel from userspace will
		 * thread_exit() in trap().  Any thread attempting to
		 * sleep will return immediately with EINTR or EWOULDBLOCK
		 * which will hopefully force them to back out to userland
		 * freeing resources as they go.  Any thread attempting
		 * to return to userland will thread_exit() from userret().
		 * thread_exit() will unsuspend us when the last of the
		 * other threads exits.
		 * If there is already a thread singler after resumption,
		 * calling thread_single will fail; in that case, we just
		 * re-check all suspension request, the thread should
		 * either be suspended there or exit.
		 */
		if (!thread_single(p, SINGLE_EXIT))
			/*
			 * All other activity in this process is now
			 * stopped.  Threading support has been turned
			 * off.
			 */
			break;
		/*
		 * Recheck for new stop or suspend requests which
		 * might appear while process lock was dropped in
		 * thread_single().
		 */
		thread_suspend_check(0);
	}
	KASSERT(p->p_numthreads == 1,
	    ("exit1: proc %p exiting with %d threads", p, p->p_numthreads));
	racct_sub(p, RACCT_NTHR, 1);

	/* Let event handler change exit status */
	p->p_xexit = rval;
	p->p_xsig = signo;

	/*
	 * Wakeup anyone in procfs' PIOCWAIT.  They should have a hold
	 * on our vmspace, so we should block below until they have
	 * released their reference to us.  Note that if they have
	 * requested S_EXIT stops we will block here until they ack
	 * via PIOCCONT.
	 */
	_STOPEVENT(p, S_EXIT, 0);

	/*
	 * Ignore any pending request to stop due to a stop signal.
	 * Once P_WEXIT is set, future requests will be ignored as
	 * well.
	 */
	p->p_flag &= ~P_STOPPED_SIG;
	KASSERT(!P_SHOULDSTOP(p), ("exiting process is stopped"));

	/*
	 * Note that we are exiting and do another wakeup of anyone in
	 * PIOCWAIT in case they aren't listening for S_EXIT stops or
	 * decided to wait again after we told them we are exiting.
	 */
	p->p_flag |= P_WEXIT;
	wakeup(&p->p_stype);

	/*
	 * Wait for any processes that have a hold on our vmspace to
	 * release their reference.
	 */
	while (p->p_lock > 0)
		msleep(&p->p_lock, &p->p_mtx, PWAIT, "exithold", 0);

	PROC_UNLOCK(p);
	/* Drain the limit callout while we don't have the proc locked */
	callout_drain(&p->p_limco);

#ifdef AUDIT
	/*
	 * The Sun BSM exit token contains two components: an exit status as
	 * passed to exit(), and a return value to indicate what sort of exit
	 * it was.  The exit status is WEXITSTATUS(rv), but it's not clear
	 * what the return value is.
	 */
	AUDIT_ARG_EXIT(rval, 0);
	AUDIT_SYSCALL_EXIT(0, td);
#endif

	/* Are we a task leader with peers? */
	if (p->p_peers != NULL && p == p->p_leader) {
		mtx_lock(&ppeers_lock);
		q = p->p_peers;
		while (q != NULL) {
			PROC_LOCK(q);
			kern_psignal(q, SIGKILL);
			PROC_UNLOCK(q);
			q = q->p_peers;
		}
		while (p->p_peers != NULL)
			msleep(p, &ppeers_lock, PWAIT, "exit1", 0);
		mtx_unlock(&ppeers_lock);
	}

	/*
	 * Check if any loadable modules need anything done at process exit.
	 * E.g. SYSV IPC stuff.
	 * Event handler could change exit status.
	 * XXX what if one of these generates an error?
	 */
	EVENTHANDLER_DIRECT_INVOKE(process_exit, p);

	/*
	 * If parent is waiting for us to exit or exec,
	 * P_PPWAIT is set; we will wakeup the parent below.
	 */
	PROC_LOCK(p);
	stopprofclock(p);
	p->p_flag &= ~(P_TRACED | P_PPWAIT | P_PPTRACE);
	p->p_ptevents = 0;

	/*
	 * Stop the real interval timer.  If the handler is currently
	 * executing, prevent it from rearming itself and let it finish.
	 */
	if (timevalisset(&p->p_realtimer.it_value) &&
	    _callout_stop_safe(&p->p_itcallout, CS_EXECUTING, NULL) == 0) {
		timevalclear(&p->p_realtimer.it_interval);
		msleep(&p->p_itcallout, &p->p_mtx, PWAIT, "ritwait", 0);
		KASSERT(!timevalisset(&p->p_realtimer.it_value),
		    ("realtime timer is still armed"));
	}

	PROC_UNLOCK(p);

	umtx_thread_exit(td);

	/*
	 * Reset any sigio structures pointing to us as a result of
	 * F_SETOWN with our pid.
	 */
	funsetownlst(&p->p_sigiolst);

	/*
	 * If this process has an nlminfo data area (for lockd), release it
	 */
	if (nlminfo_release_p != NULL && p->p_nlminfo != NULL)
		(*nlminfo_release_p)(p);

	/*
	 * Close open files and release open-file table.
	 * This may block!
	 */
	fdescfree(td);

	/*
	 * If this thread tickled GEOM, we need to wait for the giggling to
	 * stop before we return to userland
	 */
	if (td->td_pflags & TDP_GEOM)
		g_waitidle();

	/*
	 * Remove ourself from our leader's peer list and wake our leader.
	 */
	if (p->p_leader->p_peers != NULL) {
		mtx_lock(&ppeers_lock);
		if (p->p_leader->p_peers != NULL) {
			q = p->p_leader;
			while (q->p_peers != p)
				q = q->p_peers;
			q->p_peers = p->p_peers;
			wakeup(p->p_leader);
		}
		mtx_unlock(&ppeers_lock);
	}

	vmspace_exit(td);
	killjobc();
	(void)acct_process(td);

#ifdef KTRACE
	ktrprocexit(td);
#endif
	/*
	 * Release reference to text vnode
	 */
	if (p->p_textvp != NULL) {
		vrele(p->p_textvp);
		p->p_textvp = NULL;
	}

	/*
	 * Release our limits structure.
	 */
	lim_free(p->p_limit);
	p->p_limit = NULL;

	tidhash_remove(td);

	/*
	 * Call machine-dependent code to release any
	 * machine-dependent resources other than the address space.
	 * The address space is released by "vmspace_exitfree(p)" in
	 * vm_waitproc().
	 */
	cpu_exit(td);

	WITNESS_WARN(WARN_PANIC, NULL, "process (pid %d) exiting", p->p_pid);

	/*
	 * Move proc from allproc queue to zombproc.
	 */
	sx_xlock(&allproc_lock);
	sx_xlock(&zombproc_lock);
	LIST_REMOVE(p, p_list);
	LIST_INSERT_HEAD(&zombproc, p, p_list);
	sx_xunlock(&zombproc_lock);
	sx_xunlock(&allproc_lock);

	sx_xlock(&proctree_lock);

	/*
	 * Reparent all children processes:
	 * - traced ones to the original parent (or init if we are that parent)
	 * - the rest to init
	 */
	q = LIST_FIRST(&p->p_children);
	if (q != NULL)		/* only need this if any child is S_ZOMB */
		wakeup(q->p_reaper);
	for (; q != NULL; q = nq) {
		nq = LIST_NEXT(q, p_sibling);
		ksi = ksiginfo_alloc(TRUE);
		PROC_LOCK(q);
		q->p_sigparent = SIGCHLD;

		if (!(q->p_flag & P_TRACED)) {
			proc_reparent(q, q->p_reaper, true);
			if (q->p_state == PRS_ZOMBIE) {
				/*
				 * Inform reaper about the reparented
				 * zombie, since wait(2) has something
				 * new to report.  Guarantee queueing
				 * of the SIGCHLD signal, similar to
				 * the _exit() behaviour, by providing
				 * our ksiginfo.  Ksi is freed by the
				 * signal delivery.
				 */
				if (q->p_ksi == NULL) {
					ksi1 = NULL;
				} else {
					ksiginfo_copy(q->p_ksi, ksi);
					ksi->ksi_flags |= KSI_INS;
					ksi1 = ksi;
					ksi = NULL;
				}
				PROC_LOCK(q->p_reaper);
				pksignal(q->p_reaper, SIGCHLD, ksi1);
				PROC_UNLOCK(q->p_reaper);
			} else if (q->p_pdeathsig > 0) {
				/*
				 * The child asked to received a signal
				 * when we exit.
				 */
				kern_psignal(q, q->p_pdeathsig);
			}
		} else {
			/*
			 * Traced processes are killed since their existence
			 * means someone is screwing up.
			 */
			t = proc_realparent(q);
			if (t == p) {
				proc_reparent(q, q->p_reaper, true);
			} else {
				PROC_LOCK(t);
				proc_reparent(q, t, true);
				PROC_UNLOCK(t);
			}
			/*
			 * Since q was found on our children list, the
			 * proc_reparent() call moved q to the orphan
			 * list due to present P_TRACED flag. Clear
			 * orphan link for q now while q is locked.
			 */
			clear_orphan(q);
			q->p_flag &= ~(P_TRACED | P_STOPPED_TRACE);
			q->p_flag2 &= ~P2_PTRACE_FSTP;
			q->p_ptevents = 0;
			FOREACH_THREAD_IN_PROC(q, tdt) {
				tdt->td_dbgflags &= ~(TDB_SUSPEND | TDB_XSIG |
				    TDB_FSTP);
			}
			kern_psignal(q, SIGKILL);
		}
		PROC_UNLOCK(q);
		if (ksi != NULL)
			ksiginfo_free(ksi);
	}

	/*
	 * Also get rid of our orphans.
	 */
	while ((q = LIST_FIRST(&p->p_orphans)) != NULL) {
		PROC_LOCK(q);
		/*
		 * If we are the real parent of this process
		 * but it has been reparented to a debugger, then
		 * check if it asked for a signal when we exit.
		 */
		if (q->p_pdeathsig > 0)
			kern_psignal(q, q->p_pdeathsig);
		CTR2(KTR_PTRACE, "exit: pid %d, clearing orphan %d", p->p_pid,
		    q->p_pid);
		clear_orphan(q);
		PROC_UNLOCK(q);
	}

#ifdef KDTRACE_HOOKS
	if (SDT_PROBES_ENABLED()) {
		int reason = CLD_EXITED;
		if (WCOREDUMP(signo))
			reason = CLD_DUMPED;
		else if (WIFSIGNALED(signo))
			reason = CLD_KILLED;
		SDT_PROBE1(proc, , , exit, reason);
	}
#endif

	/* Save exit status. */
	PROC_LOCK(p);
	p->p_xthread = td;

#ifdef KDTRACE_HOOKS
	/*
	 * Tell the DTrace fasttrap provider about the exit if it
	 * has declared an interest.
	 */
	if (dtrace_fasttrap_exit)
		dtrace_fasttrap_exit(p);
#endif

	/*
	 * Notify interested parties of our demise.
	 */
	KNOTE_LOCKED(p->p_klist, NOTE_EXIT);

	/*
	 * If this is a process with a descriptor, we may not need to deliver
	 * a signal to the parent.  proctree_lock is held over
	 * procdesc_exit() to serialize concurrent calls to close() and
	 * exit().
	 */
	signal_parent = 0;
	if (p->p_procdesc == NULL || procdesc_exit(p)) {
		/*
		 * Notify parent that we're gone.  If parent has the
		 * PS_NOCLDWAIT flag set, or if the handler is set to SIG_IGN,
		 * notify process 1 instead (and hope it will handle this
		 * situation).
		 */
		PROC_LOCK(p->p_pptr);
		mtx_lock(&p->p_pptr->p_sigacts->ps_mtx);
		if (p->p_pptr->p_sigacts->ps_flag &
		    (PS_NOCLDWAIT | PS_CLDSIGIGN)) {
			struct proc *pp;

			mtx_unlock(&p->p_pptr->p_sigacts->ps_mtx);
			pp = p->p_pptr;
			PROC_UNLOCK(pp);
			proc_reparent(p, p->p_reaper, true);
			p->p_sigparent = SIGCHLD;
			PROC_LOCK(p->p_pptr);

			/*
			 * Notify parent, so in case he was wait(2)ing or
			 * executing waitpid(2) with our pid, he will
			 * continue.
			 */
			wakeup(pp);
		} else
			mtx_unlock(&p->p_pptr->p_sigacts->ps_mtx);

		if (p->p_pptr == p->p_reaper || p->p_pptr == initproc) {
			signal_parent = 1;
		} else if (p->p_sigparent != 0) {
			if (p->p_sigparent == SIGCHLD) {
				signal_parent = 1;
			} else { /* LINUX thread */
				signal_parent = 2;
			}
		}
	} else
		PROC_LOCK(p->p_pptr);
	sx_xunlock(&proctree_lock);

	if (signal_parent == 1) {
		childproc_exited(p);
	} else if (signal_parent == 2) {
		kern_psignal(p->p_pptr, p->p_sigparent);
	}

	/* Tell the prison that we are gone. */
	prison_proc_free(p->p_ucred->cr_prison);

	/*
	 * The state PRS_ZOMBIE prevents other proesses from sending
	 * signal to the process, to avoid memory leak, we free memory
	 * for signal queue at the time when the state is set.
	 */
	sigqueue_flush(&p->p_sigqueue);
	sigqueue_flush(&td->td_sigqueue);

	/*
	 * We have to wait until after acquiring all locks before
	 * changing p_state.  We need to avoid all possible context
	 * switches (including ones from blocking on a mutex) while
	 * marked as a zombie.  We also have to set the zombie state
	 * before we release the parent process' proc lock to avoid
	 * a lost wakeup.  So, we first call wakeup, then we grab the
	 * sched lock, update the state, and release the parent process'
	 * proc lock.
	 */
	wakeup(p->p_pptr);
	cv_broadcast(&p->p_pwait);
	sched_exit(p->p_pptr, td);
	PROC_SLOCK(p);
	p->p_state = PRS_ZOMBIE;
	PROC_UNLOCK(p->p_pptr);

	/*
	 * Save our children's rusage information in our exit rusage.
	 */
	PROC_STATLOCK(p);
	ruadd(&p->p_ru, &p->p_rux, &p->p_stats->p_cru, &p->p_crux);
	PROC_STATUNLOCK(p);

	/*
	 * Make sure the scheduler takes this thread out of its tables etc.
	 * This will also release this thread's reference to the ucred.
	 * Other thread parts to release include pcb bits and such.
	 */
	thread_exit();
}


#ifndef _SYS_SYSPROTO_H_
struct abort2_args {
	char *why;
	int nargs;
	void **args;
};
#endif

int
sys_abort2(struct thread *td, struct abort2_args *uap)
{
	struct proc *p = td->td_proc;
	struct sbuf *sb;
	void *uargs[16];
	int error, i, sig;

	/*
	 * Do it right now so we can log either proper call of abort2(), or
	 * note, that invalid argument was passed. 512 is big enough to
	 * handle 16 arguments' descriptions with additional comments.
	 */
	sb = sbuf_new(NULL, NULL, 512, SBUF_FIXEDLEN);
	sbuf_clear(sb);
	sbuf_printf(sb, "%s(pid %d uid %d) aborted: ",
	    p->p_comm, p->p_pid, td->td_ucred->cr_uid);
	/*
	 * Since we can't return from abort2(), send SIGKILL in cases, where
	 * abort2() was called improperly
	 */
	sig = SIGKILL;
	/* Prevent from DoSes from user-space. */
	if (uap->nargs < 0 || uap->nargs > 16)
		goto out;
	if (uap->nargs > 0) {
		if (uap->args == NULL)
			goto out;
		error = copyin(uap->args, uargs, uap->nargs * sizeof(void *));
		if (error != 0)
			goto out;
	}
	/*
	 * Limit size of 'reason' string to 128. Will fit even when
	 * maximal number of arguments was chosen to be logged.
	 */
	if (uap->why != NULL) {
		error = sbuf_copyin(sb, uap->why, 128);
		if (error < 0)
			goto out;
	} else {
		sbuf_printf(sb, "(null)");
	}
	if (uap->nargs > 0) {
		sbuf_printf(sb, "(");
		for (i = 0;i < uap->nargs; i++)
			sbuf_printf(sb, "%s%p", i == 0 ? "" : ", ", uargs[i]);
		sbuf_printf(sb, ")");
	}
	/*
	 * Final stage: arguments were proper, string has been
	 * successfully copied from userspace, and copying pointers
	 * from user-space succeed.
	 */
	sig = SIGABRT;
out:
	if (sig == SIGKILL) {
		sbuf_trim(sb);
		sbuf_printf(sb, " (Reason text inaccessible)");
	}
	sbuf_cat(sb, "\n");
	sbuf_finish(sb);
	log(LOG_INFO, "%s", sbuf_data(sb));
	sbuf_delete(sb);
	exit1(td, 0, sig);
	return (0);
}


#ifdef COMPAT_43
/*
 * The dirty work is handled by kern_wait().
 */
int
owait(struct thread *td, struct owait_args *uap __unused)
{
	int error, status;

	error = kern_wait(td, WAIT_ANY, &status, 0, NULL);
	if (error == 0)
		td->td_retval[1] = status;
	return (error);
}
#endif /* COMPAT_43 */

/*
 * The dirty work is handled by kern_wait().
 */
int
sys_wait4(struct thread *td, struct wait4_args *uap)
{
	struct rusage ru, *rup;
	int error, status;

	if (uap->rusage != NULL)
		rup = &ru;
	else
		rup = NULL;
	error = kern_wait(td, uap->pid, &status, uap->options, rup);
	if (uap->status != NULL && error == 0 && td->td_retval[0] != 0)
		error = copyout(&status, uap->status, sizeof(status));
	if (uap->rusage != NULL && error == 0 && td->td_retval[0] != 0)
		error = copyout(&ru, uap->rusage, sizeof(struct rusage));
	return (error);
}

int
sys_wait6(struct thread *td, struct wait6_args *uap)
{
	struct __wrusage wru, *wrup;
	siginfo_t si, *sip;
	idtype_t idtype;
	id_t id;
	int error, status;

	idtype = uap->idtype;
	id = uap->id;

	if (uap->wrusage != NULL)
		wrup = &wru;
	else
		wrup = NULL;

	if (uap->info != NULL) {
		sip = &si;
		bzero(sip, sizeof(*sip));
	} else
		sip = NULL;

	/*
	 *  We expect all callers of wait6() to know about WEXITED and
	 *  WTRAPPED.
	 */
	error = kern_wait6(td, idtype, id, &status, uap->options, wrup, sip);

	if (uap->status != NULL && error == 0 && td->td_retval[0] != 0)
		error = copyout(&status, uap->status, sizeof(status));
	if (uap->wrusage != NULL && error == 0 && td->td_retval[0] != 0)
		error = copyout(&wru, uap->wrusage, sizeof(wru));
	if (uap->info != NULL && error == 0)
		error = copyout(&si, uap->info, sizeof(si));
	return (error);
}

/*
 * Reap the remains of a zombie process and optionally return status and
 * rusage.  Asserts and will release both the proctree_lock and the process
 * lock as part of its work.
 */
void
proc_reap(struct thread *td, struct proc *p, int *status, int options)
{
	struct proc *q, *t;

	sx_assert(&proctree_lock, SA_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_OWNED);
	KASSERT(p->p_state == PRS_ZOMBIE, ("proc_reap: !PRS_ZOMBIE"));

	mtx_spin_wait_unlocked(&p->p_slock);

	q = td->td_proc;

	if (status)
		*status = KW_EXITCODE(p->p_xexit, p->p_xsig);
	if (options & WNOWAIT) {
		/*
		 *  Only poll, returning the status.  Caller does not wish to
		 * release the proc struct just yet.
		 */
		PROC_UNLOCK(p);
		sx_xunlock(&proctree_lock);
		return;
	}

	PROC_LOCK(q);
	sigqueue_take(p->p_ksi);
	PROC_UNLOCK(q);

	/*
	 * If we got the child via a ptrace 'attach', we need to give it back
	 * to the old parent.
	 */
	if (p->p_oppid != p->p_pptr->p_pid) {
		PROC_UNLOCK(p);
		t = proc_realparent(p);
		PROC_LOCK(t);
		PROC_LOCK(p);
		CTR2(KTR_PTRACE,
		    "wait: traced child %d moved back to parent %d", p->p_pid,
		    t->p_pid);
		proc_reparent(p, t, false);
		PROC_UNLOCK(p);
		pksignal(t, SIGCHLD, p->p_ksi);
		wakeup(t);
		cv_broadcast(&p->p_pwait);
		PROC_UNLOCK(t);
		sx_xunlock(&proctree_lock);
		return;
	}
	PROC_UNLOCK(p);

	/*
	 * Remove other references to this process to ensure we have an
	 * exclusive reference.
	 */
	sx_xlock(&zombproc_lock);
	LIST_REMOVE(p, p_list);	/* off zombproc */
	sx_xunlock(&zombproc_lock);
	sx_xlock(PIDHASHLOCK(p->p_pid));
	LIST_REMOVE(p, p_hash);
	sx_xunlock(PIDHASHLOCK(p->p_pid));
	LIST_REMOVE(p, p_sibling);
	reaper_abandon_children(p, true);
	reaper_clear(p);
	proc_id_clear(PROC_ID_PID, p->p_pid);
	PROC_LOCK(p);
	clear_orphan(p);
	PROC_UNLOCK(p);
	leavepgrp(p);
	if (p->p_procdesc != NULL)
		procdesc_reap(p);
	sx_xunlock(&proctree_lock);

	PROC_LOCK(p);
	knlist_detach(p->p_klist);
	p->p_klist = NULL;
	PROC_UNLOCK(p);

	/*
	 * Removal from allproc list and process group list paired with
	 * PROC_LOCK which was executed during that time should guarantee
	 * nothing can reach this process anymore. As such further locking
	 * is unnecessary.
	 */
	p->p_xexit = p->p_xsig = 0;		/* XXX: why? */

	PROC_LOCK(q);
	ruadd(&q->p_stats->p_cru, &q->p_crux, &p->p_ru, &p->p_rux);
	PROC_UNLOCK(q);

	/*
	 * Decrement the count of procs running with this uid.
	 */
	(void)chgproccnt(p->p_ucred->cr_ruidinfo, -1, 0);

	/*
	 * Destroy resource accounting information associated with the process.
	 */
#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(p);
		racct_sub(p, RACCT_NPROC, 1);
		PROC_UNLOCK(p);
	}
#endif
	racct_proc_exit(p);

	/*
	 * Free credentials, arguments, and sigacts.
	 */
	crfree(p->p_ucred);
	proc_set_cred(p, NULL);
	pargs_drop(p->p_args);
	p->p_args = NULL;
	sigacts_free(p->p_sigacts);
	p->p_sigacts = NULL;

	/*
	 * Do any thread-system specific cleanups.
	 */
	thread_wait(p);

	/*
	 * Give vm and machine-dependent layer a chance to free anything that
	 * cpu_exit couldn't release while still running in process context.
	 */
	vm_waitproc(p);
#ifdef MAC
	mac_proc_destroy(p);
#endif

	KASSERT(FIRST_THREAD_IN_PROC(p),
	    ("proc_reap: no residual thread!"));
	uma_zfree(proc_zone, p);
	atomic_add_int(&nprocs, -1);
}

static int
proc_to_reap(struct thread *td, struct proc *p, idtype_t idtype, id_t id,
    int *status, int options, struct __wrusage *wrusage, siginfo_t *siginfo,
    int check_only)
{
	struct rusage *rup;

	sx_assert(&proctree_lock, SA_XLOCKED);

	PROC_LOCK(p);

	switch (idtype) {
	case P_ALL:
		if (p->p_procdesc != NULL) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_PID:
		if (p->p_pid != (pid_t)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_PGID:
		if (p->p_pgid != (pid_t)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_SID:
		if (p->p_session->s_sid != (pid_t)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_UID:
		if (p->p_ucred->cr_uid != (uid_t)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_GID:
		if (p->p_ucred->cr_gid != (gid_t)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	case P_JAILID:
		if (p->p_ucred->cr_prison->pr_id != (int)id) {
			PROC_UNLOCK(p);
			return (0);
		}
		break;
	/*
	 * It seems that the thread structures get zeroed out
	 * at process exit.  This makes it impossible to
	 * support P_SETID, P_CID or P_CPUID.
	 */
	default:
		PROC_UNLOCK(p);
		return (0);
	}

	if (p_canwait(td, p)) {
		PROC_UNLOCK(p);
		return (0);
	}

	if (((options & WEXITED) == 0) && (p->p_state == PRS_ZOMBIE)) {
		PROC_UNLOCK(p);
		return (0);
	}

	/*
	 * This special case handles a kthread spawned by linux_clone
	 * (see linux_misc.c).  The linux_wait4 and linux_waitpid
	 * functions need to be able to distinguish between waiting
	 * on a process and waiting on a thread.  It is a thread if
	 * p_sigparent is not SIGCHLD, and the WLINUXCLONE option
	 * signifies we want to wait for threads and not processes.
	 */
	if ((p->p_sigparent != SIGCHLD) ^
	    ((options & WLINUXCLONE) != 0)) {
		PROC_UNLOCK(p);
		return (0);
	}

	if (siginfo != NULL) {
		bzero(siginfo, sizeof(*siginfo));
		siginfo->si_errno = 0;

		/*
		 * SUSv4 requires that the si_signo value is always
		 * SIGCHLD. Obey it despite the rfork(2) interface
		 * allows to request other signal for child exit
		 * notification.
		 */
		siginfo->si_signo = SIGCHLD;

		/*
		 *  This is still a rough estimate.  We will fix the
		 *  cases TRAPPED, STOPPED, and CONTINUED later.
		 */
		if (WCOREDUMP(p->p_xsig)) {
			siginfo->si_code = CLD_DUMPED;
			siginfo->si_status = WTERMSIG(p->p_xsig);
		} else if (WIFSIGNALED(p->p_xsig)) {
			siginfo->si_code = CLD_KILLED;
			siginfo->si_status = WTERMSIG(p->p_xsig);
		} else {
			siginfo->si_code = CLD_EXITED;
			siginfo->si_status = p->p_xexit;
		}

		siginfo->si_pid = p->p_pid;
		siginfo->si_uid = p->p_ucred->cr_uid;

		/*
		 * The si_addr field would be useful additional
		 * detail, but apparently the PC value may be lost
		 * when we reach this point.  bzero() above sets
		 * siginfo->si_addr to NULL.
		 */
	}

	/*
	 * There should be no reason to limit resources usage info to
	 * exited processes only.  A snapshot about any resources used
	 * by a stopped process may be exactly what is needed.
	 */
	if (wrusage != NULL) {
		rup = &wrusage->wru_self;
		*rup = p->p_ru;
		PROC_STATLOCK(p);
		calcru(p, &rup->ru_utime, &rup->ru_stime);
		PROC_STATUNLOCK(p);

		rup = &wrusage->wru_children;
		*rup = p->p_stats->p_cru;
		calccru(p, &rup->ru_utime, &rup->ru_stime);
	}

	if (p->p_state == PRS_ZOMBIE && !check_only) {
		proc_reap(td, p, status, options);
		return (-1);
	}
	return (1);
}

int
kern_wait(struct thread *td, pid_t pid, int *status, int options,
    struct rusage *rusage)
{
	struct __wrusage wru, *wrup;
	idtype_t idtype;
	id_t id;
	int ret;

	/*
	 * Translate the special pid values into the (idtype, pid)
	 * pair for kern_wait6.  The WAIT_MYPGRP case is handled by
	 * kern_wait6() on its own.
	 */
	if (pid == WAIT_ANY) {
		idtype = P_ALL;
		id = 0;
	} else if (pid < 0) {
		idtype = P_PGID;
		id = (id_t)-pid;
	} else {
		idtype = P_PID;
		id = (id_t)pid;
	}

	if (rusage != NULL)
		wrup = &wru;
	else
		wrup = NULL;

	/*
	 * For backward compatibility we implicitly add flags WEXITED
	 * and WTRAPPED here.
	 */
	options |= WEXITED | WTRAPPED;
	ret = kern_wait6(td, idtype, id, status, options, wrup, NULL);
	if (rusage != NULL)
		*rusage = wru.wru_self;
	return (ret);
}

static void
report_alive_proc(struct thread *td, struct proc *p, siginfo_t *siginfo,
    int *status, int options, int si_code)
{
	bool cont;

	PROC_LOCK_ASSERT(p, MA_OWNED);
	sx_assert(&proctree_lock, SA_XLOCKED);
	MPASS(si_code == CLD_TRAPPED || si_code == CLD_STOPPED ||
	    si_code == CLD_CONTINUED);

	cont = si_code == CLD_CONTINUED;
	if ((options & WNOWAIT) == 0) {
		if (cont)
			p->p_flag &= ~P_CONTINUED;
		else
			p->p_flag |= P_WAITED;
		PROC_LOCK(td->td_proc);
		sigqueue_take(p->p_ksi);
		PROC_UNLOCK(td->td_proc);
	}
	sx_xunlock(&proctree_lock);
	if (siginfo != NULL) {
		siginfo->si_code = si_code;
		siginfo->si_status = cont ? SIGCONT : p->p_xsig;
	}
	if (status != NULL)
		*status = cont ? SIGCONT : W_STOPCODE(p->p_xsig);
	PROC_UNLOCK(p);
	td->td_retval[0] = p->p_pid;
}

int
kern_wait6(struct thread *td, idtype_t idtype, id_t id, int *status,
    int options, struct __wrusage *wrusage, siginfo_t *siginfo)
{
	struct proc *p, *q;
	pid_t pid;
	int error, nfound, ret;
	bool report;

	AUDIT_ARG_VALUE((int)idtype);	/* XXX - This is likely wrong! */
	AUDIT_ARG_PID((pid_t)id);	/* XXX - This may be wrong! */
	AUDIT_ARG_VALUE(options);

	q = td->td_proc;

	if ((pid_t)id == WAIT_MYPGRP && (idtype == P_PID || idtype == P_PGID)) {
		PROC_LOCK(q);
		id = (id_t)q->p_pgid;
		PROC_UNLOCK(q);
		idtype = P_PGID;
	}

	/* If we don't know the option, just return. */
	if ((options & ~(WUNTRACED | WNOHANG | WCONTINUED | WNOWAIT |
	    WEXITED | WTRAPPED | WLINUXCLONE)) != 0)
		return (EINVAL);
	if ((options & (WEXITED | WUNTRACED | WCONTINUED | WTRAPPED)) == 0) {
		/*
		 * We will be unable to find any matching processes,
		 * because there are no known events to look for.
		 * Prefer to return error instead of blocking
		 * indefinitely.
		 */
		return (EINVAL);
	}

loop:
	if (q->p_flag & P_STATCHILD) {
		PROC_LOCK(q);
		q->p_flag &= ~P_STATCHILD;
		PROC_UNLOCK(q);
	}
	sx_xlock(&proctree_lock);
loop_locked:
	nfound = 0;
	LIST_FOREACH(p, &q->p_children, p_sibling) {
		pid = p->p_pid;
		ret = proc_to_reap(td, p, idtype, id, status, options,
		    wrusage, siginfo, 0);
		if (ret == 0)
			continue;
		else if (ret != 1) {
			td->td_retval[0] = pid;
			return (0);
		}

		nfound++;
		PROC_LOCK_ASSERT(p, MA_OWNED);

		if ((options & WTRAPPED) != 0 &&
		    (p->p_flag & P_TRACED) != 0) {
			PROC_SLOCK(p);
			report =
			    ((p->p_flag & (P_STOPPED_TRACE | P_STOPPED_SIG)) &&
			    p->p_suspcount == p->p_numthreads &&
			    (p->p_flag & P_WAITED) == 0);
			PROC_SUNLOCK(p);
			if (report) {
			CTR4(KTR_PTRACE,
			    "wait: returning trapped pid %d status %#x "
			    "(xstat %d) xthread %d",
			    p->p_pid, W_STOPCODE(p->p_xsig), p->p_xsig,
			    p->p_xthread != NULL ?
			    p->p_xthread->td_tid : -1);
				report_alive_proc(td, p, siginfo, status,
				    options, CLD_TRAPPED);
				return (0);
			}
		}
		if ((options & WUNTRACED) != 0 &&
		    (p->p_flag & P_STOPPED_SIG) != 0) {
			PROC_SLOCK(p);
			report = (p->p_suspcount == p->p_numthreads &&
			    ((p->p_flag & P_WAITED) == 0));
			PROC_SUNLOCK(p);
			if (report) {
				report_alive_proc(td, p, siginfo, status,
				    options, CLD_STOPPED);
				return (0);
			}
		}
		if ((options & WCONTINUED) != 0 &&
		    (p->p_flag & P_CONTINUED) != 0) {
			report_alive_proc(td, p, siginfo, status, options,
			    CLD_CONTINUED);
			return (0);
		}
		PROC_UNLOCK(p);
	}

	/*
	 * Look in the orphans list too, to allow the parent to
	 * collect it's child exit status even if child is being
	 * debugged.
	 *
	 * Debugger detaches from the parent upon successful
	 * switch-over from parent to child.  At this point due to
	 * re-parenting the parent loses the child to debugger and a
	 * wait4(2) call would report that it has no children to wait
	 * for.  By maintaining a list of orphans we allow the parent
	 * to successfully wait until the child becomes a zombie.
	 */
	if (nfound == 0) {
		LIST_FOREACH(p, &q->p_orphans, p_orphan) {
			ret = proc_to_reap(td, p, idtype, id, NULL, options,
			    NULL, NULL, 1);
			if (ret != 0) {
				KASSERT(ret != -1, ("reaped an orphan (pid %d)",
				    (int)td->td_retval[0]));
				PROC_UNLOCK(p);
				nfound++;
				break;
			}
		}
	}
	if (nfound == 0) {
		sx_xunlock(&proctree_lock);
		return (ECHILD);
	}
	if (options & WNOHANG) {
		sx_xunlock(&proctree_lock);
		td->td_retval[0] = 0;
		return (0);
	}
	PROC_LOCK(q);
	if (q->p_flag & P_STATCHILD) {
		q->p_flag &= ~P_STATCHILD;
		PROC_UNLOCK(q);
		goto loop_locked;
	}
	sx_xunlock(&proctree_lock);
	error = msleep(q, &q->p_mtx, PWAIT | PCATCH | PDROP, "wait", 0);
	if (error)
		return (error);
	goto loop;
}

/*
 * Make process 'parent' the new parent of process 'child'.
 * Must be called with an exclusive hold of proctree lock.
 */
void
proc_reparent(struct proc *child, struct proc *parent, bool set_oppid)
{

	sx_assert(&proctree_lock, SX_XLOCKED);
	PROC_LOCK_ASSERT(child, MA_OWNED);
	if (child->p_pptr == parent)
		return;

	PROC_LOCK(child->p_pptr);
	sigqueue_take(child->p_ksi);
	PROC_UNLOCK(child->p_pptr);
	LIST_REMOVE(child, p_sibling);
	LIST_INSERT_HEAD(&parent->p_children, child, p_sibling);

	clear_orphan(child);
	if (child->p_flag & P_TRACED) {
		if (LIST_EMPTY(&child->p_pptr->p_orphans)) {
			child->p_treeflag |= P_TREE_FIRST_ORPHAN;
			LIST_INSERT_HEAD(&child->p_pptr->p_orphans, child,
			    p_orphan);
		} else {
			LIST_INSERT_AFTER(LIST_FIRST(&child->p_pptr->p_orphans),
			    child, p_orphan);
		}
		child->p_treeflag |= P_TREE_ORPHANED;
	}

	child->p_pptr = parent;
	if (set_oppid)
		child->p_oppid = parent->p_pid;
}

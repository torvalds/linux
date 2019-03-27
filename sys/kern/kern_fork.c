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
 *	@(#)kern_fork.c	8.6 (Berkeley) 4/8/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ktrace.h"
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitstring.h>
#include <sys/sysproto.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/procdesc.h>
#include <sys/pioctl.h>
#include <sys/ptrace.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/acct.h>
#include <sys/ktr.h>
#include <sys/ktrace.h>
#include <sys/unistd.h>
#include <sys/sdt.h>
#include <sys/sx.h>
#include <sys/sysent.h>
#include <sys/signalvar.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
dtrace_fork_func_t	dtrace_fasttrap_fork;
#endif

SDT_PROVIDER_DECLARE(proc);
SDT_PROBE_DEFINE3(proc, , , create, "struct proc *", "struct proc *", "int");

#ifndef _SYS_SYSPROTO_H_
struct fork_args {
	int     dummy;
};
#endif

EVENTHANDLER_LIST_DECLARE(process_fork);

/* ARGSUSED */
int
sys_fork(struct thread *td, struct fork_args *uap)
{
	struct fork_req fr;
	int error, pid;

	bzero(&fr, sizeof(fr));
	fr.fr_flags = RFFDG | RFPROC;
	fr.fr_pidp = &pid;
	error = fork1(td, &fr);
	if (error == 0) {
		td->td_retval[0] = pid;
		td->td_retval[1] = 0;
	}
	return (error);
}

/* ARGUSED */
int
sys_pdfork(struct thread *td, struct pdfork_args *uap)
{
	struct fork_req fr;
	int error, fd, pid;

	bzero(&fr, sizeof(fr));
	fr.fr_flags = RFFDG | RFPROC | RFPROCDESC;
	fr.fr_pidp = &pid;
	fr.fr_pd_fd = &fd;
	fr.fr_pd_flags = uap->flags;
	/*
	 * It is necessary to return fd by reference because 0 is a valid file
	 * descriptor number, and the child needs to be able to distinguish
	 * itself from the parent using the return value.
	 */
	error = fork1(td, &fr);
	if (error == 0) {
		td->td_retval[0] = pid;
		td->td_retval[1] = 0;
		error = copyout(&fd, uap->fdp, sizeof(fd));
	}
	return (error);
}

/* ARGSUSED */
int
sys_vfork(struct thread *td, struct vfork_args *uap)
{
	struct fork_req fr;
	int error, pid;

	bzero(&fr, sizeof(fr));
	fr.fr_flags = RFFDG | RFPROC | RFPPWAIT | RFMEM;
	fr.fr_pidp = &pid;
	error = fork1(td, &fr);
	if (error == 0) {
		td->td_retval[0] = pid;
		td->td_retval[1] = 0;
	}
	return (error);
}

int
sys_rfork(struct thread *td, struct rfork_args *uap)
{
	struct fork_req fr;
	int error, pid;

	/* Don't allow kernel-only flags. */
	if ((uap->flags & RFKERNELONLY) != 0)
		return (EINVAL);

	AUDIT_ARG_FFLAGS(uap->flags);
	bzero(&fr, sizeof(fr));
	fr.fr_flags = uap->flags;
	fr.fr_pidp = &pid;
	error = fork1(td, &fr);
	if (error == 0) {
		td->td_retval[0] = pid;
		td->td_retval[1] = 0;
	}
	return (error);
}

int	nprocs = 1;		/* process 0 */
int	lastpid = 0;
SYSCTL_INT(_kern, OID_AUTO, lastpid, CTLFLAG_RD, &lastpid, 0,
    "Last used PID");

/*
 * Random component to lastpid generation.  We mix in a random factor to make
 * it a little harder to predict.  We sanity check the modulus value to avoid
 * doing it in critical paths.  Don't let it be too small or we pointlessly
 * waste randomness entropy, and don't let it be impossibly large.  Using a
 * modulus that is too big causes a LOT more process table scans and slows
 * down fork processing as the pidchecked caching is defeated.
 */
static int randompid = 0;

static int
sysctl_kern_randompid(SYSCTL_HANDLER_ARGS)
{
	int error, pid;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error != 0)
		return(error);
	sx_xlock(&allproc_lock);
	pid = randompid;
	error = sysctl_handle_int(oidp, &pid, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (pid == 0)
			randompid = 0;
		else if (pid == 1)
			/* generate a random PID modulus between 100 and 1123 */
			randompid = 100 + arc4random() % 1024;
		else if (pid < 0 || pid > pid_max - 100)
			/* out of range */
			randompid = pid_max - 100;
		else if (pid < 100)
			/* Make it reasonable */
			randompid = 100;
		else
			randompid = pid;
	}
	sx_xunlock(&allproc_lock);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, randompid, CTLTYPE_INT|CTLFLAG_RW,
    0, 0, sysctl_kern_randompid, "I", "Random PID modulus. Special values: 0: disable, 1: choose random value");

extern bitstr_t proc_id_pidmap;
extern bitstr_t proc_id_grpidmap;
extern bitstr_t proc_id_sessidmap;
extern bitstr_t proc_id_reapmap;

/*
 * Find an unused process ID
 *
 * If RFHIGHPID is set (used during system boot), do not allocate
 * low-numbered pids.
 */
static int
fork_findpid(int flags)
{
	pid_t result;
	int trypid;

	trypid = lastpid + 1;
	if (flags & RFHIGHPID) {
		if (trypid < 10)
			trypid = 10;
	} else {
		if (randompid)
			trypid += arc4random() % randompid;
	}
	mtx_lock(&procid_lock);
retry:
	/*
	 * If the process ID prototype has wrapped around,
	 * restart somewhat above 0, as the low-numbered procs
	 * tend to include daemons that don't exit.
	 */
	if (trypid >= pid_max) {
		trypid = trypid % pid_max;
		if (trypid < 100)
			trypid += 100;
	}

	bit_ffc_at(&proc_id_pidmap, trypid, pid_max, &result);
	if (result == -1) {
		trypid = 100;
		goto retry;
	}
	if (bit_test(&proc_id_grpidmap, result) ||
	    bit_test(&proc_id_sessidmap, result) ||
	    bit_test(&proc_id_reapmap, result)) {
		trypid = result + 1;
		goto retry;
	}

	/*
	 * RFHIGHPID does not mess with the lastpid counter during boot.
	 */
	if ((flags & RFHIGHPID) == 0)
		lastpid = result;

	bit_set(&proc_id_pidmap, result);
	mtx_unlock(&procid_lock);

	return (result);
}

static int
fork_norfproc(struct thread *td, int flags)
{
	int error;
	struct proc *p1;

	KASSERT((flags & RFPROC) == 0,
	    ("fork_norfproc called with RFPROC set"));
	p1 = td->td_proc;

	if (((p1->p_flag & (P_HADTHREADS|P_SYSTEM)) == P_HADTHREADS) &&
	    (flags & (RFCFDG | RFFDG))) {
		PROC_LOCK(p1);
		if (thread_single(p1, SINGLE_BOUNDARY)) {
			PROC_UNLOCK(p1);
			return (ERESTART);
		}
		PROC_UNLOCK(p1);
	}

	error = vm_forkproc(td, NULL, NULL, NULL, flags);
	if (error)
		goto fail;

	/*
	 * Close all file descriptors.
	 */
	if (flags & RFCFDG) {
		struct filedesc *fdtmp;
		fdtmp = fdinit(td->td_proc->p_fd, false);
		fdescfree(td);
		p1->p_fd = fdtmp;
	}

	/*
	 * Unshare file descriptors (from parent).
	 */
	if (flags & RFFDG)
		fdunshare(td);

fail:
	if (((p1->p_flag & (P_HADTHREADS|P_SYSTEM)) == P_HADTHREADS) &&
	    (flags & (RFCFDG | RFFDG))) {
		PROC_LOCK(p1);
		thread_single_end(p1, SINGLE_BOUNDARY);
		PROC_UNLOCK(p1);
	}
	return (error);
}

static void
do_fork(struct thread *td, struct fork_req *fr, struct proc *p2, struct thread *td2,
    struct vmspace *vm2, struct file *fp_procdesc)
{
	struct proc *p1, *pptr;
	int trypid;
	struct filedesc *fd;
	struct filedesc_to_leader *fdtol;
	struct sigacts *newsigacts;

	sx_assert(&allproc_lock, SX_XLOCKED);

	p1 = td->td_proc;

	trypid = fork_findpid(fr->fr_flags);
	p2->p_state = PRS_NEW;		/* protect against others */
	p2->p_pid = trypid;
	AUDIT_ARG_PID(p2->p_pid);
	LIST_INSERT_HEAD(&allproc, p2, p_list);
	allproc_gen++;
	sx_xlock(PIDHASHLOCK(p2->p_pid));
	LIST_INSERT_HEAD(PIDHASH(p2->p_pid), p2, p_hash);
	sx_xunlock(PIDHASHLOCK(p2->p_pid));
	PROC_LOCK(p2);
	PROC_LOCK(p1);

	sx_xunlock(&allproc_lock);

	bcopy(&p1->p_startcopy, &p2->p_startcopy,
	    __rangeof(struct proc, p_startcopy, p_endcopy));
	pargs_hold(p2->p_args);

	PROC_UNLOCK(p1);

	bzero(&p2->p_startzero,
	    __rangeof(struct proc, p_startzero, p_endzero));

	/* Tell the prison that we exist. */
	prison_proc_hold(p2->p_ucred->cr_prison);

	PROC_UNLOCK(p2);

	tidhash_add(td2);

	/*
	 * Malloc things while we don't hold any locks.
	 */
	if (fr->fr_flags & RFSIGSHARE)
		newsigacts = NULL;
	else
		newsigacts = sigacts_alloc();

	/*
	 * Copy filedesc.
	 */
	if (fr->fr_flags & RFCFDG) {
		fd = fdinit(p1->p_fd, false);
		fdtol = NULL;
	} else if (fr->fr_flags & RFFDG) {
		fd = fdcopy(p1->p_fd);
		fdtol = NULL;
	} else {
		fd = fdshare(p1->p_fd);
		if (p1->p_fdtol == NULL)
			p1->p_fdtol = filedesc_to_leader_alloc(NULL, NULL,
			    p1->p_leader);
		if ((fr->fr_flags & RFTHREAD) != 0) {
			/*
			 * Shared file descriptor table, and shared
			 * process leaders.
			 */
			fdtol = p1->p_fdtol;
			FILEDESC_XLOCK(p1->p_fd);
			fdtol->fdl_refcount++;
			FILEDESC_XUNLOCK(p1->p_fd);
		} else {
			/*
			 * Shared file descriptor table, and different
			 * process leaders.
			 */
			fdtol = filedesc_to_leader_alloc(p1->p_fdtol,
			    p1->p_fd, p2);
		}
	}
	/*
	 * Make a proc table entry for the new process.
	 * Start by zeroing the section of proc that is zero-initialized,
	 * then copy the section that is copied directly from the parent.
	 */

	PROC_LOCK(p2);
	PROC_LOCK(p1);

	bzero(&td2->td_startzero,
	    __rangeof(struct thread, td_startzero, td_endzero));

	bcopy(&td->td_startcopy, &td2->td_startcopy,
	    __rangeof(struct thread, td_startcopy, td_endcopy));

	bcopy(&p2->p_comm, &td2->td_name, sizeof(td2->td_name));
	td2->td_sigstk = td->td_sigstk;
	td2->td_flags = TDF_INMEM;
	td2->td_lend_user_pri = PRI_MAX;

#ifdef VIMAGE
	td2->td_vnet = NULL;
	td2->td_vnet_lpush = NULL;
#endif

	/*
	 * Allow the scheduler to initialize the child.
	 */
	thread_lock(td);
	sched_fork(td, td2);
	thread_unlock(td);

	/*
	 * Duplicate sub-structures as needed.
	 * Increase reference counts on shared objects.
	 */
	p2->p_flag = P_INMEM;
	p2->p_flag2 = p1->p_flag2 & (P2_ASLR_DISABLE | P2_ASLR_ENABLE |
	    P2_ASLR_IGNSTART | P2_NOTRACE | P2_NOTRACE_EXEC | P2_TRAPCAP);
	p2->p_swtick = ticks;
	if (p1->p_flag & P_PROFIL)
		startprofclock(p2);

	if (fr->fr_flags & RFSIGSHARE) {
		p2->p_sigacts = sigacts_hold(p1->p_sigacts);
	} else {
		sigacts_copy(newsigacts, p1->p_sigacts);
		p2->p_sigacts = newsigacts;
	}

	if (fr->fr_flags & RFTSIGZMB)
	        p2->p_sigparent = RFTSIGNUM(fr->fr_flags);
	else if (fr->fr_flags & RFLINUXTHPN)
	        p2->p_sigparent = SIGUSR1;
	else
	        p2->p_sigparent = SIGCHLD;

	p2->p_textvp = p1->p_textvp;
	p2->p_fd = fd;
	p2->p_fdtol = fdtol;

	if (p1->p_flag2 & P2_INHERIT_PROTECTED) {
		p2->p_flag |= P_PROTECTED;
		p2->p_flag2 |= P2_INHERIT_PROTECTED;
	}

	/*
	 * p_limit is copy-on-write.  Bump its refcount.
	 */
	lim_fork(p1, p2);

	thread_cow_get_proc(td2, p2);

	pstats_fork(p1->p_stats, p2->p_stats);

	PROC_UNLOCK(p1);
	PROC_UNLOCK(p2);

	/* Bump references to the text vnode (for procfs). */
	if (p2->p_textvp)
		vrefact(p2->p_textvp);

	/*
	 * Set up linkage for kernel based threading.
	 */
	if ((fr->fr_flags & RFTHREAD) != 0) {
		mtx_lock(&ppeers_lock);
		p2->p_peers = p1->p_peers;
		p1->p_peers = p2;
		p2->p_leader = p1->p_leader;
		mtx_unlock(&ppeers_lock);
		PROC_LOCK(p1->p_leader);
		if ((p1->p_leader->p_flag & P_WEXIT) != 0) {
			PROC_UNLOCK(p1->p_leader);
			/*
			 * The task leader is exiting, so process p1 is
			 * going to be killed shortly.  Since p1 obviously
			 * isn't dead yet, we know that the leader is either
			 * sending SIGKILL's to all the processes in this
			 * task or is sleeping waiting for all the peers to
			 * exit.  We let p1 complete the fork, but we need
			 * to go ahead and kill the new process p2 since
			 * the task leader may not get a chance to send
			 * SIGKILL to it.  We leave it on the list so that
			 * the task leader will wait for this new process
			 * to commit suicide.
			 */
			PROC_LOCK(p2);
			kern_psignal(p2, SIGKILL);
			PROC_UNLOCK(p2);
		} else
			PROC_UNLOCK(p1->p_leader);
	} else {
		p2->p_peers = NULL;
		p2->p_leader = p2;
	}

	sx_xlock(&proctree_lock);
	PGRP_LOCK(p1->p_pgrp);
	PROC_LOCK(p2);
	PROC_LOCK(p1);

	/*
	 * Preserve some more flags in subprocess.  P_PROFIL has already
	 * been preserved.
	 */
	p2->p_flag |= p1->p_flag & P_SUGID;
	td2->td_pflags |= (td->td_pflags & TDP_ALTSTACK) | TDP_FORKING;
	SESS_LOCK(p1->p_session);
	if (p1->p_session->s_ttyvp != NULL && p1->p_flag & P_CONTROLT)
		p2->p_flag |= P_CONTROLT;
	SESS_UNLOCK(p1->p_session);
	if (fr->fr_flags & RFPPWAIT)
		p2->p_flag |= P_PPWAIT;

	p2->p_pgrp = p1->p_pgrp;
	LIST_INSERT_AFTER(p1, p2, p_pglist);
	PGRP_UNLOCK(p1->p_pgrp);
	LIST_INIT(&p2->p_children);
	LIST_INIT(&p2->p_orphans);

	callout_init_mtx(&p2->p_itcallout, &p2->p_mtx, 0);

	/*
	 * If PF_FORK is set, the child process inherits the
	 * procfs ioctl flags from its parent.
	 */
	if (p1->p_pfsflags & PF_FORK) {
		p2->p_stops = p1->p_stops;
		p2->p_pfsflags = p1->p_pfsflags;
	}

	/*
	 * This begins the section where we must prevent the parent
	 * from being swapped.
	 */
	_PHOLD(p1);
	PROC_UNLOCK(p1);

	/*
	 * Attach the new process to its parent.
	 *
	 * If RFNOWAIT is set, the newly created process becomes a child
	 * of init.  This effectively disassociates the child from the
	 * parent.
	 */
	if ((fr->fr_flags & RFNOWAIT) != 0) {
		pptr = p1->p_reaper;
		p2->p_reaper = pptr;
	} else {
		p2->p_reaper = (p1->p_treeflag & P_TREE_REAPER) != 0 ?
		    p1 : p1->p_reaper;
		pptr = p1;
	}
	p2->p_pptr = pptr;
	p2->p_oppid = pptr->p_pid;
	LIST_INSERT_HEAD(&pptr->p_children, p2, p_sibling);
	LIST_INIT(&p2->p_reaplist);
	LIST_INSERT_HEAD(&p2->p_reaper->p_reaplist, p2, p_reapsibling);
	if (p2->p_reaper == p1 && p1 != initproc) {
		p2->p_reapsubtree = p2->p_pid;
		proc_id_set_cond(PROC_ID_REAP, p2->p_pid);
	}
	sx_xunlock(&proctree_lock);

	/* Inform accounting that we have forked. */
	p2->p_acflag = AFORK;
	PROC_UNLOCK(p2);

#ifdef KTRACE
	ktrprocfork(p1, p2);
#endif

	/*
	 * Finish creating the child process.  It will return via a different
	 * execution path later.  (ie: directly into user mode)
	 */
	vm_forkproc(td, p2, td2, vm2, fr->fr_flags);

	if (fr->fr_flags == (RFFDG | RFPROC)) {
		VM_CNT_INC(v_forks);
		VM_CNT_ADD(v_forkpages, p2->p_vmspace->vm_dsize +
		    p2->p_vmspace->vm_ssize);
	} else if (fr->fr_flags == (RFFDG | RFPROC | RFPPWAIT | RFMEM)) {
		VM_CNT_INC(v_vforks);
		VM_CNT_ADD(v_vforkpages, p2->p_vmspace->vm_dsize +
		    p2->p_vmspace->vm_ssize);
	} else if (p1 == &proc0) {
		VM_CNT_INC(v_kthreads);
		VM_CNT_ADD(v_kthreadpages, p2->p_vmspace->vm_dsize +
		    p2->p_vmspace->vm_ssize);
	} else {
		VM_CNT_INC(v_rforks);
		VM_CNT_ADD(v_rforkpages, p2->p_vmspace->vm_dsize +
		    p2->p_vmspace->vm_ssize);
	}

	/*
	 * Associate the process descriptor with the process before anything
	 * can happen that might cause that process to need the descriptor.
	 * However, don't do this until after fork(2) can no longer fail.
	 */
	if (fr->fr_flags & RFPROCDESC)
		procdesc_new(p2, fr->fr_pd_flags);

	/*
	 * Both processes are set up, now check if any loadable modules want
	 * to adjust anything.
	 */
	EVENTHANDLER_DIRECT_INVOKE(process_fork, p1, p2, fr->fr_flags);

	/*
	 * Set the child start time and mark the process as being complete.
	 */
	PROC_LOCK(p2);
	PROC_LOCK(p1);
	microuptime(&p2->p_stats->p_start);
	PROC_SLOCK(p2);
	p2->p_state = PRS_NORMAL;
	PROC_SUNLOCK(p2);

#ifdef KDTRACE_HOOKS
	/*
	 * Tell the DTrace fasttrap provider about the new process so that any
	 * tracepoints inherited from the parent can be removed. We have to do
	 * this only after p_state is PRS_NORMAL since the fasttrap module will
	 * use pfind() later on.
	 */
	if ((fr->fr_flags & RFMEM) == 0 && dtrace_fasttrap_fork)
		dtrace_fasttrap_fork(p1, p2);
#endif
	if (fr->fr_flags & RFPPWAIT) {
		td->td_pflags |= TDP_RFPPWAIT;
		td->td_rfppwait_p = p2;
		td->td_dbgflags |= TDB_VFORK;
	}
	PROC_UNLOCK(p2);

	/*
	 * Tell any interested parties about the new process.
	 */
	knote_fork(p1->p_klist, p2->p_pid);

	/*
	 * Now can be swapped.
	 */
	_PRELE(p1);
	PROC_UNLOCK(p1);
	SDT_PROBE3(proc, , , create, p2, p1, fr->fr_flags);

	if (fr->fr_flags & RFPROCDESC) {
		procdesc_finit(p2->p_procdesc, fp_procdesc);
		fdrop(fp_procdesc, td);
	}
	
	/*
	 * Speculative check for PTRACE_FORK. PTRACE_FORK is not
	 * synced with forks in progress so it is OK if we miss it
	 * if being set atm.
	 */
	if ((p1->p_ptevents & PTRACE_FORK) != 0) {
		sx_xlock(&proctree_lock);
		PROC_LOCK(p2);
		
		/*
		 * p1->p_ptevents & p1->p_pptr are protected by both
		 * process and proctree locks for modifications,
		 * so owning proctree_lock allows the race-free read.
		 */
		if ((p1->p_ptevents & PTRACE_FORK) != 0) {
			/*
			 * Arrange for debugger to receive the fork event.
			 *
			 * We can report PL_FLAG_FORKED regardless of
			 * P_FOLLOWFORK settings, but it does not make a sense
			 * for runaway child.
			 */
			td->td_dbgflags |= TDB_FORK;
			td->td_dbg_forked = p2->p_pid;
			td2->td_dbgflags |= TDB_STOPATFORK;
			proc_set_traced(p2, true);
			CTR2(KTR_PTRACE,
			    "do_fork: attaching to new child pid %d: oppid %d",
			    p2->p_pid, p2->p_oppid);
			proc_reparent(p2, p1->p_pptr, false);
		}
		PROC_UNLOCK(p2);
		sx_xunlock(&proctree_lock);
	}

	racct_proc_fork_done(p2);

	if ((fr->fr_flags & RFSTOPPED) == 0) {
		if (fr->fr_pidp != NULL)
			*fr->fr_pidp = p2->p_pid;
		/*
		 * If RFSTOPPED not requested, make child runnable and
		 * add to run queue.
		 */
		thread_lock(td2);
		TD_SET_CAN_RUN(td2);
		sched_add(td2, SRQ_BORING);
		thread_unlock(td2);
	} else {
		*fr->fr_procp = p2;
	}
}

void
fork_rfppwait(struct thread *td)
{
	struct proc *p, *p2;

	MPASS(td->td_pflags & TDP_RFPPWAIT);

	p = td->td_proc;
	/*
	 * Preserve synchronization semantics of vfork.  If
	 * waiting for child to exec or exit, fork set
	 * P_PPWAIT on child, and there we sleep on our proc
	 * (in case of exit).
	 *
	 * Do it after the ptracestop() above is finished, to
	 * not block our debugger until child execs or exits
	 * to finish vfork wait.
	 */
	td->td_pflags &= ~TDP_RFPPWAIT;
	p2 = td->td_rfppwait_p;
again:
	PROC_LOCK(p2);
	while (p2->p_flag & P_PPWAIT) {
		PROC_LOCK(p);
		if (thread_suspend_check_needed()) {
			PROC_UNLOCK(p2);
			thread_suspend_check(0);
			PROC_UNLOCK(p);
			goto again;
		} else {
			PROC_UNLOCK(p);
		}
		cv_timedwait(&p2->p_pwait, &p2->p_mtx, hz);
	}
	PROC_UNLOCK(p2);

	if (td->td_dbgflags & TDB_VFORK) {
		PROC_LOCK(p);
		if (p->p_ptevents & PTRACE_VFORK)
			ptracestop(td, SIGTRAP, NULL);
		td->td_dbgflags &= ~TDB_VFORK;
		PROC_UNLOCK(p);
	}
}

int
fork1(struct thread *td, struct fork_req *fr)
{
	struct proc *p1, *newproc;
	struct thread *td2;
	struct vmspace *vm2;
	struct file *fp_procdesc;
	vm_ooffset_t mem_charged;
	int error, nprocs_new, ok;
	static int curfail;
	static struct timeval lastfail;
	int flags, pages;

	flags = fr->fr_flags;
	pages = fr->fr_pages;

	if ((flags & RFSTOPPED) != 0)
		MPASS(fr->fr_procp != NULL && fr->fr_pidp == NULL);
	else
		MPASS(fr->fr_procp == NULL);

	/* Check for the undefined or unimplemented flags. */
	if ((flags & ~(RFFLAGS | RFTSIGFLAGS(RFTSIGMASK))) != 0)
		return (EINVAL);

	/* Signal value requires RFTSIGZMB. */
	if ((flags & RFTSIGFLAGS(RFTSIGMASK)) != 0 && (flags & RFTSIGZMB) == 0)
		return (EINVAL);

	/* Can't copy and clear. */
	if ((flags & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		return (EINVAL);

	/* Check the validity of the signal number. */
	if ((flags & RFTSIGZMB) != 0 && (u_int)RFTSIGNUM(flags) > _SIG_MAXSIG)
		return (EINVAL);

	if ((flags & RFPROCDESC) != 0) {
		/* Can't not create a process yet get a process descriptor. */
		if ((flags & RFPROC) == 0)
			return (EINVAL);

		/* Must provide a place to put a procdesc if creating one. */
		if (fr->fr_pd_fd == NULL)
			return (EINVAL);

		/* Check if we are using supported flags. */
		if ((fr->fr_pd_flags & ~PD_ALLOWED_AT_FORK) != 0)
			return (EINVAL);
	}

	p1 = td->td_proc;

	/*
	 * Here we don't create a new process, but we divorce
	 * certain parts of a process from itself.
	 */
	if ((flags & RFPROC) == 0) {
		if (fr->fr_procp != NULL)
			*fr->fr_procp = NULL;
		else if (fr->fr_pidp != NULL)
			*fr->fr_pidp = 0;
		return (fork_norfproc(td, flags));
	}

	fp_procdesc = NULL;
	newproc = NULL;
	vm2 = NULL;

	/*
	 * Increment the nprocs resource before allocations occur.
	 * Although process entries are dynamically created, we still
	 * keep a global limit on the maximum number we will
	 * create. There are hard-limits as to the number of processes
	 * that can run, established by the KVA and memory usage for
	 * the process data.
	 *
	 * Don't allow a nonprivileged user to use the last ten
	 * processes; don't let root exceed the limit.
	 */
	nprocs_new = atomic_fetchadd_int(&nprocs, 1) + 1;
	if ((nprocs_new >= maxproc - 10 &&
	    priv_check_cred(td->td_ucred, PRIV_MAXPROC) != 0) ||
	    nprocs_new >= maxproc) {
		error = EAGAIN;
		sx_xlock(&allproc_lock);
		if (ppsratecheck(&lastfail, &curfail, 1)) {
			printf("maxproc limit exceeded by uid %u (pid %d); "
			    "see tuning(7) and login.conf(5)\n",
			    td->td_ucred->cr_ruid, p1->p_pid);
		}
		sx_xunlock(&allproc_lock);
		goto fail2;
	}

	/*
	 * If required, create a process descriptor in the parent first; we
	 * will abandon it if something goes wrong. We don't finit() until
	 * later.
	 */
	if (flags & RFPROCDESC) {
		error = procdesc_falloc(td, &fp_procdesc, fr->fr_pd_fd,
		    fr->fr_pd_flags, fr->fr_pd_fcaps);
		if (error != 0)
			goto fail2;
	}

	mem_charged = 0;
	if (pages == 0)
		pages = kstack_pages;
	/* Allocate new proc. */
	newproc = uma_zalloc(proc_zone, M_WAITOK);
	td2 = FIRST_THREAD_IN_PROC(newproc);
	if (td2 == NULL) {
		td2 = thread_alloc(pages);
		if (td2 == NULL) {
			error = ENOMEM;
			goto fail2;
		}
		proc_linkup(newproc, td2);
	} else {
		if (td2->td_kstack == 0 || td2->td_kstack_pages != pages) {
			if (td2->td_kstack != 0)
				vm_thread_dispose(td2);
			if (!thread_alloc_stack(td2, pages)) {
				error = ENOMEM;
				goto fail2;
			}
		}
	}

	if ((flags & RFMEM) == 0) {
		vm2 = vmspace_fork(p1->p_vmspace, &mem_charged);
		if (vm2 == NULL) {
			error = ENOMEM;
			goto fail2;
		}
		if (!swap_reserve(mem_charged)) {
			/*
			 * The swap reservation failed. The accounting
			 * from the entries of the copied vm2 will be
			 * subtracted in vmspace_free(), so force the
			 * reservation there.
			 */
			swap_reserve_force(mem_charged);
			error = ENOMEM;
			goto fail2;
		}
	} else
		vm2 = NULL;

	/*
	 * XXX: This is ugly; when we copy resource usage, we need to bump
	 *      per-cred resource counters.
	 */
	proc_set_cred_init(newproc, crhold(td->td_ucred));

	/*
	 * Initialize resource accounting for the child process.
	 */
	error = racct_proc_fork(p1, newproc);
	if (error != 0) {
		error = EAGAIN;
		goto fail1;
	}

#ifdef MAC
	mac_proc_init(newproc);
#endif
	newproc->p_klist = knlist_alloc(&newproc->p_mtx);
	STAILQ_INIT(&newproc->p_ktr);

	sx_xlock(&allproc_lock);

	/*
	 * Increment the count of procs running with this uid. Don't allow
	 * a nonprivileged user to exceed their current limit.
	 *
	 * XXXRW: Can we avoid privilege here if it's not needed?
	 */
	error = priv_check_cred(td->td_ucred, PRIV_PROC_LIMIT);
	if (error == 0)
		ok = chgproccnt(td->td_ucred->cr_ruidinfo, 1, 0);
	else {
		ok = chgproccnt(td->td_ucred->cr_ruidinfo, 1,
		    lim_cur(td, RLIMIT_NPROC));
	}
	if (ok) {
		do_fork(td, fr, newproc, td2, vm2, fp_procdesc);
		return (0);
	}

	error = EAGAIN;
	sx_xunlock(&allproc_lock);
#ifdef MAC
	mac_proc_destroy(newproc);
#endif
	racct_proc_exit(newproc);
fail1:
	crfree(newproc->p_ucred);
	newproc->p_ucred = NULL;
fail2:
	if (vm2 != NULL)
		vmspace_free(vm2);
	uma_zfree(proc_zone, newproc);
	if ((flags & RFPROCDESC) != 0 && fp_procdesc != NULL) {
		fdclose(td, fp_procdesc, *fr->fr_pd_fd);
		fdrop(fp_procdesc, td);
	}
	atomic_add_int(&nprocs, -1);
	pause("fork", hz / 2);
	return (error);
}

/*
 * Handle the return of a child process from fork1().  This function
 * is called from the MD fork_trampoline() entry point.
 */
void
fork_exit(void (*callout)(void *, struct trapframe *), void *arg,
    struct trapframe *frame)
{
	struct proc *p;
	struct thread *td;
	struct thread *dtd;

	td = curthread;
	p = td->td_proc;
	KASSERT(p->p_state == PRS_NORMAL, ("executing process is still new"));

	CTR4(KTR_PROC, "fork_exit: new thread %p (td_sched %p, pid %d, %s)",
	    td, td_get_sched(td), p->p_pid, td->td_name);

	sched_fork_exit(td);
	/*
	* Processes normally resume in mi_switch() after being
	* cpu_switch()'ed to, but when children start up they arrive here
	* instead, so we must do much the same things as mi_switch() would.
	*/
	if ((dtd = PCPU_GET(deadthread))) {
		PCPU_SET(deadthread, NULL);
		thread_stash(dtd);
	}
	thread_unlock(td);

	/*
	 * cpu_fork_kthread_handler intercepts this function call to
	 * have this call a non-return function to stay in kernel mode.
	 * initproc has its own fork handler, but it does return.
	 */
	KASSERT(callout != NULL, ("NULL callout in fork_exit"));
	callout(arg, frame);

	/*
	 * Check if a kernel thread misbehaved and returned from its main
	 * function.
	 */
	if (p->p_flag & P_KPROC) {
		printf("Kernel thread \"%s\" (pid %d) exited prematurely.\n",
		    td->td_name, p->p_pid);
		kthread_exit();
	}
	mtx_assert(&Giant, MA_NOTOWNED);

	if (p->p_sysent->sv_schedtail != NULL)
		(p->p_sysent->sv_schedtail)(td);
	td->td_pflags &= ~TDP_FORKING;
}

/*
 * Simplified back end of syscall(), used when returning from fork()
 * directly into user mode.  This function is passed in to fork_exit()
 * as the first parameter and is called when returning to a new
 * userland process.
 */
void
fork_return(struct thread *td, struct trapframe *frame)
{
	struct proc *p;

	p = td->td_proc;
	if (td->td_dbgflags & TDB_STOPATFORK) {
		PROC_LOCK(p);
		if ((p->p_flag & P_TRACED) != 0) {
			/*
			 * Inform the debugger if one is still present.
			 */
			td->td_dbgflags |= TDB_CHILD | TDB_SCX | TDB_FSTP;
			ptracestop(td, SIGSTOP, NULL);
			td->td_dbgflags &= ~(TDB_CHILD | TDB_SCX);
		} else {
			/*
			 * ... otherwise clear the request.
			 */
			td->td_dbgflags &= ~TDB_STOPATFORK;
		}
		PROC_UNLOCK(p);
	} else if (p->p_flag & P_TRACED || td->td_dbgflags & TDB_BORN) {
 		/*
		 * This is the start of a new thread in a traced
		 * process.  Report a system call exit event.
		 */
		PROC_LOCK(p);
		td->td_dbgflags |= TDB_SCX;
		_STOPEVENT(p, S_SCX, td->td_sa.code);
		if ((p->p_ptevents & PTRACE_SCX) != 0 ||
		    (td->td_dbgflags & TDB_BORN) != 0)
			ptracestop(td, SIGTRAP, NULL);
		td->td_dbgflags &= ~(TDB_SCX | TDB_BORN);
		PROC_UNLOCK(p);
	}

	userret(td, frame);

#ifdef KTRACE
	if (KTRPOINT(td, KTR_SYSRET))
		ktrsysret(SYS_fork, 0, 0);
#endif
}

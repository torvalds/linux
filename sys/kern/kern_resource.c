/*	$OpenBSD: kern_resource.c,v 1.96 2025/08/15 09:53:53 mpi Exp $	*/
/*	$NetBSD: kern_resource.c,v 1.38 1996/10/23 07:19:38 matthias Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
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
 *	@(#)kern_resource.c	8.5 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/resourcevar.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/ktrace.h>
#include <sys/sched.h>
#include <sys/signalvar.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <uvm/uvm.h>

/* Resource usage check interval in msec */
#define RUCHECK_INTERVAL	1000

/* SIGXCPU interval in seconds of process runtime */
#define SIGXCPU_INTERVAL	5

struct plimit	*lim_copy(struct plimit *);
struct plimit	*lim_write_begin(void);
void		 lim_write_commit(struct plimit *);

void	tuagg_sumup(struct tusage *, struct tusage *);

/*
 * Patchable maximum data and stack limits.
 */
rlim_t maxdmap = MAXDSIZ;
rlim_t maxsmap = MAXSSIZ;

/*
 * Serializes resource limit updates.
 * This lock has to be held together with ps_mtx when updating
 * the process' ps_limit.
 */
struct rwlock rlimit_lock = RWLOCK_INITIALIZER("rlimitlk");

/*
 * Resource controls and accounting.
 */

int
sys_getpriority(struct proc *curp, void *v, register_t *retval)
{
	struct sys_getpriority_args /* {
		syscallarg(int) which;
		syscallarg(id_t) who;
	} */ *uap = v;
	struct process *pr;
	int low = NZERO + PRIO_MAX + 1;

	switch (SCARG(uap, which)) {

	case PRIO_PROCESS:
		if (SCARG(uap, who) == 0)
			pr = curp->p_p;
		else
			pr = prfind(SCARG(uap, who));
		if (pr == NULL)
			break;
		if (pr->ps_nice < low)
			low = pr->ps_nice;
		break;

	case PRIO_PGRP: {
		struct pgrp *pg;

		if (SCARG(uap, who) == 0)
			pg = curp->p_p->ps_pgrp;
		else if ((pg = pgfind(SCARG(uap, who))) == NULL)
			break;
		LIST_FOREACH(pr, &pg->pg_members, ps_pglist)
			if (pr->ps_nice < low)
				low = pr->ps_nice;
		break;
	}

	case PRIO_USER:
		if (SCARG(uap, who) == 0)
			SCARG(uap, who) = curp->p_ucred->cr_uid;
		LIST_FOREACH(pr, &allprocess, ps_list)
			if (pr->ps_ucred->cr_uid == SCARG(uap, who) &&
			    pr->ps_nice < low)
				low = pr->ps_nice;
		break;

	default:
		return (EINVAL);
	}
	if (low == NZERO + PRIO_MAX + 1)
		return (ESRCH);
	*retval = low - NZERO;
	return (0);
}

int
sys_setpriority(struct proc *curp, void *v, register_t *retval)
{
	struct sys_setpriority_args /* {
		syscallarg(int) which;
		syscallarg(id_t) who;
		syscallarg(int) prio;
	} */ *uap = v;
	struct process *pr;
	int found = 0, error = 0;

	switch (SCARG(uap, which)) {

	case PRIO_PROCESS:
		if (SCARG(uap, who) == 0)
			pr = curp->p_p;
		else
			pr = prfind(SCARG(uap, who));
		if (pr == NULL)
			break;
		error = donice(curp, pr, SCARG(uap, prio));
		found = 1;
		break;

	case PRIO_PGRP: {
		struct pgrp *pg;
		 
		if (SCARG(uap, who) == 0)
			pg = curp->p_p->ps_pgrp;
		else if ((pg = pgfind(SCARG(uap, who))) == NULL)
			break;
		LIST_FOREACH(pr, &pg->pg_members, ps_pglist) {
			error = donice(curp, pr, SCARG(uap, prio));
			found = 1;
		}
		break;
	}

	case PRIO_USER:
		if (SCARG(uap, who) == 0)
			SCARG(uap, who) = curp->p_ucred->cr_uid;
		LIST_FOREACH(pr, &allprocess, ps_list)
			if (pr->ps_ucred->cr_uid == SCARG(uap, who)) {
				error = donice(curp, pr, SCARG(uap, prio));
				found = 1;
			}
		break;

	default:
		return (EINVAL);
	}
	if (!found)
		return (ESRCH);
	return (error);
}

int
donice(struct proc *curp, struct process *chgpr, int n)
{
	struct ucred *ucred = curp->p_ucred;
	struct proc *p;

	if (ucred->cr_uid != 0 && ucred->cr_ruid != 0 &&
	    ucred->cr_uid != chgpr->ps_ucred->cr_uid &&
	    ucred->cr_ruid != chgpr->ps_ucred->cr_uid)
		return (EPERM);
	if (n > PRIO_MAX)
		n = PRIO_MAX;
	if (n < PRIO_MIN)
		n = PRIO_MIN;
	n += NZERO;
	if (n < chgpr->ps_nice && suser(curp))
		return (EACCES);
	chgpr->ps_nice = n;
	mtx_enter(&chgpr->ps_mtx);
	SCHED_LOCK();
	TAILQ_FOREACH(p, &chgpr->ps_threads, p_thr_link) {
		setpriority(p, p->p_estcpu, n);
	}
	SCHED_UNLOCK();
	mtx_leave(&chgpr->ps_mtx);
	return (0);
}

int
sys_setrlimit(struct proc *p, void *v, register_t *retval)
{
	struct sys_setrlimit_args /* {
		syscallarg(int) which;
		syscallarg(const struct rlimit *) rlp;
	} */ *uap = v;
	struct rlimit alim;
	int error;

	error = copyin((caddr_t)SCARG(uap, rlp), (caddr_t)&alim,
		       sizeof (struct rlimit));
	if (error)
		return (error);
#ifdef KTRACE
	if (KTRPOINT(p, KTR_STRUCT))
		ktrrlimit(p, &alim);
#endif
	return (dosetrlimit(p, SCARG(uap, which), &alim));
}

int
dosetrlimit(struct proc *p, u_int which, struct rlimit *limp)
{
	struct rlimit *alimp;
	struct plimit *limit;
	rlim_t maxlim;
	int error;

	if (which >= RLIM_NLIMITS || limp->rlim_cur > limp->rlim_max)
		return (EINVAL);

	rw_enter_write(&rlimit_lock);

	alimp = &p->p_p->ps_limit->pl_rlimit[which];
	if (limp->rlim_max > alimp->rlim_max) {
		if ((error = suser(p)) != 0) {
			rw_exit_write(&rlimit_lock);
			return (error);
		}
	}

	/* Get exclusive write access to the limit structure. */
	limit = lim_write_begin();
	alimp = &limit->pl_rlimit[which];

	switch (which) {
	case RLIMIT_DATA:
		maxlim = maxdmap;
		break;
	case RLIMIT_STACK:
		maxlim = maxsmap;
		break;
	case RLIMIT_NOFILE:
		maxlim = atomic_load_int(&maxfiles);
		break;
	case RLIMIT_NPROC:
		maxlim = atomic_load_int(&maxprocess);
		break;
	default:
		maxlim = RLIM_INFINITY;
		break;
	}

	if (limp->rlim_max > maxlim)
		limp->rlim_max = maxlim;
	if (limp->rlim_cur > limp->rlim_max)
		limp->rlim_cur = limp->rlim_max;

	if (which == RLIMIT_CPU && limp->rlim_cur != RLIM_INFINITY &&
	    alimp->rlim_cur == RLIM_INFINITY)
		timeout_add_msec(&p->p_p->ps_rucheck_to, RUCHECK_INTERVAL);

	if (which == RLIMIT_STACK) {
		/*
		 * Stack is allocated to the max at exec time with only
		 * "rlim_cur" bytes accessible.  If stack limit is going
		 * up make more accessible, if going down make inaccessible.
		 */
		if (limp->rlim_cur != alimp->rlim_cur) {
			vaddr_t addr;
			vsize_t size;
			vm_prot_t prot;
			struct vmspace *vm = p->p_vmspace;

			if (limp->rlim_cur > alimp->rlim_cur) {
				prot = PROT_READ | PROT_WRITE;
				size = limp->rlim_cur - alimp->rlim_cur;
#ifdef MACHINE_STACK_GROWS_UP
				addr = (vaddr_t)vm->vm_maxsaddr +
				    alimp->rlim_cur;
#else
				addr = (vaddr_t)vm->vm_minsaddr -
				    limp->rlim_cur;
#endif
			} else {
				prot = PROT_NONE;
				size = alimp->rlim_cur - limp->rlim_cur;
#ifdef MACHINE_STACK_GROWS_UP
				addr = (vaddr_t)vm->vm_maxsaddr +
				    limp->rlim_cur;
#else
				addr = (vaddr_t)vm->vm_minsaddr -
				    alimp->rlim_cur;
#endif
			}
			addr = trunc_page(addr);
			size = round_page(size);
			(void) uvm_map_protect(&vm->vm_map, addr,
			    addr+size, prot, UVM_ET_STACK, FALSE, FALSE);
		}
	}

	*alimp = *limp;

	lim_write_commit(limit);
	rw_exit_write(&rlimit_lock);

	return (0);
}

int
sys_getrlimit(struct proc *p, void *v, register_t *retval)
{
	struct sys_getrlimit_args /* {
		syscallarg(int) which;
		syscallarg(struct rlimit *) rlp;
	} */ *uap = v;
	struct plimit *limit;
	struct rlimit alimp;
	int error;

	if (SCARG(uap, which) < 0 || SCARG(uap, which) >= RLIM_NLIMITS)
		return (EINVAL);
	limit = lim_read_enter();
	alimp = limit->pl_rlimit[SCARG(uap, which)];
	lim_read_leave(limit);
	error = copyout(&alimp, SCARG(uap, rlp), sizeof(struct rlimit));
#ifdef KTRACE
	if (error == 0 && KTRPOINT(p, KTR_STRUCT))
		ktrrlimit(p, &alimp);
#endif
	return (error);
}

/* Add the counts from *from to *tu, ensuring a consistent read of *from. */ 
void
tuagg_sumup(struct tusage *tu, struct tusage *from)
{
	struct tusage	tmp;
	unsigned int	gen;

	pc_cons_enter(&from->tu_pcl, &gen);
	do {
		tmp = *from;
	} while (pc_cons_leave(&from->tu_pcl, &gen) != 0);

	tu->tu_uticks += tmp.tu_uticks;
	tu->tu_sticks += tmp.tu_sticks;
	tu->tu_iticks += tmp.tu_iticks;
	tu->tu_ixrss += tmp.tu_ixrss;
	tu->tu_idrss += tmp.tu_idrss;
	tu->tu_isrss += tmp.tu_isrss;
	timespecadd(&tu->tu_runtime, &tmp.tu_runtime, &tu->tu_runtime);
}

void
tuagg_get_proc(struct tusage *tu, struct proc *p)
{
	memset(tu, 0, sizeof(*tu));
	tuagg_sumup(tu, &p->p_tu);
}

void
tuagg_get_process(struct tusage *tu, struct process *pr)
{
	struct proc *q;

	memset(tu, 0, sizeof(*tu));

	mtx_enter(&pr->ps_mtx);
	tuagg_sumup(tu, &pr->ps_tu);
	/* add on all living threads */
	TAILQ_FOREACH(q, &pr->ps_threads, p_thr_link)
		tuagg_sumup(tu, &q->p_tu);
	mtx_leave(&pr->ps_mtx);
}

/*
 * Update the process ps_tu usage with the values from proc p while
 * doing so the times for proc p are reset.
 * This requires that p is either curproc or SDEAD and that the
 * IPL is higher than IPL_STATCLOCK. ps_mtx uses IPL_HIGH so
 * this should always be the case.
 */
void
tuagg_add_process(struct process *pr, struct proc *p)
{
	unsigned int gen;

	MUTEX_ASSERT_LOCKED(&pr->ps_mtx);
	KASSERT(curproc == p || p->p_stat == SDEAD);

	gen = tu_enter(&pr->ps_tu);
	tuagg_sumup(&pr->ps_tu, &p->p_tu);
	tu_leave(&pr->ps_tu, gen);

	/* Now reset CPU time usage for the thread. */
	timespecclear(&p->p_tu.tu_runtime);
	p->p_tu.tu_uticks = p->p_tu.tu_sticks = p->p_tu.tu_iticks = 0;
	p->p_tu.tu_ixrss = p->p_tu.tu_idrss = p->p_tu.tu_isrss = 0;
}

void
tuagg_add_runtime(void)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
	struct proc *p = curproc;
	struct timespec ts, delta;
	unsigned int gen;

	/*
	 * Compute the amount of time during which the current
	 * process was running, and add that to its total so far.
	 */
	nanouptime(&ts);
	if (timespeccmp(&ts, &spc->spc_runtime, <)) {
#if 0
		printf("uptime is not monotonic! "
		    "ts=%lld.%09lu, runtime=%lld.%09lu\n",
		    (long long)tv.tv_sec, tv.tv_nsec,
		    (long long)spc->spc_runtime.tv_sec,
		    spc->spc_runtime.tv_nsec);
#endif
		timespecclear(&delta);
	} else {
		timespecsub(&ts, &spc->spc_runtime, &delta);
	}
	/* update spc_runtime */
	spc->spc_runtime = ts;
	gen = tu_enter(&p->p_tu);
	timespecadd(&p->p_tu.tu_runtime, &delta, &p->p_tu.tu_runtime);
	tu_leave(&p->p_tu, gen);
}

/*
 * Transform the running time and tick information in a struct tusage
 * into user, system, and interrupt time usage.
 */
void
calctsru(struct tusage *tup, struct timespec *up, struct timespec *sp,
    struct timespec *ip)
{
	u_quad_t st, ut, it;

	st = tup->tu_sticks;
	ut = tup->tu_uticks;
	it = tup->tu_iticks;

	if (st + ut + it == 0) {
		timespecclear(up);
		timespecclear(sp);
		if (ip != NULL)
			timespecclear(ip);
		return;
	}

	st = st * 1000000000 / stathz;
	sp->tv_sec = st / 1000000000;
	sp->tv_nsec = st % 1000000000;
	ut = ut * 1000000000 / stathz;
	up->tv_sec = ut / 1000000000;
	up->tv_nsec = ut % 1000000000;
	if (ip != NULL) {
		it = it * 1000000000 / stathz;
		ip->tv_sec = it / 1000000000;
		ip->tv_nsec = it % 1000000000;
	}
}

void
calcru(struct tusage *tup, struct timeval *up, struct timeval *sp,
    struct timeval *ip)
{
	struct timespec u, s, i;

	calctsru(tup, &u, &s, ip != NULL ? &i : NULL);
	TIMESPEC_TO_TIMEVAL(up, &u);
	TIMESPEC_TO_TIMEVAL(sp, &s);
	if (ip != NULL)
		TIMESPEC_TO_TIMEVAL(ip, &i);
}

int
sys_getrusage(struct proc *p, void *v, register_t *retval)
{
	struct sys_getrusage_args /* {
		syscallarg(int) who;
		syscallarg(struct rusage *) rusage;
	} */ *uap = v;
	struct rusage ru;
	int error;

	error = dogetrusage(p, SCARG(uap, who), &ru);
	if (error == 0) {
		error = copyout(&ru, SCARG(uap, rusage), sizeof(ru));
#ifdef KTRACE
		if (error == 0 && KTRPOINT(p, KTR_STRUCT))
			ktrrusage(p, &ru);
#endif
	}
	return (error);
}

int
dogetrusage(struct proc *p, int who, struct rusage *rup)
{
	struct process *pr = p->p_p;
	struct proc *q;
	struct tusage tu = { 0 };

	KERNEL_ASSERT_LOCKED();

	switch (who) {
	case RUSAGE_SELF:
		/* start with the sum of dead threads, if any */
		if (pr->ps_ru != NULL)
			*rup = *pr->ps_ru;
		else
			memset(rup, 0, sizeof(*rup));
		tuagg_sumup(&tu, &pr->ps_tu);

		/* add on all living threads */
		TAILQ_FOREACH(q, &pr->ps_threads, p_thr_link) {
			ruadd(rup, &q->p_ru);
			tuagg_sumup(&tu, &q->p_tu);
		}

		calcru(&tu, &rup->ru_utime, &rup->ru_stime, NULL);

		rup->ru_ixrss = tu.tu_ixrss;
		rup->ru_idrss = tu.tu_idrss;
		rup->ru_isrss = tu.tu_isrss;
		break;

	case RUSAGE_THREAD:
		*rup = p->p_ru;
		calcru(&p->p_tu, &rup->ru_utime, &rup->ru_stime, NULL);
		break;

	case RUSAGE_CHILDREN:
		*rup = pr->ps_cru;
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

void
ruadd(struct rusage *ru, const struct rusage *ru2)
{
	long *ip;
	const long *ip2;
	int i;

	timeradd(&ru->ru_utime, &ru2->ru_utime, &ru->ru_utime);
	timeradd(&ru->ru_stime, &ru2->ru_stime, &ru->ru_stime);
	if (ru->ru_maxrss < ru2->ru_maxrss)
		ru->ru_maxrss = ru2->ru_maxrss;
	ip = &ru->ru_first; ip2 = &ru2->ru_first;
	for (i = &ru->ru_last - &ru->ru_first; i >= 0; i--)
		*ip++ += *ip2++;
}

/*
 * Check if the process exceeds its cpu resource allocation.
 * If over max, kill it.
 */
void
rucheck(void *arg)
{
	struct rlimit rlim;
	struct tusage tu = { 0 };
	struct process *pr = arg;
	struct proc *q;
	time_t runtime;

	KERNEL_ASSERT_LOCKED();

	mtx_enter(&pr->ps_mtx);
	rlim = pr->ps_limit->pl_rlimit[RLIMIT_CPU];
	tuagg_sumup(&tu, &pr->ps_tu);
	TAILQ_FOREACH(q, &pr->ps_threads, p_thr_link)
		tuagg_sumup(&tu, &q->p_tu);
	mtx_leave(&pr->ps_mtx);

	runtime = tu.tu_runtime.tv_sec;

	if ((rlim_t)runtime >= rlim.rlim_cur) {
		if ((rlim_t)runtime >= rlim.rlim_max) {
			prsignal(pr, SIGKILL);
		} else if (runtime >= pr->ps_nextxcpu) {
			prsignal(pr, SIGXCPU);
			pr->ps_nextxcpu = runtime + SIGXCPU_INTERVAL;
		}
	}

	timeout_add_msec(&pr->ps_rucheck_to, RUCHECK_INTERVAL);
}

struct pool plimit_pool;

void
lim_startup(struct plimit *limit0)
{
	rlim_t lim;
	int i;

	pool_init(&plimit_pool, sizeof(struct plimit), 0, IPL_MPFLOOR,
	    PR_WAITOK, "plimitpl", NULL);

	for (i = 0; i < nitems(limit0->pl_rlimit); i++)
		limit0->pl_rlimit[i].rlim_cur =
		    limit0->pl_rlimit[i].rlim_max = RLIM_INFINITY;
	limit0->pl_rlimit[RLIMIT_NOFILE].rlim_cur = NOFILE;
	limit0->pl_rlimit[RLIMIT_NOFILE].rlim_max = MIN(NOFILE_MAX,
	    (maxfiles - NOFILE > NOFILE) ? maxfiles - NOFILE : NOFILE);
	limit0->pl_rlimit[RLIMIT_NPROC].rlim_cur = MAXUPRC;
	lim = ptoa(uvmexp.free);
	limit0->pl_rlimit[RLIMIT_RSS].rlim_max = lim;
	lim = ptoa(64*1024);		/* Default to very low */
	limit0->pl_rlimit[RLIMIT_MEMLOCK].rlim_max = lim;
	limit0->pl_rlimit[RLIMIT_MEMLOCK].rlim_cur = lim / 3;
	refcnt_init(&limit0->pl_refcnt);
}

/*
 * Make a copy of the plimit structure.
 * We share these structures copy-on-write after fork,
 * and copy when a limit is changed.
 */
struct plimit *
lim_copy(struct plimit *lim)
{
	struct plimit *newlim;

	newlim = pool_get(&plimit_pool, PR_WAITOK);
	memcpy(newlim->pl_rlimit, lim->pl_rlimit,
	    sizeof(struct rlimit) * RLIM_NLIMITS);
	refcnt_init(&newlim->pl_refcnt);
	return (newlim);
}

void
lim_free(struct plimit *lim)
{
	if (refcnt_rele(&lim->pl_refcnt) == 0)
		return;
	pool_put(&plimit_pool, lim);
}

void
lim_fork(struct process *parent, struct process *child)
{
	struct plimit *limit;

	mtx_enter(&parent->ps_mtx);
	limit = parent->ps_limit;
	refcnt_take(&limit->pl_refcnt);
	mtx_leave(&parent->ps_mtx);

	child->ps_limit = limit;

	if (limit->pl_rlimit[RLIMIT_CPU].rlim_cur != RLIM_INFINITY)
		timeout_add_msec(&child->ps_rucheck_to, RUCHECK_INTERVAL);
}

/*
 * Return an exclusive write reference to the process' resource limit structure.
 * The caller has to release the structure by calling lim_write_commit().
 *
 * This invalidates any plimit read reference held by the calling thread.
 */
struct plimit *
lim_write_begin(void)
{
	struct plimit *limit;
	struct proc *p = curproc;

	rw_assert_wrlock(&rlimit_lock);

	if (p->p_limit != NULL)
		lim_free(p->p_limit);
	p->p_limit = NULL;

	/*
	 * It is safe to access ps_limit here without holding ps_mtx
	 * because rlimit_lock excludes other writers.
	 */

	limit = p->p_p->ps_limit;
	if (P_HASSIBLING(p) || refcnt_shared(&limit->pl_refcnt))
		limit = lim_copy(limit);

	return (limit);
}

/*
 * Finish exclusive write access to the plimit structure.
 * This makes the structure visible to other threads in the process.
 */
void
lim_write_commit(struct plimit *limit)
{
	struct plimit *olimit;
	struct proc *p = curproc;

	rw_assert_wrlock(&rlimit_lock);

	if (limit != p->p_p->ps_limit) {
		mtx_enter(&p->p_p->ps_mtx);
		olimit = p->p_p->ps_limit;
		p->p_p->ps_limit = limit;
		mtx_leave(&p->p_p->ps_mtx);

		lim_free(olimit);
	}
}

/*
 * Begin read access to the process' resource limit structure.
 * The access has to be finished by calling lim_read_leave().
 *
 * Sections denoted by lim_read_enter() and lim_read_leave() cannot nest.
 */
struct plimit *
lim_read_enter(void)
{
	struct plimit *limit;
	struct proc *p = curproc;
	struct process *pr = p->p_p;

	/*
	 * This thread might not observe the latest value of ps_limit
	 * if another thread updated the limits very recently on another CPU.
	 * However, the anomaly should disappear quickly, especially if
	 * there is any synchronization activity between the threads (or
	 * the CPUs).
	 */

	limit = p->p_limit;
	if (limit != pr->ps_limit) {
		mtx_enter(&pr->ps_mtx);
		limit = pr->ps_limit;
		refcnt_take(&limit->pl_refcnt);
		mtx_leave(&pr->ps_mtx);
		if (p->p_limit != NULL)
			lim_free(p->p_limit);
		p->p_limit = limit;
	}
	KASSERT(limit != NULL);
	return (limit);
}

/*
 * Get the value of the resource limit in given process.
 */
rlim_t
lim_cur_proc(struct proc *p, int which)
{
	struct process *pr = p->p_p;
	rlim_t val;

	KASSERT(which >= 0 && which < RLIM_NLIMITS);

	mtx_enter(&pr->ps_mtx);
	val = pr->ps_limit->pl_rlimit[which].rlim_cur;
	mtx_leave(&pr->ps_mtx);
	return (val);
}

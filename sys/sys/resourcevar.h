/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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
 *	@(#)resourcevar.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef	_SYS_RESOURCEVAR_H_
#define	_SYS_RESOURCEVAR_H_

#include <sys/resource.h>
#include <sys/queue.h>
#ifdef _KERNEL
#include <sys/_lock.h>
#include <sys/_mutex.h>
#endif

/*
 * Kernel per-process accounting / statistics
 * (not necessarily resident except when running).
 *
 * Locking key:
 *      b - created at fork, never changes
 *      c - locked by proc mtx
 *      k - only accessed by curthread
 *      w - locked by proc itim lock
 *	w2 - locked by proc prof lock
 */
struct pstats {
#define	pstat_startzero	p_cru
	struct	rusage p_cru;		/* Stats for reaped children. */
	struct	itimerval p_timer[3];	/* (w) Virtual-time timers. */
#define	pstat_endzero	pstat_startcopy

#define	pstat_startcopy	p_prof
	struct uprof {			/* Profile arguments. */
		caddr_t	pr_base;	/* (c + w2) Buffer base. */
		u_long	pr_size;	/* (c + w2) Buffer size. */
		u_long	pr_off;		/* (c + w2) PC offset. */
		u_long	pr_scale;	/* (c + w2) PC scaling. */
	} p_prof;
#define	pstat_endcopy	p_start
	struct	timeval p_start;	/* (b) Starting time. */
};

#ifdef _KERNEL

/*
 * Kernel shareable process resource limits.  Because this structure
 * is moderately large but changes infrequently, it is normally
 * shared copy-on-write after forks.
 */
struct plimit {
	struct	rlimit pl_rlimit[RLIM_NLIMITS];
	int	pl_refcnt;		/* number of references */
};

struct racct;

/*-
 * Per uid resource consumption.  This structure is used to track
 * the total resource consumption (process count, socket buffer size,
 * etc) for the uid and impose limits.
 *
 * Locking guide:
 * (a) Constant from inception
 * (b) Lockless, updated using atomics
 * (c) Locked by global uihashtbl_lock
 */
struct uidinfo {
	LIST_ENTRY(uidinfo) ui_hash;	/* (c) hash chain of uidinfos */
	u_long ui_vmsize;	/* (b) pages of swap reservation by uid */
	long	ui_sbsize;		/* (b) socket buffer space consumed */
	long	ui_proccnt;		/* (b) number of processes */
	long	ui_ptscnt;		/* (b) number of pseudo-terminals */
	long	ui_kqcnt;		/* (b) number of kqueues */
	long	ui_umtxcnt;		/* (b) number of shared umtxs */
	uid_t	ui_uid;			/* (a) uid */
	u_int	ui_ref;			/* (b) reference count */
#ifdef	RACCT
	struct racct *ui_racct;		/* (a) resource accounting */
#endif
};

#define	UIDINFO_VMSIZE_LOCK(ui)		mtx_lock(&((ui)->ui_vmsize_mtx))
#define	UIDINFO_VMSIZE_UNLOCK(ui)	mtx_unlock(&((ui)->ui_vmsize_mtx))

struct proc;
struct rusage_ext;
struct thread;

void	 addupc_intr(struct thread *td, uintfptr_t pc, u_int ticks);
void	 addupc_task(struct thread *td, uintfptr_t pc, u_int ticks);
void	 calccru(struct proc *p, struct timeval *up, struct timeval *sp);
void	 calcru(struct proc *p, struct timeval *up, struct timeval *sp);
int	 chgkqcnt(struct uidinfo *uip, int diff, rlim_t max);
int	 chgproccnt(struct uidinfo *uip, int diff, rlim_t maxval);
int	 chgsbsize(struct uidinfo *uip, u_int *hiwat, u_int to,
	    rlim_t maxval);
int	 chgptscnt(struct uidinfo *uip, int diff, rlim_t maxval);
int	 chgumtxcnt(struct uidinfo *uip, int diff, rlim_t maxval);
int	 kern_proc_setrlimit(struct thread *td, struct proc *p, u_int which,
	    struct rlimit *limp);
struct plimit
	*lim_alloc(void);
void	 lim_copy(struct plimit *dst, struct plimit *src);
rlim_t	 lim_cur(struct thread *td, int which);
#define lim_cur(td, which)	({					\
	rlim_t _rlim;							\
	struct thread *_td = (td);					\
	int _which = (which);						\
	if (__builtin_constant_p(which) && which != RLIMIT_DATA &&	\
	    which != RLIMIT_STACK && which != RLIMIT_VMEM) {		\
		_rlim = td->td_limit->pl_rlimit[which].rlim_cur;	\
	} else {							\
		_rlim = lim_cur(_td, _which);				\
	}								\
	_rlim;								\
})

rlim_t	 lim_cur_proc(struct proc *p, int which);
void	 lim_fork(struct proc *p1, struct proc *p2);
void	 lim_free(struct plimit *limp);
struct plimit
	*lim_hold(struct plimit *limp);
rlim_t	 lim_max(struct thread *td, int which);
rlim_t	 lim_max_proc(struct proc *p, int which);
void	 lim_rlimit(struct thread *td, int which, struct rlimit *rlp);
void	 lim_rlimit_proc(struct proc *p, int which, struct rlimit *rlp);
void	 ruadd(struct rusage *ru, struct rusage_ext *rux, struct rusage *ru2,
	    struct rusage_ext *rux2);
void	 rucollect(struct rusage *ru, struct rusage *ru2);
void	 rufetch(struct proc *p, struct rusage *ru);
void	 rufetchcalc(struct proc *p, struct rusage *ru, struct timeval *up,
	    struct timeval *sp);
void	 rufetchtd(struct thread *td, struct rusage *ru);
void	 ruxagg(struct proc *p, struct thread *td);
struct uidinfo
	*uifind(uid_t uid);
void	 uifree(struct uidinfo *uip);
void	 uihashinit(void);
void	 uihold(struct uidinfo *uip);
#ifdef	RACCT
void	 ui_racct_foreach(void (*callback)(struct racct *racct,
	    void *arg2, void *arg3), void (*pre)(void), void (*post)(void),
	    void *arg2, void *arg3);
#endif

#endif /* _KERNEL */
#endif /* !_SYS_RESOURCEVAR_H_ */

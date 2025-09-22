/*	$OpenBSD: kern_event.c,v 1.205 2025/05/21 14:10:16 visa Exp $	*/

/*-
 * Copyright (c) 1999,2000,2001 Jonathan Lemon <jlemon@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_event.c,v 1.22 2001/02/23 20:32:42 jlemon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <sys/pledge.h>
#include <sys/malloc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/eventvar.h>
#include <sys/ktrace.h>
#include <sys/pool.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/time.h>
#include <sys/timeout.h>
#include <sys/vnode.h>
#include <sys/wait.h>

#ifdef DIAGNOSTIC
#define KLIST_ASSERT_LOCKED(kl) do {					\
	if ((kl)->kl_ops != NULL)					\
		(kl)->kl_ops->klo_assertlk((kl)->kl_arg);		\
	else								\
		KERNEL_ASSERT_LOCKED();					\
} while (0)
#else
#define KLIST_ASSERT_LOCKED(kl)	((void)(kl))
#endif

int	dokqueue(struct proc *, int, register_t *);
struct	kqueue *kqueue_alloc(struct filedesc *);
void	kqueue_terminate(struct proc *p, struct kqueue *);
void	KQREF(struct kqueue *);
void	KQRELE(struct kqueue *);

void	kqueue_purge(struct proc *, struct kqueue *);
int	kqueue_sleep(struct kqueue *, struct timespec *);

int	kqueue_read(struct file *, struct uio *, int);
int	kqueue_write(struct file *, struct uio *, int);
int	kqueue_ioctl(struct file *fp, u_long com, caddr_t data,
		    struct proc *p);
int	kqueue_kqfilter(struct file *fp, struct knote *kn);
int	kqueue_stat(struct file *fp, struct stat *st, struct proc *p);
int	kqueue_close(struct file *fp, struct proc *p);
void	kqueue_wakeup(struct kqueue *kq);

#ifdef KQUEUE_DEBUG
void	kqueue_do_check(struct kqueue *kq, const char *func, int line);
#define kqueue_check(kq)	kqueue_do_check((kq), __func__, __LINE__)
#else
#define kqueue_check(kq)	do {} while (0)
#endif

static int	filter_attach(struct knote *kn);
static void	filter_detach(struct knote *kn);
static int	filter_event(struct knote *kn, long hint);
static int	filter_modify(struct kevent *kev, struct knote *kn);
static int	filter_process(struct knote *kn, struct kevent *kev);
static void	kqueue_expand_hash(struct kqueue *kq);
static void	kqueue_expand_list(struct kqueue *kq, int fd);
static void	kqueue_task(void *);
static int	klist_lock(struct klist *);
static void	klist_unlock(struct klist *, int);

const struct fileops kqueueops = {
	.fo_read	= kqueue_read,
	.fo_write	= kqueue_write,
	.fo_ioctl	= kqueue_ioctl,
	.fo_kqfilter	= kqueue_kqfilter,
	.fo_stat	= kqueue_stat,
	.fo_close	= kqueue_close
};

void	knote_attach(struct knote *kn);
void	knote_detach(struct knote *kn);
void	knote_drop(struct knote *kn, struct proc *p);
void	knote_enqueue(struct knote *kn);
void	knote_dequeue(struct knote *kn);
int	knote_acquire(struct knote *kn, struct klist *, int);
void	knote_release(struct knote *kn);
void	knote_activate(struct knote *kn);
void	knote_remove(struct proc *p, struct kqueue *kq, struct knlist **plist,
	    int idx, int purge);

void	filt_kqdetach(struct knote *kn);
int	filt_kqueue(struct knote *kn, long hint);
int	filt_kqueuemodify(struct kevent *kev, struct knote *kn);
int	filt_kqueueprocess(struct knote *kn, struct kevent *kev);
int	filt_kqueue_common(struct knote *kn, struct kqueue *kq);
int	filt_procattach(struct knote *kn);
void	filt_procdetach(struct knote *kn);
int	filt_proc(struct knote *kn, long hint);
int	filt_procmodify(struct kevent *kev, struct knote *kn);
int	filt_procprocess(struct knote *kn, struct kevent *kev);
int	filt_sigattach(struct knote *kn);
void	filt_sigdetach(struct knote *kn);
int	filt_signal(struct knote *kn, long hint);
int	filt_fileattach(struct knote *kn);
void	filt_timerexpire(void *knx);
void	filt_dotimerexpire(struct knote *kn);
int	filt_timerattach(struct knote *kn);
void	filt_timerdetach(struct knote *kn);
int	filt_timermodify(struct kevent *kev, struct knote *kn);
int	filt_timerprocess(struct knote *kn, struct kevent *kev);
int	filt_userattach(struct knote *kn);
void	filt_userdetach(struct knote *kn);
int	filt_usermodify(struct kevent *kev, struct knote *kn);
int	filt_userprocess(struct knote *kn, struct kevent *kev);
void	filt_seltruedetach(struct knote *kn);

const struct filterops kqread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_kqdetach,
	.f_event	= filt_kqueue,
	.f_modify	= filt_kqueuemodify,
	.f_process	= filt_kqueueprocess,
};

const struct filterops proc_filtops = {
	.f_flags	= FILTEROP_MPSAFE,
	.f_attach	= filt_procattach,
	.f_detach	= filt_procdetach,
	.f_event	= filt_proc,
	.f_modify	= filt_procmodify,
	.f_process	= filt_procprocess,
};

const struct filterops sig_filtops = {
	.f_flags	= FILTEROP_MPSAFE,
	.f_attach	= filt_sigattach,
	.f_detach	= filt_sigdetach,
	.f_event	= filt_signal,
	.f_modify	= filt_procmodify,
	.f_process	= filt_procprocess,
};

const struct filterops file_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= filt_fileattach,
	.f_detach	= NULL,
	.f_event	= NULL,
};

const struct filterops timer_filtops = {
	.f_flags	= FILTEROP_MPSAFE,
	.f_attach	= filt_timerattach,
	.f_detach	= filt_timerdetach,
	.f_event	= NULL,
	.f_modify	= filt_timermodify,
	.f_process	= filt_timerprocess,
};

const struct filterops user_filtops = {
	.f_flags	= FILTEROP_MPSAFE,
	.f_attach	= filt_userattach,
	.f_detach	= filt_userdetach,
	.f_event	= NULL,
	.f_modify	= filt_usermodify,
	.f_process	= filt_userprocess,
};

/*
 * Locking:
 *	I	immutable after creation
 *	a	atomic operations
 *	t	ft_mtx
 */

struct	pool knote_pool;
struct	pool kqueue_pool;
struct	mutex kqueue_klist_lock = MUTEX_INITIALIZER(IPL_MPFLOOR);
struct	rwlock kqueue_ps_list_lock = RWLOCK_INITIALIZER("kqpsl");
unsigned int kq_usereventsmax = 1024;	/* per process */

#define KN_HASH(val, mask)	(((val) ^ (val >> 8)) & (mask))

/*
 * Table for all system-defined filters.
 */
const struct filterops *const sysfilt_ops[] = {
	&file_filtops,			/* EVFILT_READ */
	&file_filtops,			/* EVFILT_WRITE */
	NULL, /*&aio_filtops,*/		/* EVFILT_AIO */
	&file_filtops,			/* EVFILT_VNODE */
	&proc_filtops,			/* EVFILT_PROC */
	&sig_filtops,			/* EVFILT_SIGNAL */
	&timer_filtops,			/* EVFILT_TIMER */
	&file_filtops,			/* EVFILT_DEVICE */
	&file_filtops,			/* EVFILT_EXCEPT */
	&user_filtops,			/* EVFILT_USER */
};

void
KQREF(struct kqueue *kq)
{
	refcnt_take(&kq->kq_refcnt);
}

void
KQRELE(struct kqueue *kq)
{
	struct filedesc *fdp;

	if (refcnt_rele(&kq->kq_refcnt) == 0)
		return;

	fdp = kq->kq_fdp;
	if (rw_status(&fdp->fd_lock) == RW_WRITE) {
		LIST_REMOVE(kq, kq_next);
	} else {
		fdplock(fdp);
		LIST_REMOVE(kq, kq_next);
		fdpunlock(fdp);
	}

	KASSERT(TAILQ_EMPTY(&kq->kq_head));
	KASSERT(kq->kq_nknotes == 0);

	free(kq->kq_knlist, M_KEVENT, kq->kq_knlistsize *
	    sizeof(struct knlist));
	hashfree(kq->kq_knhash, KN_HASHSIZE, M_KEVENT);
	klist_free(&kq->kq_klist);
	pool_put(&kqueue_pool, kq);
}

void
kqueue_init(void)
{
	pool_init(&kqueue_pool, sizeof(struct kqueue), 0, IPL_MPFLOOR,
	    PR_WAITOK, "kqueuepl", NULL);
	pool_init(&knote_pool, sizeof(struct knote), 0, IPL_MPFLOOR,
	    PR_WAITOK, "knotepl", NULL);
}

void
kqueue_init_percpu(void)
{
	pool_cache_init(&knote_pool);
}

int
filt_fileattach(struct knote *kn)
{
	struct file *fp = kn->kn_fp;

	return fp->f_ops->fo_kqfilter(fp, kn);
}

int
kqueue_kqfilter(struct file *fp, struct knote *kn)
{
	struct kqueue *kq = kn->kn_fp->f_data;

	if (kn->kn_filter != EVFILT_READ)
		return (EINVAL);

	kn->kn_fop = &kqread_filtops;
	klist_insert(&kq->kq_klist, kn);
	return (0);
}

void
filt_kqdetach(struct knote *kn)
{
	struct kqueue *kq = kn->kn_fp->f_data;

	klist_remove(&kq->kq_klist, kn);
}

int
filt_kqueue_common(struct knote *kn, struct kqueue *kq)
{
	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	kn->kn_data = kq->kq_count;

	return (kn->kn_data > 0);
}

int
filt_kqueue(struct knote *kn, long hint)
{
	struct kqueue *kq = kn->kn_fp->f_data;
	int active;

	mtx_enter(&kq->kq_lock);
	active = filt_kqueue_common(kn, kq);
	mtx_leave(&kq->kq_lock);

	return (active);
}

int
filt_kqueuemodify(struct kevent *kev, struct knote *kn)
{
	struct kqueue *kq = kn->kn_fp->f_data;
	int active;

	mtx_enter(&kq->kq_lock);
	knote_assign(kev, kn);
	active = filt_kqueue_common(kn, kq);
	mtx_leave(&kq->kq_lock);

	return (active);
}

int
filt_kqueueprocess(struct knote *kn, struct kevent *kev)
{
	struct kqueue *kq = kn->kn_fp->f_data;
	int active;

	mtx_enter(&kq->kq_lock);
	if (kev != NULL && (kn->kn_flags & EV_ONESHOT))
		active = 1;
	else
		active = filt_kqueue_common(kn, kq);
	if (active)
		knote_submit(kn, kev);
	mtx_leave(&kq->kq_lock);

	return (active);
}

int
filt_procattach(struct knote *kn)
{
	struct process *pr;
	int nolock;

	if ((curproc->p_p->ps_flags & PS_PLEDGE) &&
	    (curproc->p_pledge & PLEDGE_PROC) == 0)
		return pledge_fail(curproc, EPERM, PLEDGE_PROC);

	if (kn->kn_id > PID_MAX)
		return ESRCH;

	KERNEL_LOCK();
	pr = prfind(kn->kn_id);
	if (pr == NULL)
		goto fail;

	/* exiting processes can't be specified */
	if (pr->ps_flags & PS_EXITING)
		goto fail;

	kn->kn_ptr.p_process = pr;
	kn->kn_flags |= EV_CLEAR;		/* automatically set */

	/*
	 * internal flag indicating registration done by kernel
	 */
	if (kn->kn_flags & EV_FLAG1) {
		kn->kn_data = kn->kn_sdata;		/* ppid */
		kn->kn_fflags = NOTE_CHILD;
		kn->kn_flags &= ~EV_FLAG1;
		rw_assert_wrlock(&kqueue_ps_list_lock);
	}

	/* this needs both the ps_mtx and exclusive kqueue_ps_list_lock. */
	nolock = (rw_status(&kqueue_ps_list_lock) == RW_WRITE);
	if (!nolock)
		rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	klist_insert_locked(&pr->ps_klist, kn);
	mtx_leave(&pr->ps_mtx);
	if (!nolock)
		rw_exit_write(&kqueue_ps_list_lock);

	KERNEL_UNLOCK();

	return (0);

fail:
	KERNEL_UNLOCK();
	return (ESRCH);
}

/*
 * The knote may be attached to a different process, which may exit,
 * leaving nothing for the knote to be attached to.  So when the process
 * exits, the knote is marked as DETACHED and also flagged as ONESHOT so
 * it will be deleted when read out.  However, as part of the knote deletion,
 * this routine is called, so a check is needed to avoid actually performing
 * a detach, because the original process does not exist any more.
 */
void
filt_procdetach(struct knote *kn)
{
	struct process *pr = kn->kn_ptr.p_process;
	int status;

	/* this needs both the ps_mtx and exclusive kqueue_ps_list_lock. */
	rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	status = kn->kn_status;

	if ((status & KN_DETACHED) == 0)
		klist_remove_locked(&pr->ps_klist, kn);

	mtx_leave(&pr->ps_mtx);
	rw_exit_write(&kqueue_ps_list_lock);
}

int
filt_proc(struct knote *kn, long hint)
{
	struct process *pr = kn->kn_ptr.p_process;
	struct kqueue *kq = kn->kn_kq;
	u_int event;

	/*
	 * mask off extra data
	 */
	event = (u_int)hint & NOTE_PCTRLMASK;

	/*
	 * if the user is interested in this event, record it.
	 */
	if (kn->kn_sfflags & event)
		kn->kn_fflags |= event;

	/*
	 * process is gone, so flag the event as finished and remove it
	 * from the process's klist
	 */
	if (event == NOTE_EXIT) {
		struct process *pr = kn->kn_ptr.p_process;

		mtx_enter(&kq->kq_lock);
		kn->kn_status |= KN_DETACHED;
		mtx_leave(&kq->kq_lock);

		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		kn->kn_data = W_EXITCODE(pr->ps_xexit, pr->ps_xsig);
		klist_remove_locked(&pr->ps_klist, kn);
		return (1);
	}

	/*
	 * process forked, and user wants to track the new process,
	 * so attach a new knote to it, and immediately report an
	 * event with the parent's pid.
	 */
	if ((event == NOTE_FORK) && (kn->kn_sfflags & NOTE_TRACK)) {
		struct kevent kev;
		int error;

		/*
		 * register knote with new process.
		 */
		memset(&kev, 0, sizeof(kev));
		kev.ident = hint & NOTE_PDATAMASK;	/* pid */
		kev.filter = kn->kn_filter;
		kev.flags = kn->kn_flags | EV_ADD | EV_ENABLE | EV_FLAG1;
		kev.fflags = kn->kn_sfflags;
		kev.data = kn->kn_id;			/* parent */
		kev.udata = kn->kn_udata;		/* preserve udata */

		rw_assert_wrlock(&kqueue_ps_list_lock);
		mtx_leave(&pr->ps_mtx);
		error = kqueue_register(kq, &kev, 0, NULL);
		mtx_enter(&pr->ps_mtx);

		if (error)
			kn->kn_fflags |= NOTE_TRACKERR;
	}

	return (kn->kn_fflags != 0);
}

int
filt_procmodify(struct kevent *kev, struct knote *kn)
{
	struct process *pr = kn->kn_ptr.p_process;
	int active;

	mtx_enter(&pr->ps_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&pr->ps_mtx);

	return (active);
}

/*
 * By default only grab the mutex here. If the event requires extra protection
 * because it alters the klist (NOTE_EXIT, NOTE_FORK the caller of the knote
 * needs to grab the rwlock first.
 */
int
filt_procprocess(struct knote *kn, struct kevent *kev)
{
	struct process *pr = kn->kn_ptr.p_process;
	int active;

	mtx_enter(&pr->ps_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&pr->ps_mtx);

	return (active);
}

/*
 * signal knotes are shared with proc knotes, so we apply a mask to
 * the hint in order to differentiate them from process hints.  This
 * could be avoided by using a signal-specific knote list, but probably
 * isn't worth the trouble.
 */
int
filt_sigattach(struct knote *kn)
{
	struct process *pr = curproc->p_p;

	if (kn->kn_id >= NSIG)
		return EINVAL;

	kn->kn_ptr.p_process = pr;
	kn->kn_flags |= EV_CLEAR;		/* automatically set */

	/* this needs both the ps_mtx and exclusive kqueue_ps_list_lock. */
	rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	klist_insert_locked(&pr->ps_klist, kn);
	mtx_leave(&pr->ps_mtx);
	rw_exit_write(&kqueue_ps_list_lock);

	return (0);
}

void
filt_sigdetach(struct knote *kn)
{
	struct process *pr = kn->kn_ptr.p_process;

	rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	klist_remove_locked(&pr->ps_klist, kn);
	mtx_leave(&pr->ps_mtx);
	rw_exit_write(&kqueue_ps_list_lock);
}

int
filt_signal(struct knote *kn, long hint)
{
	if (hint & NOTE_SIGNAL) {
		hint &= ~NOTE_SIGNAL;

		if (kn->kn_id == hint)
			kn->kn_data++;
	}
	return (kn->kn_data != 0);
}

#define NOTE_TIMER_UNITMASK \
	(NOTE_SECONDS|NOTE_MSECONDS|NOTE_USECONDS|NOTE_NSECONDS)

static int
filt_timervalidate(int sfflags, int64_t sdata, struct timespec *ts)
{
	if (sfflags & ~(NOTE_TIMER_UNITMASK | NOTE_ABSTIME))
		return (EINVAL);

	switch (sfflags & NOTE_TIMER_UNITMASK) {
	case NOTE_SECONDS:
		ts->tv_sec = sdata;
		ts->tv_nsec = 0;
		break;
	case NOTE_MSECONDS:
		ts->tv_sec = sdata / 1000;
		ts->tv_nsec = (sdata % 1000) * 1000000;
		break;
	case NOTE_USECONDS:
		ts->tv_sec = sdata / 1000000;
		ts->tv_nsec = (sdata % 1000000) * 1000;
		break;
	case NOTE_NSECONDS:
		ts->tv_sec = sdata / 1000000000;
		ts->tv_nsec = sdata % 1000000000;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

struct filt_timer {
	struct mutex	ft_mtx;
	struct timeout	ft_to;		/* [t] */
	int		ft_reschedule;	/* [t] */
};

static void
filt_timeradd(struct knote *kn, struct timespec *ts)
{
	struct timespec expiry, now;
	struct filt_timer *ft = kn->kn_hook;
	int tticks;

	MUTEX_ASSERT_LOCKED(&ft->ft_mtx);

	ft->ft_reschedule = ((kn->kn_flags & EV_ONESHOT) == 0 &&
	    (kn->kn_sfflags & NOTE_ABSTIME) == 0);

	if (kn->kn_sfflags & NOTE_ABSTIME) {
		nanotime(&now);
		if (timespeccmp(ts, &now, >)) {
			timespecsub(ts, &now, &expiry);
			/* XXX timeout_abs_ts with CLOCK_REALTIME */
			timeout_add(&ft->ft_to, tstohz(&expiry));
		} else {
			/* Expire immediately. */
			filt_dotimerexpire(kn);
		}
		return;
	}

	tticks = tstohz(ts);
	/* Remove extra tick from tstohz() if timeout has fired before. */
	if (timeout_triggered(&ft->ft_to))
		tticks--;
	timeout_add(&ft->ft_to, (tticks > 0) ? tticks : 1);
}

void
filt_dotimerexpire(struct knote *kn)
{
	struct timespec ts;
	struct filt_timer *ft = kn->kn_hook;
	struct kqueue *kq = kn->kn_kq;

	MUTEX_ASSERT_LOCKED(&ft->ft_mtx);

	kn->kn_data++;

	mtx_enter(&kq->kq_lock);
	knote_activate(kn);
	mtx_leave(&kq->kq_lock);

	if (ft->ft_reschedule) {
		(void)filt_timervalidate(kn->kn_sfflags, kn->kn_sdata, &ts);
		filt_timeradd(kn, &ts);
	}
}

void
filt_timerexpire(void *knx)
{
	struct knote *kn = knx;
	struct filt_timer *ft = kn->kn_hook;

	mtx_enter(&ft->ft_mtx);
	filt_dotimerexpire(kn);
	mtx_leave(&ft->ft_mtx);
}

/*
 * data contains amount of time to sleep
 */
int
filt_timerattach(struct knote *kn)
{
	struct filedesc *fdp = kn->kn_kq->kq_fdp;
	struct timespec ts;
	struct filt_timer *ft;
	u_int nuserevents;
	int error;

	error = filt_timervalidate(kn->kn_sfflags, kn->kn_sdata, &ts);
	if (error != 0)
		return (error);

	nuserevents = atomic_inc_int_nv(&fdp->fd_nuserevents);
	if (nuserevents > atomic_load_int(&kq_usereventsmax)) {
		atomic_dec_int(&fdp->fd_nuserevents);
		return (ENOMEM);
	}

	if ((kn->kn_sfflags & NOTE_ABSTIME) == 0)
		kn->kn_flags |= EV_CLEAR;	/* automatically set */

	ft = malloc(sizeof(*ft), M_KEVENT, M_WAITOK);
	mtx_init(&ft->ft_mtx, IPL_SOFTCLOCK);
	timeout_set(&ft->ft_to, filt_timerexpire, kn);
	kn->kn_hook = ft;

	mtx_enter(&ft->ft_mtx);
	filt_timeradd(kn, &ts);
	mtx_leave(&ft->ft_mtx);

	return (0);
}

void
filt_timerdetach(struct knote *kn)
{
	struct filedesc *fdp = kn->kn_kq->kq_fdp;
	struct filt_timer *ft = kn->kn_hook;

	mtx_enter(&ft->ft_mtx);
	ft->ft_reschedule = 0;
	mtx_leave(&ft->ft_mtx);

	timeout_del_barrier(&ft->ft_to);
	free(ft, M_KEVENT, sizeof(*ft));
	atomic_dec_int(&fdp->fd_nuserevents);
}

int
filt_timermodify(struct kevent *kev, struct knote *kn)
{
	struct timespec ts;
	struct kqueue *kq = kn->kn_kq;
	struct filt_timer *ft = kn->kn_hook;
	int error;

	error = filt_timervalidate(kev->fflags, kev->data, &ts);
	if (error != 0) {
		kev->flags |= EV_ERROR;
		kev->data = error;
		return (0);
	}

	/* Reset the timer. Any pending events are discarded. */
	mtx_enter(&ft->ft_mtx);
	ft->ft_reschedule = 0;
	mtx_leave(&ft->ft_mtx);

	timeout_del_barrier(&ft->ft_to);

	mtx_enter(&ft->ft_mtx);
	mtx_enter(&kq->kq_lock);
	if (kn->kn_status & KN_QUEUED)
		knote_dequeue(kn);
	kn->kn_status &= ~KN_ACTIVE;
	mtx_leave(&kq->kq_lock);

	kn->kn_data = 0;
	knote_assign(kev, kn);
	/* Reinit timeout to invoke tick adjustment again. */
	timeout_set(&ft->ft_to, filt_timerexpire, kn);
	filt_timeradd(kn, &ts);
	mtx_leave(&ft->ft_mtx);

	return (0);
}

int
filt_timerprocess(struct knote *kn, struct kevent *kev)
{
	struct filt_timer *ft = kn->kn_hook;
	int active;

	mtx_enter(&ft->ft_mtx);
	active = (kn->kn_data != 0);
	if (active)
		knote_submit(kn, kev);
	mtx_leave(&ft->ft_mtx);

	return (active);
}

int
filt_userattach(struct knote *kn)
{
	struct filedesc *fdp = kn->kn_kq->kq_fdp;
	u_int nuserevents;

	nuserevents = atomic_inc_int_nv(&fdp->fd_nuserevents);
	if (nuserevents > atomic_load_int(&kq_usereventsmax)) {
		atomic_dec_int(&fdp->fd_nuserevents);
		return (ENOMEM);
	}

	kn->kn_ptr.p_useract = ((kn->kn_sfflags & NOTE_TRIGGER) != 0);
	kn->kn_fflags = kn->kn_sfflags & NOTE_FFLAGSMASK;
	kn->kn_data = kn->kn_sdata;

	return (0);
}

void
filt_userdetach(struct knote *kn)
{
	struct filedesc *fdp = kn->kn_kq->kq_fdp;

	atomic_dec_int(&fdp->fd_nuserevents);
}

int
filt_usermodify(struct kevent *kev, struct knote *kn)
{
	unsigned int ffctrl, fflags;

	if (kev->fflags & NOTE_TRIGGER)
		kn->kn_ptr.p_useract = 1;

	ffctrl = kev->fflags & NOTE_FFCTRLMASK;
	fflags = kev->fflags & NOTE_FFLAGSMASK;
	switch (ffctrl) {
	case NOTE_FFNOP:
		break;
	case NOTE_FFAND:
		kn->kn_fflags &= fflags;
		break;
	case NOTE_FFOR:
		kn->kn_fflags |= fflags;
		break;
	case NOTE_FFCOPY:
		kn->kn_fflags = fflags;
		break;
	default:
		/* ignored, should not happen */
		break;
	}

	kn->kn_data = kev->data;
	kn->kn_udata = kev->udata;

	/* Allow clearing of an activated event. */
	if (kev->flags & EV_CLEAR)
		kn->kn_ptr.p_useract = 0;

	return (kn->kn_ptr.p_useract);
}

int
filt_userprocess(struct knote *kn, struct kevent *kev)
{
	int active;

	active = kn->kn_ptr.p_useract;
	if (active && kev != NULL) {
		*kev = kn->kn_kevent;
		if (kn->kn_flags & EV_CLEAR)
			kn->kn_ptr.p_useract = 0;
	}

	return (active);
}

/*
 * filt_seltrue:
 *
 *	This filter "event" routine simulates seltrue().
 */
int
filt_seltrue(struct knote *kn, long hint)
{

	/*
	 * We don't know how much data can be read/written,
	 * but we know that it *can* be.  This is about as
	 * good as select/poll does as well.
	 */
	kn->kn_data = 0;
	return (1);
}

int
filt_seltruemodify(struct kevent *kev, struct knote *kn)
{
	knote_assign(kev, kn);
	return (kn->kn_fop->f_event(kn, 0));
}

int
filt_seltrueprocess(struct knote *kn, struct kevent *kev)
{
	int active;

	active = kn->kn_fop->f_event(kn, 0);
	if (active)
		knote_submit(kn, kev);
	return (active);
}

/*
 * This provides full kqfilter entry for device switch tables, which
 * has same effect as filter using filt_seltrue() as filter method.
 */
void
filt_seltruedetach(struct knote *kn)
{
	/* Nothing to do */
}

const struct filterops seltrue_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_seltruedetach,
	.f_event	= filt_seltrue,
	.f_modify	= filt_seltruemodify,
	.f_process	= filt_seltrueprocess,
};

int
seltrue_kqfilter(dev_t dev, struct knote *kn)
{
	switch (kn->kn_filter) {
	case EVFILT_READ:
	case EVFILT_WRITE:
		kn->kn_fop = &seltrue_filtops;
		break;
	default:
		return (EINVAL);
	}

	/* Nothing more to do */
	return (0);
}

static int
filt_dead(struct knote *kn, long hint)
{
	if (kn->kn_filter == EVFILT_EXCEPT) {
		/*
		 * Do not deliver event because there is no out-of-band data.
		 * However, let HUP condition pass for poll(2).
		 */
		if ((kn->kn_flags & __EV_POLL) == 0) {
			kn->kn_flags |= EV_DISABLE;
			return (0);
		}
	}

	kn->kn_flags |= (EV_EOF | EV_ONESHOT);
	if (kn->kn_flags & __EV_POLL)
		kn->kn_flags |= __EV_HUP;
	kn->kn_data = 0;
	return (1);
}

static void
filt_deaddetach(struct knote *kn)
{
	/* Nothing to do */
}

const struct filterops dead_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_deaddetach,
	.f_event	= filt_dead,
	.f_modify	= filt_seltruemodify,
	.f_process	= filt_seltrueprocess,
};

static int
filt_badfd(struct knote *kn, long hint)
{
	kn->kn_flags |= (EV_ERROR | EV_ONESHOT);
	kn->kn_data = EBADF;
	return (1);
}

/* For use with kqpoll. */
const struct filterops badfd_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_deaddetach,
	.f_event	= filt_badfd,
	.f_modify	= filt_seltruemodify,
	.f_process	= filt_seltrueprocess,
};

static int
filter_attach(struct knote *kn)
{
	int error;

	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		error = kn->kn_fop->f_attach(kn);
	} else {
		KERNEL_LOCK();
		error = kn->kn_fop->f_attach(kn);
		KERNEL_UNLOCK();
	}
	return (error);
}

static void
filter_detach(struct knote *kn)
{
	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		kn->kn_fop->f_detach(kn);
	} else {
		KERNEL_LOCK();
		kn->kn_fop->f_detach(kn);
		KERNEL_UNLOCK();
	}
}

static int
filter_event(struct knote *kn, long hint)
{
	if ((kn->kn_fop->f_flags & FILTEROP_MPSAFE) == 0)
		KERNEL_ASSERT_LOCKED();

	return (kn->kn_fop->f_event(kn, hint));
}

static int
filter_modify(struct kevent *kev, struct knote *kn)
{
	int active, s;

	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		active = kn->kn_fop->f_modify(kev, kn);
	} else {
		KERNEL_LOCK();
		if (kn->kn_fop->f_modify != NULL) {
			active = kn->kn_fop->f_modify(kev, kn);
		} else {
			s = splhigh();
			active = knote_modify(kev, kn);
			splx(s);
		}
		KERNEL_UNLOCK();
	}
	return (active);
}

static int
filter_process(struct knote *kn, struct kevent *kev)
{
	int active, s;

	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		active = kn->kn_fop->f_process(kn, kev);
	} else {
		KERNEL_LOCK();
		if (kn->kn_fop->f_process != NULL) {
			active = kn->kn_fop->f_process(kn, kev);
		} else {
			s = splhigh();
			active = knote_process(kn, kev);
			splx(s);
		}
		KERNEL_UNLOCK();
	}
	return (active);
}

/*
 * Initialize the current thread for poll/select system call.
 * num indicates the number of serials that the system call may utilize.
 * After this function, the valid range of serials is
 * p_kq_serial <= x < p_kq_serial + num.
 */
void
kqpoll_init(unsigned int num)
{
	struct proc *p = curproc;
	struct filedesc *fdp;

	if (p->p_kq == NULL) {
		p->p_kq = kqueue_alloc(p->p_fd);
		p->p_kq_serial = arc4random();
		fdp = p->p_fd;
		fdplock(fdp);
		LIST_INSERT_HEAD(&fdp->fd_kqlist, p->p_kq, kq_next);
		fdpunlock(fdp);
	}

	if (p->p_kq_serial + num < p->p_kq_serial) {
		/* Serial is about to wrap. Clear all attached knotes. */
		kqueue_purge(p, p->p_kq);
		p->p_kq_serial = 0;
	}
}

/*
 * Finish poll/select system call.
 * num must have the same value that was used with kqpoll_init().
 */
void
kqpoll_done(unsigned int num)
{
	struct proc *p = curproc;
	struct kqueue *kq = p->p_kq;

	KASSERT(p->p_kq != NULL);
	KASSERT(p->p_kq_serial + num >= p->p_kq_serial);

	p->p_kq_serial += num;

	/*
	 * Because of kn_pollid key, a thread can in principle allocate
	 * up to O(maxfiles^2) knotes by calling poll(2) repeatedly
	 * with suitably varying pollfd arrays.
	 * Prevent such a large allocation by clearing knotes eagerly
	 * if there are too many of them.
	 *
	 * A small multiple of kq_knlistsize should give enough margin
	 * that eager clearing is infrequent, or does not happen at all,
	 * with normal programs.
	 * A single pollfd entry can use up to three knotes.
	 * Typically there is no significant overlap of fd and events
	 * between different entries in the pollfd array.
	 */
	if (kq->kq_nknotes > 4 * kq->kq_knlistsize)
		kqueue_purge(p, kq);
}

void
kqpoll_exit(void)
{
	struct proc *p = curproc;

	if (p->p_kq == NULL)
		return;

	kqueue_purge(p, p->p_kq);
	kqueue_terminate(p, p->p_kq);
	KASSERT(p->p_kq->kq_refcnt.r_refs == 1);
	KQRELE(p->p_kq);
	p->p_kq = NULL;
}

struct kqueue *
kqueue_alloc(struct filedesc *fdp)
{
	struct kqueue *kq;

	kq = pool_get(&kqueue_pool, PR_WAITOK | PR_ZERO);
	refcnt_init(&kq->kq_refcnt);
	kq->kq_fdp = fdp;
	TAILQ_INIT(&kq->kq_head);
	mtx_init(&kq->kq_lock, IPL_HIGH);
	task_set(&kq->kq_task, kqueue_task, kq);
	klist_init_mutex(&kq->kq_klist, &kqueue_klist_lock);

	return (kq);
}

int
dokqueue(struct proc *p, int flags, register_t *retval)
{
	struct filedesc *fdp = p->p_fd;
	struct kqueue *kq;
	struct file *fp;
	int cloexec, error, fd;

	cloexec = (flags & O_CLOEXEC) ? UF_EXCLOSE : 0;

	kq = kqueue_alloc(fdp);

	fdplock(fdp);
	error = falloc(p, &fp, &fd);
	if (error)
		goto out;
	fp->f_flag = FREAD | FWRITE | (flags & FNONBLOCK);
	fp->f_type = DTYPE_KQUEUE;
	fp->f_ops = &kqueueops;
	fp->f_data = kq;
	*retval = fd;
	LIST_INSERT_HEAD(&fdp->fd_kqlist, kq, kq_next);
	kq = NULL;
	fdinsert(fdp, fd, cloexec, fp);
	FRELE(fp, p);
out:
	fdpunlock(fdp);
	if (kq != NULL)
		pool_put(&kqueue_pool, kq);
	return (error);
}

int
sys_kqueue(struct proc *p, void *v, register_t *retval)
{
	return (dokqueue(p, 0, retval));
}

int
sys_kqueue1(struct proc *p, void *v, register_t *retval)
{
	struct sys_kqueue1_args /* {
		syscallarg(int)	flags;
	} */ *uap = v;

	if (SCARG(uap, flags) & ~(O_CLOEXEC | FNONBLOCK))
		return (EINVAL);
	return (dokqueue(p, SCARG(uap, flags), retval));
}

int
sys_kevent(struct proc *p, void *v, register_t *retval)
{
	struct kqueue_scan_state scan;
	struct filedesc* fdp = p->p_fd;
	struct sys_kevent_args /* {
		syscallarg(int)	fd;
		syscallarg(const struct kevent *) changelist;
		syscallarg(int)	nchanges;
		syscallarg(struct kevent *) eventlist;
		syscallarg(int)	nevents;
		syscallarg(const struct timespec *) timeout;
	} */ *uap = v;
	struct kevent *kevp;
	struct kqueue *kq;
	struct file *fp;
	struct timespec ts;
	struct timespec *tsp = NULL;
	int i, n, nerrors, error;
	int ready, total;
	struct kevent kev[KQ_NEVENTS];

	if ((fp = fd_getfile(fdp, SCARG(uap, fd))) == NULL)
		return (EBADF);

	if (fp->f_type != DTYPE_KQUEUE) {
		error = EBADF;
		goto done;
	}

	if (SCARG(uap, timeout) != NULL) {
		error = copyin(SCARG(uap, timeout), &ts, sizeof(ts));
		if (error)
			goto done;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrreltimespec(p, &ts);
#endif
		if (ts.tv_sec < 0 || !timespecisvalid(&ts)) {
			error = EINVAL;
			goto done;
		}
		tsp = &ts;
	}

	kq = fp->f_data;
	nerrors = 0;

	while ((n = SCARG(uap, nchanges)) > 0) {
		if (n > nitems(kev))
			n = nitems(kev);
		error = copyin(SCARG(uap, changelist), kev,
		    n * sizeof(struct kevent));
		if (error)
			goto done;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrevent(p, kev, n);
#endif
		for (i = 0; i < n; i++) {
			kevp = &kev[i];
			kevp->flags &= ~EV_SYSFLAGS;
			error = kqueue_register(kq, kevp, 0, p);
			if (error || (kevp->flags & EV_RECEIPT)) {
				if (SCARG(uap, nevents) != 0) {
					kevp->flags = EV_ERROR;
					kevp->data = error;
					copyout(kevp, SCARG(uap, eventlist),
					    sizeof(*kevp));
					SCARG(uap, eventlist)++;
					SCARG(uap, nevents)--;
					nerrors++;
				} else {
					goto done;
				}
			}
		}
		SCARG(uap, nchanges) -= n;
		SCARG(uap, changelist) += n;
	}
	if (nerrors) {
		*retval = nerrors;
		error = 0;
		goto done;
	}

	kqueue_scan_setup(&scan, kq);
	FRELE(fp, p);
	/*
	 * Collect as many events as we can.  The timeout on successive
	 * loops is disabled (kqueue_scan() becomes non-blocking).
	 */
	total = 0;
	error = 0;
	while ((n = SCARG(uap, nevents) - total) > 0) {
		if (n > nitems(kev))
			n = nitems(kev);
		ready = kqueue_scan(&scan, n, kev, tsp, p, &error);
		if (ready == 0)
			break;
		error = copyout(kev, SCARG(uap, eventlist) + total,
		    sizeof(struct kevent) * ready);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrevent(p, kev, ready);
#endif
		total += ready;
		if (error || ready < n)
			break;
	}
	kqueue_scan_finish(&scan);
	*retval = total;
	return (error);

 done:
	FRELE(fp, p);
	return (error);
}

#ifdef KQUEUE_DEBUG
void
kqueue_do_check(struct kqueue *kq, const char *func, int line)
{
	struct knote *kn;
	int count = 0, nmarker = 0;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	TAILQ_FOREACH(kn, &kq->kq_head, kn_tqe) {
		if (kn->kn_filter == EVFILT_MARKER) {
			if ((kn->kn_status & KN_QUEUED) != 0)
				panic("%s:%d: kq=%p kn=%p marker QUEUED",
				    func, line, kq, kn);
			nmarker++;
		} else {
			if ((kn->kn_status & KN_ACTIVE) == 0)
				panic("%s:%d: kq=%p kn=%p knote !ACTIVE",
				    func, line, kq, kn);
			if ((kn->kn_status & KN_QUEUED) == 0)
				panic("%s:%d: kq=%p kn=%p knote !QUEUED",
				    func, line, kq, kn);
			if (kn->kn_kq != kq)
				panic("%s:%d: kq=%p kn=%p kn_kq=%p != kq",
				    func, line, kq, kn, kn->kn_kq);
			count++;
			if (count > kq->kq_count)
				goto bad;
		}
	}
	if (count != kq->kq_count) {
bad:
		panic("%s:%d: kq=%p kq_count=%d count=%d nmarker=%d",
		    func, line, kq, kq->kq_count, count, nmarker);
	}
}
#endif

int
kqueue_register(struct kqueue *kq, struct kevent *kev, unsigned int pollid,
    struct proc *p)
{
	struct filedesc *fdp = kq->kq_fdp;
	const struct filterops *fops = NULL;
	struct file *fp = NULL;
	struct knote *kn = NULL, *newkn = NULL;
	struct knlist *list = NULL;
	int active, error = 0;

	KASSERT(pollid == 0 || (p != NULL && p->p_kq == kq));

	if (kev->filter < 0) {
		if (kev->filter + EVFILT_SYSCOUNT < 0)
			return (EINVAL);
		fops = sysfilt_ops[~kev->filter];	/* to 0-base index */
	}

	if (fops == NULL) {
		/*
		 * XXX
		 * filter attach routine is responsible for ensuring that
		 * the identifier can be attached to it.
		 */
		return (EINVAL);
	}

	if (fops->f_flags & FILTEROP_ISFD) {
		/* validate descriptor */
		if (kev->ident > INT_MAX)
			return (EBADF);
	}

	if (kev->flags & EV_ADD)
		newkn = pool_get(&knote_pool, PR_WAITOK | PR_ZERO);

again:
	if (fops->f_flags & FILTEROP_ISFD) {
		if ((fp = fd_getfile(fdp, kev->ident)) == NULL) {
			error = EBADF;
			goto done;
		}
		mtx_enter(&kq->kq_lock);
		if (kev->flags & EV_ADD)
			kqueue_expand_list(kq, kev->ident);
		if (kev->ident < kq->kq_knlistsize)
			list = &kq->kq_knlist[kev->ident];
	} else {
		mtx_enter(&kq->kq_lock);
		if (kev->flags & EV_ADD)
			kqueue_expand_hash(kq);
		if (kq->kq_knhashmask != 0) {
			list = &kq->kq_knhash[
			    KN_HASH((u_long)kev->ident, kq->kq_knhashmask)];
		}
	}
	if (list != NULL) {
		SLIST_FOREACH(kn, list, kn_link) {
			if (kev->filter == kn->kn_filter &&
			    kev->ident == kn->kn_id &&
			    pollid == kn->kn_pollid) {
				if (!knote_acquire(kn, NULL, 0)) {
					/* knote_acquire() has released
					 * kq_lock. */
					if (fp != NULL) {
						FRELE(fp, p);
						fp = NULL;
					}
					goto again;
				}
				break;
			}
		}
	}
	KASSERT(kn == NULL || (kn->kn_status & KN_PROCESSING) != 0);

	if (kn == NULL && ((kev->flags & EV_ADD) == 0)) {
		mtx_leave(&kq->kq_lock);
		error = ENOENT;
		goto done;
	}

	/*
	 * kn now contains the matching knote, or NULL if no match.
	 */
	if (kev->flags & EV_ADD) {
		if (kn == NULL) {
			kn = newkn;
			newkn = NULL;
			kn->kn_status = KN_PROCESSING;
			kn->kn_fp = fp;
			kn->kn_kq = kq;
			kn->kn_fop = fops;

			/*
			 * apply reference count to knote structure, and
			 * do not release it at the end of this routine.
			 */
			fp = NULL;

			kn->kn_sfflags = kev->fflags;
			kn->kn_sdata = kev->data;
			kev->fflags = 0;
			kev->data = 0;
			kn->kn_kevent = *kev;
			kn->kn_pollid = pollid;

			knote_attach(kn);
			mtx_leave(&kq->kq_lock);

			error = filter_attach(kn);
			if (error != 0) {
				knote_drop(kn, p);
				goto done;
			}

			/*
			 * If this is a file descriptor filter, check if
			 * fd was closed while the knote was being added.
			 * knote_fdclose() has missed kn if the function
			 * ran before kn appeared in kq_knlist.
			 */
			if ((fops->f_flags & FILTEROP_ISFD) &&
			    fd_checkclosed(fdp, kev->ident, kn->kn_fp)) {
				/*
				 * Drop the knote silently without error
				 * because another thread might already have
				 * seen it. This corresponds to the insert
				 * happening in full before the close.
				 */
				filter_detach(kn);
				knote_drop(kn, p);
				goto done;
			}

			/* Check if there is a pending event. */
			active = filter_process(kn, NULL);
			mtx_enter(&kq->kq_lock);
			if (active)
				knote_activate(kn);
		} else if (kn->kn_fop == &badfd_filtops) {
			/*
			 * Nothing expects this badfd knote any longer.
			 * Drop it to make room for the new knote and retry.
			 */
			KASSERT(kq == p->p_kq);
			mtx_leave(&kq->kq_lock);
			filter_detach(kn);
			knote_drop(kn, p);

			KASSERT(fp != NULL);
			FRELE(fp, p);
			fp = NULL;

			goto again;
		} else {
			/*
			 * The user may change some filter values after the
			 * initial EV_ADD, but doing so will not reset any
			 * filters which have already been triggered.
			 */
			mtx_leave(&kq->kq_lock);
			active = filter_modify(kev, kn);
			mtx_enter(&kq->kq_lock);
			if (active)
				knote_activate(kn);
			if (kev->flags & EV_ERROR) {
				error = kev->data;
				goto release;
			}
		}
	} else if (kev->flags & EV_DELETE) {
		mtx_leave(&kq->kq_lock);
		filter_detach(kn);
		knote_drop(kn, p);
		goto done;
	} else if (kn->kn_fop == &user_filtops) {
		/* Call f_modify to allow NOTE_TRIGGER without EV_ADD. */
		mtx_leave(&kq->kq_lock);
		active = filter_modify(kev, kn);
		mtx_enter(&kq->kq_lock);
		if (active)
			knote_activate(kn);
		if (kev->flags & EV_ERROR) {
			error = kev->data;
			goto release;
		}
	}

	if ((kev->flags & EV_DISABLE) && ((kn->kn_status & KN_DISABLED) == 0))
		kn->kn_status |= KN_DISABLED;

	if ((kev->flags & EV_ENABLE) && (kn->kn_status & KN_DISABLED)) {
		kn->kn_status &= ~KN_DISABLED;
		mtx_leave(&kq->kq_lock);
		/* Check if there is a pending event. */
		active = filter_process(kn, NULL);
		mtx_enter(&kq->kq_lock);
		if (active)
			knote_activate(kn);
	}

release:
	knote_release(kn);
	mtx_leave(&kq->kq_lock);
done:
	if (fp != NULL)
		FRELE(fp, p);
	if (newkn != NULL)
		pool_put(&knote_pool, newkn);
	return (error);
}

int
kqueue_sleep(struct kqueue *kq, struct timespec *tsp)
{
	struct timespec elapsed, start, stop;
	uint64_t nsecs;
	int error;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	if (tsp != NULL) {
		getnanouptime(&start);
		nsecs = MIN(TIMESPEC_TO_NSEC(tsp), MAXTSLP);
	} else
		nsecs = INFSLP;
	error = msleep_nsec(kq, &kq->kq_lock, PSOCK | PCATCH | PNORELOCK,
	    "kqread", nsecs);
	if (tsp != NULL) {
		getnanouptime(&stop);
		timespecsub(&stop, &start, &elapsed);
		timespecsub(tsp, &elapsed, tsp);
		if (tsp->tv_sec < 0)
			timespecclear(tsp);
	}

	return (error);
}

/*
 * Scan the kqueue, blocking if necessary until the target time is reached.
 * If tsp is NULL we block indefinitely.  If tsp->ts_secs/nsecs are both
 * 0 we do not block at all.
 */
int
kqueue_scan(struct kqueue_scan_state *scan, int maxevents,
    struct kevent *kevp, struct timespec *tsp, struct proc *p, int *errorp)
{
	struct kqueue *kq = scan->kqs_kq;
	struct knote *kn;
	int error = 0, nkev = 0;
	int reinserted;

	if (maxevents == 0)
		goto done;
retry:
	KASSERT(nkev == 0);

	error = 0;
	reinserted = 0;

	mtx_enter(&kq->kq_lock);

	if (kq->kq_state & KQ_DYING) {
		mtx_leave(&kq->kq_lock);
		error = EBADF;
		goto done;
	}

	if (kq->kq_count == 0) {
		/*
		 * Successive loops are only necessary if there are more
		 * ready events to gather, so they don't need to block.
		 */
		if ((tsp != NULL && !timespecisset(tsp)) ||
		    scan->kqs_nevent != 0) {
			mtx_leave(&kq->kq_lock);
			error = 0;
			goto done;
		}
		kq->kq_state |= KQ_SLEEP;
		error = kqueue_sleep(kq, tsp);
		/* kqueue_sleep() has released kq_lock. */
		if (error == 0 || error == EWOULDBLOCK)
			goto retry;
		/* don't restart after signals... */
		if (error == ERESTART)
			error = EINTR;
		goto done;
	}

	/*
	 * Put the end marker in the queue to limit the scan to the events
	 * that are currently active.  This prevents events from being
	 * recollected if they reactivate during scan.
	 *
	 * If a partial scan has been performed already but no events have
	 * been collected, reposition the end marker to make any new events
	 * reachable.
	 */
	if (!scan->kqs_queued) {
		TAILQ_INSERT_TAIL(&kq->kq_head, &scan->kqs_end, kn_tqe);
		scan->kqs_queued = 1;
	} else if (scan->kqs_nevent == 0) {
		TAILQ_REMOVE(&kq->kq_head, &scan->kqs_end, kn_tqe);
		TAILQ_INSERT_TAIL(&kq->kq_head, &scan->kqs_end, kn_tqe);
	}

	TAILQ_INSERT_HEAD(&kq->kq_head, &scan->kqs_start, kn_tqe);
	while (nkev < maxevents) {
		kn = TAILQ_NEXT(&scan->kqs_start, kn_tqe);
		if (kn->kn_filter == EVFILT_MARKER) {
			if (kn == &scan->kqs_end)
				break;

			/* Move start marker past another thread's marker. */
			TAILQ_REMOVE(&kq->kq_head, &scan->kqs_start, kn_tqe);
			TAILQ_INSERT_AFTER(&kq->kq_head, kn, &scan->kqs_start,
			    kn_tqe);
			continue;
		}

		if (!knote_acquire(kn, NULL, 0)) {
			/* knote_acquire() has released kq_lock. */
			mtx_enter(&kq->kq_lock);
			continue;
		}

		kqueue_check(kq);
		TAILQ_REMOVE(&kq->kq_head, kn, kn_tqe);
		kn->kn_status &= ~KN_QUEUED;
		kq->kq_count--;
		kqueue_check(kq);

		if (kn->kn_status & KN_DISABLED) {
			knote_release(kn);
			continue;
		}

		mtx_leave(&kq->kq_lock);

		/* Drop expired kqpoll knotes. */
		if (p->p_kq == kq &&
		    p->p_kq_serial > (unsigned long)kn->kn_udata) {
			filter_detach(kn);
			knote_drop(kn, p);
			mtx_enter(&kq->kq_lock);
			continue;
		}

		/*
		 * Invalidate knotes whose vnodes have been revoked.
		 * This is a workaround; it is tricky to clear existing
		 * knotes and prevent new ones from being registered
		 * with the current revocation mechanism.
		 */
		if ((kn->kn_fop->f_flags & FILTEROP_ISFD) &&
		    kn->kn_fp != NULL &&
		    kn->kn_fp->f_type == DTYPE_VNODE) {
			struct vnode *vp = kn->kn_fp->f_data;

			if (__predict_false(vp->v_op == &dead_vops &&
			    kn->kn_fop != &dead_filtops)) {
				filter_detach(kn);
				kn->kn_fop = &dead_filtops;

				/*
				 * Check if the event should be delivered.
				 * Use f_event directly because this is
				 * a special situation.
				 */
				if (kn->kn_fop->f_event(kn, 0) == 0) {
					filter_detach(kn);
					knote_drop(kn, p);
					mtx_enter(&kq->kq_lock);
					continue;
				}
			}
		}

		memset(kevp, 0, sizeof(*kevp));
		if (filter_process(kn, kevp) == 0) {
			mtx_enter(&kq->kq_lock);
			if ((kn->kn_status & KN_QUEUED) == 0)
				kn->kn_status &= ~KN_ACTIVE;
			knote_release(kn);
			kqueue_check(kq);
			continue;
		}

		/*
		 * Post-event action on the note
		 */
		if (kevp->flags & EV_ONESHOT) {
			filter_detach(kn);
			knote_drop(kn, p);
			mtx_enter(&kq->kq_lock);
		} else if (kevp->flags & (EV_CLEAR | EV_DISPATCH)) {
			mtx_enter(&kq->kq_lock);
			if (kevp->flags & EV_DISPATCH)
				kn->kn_status |= KN_DISABLED;
			if ((kn->kn_status & KN_QUEUED) == 0)
				kn->kn_status &= ~KN_ACTIVE;
			knote_release(kn);
		} else {
			mtx_enter(&kq->kq_lock);
			if ((kn->kn_status & KN_QUEUED) == 0) {
				kqueue_check(kq);
				kq->kq_count++;
				kn->kn_status |= KN_QUEUED;
				TAILQ_INSERT_TAIL(&kq->kq_head, kn, kn_tqe);
				/* Wakeup is done after loop. */
				reinserted = 1;
			}
			knote_release(kn);
		}
		kqueue_check(kq);

		kevp++;
		nkev++;
		scan->kqs_nevent++;
	}
	TAILQ_REMOVE(&kq->kq_head, &scan->kqs_start, kn_tqe);
	if (reinserted && kq->kq_count != 0)
		kqueue_wakeup(kq);
	mtx_leave(&kq->kq_lock);
	if (scan->kqs_nevent == 0)
		goto retry;
done:
	*errorp = error;
	return (nkev);
}

void
kqueue_scan_setup(struct kqueue_scan_state *scan, struct kqueue *kq)
{
	memset(scan, 0, sizeof(*scan));

	KQREF(kq);
	scan->kqs_kq = kq;
	scan->kqs_start.kn_filter = EVFILT_MARKER;
	scan->kqs_start.kn_status = KN_PROCESSING;
	scan->kqs_end.kn_filter = EVFILT_MARKER;
	scan->kqs_end.kn_status = KN_PROCESSING;
}

void
kqueue_scan_finish(struct kqueue_scan_state *scan)
{
	struct kqueue *kq = scan->kqs_kq;

	KASSERT(scan->kqs_start.kn_filter == EVFILT_MARKER);
	KASSERT(scan->kqs_start.kn_status == KN_PROCESSING);
	KASSERT(scan->kqs_end.kn_filter == EVFILT_MARKER);
	KASSERT(scan->kqs_end.kn_status == KN_PROCESSING);

	if (scan->kqs_queued) {
		scan->kqs_queued = 0;
		mtx_enter(&kq->kq_lock);
		TAILQ_REMOVE(&kq->kq_head, &scan->kqs_end, kn_tqe);
		mtx_leave(&kq->kq_lock);
	}
	KQRELE(kq);
}

/*
 * XXX
 * This could be expanded to call kqueue_scan, if desired.
 */
int
kqueue_read(struct file *fp, struct uio *uio, int fflags)
{
	return (ENXIO);
}

int
kqueue_write(struct file *fp, struct uio *uio, int fflags)
{
	return (ENXIO);
}

int
kqueue_ioctl(struct file *fp, u_long com, caddr_t data, struct proc *p)
{
	return (ENOTTY);
}

int
kqueue_stat(struct file *fp, struct stat *st, struct proc *p)
{
	struct kqueue *kq = fp->f_data;

	memset(st, 0, sizeof(*st));
	st->st_size = kq->kq_count;	/* unlocked read */
	st->st_blksize = sizeof(struct kevent);
	st->st_mode = S_IFIFO;
	return (0);
}

void
kqueue_purge(struct proc *p, struct kqueue *kq)
{
	int i;

	mtx_enter(&kq->kq_lock);
	for (i = 0; i < kq->kq_knlistsize; i++)
		knote_remove(p, kq, &kq->kq_knlist, i, 1);
	if (kq->kq_knhashmask != 0) {
		for (i = 0; i < kq->kq_knhashmask + 1; i++)
			knote_remove(p, kq, &kq->kq_knhash, i, 1);
	}
	mtx_leave(&kq->kq_lock);
}

void
kqueue_terminate(struct proc *p, struct kqueue *kq)
{
	struct knote *kn;
	int state;

	mtx_enter(&kq->kq_lock);

	/*
	 * Any remaining entries should be scan markers.
	 * They are removed when the ongoing scans finish.
	 */
	KASSERT(kq->kq_count == 0);
	TAILQ_FOREACH(kn, &kq->kq_head, kn_tqe)
		KASSERT(kn->kn_filter == EVFILT_MARKER);

	kq->kq_state |= KQ_DYING;
	state = kq->kq_state;
	kqueue_wakeup(kq);
	mtx_leave(&kq->kq_lock);

	/*
	 * Any knotes that were attached to this kqueue were deleted
	 * by knote_fdclose() when this kqueue's file descriptor was closed.
	 */
	KASSERT(klist_empty(&kq->kq_klist));
	if (state & KQ_TASK)
		taskq_del_barrier(systqmp, &kq->kq_task);
}

int
kqueue_close(struct file *fp, struct proc *p)
{
	struct kqueue *kq = fp->f_data;

	fp->f_data = NULL;

	kqueue_purge(p, kq);
	kqueue_terminate(p, kq);

	KQRELE(kq);

	return (0);
}

static void
kqueue_task(void *arg)
{
	struct kqueue *kq = arg;

	knote(&kq->kq_klist, 0);
}

void
kqueue_wakeup(struct kqueue *kq)
{
	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	if (kq->kq_state & KQ_SLEEP) {
		kq->kq_state &= ~KQ_SLEEP;
		wakeup(kq);
	}
	if (!klist_empty(&kq->kq_klist)) {
		/* Defer activation to avoid recursion. */
		kq->kq_state |= KQ_TASK;
		task_add(systqmp, &kq->kq_task);
	}
}

static void
kqueue_expand_hash(struct kqueue *kq)
{
	struct knlist *hash;
	u_long hashmask;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	if (kq->kq_knhashmask == 0) {
		mtx_leave(&kq->kq_lock);
		hash = hashinit(KN_HASHSIZE, M_KEVENT, M_WAITOK, &hashmask);
		mtx_enter(&kq->kq_lock);
		if (kq->kq_knhashmask == 0) {
			kq->kq_knhash = hash;
			kq->kq_knhashmask = hashmask;
		} else {
			/* Another thread has allocated the hash. */
			mtx_leave(&kq->kq_lock);
			hashfree(hash, KN_HASHSIZE, M_KEVENT);
			mtx_enter(&kq->kq_lock);
		}
	}
}

static void
kqueue_expand_list(struct kqueue *kq, int fd)
{
	struct knlist *list, *olist;
	int size, osize;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	if (kq->kq_knlistsize <= fd) {
		size = kq->kq_knlistsize;
		mtx_leave(&kq->kq_lock);
		while (size <= fd)
			size += KQEXTENT;
		list = mallocarray(size, sizeof(*list), M_KEVENT, M_WAITOK);
		mtx_enter(&kq->kq_lock);
		if (kq->kq_knlistsize <= fd) {
			memcpy(list, kq->kq_knlist,
			    kq->kq_knlistsize * sizeof(*list));
			memset(&list[kq->kq_knlistsize], 0,
			    (size - kq->kq_knlistsize) * sizeof(*list));
			olist = kq->kq_knlist;
			osize = kq->kq_knlistsize;
			kq->kq_knlist = list;
			kq->kq_knlistsize = size;
			mtx_leave(&kq->kq_lock);
			free(olist, M_KEVENT, osize * sizeof(*list));
			mtx_enter(&kq->kq_lock);
		} else {
			/* Another thread has expanded the list. */
			mtx_leave(&kq->kq_lock);
			free(list, M_KEVENT, size * sizeof(*list));
			mtx_enter(&kq->kq_lock);
		}
	}
}

/*
 * Acquire a knote, return non-zero on success, 0 on failure.
 *
 * If we cannot acquire the knote we sleep and return 0.  The knote
 * may be stale on return in this case and the caller must restart
 * whatever loop they are in.
 *
 * If we are about to sleep and klist is non-NULL, the list is unlocked
 * before sleep and remains unlocked on return.
 */
int
knote_acquire(struct knote *kn, struct klist *klist, int ls)
{
	struct kqueue *kq = kn->kn_kq;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);
	KASSERT(kn->kn_filter != EVFILT_MARKER);

	if (kn->kn_status & KN_PROCESSING) {
		kn->kn_status |= KN_WAITING;
		if (klist != NULL) {
			mtx_leave(&kq->kq_lock);
			klist_unlock(klist, ls);
			/* XXX Timeout resolves potential loss of wakeup. */
			tsleep_nsec(kn, 0, "kqepts", SEC_TO_NSEC(1));
		} else {
			msleep_nsec(kn, &kq->kq_lock, PNORELOCK, "kqepts",
			    SEC_TO_NSEC(1));
		}
		/* knote may be stale now */
		return (0);
	}
	kn->kn_status |= KN_PROCESSING;
	return (1);
}

/*
 * Release an acquired knote, clearing KN_PROCESSING.
 */
void
knote_release(struct knote *kn)
{
	MUTEX_ASSERT_LOCKED(&kn->kn_kq->kq_lock);
	KASSERT(kn->kn_filter != EVFILT_MARKER);
	KASSERT(kn->kn_status & KN_PROCESSING);

	if (kn->kn_status & KN_WAITING) {
		kn->kn_status &= ~KN_WAITING;
		wakeup(kn);
	}
	kn->kn_status &= ~KN_PROCESSING;
	/* kn should not be accessed anymore */
}

/*
 * activate one knote.
 */
void
knote_activate(struct knote *kn)
{
	MUTEX_ASSERT_LOCKED(&kn->kn_kq->kq_lock);

	kn->kn_status |= KN_ACTIVE;
	if ((kn->kn_status & (KN_QUEUED | KN_DISABLED)) == 0)
		knote_enqueue(kn);
}

/*
 * walk down a list of knotes, activating them if their event has triggered.
 */
void
knote(struct klist *list, long hint)
{
	int ls;

	ls = klist_lock(list);
	knote_locked(list, hint);
	klist_unlock(list, ls);
}

void
knote_locked(struct klist *list, long hint)
{
	struct knote *kn, *kn0;
	struct kqueue *kq;

	KLIST_ASSERT_LOCKED(list);

	SLIST_FOREACH_SAFE(kn, &list->kl_list, kn_selnext, kn0) {
		if (filter_event(kn, hint)) {
			kq = kn->kn_kq;
			mtx_enter(&kq->kq_lock);
			knote_activate(kn);
			mtx_leave(&kq->kq_lock);
		}
	}
}

/*
 * remove all knotes from a specified knlist
 */
void
knote_remove(struct proc *p, struct kqueue *kq, struct knlist **plist, int idx,
    int purge)
{
	struct knote *kn;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);

	/* Always fetch array pointer as another thread can resize kq_knlist. */
	while ((kn = SLIST_FIRST(*plist + idx)) != NULL) {
		KASSERT(kn->kn_kq == kq);

		if (!purge) {
			/* Skip pending badfd knotes. */
			while (kn->kn_fop == &badfd_filtops) {
				kn = SLIST_NEXT(kn, kn_link);
				if (kn == NULL)
					return;
				KASSERT(kn->kn_kq == kq);
			}
		}

		if (!knote_acquire(kn, NULL, 0)) {
			/* knote_acquire() has released kq_lock. */
			mtx_enter(&kq->kq_lock);
			continue;
		}
		mtx_leave(&kq->kq_lock);
		filter_detach(kn);

		/*
		 * Notify poll(2) and select(2) when a monitored
		 * file descriptor is closed.
		 *
		 * This reuses the original knote for delivering the
		 * notification so as to avoid allocating memory.
		 */
		if (!purge && (kn->kn_flags & (__EV_POLL | __EV_SELECT)) &&
		    !(p->p_kq == kq &&
		      p->p_kq_serial > (unsigned long)kn->kn_udata) &&
		    kn->kn_fop != &badfd_filtops) {
			KASSERT(kn->kn_fop->f_flags & FILTEROP_ISFD);
			FRELE(kn->kn_fp, p);
			kn->kn_fp = NULL;

			kn->kn_fop = &badfd_filtops;
			filter_event(kn, 0);
			mtx_enter(&kq->kq_lock);
			knote_activate(kn);
			knote_release(kn);
			continue;
		}

		knote_drop(kn, p);
		mtx_enter(&kq->kq_lock);
	}
}

/*
 * remove all knotes referencing a specified fd
 */
void
knote_fdclose(struct proc *p, int fd)
{
	struct filedesc *fdp = p->p_p->ps_fd;
	struct kqueue *kq;

	/*
	 * fdplock can be ignored if the file descriptor table is being freed
	 * because no other thread can access the fdp.
	 */
	if (fdp->fd_refcnt != 0)
		fdpassertlocked(fdp);

	LIST_FOREACH(kq, &fdp->fd_kqlist, kq_next) {
		mtx_enter(&kq->kq_lock);
		if (fd < kq->kq_knlistsize)
			knote_remove(p, kq, &kq->kq_knlist, fd, 0);
		mtx_leave(&kq->kq_lock);
	}
}

/*
 * handle a process exiting, including the triggering of NOTE_EXIT notes
 * XXX this could be more efficient, doing a single pass down the klist
 */
void
knote_processexit(struct process *pr)
{
	/* this needs both the ps_mtx and exclusive kqueue_ps_list_lock. */
	rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	knote_locked(&pr->ps_klist, NOTE_EXIT);
	mtx_leave(&pr->ps_mtx);
	rw_exit_write(&kqueue_ps_list_lock);

	/* remove other knotes hanging off the process */
	klist_invalidate(&pr->ps_klist);
}

void
knote_processfork(struct process *pr, pid_t pid)
{
	/* this needs both the ps_mtx and exclusive kqueue_ps_list_lock. */
	rw_enter_write(&kqueue_ps_list_lock);
	mtx_enter(&pr->ps_mtx);
	knote_locked(&pr->ps_klist, NOTE_FORK | pid);
	mtx_leave(&pr->ps_mtx);
	rw_exit_write(&kqueue_ps_list_lock);
}

void
knote_attach(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;
	struct knlist *list;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);
	KASSERT(kn->kn_status & KN_PROCESSING);

	if (kn->kn_fop->f_flags & FILTEROP_ISFD) {
		KASSERT(kq->kq_knlistsize > kn->kn_id);
		list = &kq->kq_knlist[kn->kn_id];
	} else {
		KASSERT(kq->kq_knhashmask != 0);
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];
	}
	SLIST_INSERT_HEAD(list, kn, kn_link);
	kq->kq_nknotes++;
}

void
knote_detach(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;
	struct knlist *list;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);
	KASSERT(kn->kn_status & KN_PROCESSING);

	kq->kq_nknotes--;
	if (kn->kn_fop->f_flags & FILTEROP_ISFD)
		list = &kq->kq_knlist[kn->kn_id];
	else
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];
	SLIST_REMOVE(list, kn, knote, kn_link);
}

/*
 * should be called at spl == 0, since we don't want to hold spl
 * while calling FRELE and pool_put.
 */
void
knote_drop(struct knote *kn, struct proc *p)
{
	struct kqueue *kq = kn->kn_kq;

	KASSERT(kn->kn_filter != EVFILT_MARKER);

	mtx_enter(&kq->kq_lock);
	knote_detach(kn);
	if (kn->kn_status & KN_QUEUED)
		knote_dequeue(kn);
	if (kn->kn_status & KN_WAITING) {
		kn->kn_status &= ~KN_WAITING;
		wakeup(kn);
	}
	mtx_leave(&kq->kq_lock);

	if ((kn->kn_fop->f_flags & FILTEROP_ISFD) && kn->kn_fp != NULL)
		FRELE(kn->kn_fp, p);
	pool_put(&knote_pool, kn);
}


void
knote_enqueue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);
	KASSERT(kn->kn_filter != EVFILT_MARKER);
	KASSERT((kn->kn_status & KN_QUEUED) == 0);

	kqueue_check(kq);
	TAILQ_INSERT_TAIL(&kq->kq_head, kn, kn_tqe);
	kn->kn_status |= KN_QUEUED;
	kq->kq_count++;
	kqueue_check(kq);
	kqueue_wakeup(kq);
}

void
knote_dequeue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	MUTEX_ASSERT_LOCKED(&kq->kq_lock);
	KASSERT(kn->kn_filter != EVFILT_MARKER);
	KASSERT(kn->kn_status & KN_QUEUED);

	kqueue_check(kq);
	TAILQ_REMOVE(&kq->kq_head, kn, kn_tqe);
	kn->kn_status &= ~KN_QUEUED;
	kq->kq_count--;
	kqueue_check(kq);
}

/*
 * Assign parameters to the knote.
 *
 * The knote's object lock must be held.
 */
void
knote_assign(const struct kevent *kev, struct knote *kn)
{
	if ((kn->kn_fop->f_flags & FILTEROP_MPSAFE) == 0)
		KERNEL_ASSERT_LOCKED();

	kn->kn_sfflags = kev->fflags;
	kn->kn_sdata = kev->data;
	kn->kn_udata = kev->udata;
}

/*
 * Submit the knote's event for delivery.
 *
 * The knote's object lock must be held.
 */
void
knote_submit(struct knote *kn, struct kevent *kev)
{
	if ((kn->kn_fop->f_flags & FILTEROP_MPSAFE) == 0)
		KERNEL_ASSERT_LOCKED();

	if (kev != NULL) {
		*kev = kn->kn_kevent;
		if (kn->kn_flags & EV_CLEAR) {
			kn->kn_fflags = 0;
			kn->kn_data = 0;
		}
	}
}

void
klist_init(struct klist *klist, const struct klistops *ops, void *arg)
{
	SLIST_INIT(&klist->kl_list);
	klist->kl_ops = ops;
	klist->kl_arg = arg;
}

void
klist_free(struct klist *klist)
{
	KASSERT(SLIST_EMPTY(&klist->kl_list));
}

void
klist_insert(struct klist *klist, struct knote *kn)
{
	int ls;

	ls = klist_lock(klist);
	SLIST_INSERT_HEAD(&klist->kl_list, kn, kn_selnext);
	klist_unlock(klist, ls);
}

void
klist_insert_locked(struct klist *klist, struct knote *kn)
{
	KLIST_ASSERT_LOCKED(klist);

	SLIST_INSERT_HEAD(&klist->kl_list, kn, kn_selnext);
}

void
klist_remove(struct klist *klist, struct knote *kn)
{
	int ls;

	ls = klist_lock(klist);
	SLIST_REMOVE(&klist->kl_list, kn, knote, kn_selnext);
	klist_unlock(klist, ls);
}

void
klist_remove_locked(struct klist *klist, struct knote *kn)
{
	KLIST_ASSERT_LOCKED(klist);

	SLIST_REMOVE(&klist->kl_list, kn, knote, kn_selnext);
}

/*
 * Detach all knotes from klist. The knotes are rewired to indicate EOF.
 *
 * The caller of this function must not hold any locks that can block
 * filterops callbacks that run with KN_PROCESSING.
 * Otherwise this function might deadlock.
 */
void
klist_invalidate(struct klist *list)
{
	struct knote *kn;
	struct kqueue *kq;
	struct proc *p = curproc;
	int ls;

	NET_ASSERT_UNLOCKED();

	ls = klist_lock(list);
	while ((kn = SLIST_FIRST(&list->kl_list)) != NULL) {
		kq = kn->kn_kq;
		mtx_enter(&kq->kq_lock);
		if (!knote_acquire(kn, list, ls)) {
			/* knote_acquire() has released kq_lock
			 * and klist lock. */
			ls = klist_lock(list);
			continue;
		}
		mtx_leave(&kq->kq_lock);
		klist_unlock(list, ls);
		filter_detach(kn);
		if (kn->kn_fop->f_flags & FILTEROP_ISFD) {
			kn->kn_fop = &dead_filtops;
			filter_event(kn, 0);
			mtx_enter(&kq->kq_lock);
			knote_activate(kn);
			knote_release(kn);
			mtx_leave(&kq->kq_lock);
		} else {
			knote_drop(kn, p);
		}
		ls = klist_lock(list);
	}
	klist_unlock(list, ls);
}

static int
klist_lock(struct klist *list)
{
	int ls = 0;

	if (list->kl_ops != NULL) {
		ls = list->kl_ops->klo_lock(list->kl_arg);
	} else {
		KERNEL_LOCK();
		ls = splhigh();
	}
	return ls;
}

static void
klist_unlock(struct klist *list, int ls)
{
	if (list->kl_ops != NULL) {
		list->kl_ops->klo_unlock(list->kl_arg, ls);
	} else {
		splx(ls);
		KERNEL_UNLOCK();
	}
}

static void
klist_mutex_assertlk(void *arg)
{
	struct mutex *mtx = arg;

	(void)mtx;

	MUTEX_ASSERT_LOCKED(mtx);
}

static int
klist_mutex_lock(void *arg)
{
	struct mutex *mtx = arg;

	mtx_enter(mtx);
	return 0;
}

static void
klist_mutex_unlock(void *arg, int s)
{
	struct mutex *mtx = arg;

	mtx_leave(mtx);
}

static const struct klistops mutex_klistops = {
	.klo_assertlk	= klist_mutex_assertlk,
	.klo_lock	= klist_mutex_lock,
	.klo_unlock	= klist_mutex_unlock,
};

void
klist_init_mutex(struct klist *klist, struct mutex *mtx)
{
	klist_init(klist, &mutex_klistops, mtx);
}

static void
klist_rwlock_assertlk(void *arg)
{
	struct rwlock *rwl = arg;

	(void)rwl;

	rw_assert_wrlock(rwl);
}

static int
klist_rwlock_lock(void *arg)
{
	struct rwlock *rwl = arg;

	rw_enter_write(rwl);
	return 0;
}

static void
klist_rwlock_unlock(void *arg, int s)
{
	struct rwlock *rwl = arg;

	rw_exit_write(rwl);
}

static const struct klistops rwlock_klistops = {
	.klo_assertlk	= klist_rwlock_assertlk,
	.klo_lock	= klist_rwlock_lock,
	.klo_unlock	= klist_rwlock_unlock,
};

void
klist_init_rwlock(struct klist *klist, struct rwlock *rwl)
{
	klist_init(klist, &rwlock_klistops, rwl);
}

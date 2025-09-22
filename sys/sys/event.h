/*	$OpenBSD: event.h,v 1.74 2025/05/10 09:44:39 visa Exp $	*/

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
 *	$FreeBSD: src/sys/sys/event.h,v 1.11 2001/02/24 01:41:31 jlemon Exp $
 */

#ifndef _SYS_EVENT_H_
#define _SYS_EVENT_H_

#define EVFILT_READ		(-1)
#define EVFILT_WRITE		(-2)
#define EVFILT_AIO		(-3)	/* attached to aio requests */
#define EVFILT_VNODE		(-4)	/* attached to vnodes */
#define EVFILT_PROC		(-5)	/* attached to struct process */
#define EVFILT_SIGNAL		(-6)	/* attached to struct process */
#define EVFILT_TIMER		(-7)	/* timers */
#define EVFILT_DEVICE		(-8)	/* devices */
#define EVFILT_EXCEPT		(-9)	/* exceptional conditions */
#define EVFILT_USER		(-10)	/* user event */

#define EVFILT_SYSCOUNT		10

#define EV_SET(kevp, a, b, c, d, e, f) do {	\
	struct kevent *__kevp = (kevp);		\
	(__kevp)->ident = (a);			\
	(__kevp)->filter = (b);			\
	(__kevp)->flags = (c);			\
	(__kevp)->fflags = (d);			\
	(__kevp)->data = (e);			\
	(__kevp)->udata = (f);			\
} while(0)

struct kevent {
	__uintptr_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	unsigned short	flags;		/* action flags for kqueue */
	unsigned int	fflags;		/* filter flag value */
	__int64_t	data;		/* filter data value */
	void		*udata;		/* opaque user data identifier */
};

/* actions */
#define EV_ADD		0x0001		/* add event to kq (implies enable) */
#define EV_DELETE	0x0002		/* delete event from kq */
#define EV_ENABLE	0x0004		/* enable event */
#define EV_DISABLE	0x0008		/* disable event (not reported) */

/* flags */
#define EV_ONESHOT	0x0010		/* only report one occurrence */
#define EV_CLEAR	0x0020		/* clear event state after reporting */
#define EV_RECEIPT	0x0040          /* force EV_ERROR on success, data=0 */
#define EV_DISPATCH	0x0080          /* disable event after reporting */

#define EV_SYSFLAGS	0xf800		/* reserved by system */
#define EV_FLAG1	0x2000		/* filter-specific flag */

/* returned values */
#define EV_EOF		0x8000		/* EOF detected */
#define EV_ERROR	0x4000		/* error, data contains errno */

/*
 * data/hint flags for EVFILT_{READ|WRITE}, shared with userspace
 */
#define NOTE_LOWAT	0x0001			/* low water mark */
#define NOTE_EOF	0x0002			/* return on EOF */

/*
 * data/hint flags for EVFILT_EXCEPT, shared with userspace and with
 * EVFILT_{READ|WRITE}
 */
#define NOTE_OOB	0x0004			/* OOB data on a socket */

/*
 * data/hint flags for EVFILT_VNODE, shared with userspace
 */
#define	NOTE_DELETE	0x0001			/* vnode was removed */
#define	NOTE_WRITE	0x0002			/* data contents changed */
#define	NOTE_EXTEND	0x0004			/* size increased */
#define	NOTE_ATTRIB	0x0008			/* attributes changed */
#define	NOTE_LINK	0x0010			/* link count changed */
#define	NOTE_RENAME	0x0020			/* vnode was renamed */
#define	NOTE_REVOKE	0x0040			/* vnode access was revoked */
#define	NOTE_TRUNCATE   0x0080			/* vnode was truncated */

/*
 * data/hint flags for EVFILT_PROC, shared with userspace
 */
#define	NOTE_EXIT	0x80000000		/* process exited */
#define	NOTE_FORK	0x40000000		/* process forked */
#define	NOTE_EXEC	0x20000000		/* process exec'd */
#define	NOTE_PCTRLMASK	0xf0000000		/* mask for hint bits */
#define	NOTE_PDATAMASK	0x000fffff		/* mask for pid */

/* additional flags for EVFILT_PROC */
#define	NOTE_TRACK	0x00000001		/* follow across forks */
#define	NOTE_TRACKERR	0x00000002		/* could not track child */
#define	NOTE_CHILD	0x00000004		/* am a child process */

/* data/hint flags for EVFILT_DEVICE, shared with userspace */
#define NOTE_CHANGE	0x00000001		/* device change event */

/* additional flags for EVFILT_TIMER */
#define NOTE_MSECONDS	0x00000000		/* data is milliseconds */
#define NOTE_SECONDS	0x00000001		/* data is seconds */
#define NOTE_USECONDS	0x00000002		/* data is microseconds */
#define NOTE_NSECONDS	0x00000003		/* data is nanoseconds */
#define NOTE_ABSTIME	0x00000010		/* timeout is absolute */

/*
 * data/hint flags for EVFILT_USER, shared with userspace
 */
#define NOTE_FFNOP	0x00000000		/* ignore input fflags */
#define NOTE_FFAND	0x40000000		/* AND fflags */
#define NOTE_FFOR	0x80000000		/* OR fflags */
#define NOTE_FFCOPY	0xc0000000		/* copy fflags */

#define NOTE_FFCTRLMASK	0xc0000000		/* masks for operations */
#define NOTE_FFLAGSMASK	0x00ffffff

#define NOTE_TRIGGER	0x01000000		/* trigger the event */

/*
 * This is currently visible to userland to work around broken
 * programs which pull in <sys/proc.h> or <sys/selinfo.h>.
 */
#include <sys/queue.h>

struct klistops;
struct knote;
SLIST_HEAD(knlist, knote);

struct klist {
	struct knlist		 kl_list;
	const struct klistops	*kl_ops;
	void			*kl_arg;
};

#ifdef _KERNEL

/* kernel-only flags */
#define __EV_SELECT	0x0800		/* match behavior of select */
#define __EV_POLL	0x1000		/* match behavior of poll */
#define __EV_HUP	EV_FLAG1	/* device or socket disconnected */

#define EVFILT_MARKER	0xf			/* placemarker for tailq */

/*
 * hint flag for in-kernel use - must not equal any existing note
 */
#define NOTE_SUBMIT	0x01000000		/* initial knote submission */

#define	KN_HASHSIZE		64		/* XXX should be tunable */

/*
 * Flag indicating hint is a signal.  Used by EVFILT_SIGNAL, and also
 * shared by EVFILT_PROC  (all knotes attached to p->p_klist)
 */
#define NOTE_SIGNAL	0x08000000

/*
 * = Event filter interface
 *
 * == .f_flags
 *
 * Defines properties of the event filter:
 *
 * - FILTEROP_ISFD      Each knote of this filter is associated
 *                      with a file descriptor.
 *
 * - FILTEROP_MPSAFE    The kqueue subsystem can invoke .f_attach(),
 *                      .f_detach(), .f_modify() and .f_process() without
 *                      the kernel lock.
 *
 * == .f_attach()
 *
 * Attaches the knote to the object.
 *
 * == .f_detach()
 *
 * Detaches the knote from the object. The object must not use this knote
 * for delivering events after this callback has returned.
 *
 * == .f_event()
 *
 * Notifies the filter about an event. Called through knote().
 *
 * == .f_modify()
 *
 * Modifies the knote with new state from the user.
 *
 * Returns non-zero if the knote has become active.
 *
 * == .f_process()
 *
 * Checks if the event is active and returns non-zero if the event should be
 * returned to the user.
 *
 * If kev is non-NULL and the event is active, the callback should store
 * the event's state in kev for delivery to the user.
 *
 * == Concurrency control
 *
 * The kqueue subsystem serializes calls of .f_attach(), .f_detach(),
 * .f_modify() and .f_process().
 */

#define FILTEROP_ISFD		0x00000001	/* ident == filedescriptor */
#define FILTEROP_MPSAFE		0x00000002	/* safe without kernel lock */

struct filterops {
	int	f_flags;
	int	(*f_attach)(struct knote *kn);
	void	(*f_detach)(struct knote *kn);
	int	(*f_event)(struct knote *kn, long hint);
	int	(*f_modify)(struct kevent *kev, struct knote *kn);
	int	(*f_process)(struct knote *kn, struct kevent *kev);
};

/*
 * Locking:
 *	I	immutable after creation
 *	o	object lock
 *	q	kn_kq->kq_lock
 */
struct knote {
	SLIST_ENTRY(knote)	kn_link;	/* for fd */
	SLIST_ENTRY(knote)	kn_selnext;	/* for struct selinfo */
	TAILQ_ENTRY(knote)	kn_tqe;
	struct			kqueue *kn_kq;	/* [I] which queue we are on */
	struct			kevent kn_kevent;
	int			kn_status;	/* [q] */
	int			kn_sfflags;	/* [o] saved filter flags */
	__int64_t		kn_sdata;	/* [o] saved data field */
	union {
		struct		file *p_fp;	/* file data pointer */
		struct		process *p_process;	/* process pointer */
		int		p_useract;	/* user event active */
	} kn_ptr;
	const struct		filterops *kn_fop;
	void			*kn_hook;	/* [o] */
	unsigned int		kn_pollid;	/* [I] */

#define KN_ACTIVE	0x0001			/* event has been triggered */
#define KN_QUEUED	0x0002			/* event is on queue */
#define KN_DISABLED	0x0004			/* event is disabled */
#define KN_DETACHED	0x0008			/* knote is detached */
#define KN_PROCESSING	0x0010			/* knote is being processed */
#define KN_WAITING	0x0020			/* waiting on processing */

#define kn_id		kn_kevent.ident		/* [I] */
#define kn_filter	kn_kevent.filter	/* [I] */
#define kn_flags	kn_kevent.flags		/* [o] */
#define kn_fflags	kn_kevent.fflags	/* [o] */
#define kn_data		kn_kevent.data		/* [o] */
#define kn_udata	kn_kevent.udata		/* [o] */
#define kn_fp		kn_ptr.p_fp		/* [o] */
};

struct klistops {
	void	(*klo_assertlk)(void *);
	int	(*klo_lock)(void *);
	void	(*klo_unlock)(void *, int);
};

struct kqueue_scan_state {
	struct kqueue	*kqs_kq;		/* kqueue of this scan */
	struct knote	 kqs_start;		/* start marker */
	struct knote	 kqs_end;		/* end marker */
	int		 kqs_nevent;		/* number of events collected */
	int		 kqs_queued;		/* if set, end marker is
						 * in queue */
};

struct mutex;
struct proc;
struct rwlock;
struct timespec;

extern const struct filterops dead_filtops;

extern void	kqpoll_init(unsigned int);
extern void	kqpoll_done(unsigned int);
extern void	kqpoll_exit(void);
extern void	knote(struct klist *list, long hint);
extern void	knote_locked(struct klist *list, long hint);
extern void	knote_fdclose(struct proc *p, int fd);
extern void	knote_processexit(struct process *);
extern void	knote_processfork(struct process *, pid_t);
extern void	knote_assign(const struct kevent *, struct knote *);
extern void	knote_submit(struct knote *, struct kevent *);
extern void	kqueue_init(void);
extern void	kqueue_init_percpu(void);
extern int	kqueue_register(struct kqueue *kq, struct kevent *kev,
		    unsigned int pollid, struct proc *p);
extern int	kqueue_scan(struct kqueue_scan_state *, int, struct kevent *,
		    struct timespec *, struct proc *, int *);
extern void	kqueue_scan_setup(struct kqueue_scan_state *, struct kqueue *);
extern void	kqueue_scan_finish(struct kqueue_scan_state *);
extern int	filt_seltrue(struct knote *kn, long hint);
extern int	seltrue_kqfilter(dev_t, struct knote *);
extern void	klist_init(struct klist *, const struct klistops *, void *);
extern void	klist_init_mutex(struct klist *, struct mutex *);
extern void	klist_init_rwlock(struct klist *, struct rwlock *);
extern void	klist_free(struct klist *);
extern void	klist_insert(struct klist *, struct knote *);
extern void	klist_insert_locked(struct klist *, struct knote *);
extern void	klist_remove(struct klist *, struct knote *);
extern void	klist_remove_locked(struct klist *, struct knote *);
extern void	klist_invalidate(struct klist *);

static inline int
knote_modify_fn(const struct kevent *kev, struct knote *kn,
    int (*f_event)(struct knote *, long))
{
	knote_assign(kev, kn);
	return ((*f_event)(kn, 0));
}

static inline int
knote_modify(const struct kevent *kev, struct knote *kn)
{
	return (knote_modify_fn(kev, kn, kn->kn_fop->f_event));
}

static inline int
knote_process_fn(struct knote *kn, struct kevent *kev,
    int (*f_event)(struct knote *, long))
{
	int active;

	/*
	 * If called from kqueue_scan(), skip f_event
	 * when EV_ONESHOT is set, to preserve old behaviour.
	 */
	if (kev != NULL && (kn->kn_flags & EV_ONESHOT))
		active = 1;
	else
		active = (*f_event)(kn, 0);
	if (active)
		knote_submit(kn, kev);
	return (active);
}

static inline int
knote_process(struct knote *kn, struct kevent *kev)
{
	return (knote_process_fn(kn, kev, kn->kn_fop->f_event));
}

static inline int
klist_empty(struct klist *klist)
{
	return (SLIST_EMPTY(&klist->kl_list));
}

#else	/* !_KERNEL */

#include <sys/cdefs.h>
struct timespec;

__BEGIN_DECLS
int	kqueue(void);
int	kqueue1(int flags);
int	kevent(int kq, const struct kevent *changelist, int nchanges,
		    struct kevent *eventlist, int nevents,
		    const struct timespec *timeout);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_EVENT_H_ */

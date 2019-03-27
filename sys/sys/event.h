/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * $FreeBSD$
 */

#ifndef _SYS_EVENT_H_
#define _SYS_EVENT_H_

#include <sys/_types.h>
#include <sys/queue.h>

#define EVFILT_READ		(-1)
#define EVFILT_WRITE		(-2)
#define EVFILT_AIO		(-3)	/* attached to aio requests */
#define EVFILT_VNODE		(-4)	/* attached to vnodes */
#define EVFILT_PROC		(-5)	/* attached to struct proc */
#define EVFILT_SIGNAL		(-6)	/* attached to struct proc */
#define EVFILT_TIMER		(-7)	/* timers */
#define EVFILT_PROCDESC		(-8)	/* attached to process descriptors */
#define EVFILT_FS		(-9)	/* filesystem events */
#define EVFILT_LIO		(-10)	/* attached to lio requests */
#define EVFILT_USER		(-11)	/* User events */
#define EVFILT_SENDFILE		(-12)	/* attached to sendfile requests */
#define EVFILT_EMPTY		(-13)	/* empty send socket buf */
#define EVFILT_SYSCOUNT		13

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define	EV_SET(kevp_, a, b, c, d, e, f) do {	\
	*(kevp_) = (struct kevent){		\
	    .ident = (a),			\
	    .filter = (b),			\
	    .flags = (c),			\
	    .fflags = (d),			\
	    .data = (e),			\
	    .udata = (f),			\
	    .ext = {0},				\
	};					\
} while(0)
#else /* Pre-C99 or not STDC (e.g., C++) */
/* The definition of the local variable kevp could possibly conflict
 * with a user-defined value passed in parameters a-f.
 */
#define EV_SET(kevp_, a, b, c, d, e, f) do {	\
	struct kevent *kevp = (kevp_);		\
	(kevp)->ident = (a);			\
	(kevp)->filter = (b);			\
	(kevp)->flags = (c);			\
	(kevp)->fflags = (d);			\
	(kevp)->data = (e);			\
	(kevp)->udata = (f);			\
	(kevp)->ext[0] = 0;			\
	(kevp)->ext[1] = 0;			\
	(kevp)->ext[2] = 0;			\
	(kevp)->ext[3] = 0;			\
} while(0)
#endif

struct kevent {
	__uintptr_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	unsigned short	flags;		/* action flags for kqueue */
	unsigned int	fflags;		/* filter flag value */
	__int64_t	data;		/* filter data value */
	void		*udata;		/* opaque user data identifier */
	__uint64_t	ext[4];		/* extensions */
};

#if defined(_WANT_FREEBSD11_KEVENT)
/* Older structure used in FreeBSD 11.x and older. */
struct kevent_freebsd11 {
	__uintptr_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	unsigned short	flags;
	unsigned int	fflags;
	__intptr_t	data;
	void		*udata;		/* opaque user data identifier */
};
#endif

#if defined(_WANT_KEVENT32) || (defined(_KERNEL) && defined(__LP64__))
struct kevent32 {
	uint32_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	u_short		flags;
	u_int		fflags;
#ifndef __amd64__
	uint32_t	pad0;
#endif
	int32_t		data1, data2;
	uint32_t	udata;		/* opaque user data identifier */
#ifndef __amd64__
	uint32_t	pad1;
#endif
	uint32_t	ext64[8];
};

#ifdef _WANT_FREEBSD11_KEVENT
struct kevent32_freebsd11 {
	u_int32_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	u_short		flags;
	u_int		fflags;
	int32_t		data;
	u_int32_t	udata;		/* opaque user data identifier */
};
#endif
#endif

/* actions */
#define EV_ADD		0x0001		/* add event to kq (implies enable) */
#define EV_DELETE	0x0002		/* delete event from kq */
#define EV_ENABLE	0x0004		/* enable event */
#define EV_DISABLE	0x0008		/* disable event (not reported) */
#define EV_FORCEONESHOT	0x0100		/* enable _ONESHOT and force trigger */

/* flags */
#define EV_ONESHOT	0x0010		/* only report one occurrence */
#define EV_CLEAR	0x0020		/* clear event state after reporting */
#define EV_RECEIPT	0x0040		/* force EV_ERROR on success, data=0 */
#define EV_DISPATCH	0x0080		/* disable event after reporting */

#define EV_SYSFLAGS	0xF000		/* reserved by system */
#define	EV_DROP		0x1000		/* note should be dropped */
#define EV_FLAG1	0x2000		/* filter-specific flag */
#define EV_FLAG2	0x4000		/* filter-specific flag */

/* returned values */
#define EV_EOF		0x8000		/* EOF detected */
#define EV_ERROR	0x4000		/* error, data contains errno */

 /*
  * data/hint flags/masks for EVFILT_USER, shared with userspace
  *
  * On input, the top two bits of fflags specifies how the lower twenty four
  * bits should be applied to the stored value of fflags.
  *
  * On output, the top two bits will always be set to NOTE_FFNOP and the
  * remaining twenty four bits will contain the stored fflags value.
  */
#define NOTE_FFNOP	0x00000000		/* ignore input fflags */
#define NOTE_FFAND	0x40000000		/* AND fflags */
#define NOTE_FFOR	0x80000000		/* OR fflags */
#define NOTE_FFCOPY	0xc0000000		/* copy fflags */
#define NOTE_FFCTRLMASK	0xc0000000		/* masks for operations */
#define NOTE_FFLAGSMASK	0x00ffffff

#define NOTE_TRIGGER	0x01000000		/* Cause the event to be
						   triggered for output. */

/*
 * data/hint flags for EVFILT_{READ|WRITE}, shared with userspace
 */
#define NOTE_LOWAT	0x0001			/* low water mark */
#define NOTE_FILE_POLL	0x0002			/* behave like poll() */

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
#define	NOTE_OPEN	0x0080			/* vnode was opened */
#define	NOTE_CLOSE	0x0100			/* file closed, fd did not
						   allowed write */
#define	NOTE_CLOSE_WRITE 0x0200			/* file closed, fd did allowed
						   write */
#define	NOTE_READ	0x0400			/* file was read */

/*
 * data/hint flags for EVFILT_PROC and EVFILT_PROCDESC, shared with userspace
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

/* additional flags for EVFILT_TIMER */
#define NOTE_SECONDS		0x00000001	/* data is seconds */
#define NOTE_MSECONDS		0x00000002	/* data is milliseconds */
#define NOTE_USECONDS		0x00000004	/* data is microseconds */
#define NOTE_NSECONDS		0x00000008	/* data is nanoseconds */
#define	NOTE_ABSTIME		0x00000010	/* timeout is absolute */

struct knote;
SLIST_HEAD(klist, knote);
struct kqueue;
TAILQ_HEAD(kqlist, kqueue);
struct knlist {
	struct	klist	kl_list;
	void    (*kl_lock)(void *);	/* lock function */
	void    (*kl_unlock)(void *);
	void	(*kl_assert_locked)(void *);
	void	(*kl_assert_unlocked)(void *);
	void	*kl_lockarg;		/* argument passed to lock functions */
	int	kl_autodestroy;
};


#ifdef _KERNEL

/*
 * Flags for knote call
 */
#define	KNF_LISTLOCKED	0x0001			/* knlist is locked */
#define	KNF_NOKQLOCK	0x0002			/* do not keep KQ_LOCK */

#define KNOTE(list, hint, flags)	knote(list, hint, flags)
#define KNOTE_LOCKED(list, hint)	knote(list, hint, KNF_LISTLOCKED)
#define KNOTE_UNLOCKED(list, hint)	knote(list, hint, 0)

#define	KNLIST_EMPTY(list)		SLIST_EMPTY(&(list)->kl_list)

/*
 * Flag indicating hint is a signal.  Used by EVFILT_SIGNAL, and also
 * shared by EVFILT_PROC  (all knotes attached to p->p_klist)
 */
#define NOTE_SIGNAL	0x08000000

/*
 * Hint values for the optional f_touch event filter.  If f_touch is not set 
 * to NULL and f_isfd is zero the f_touch filter will be called with the type
 * argument set to EVENT_REGISTER during a kevent() system call.  It is also
 * called under the same conditions with the type argument set to EVENT_PROCESS
 * when the event has been triggered.
 */
#define EVENT_REGISTER	1
#define EVENT_PROCESS	2

struct filterops {
	int	f_isfd;		/* true if ident == filedescriptor */
	int	(*f_attach)(struct knote *kn);
	void	(*f_detach)(struct knote *kn);
	int	(*f_event)(struct knote *kn, long hint);
	void	(*f_touch)(struct knote *kn, struct kevent *kev, u_long type);
};

/*
 * An in-flux knote cannot be dropped from its kq while the kq is
 * unlocked.  If the KN_SCAN flag is not set, a thread can only set
 * kn_influx when it is exclusive owner of the knote state, and can
 * modify kn_status as if it had the KQ lock.  KN_SCAN must not be set
 * on a knote which is already in flux.
 *
 * kn_sfflags, kn_sdata, and kn_kevent are protected by the knlist lock.
 */
struct knote {
	SLIST_ENTRY(knote)	kn_link;	/* for kq */
	SLIST_ENTRY(knote)	kn_selnext;	/* for struct selinfo */
	struct			knlist *kn_knlist;	/* f_attach populated */
	TAILQ_ENTRY(knote)	kn_tqe;
	struct			kqueue *kn_kq;	/* which queue we are on */
	struct 			kevent kn_kevent;
	void			*kn_hook;
	int			kn_hookid;
	int			kn_status;	/* protected by kq lock */
#define KN_ACTIVE	0x01			/* event has been triggered */
#define KN_QUEUED	0x02			/* event is on queue */
#define KN_DISABLED	0x04			/* event is disabled */
#define KN_DETACHED	0x08			/* knote is detached */
#define KN_MARKER	0x20			/* ignore this knote */
#define KN_KQUEUE	0x40			/* this knote belongs to a kq */
#define	KN_SCAN		0x100			/* flux set in kqueue_scan() */
	int			kn_influx;
	int			kn_sfflags;	/* saved filter flags */
	int64_t			kn_sdata;	/* saved data field */
	union {
		struct		file *p_fp;	/* file data pointer */
		struct		proc *p_proc;	/* proc pointer */
		struct		kaiocb *p_aio;	/* AIO job pointer */
		struct		aioliojob *p_lio;	/* LIO job pointer */
		void		*p_v;		/* generic other pointer */
	} kn_ptr;
	struct			filterops *kn_fop;

#define kn_id		kn_kevent.ident
#define kn_filter	kn_kevent.filter
#define kn_flags	kn_kevent.flags
#define kn_fflags	kn_kevent.fflags
#define kn_data		kn_kevent.data
#define kn_fp		kn_ptr.p_fp
};
struct kevent_copyops {
	void	*arg;
	int	(*k_copyout)(void *arg, struct kevent *kevp, int count);
	int	(*k_copyin)(void *arg, struct kevent *kevp, int count);
	size_t	kevent_size;
};

struct thread;
struct proc;
struct knlist;
struct mtx;
struct rwlock;

void	knote(struct knlist *list, long hint, int lockflags);
void	knote_fork(struct knlist *list, int pid);
struct knlist *knlist_alloc(struct mtx *lock);
void	knlist_detach(struct knlist *knl);
void	knlist_add(struct knlist *knl, struct knote *kn, int islocked);
void	knlist_remove(struct knlist *knl, struct knote *kn, int islocked);
int	knlist_empty(struct knlist *knl);
void	knlist_init(struct knlist *knl, void *lock, void (*kl_lock)(void *),
	    void (*kl_unlock)(void *), void (*kl_assert_locked)(void *),
	    void (*kl_assert_unlocked)(void *));
void	knlist_init_mtx(struct knlist *knl, struct mtx *lock);
void	knlist_init_rw_reader(struct knlist *knl, struct rwlock *lock);
void	knlist_destroy(struct knlist *knl);
void	knlist_cleardel(struct knlist *knl, struct thread *td,
	    int islocked, int killkn);
#define knlist_clear(knl, islocked)				\
	knlist_cleardel((knl), NULL, (islocked), 0)
#define knlist_delete(knl, td, islocked)			\
	knlist_cleardel((knl), (td), (islocked), 1)
void	knote_fdclose(struct thread *p, int fd);
int 	kqfd_register(int fd, struct kevent *kev, struct thread *p,
	    int mflag);
int	kqueue_add_filteropts(int filt, struct filterops *filtops);
int	kqueue_del_filteropts(int filt);

#else 	/* !_KERNEL */

#include <sys/cdefs.h>
struct timespec;

__BEGIN_DECLS
int     kqueue(void);
int     kevent(int kq, const struct kevent *changelist, int nchanges,
	    struct kevent *eventlist, int nevents,
	    const struct timespec *timeout);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_EVENT_H_ */

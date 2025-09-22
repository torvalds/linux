/*	$OpenBSD: sys_futex.c,v 1.26 2025/08/18 03:51:45 dlg Exp $ */

/*
 * Copyright (c) 2016-2017 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/pool.h>
#include <sys/time.h>
#include <sys/rwlock.h>
#include <sys/percpu.h> /* CACHELINESIZE */
#include <sys/futex.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <uvm/uvm.h>

/*
 * Locks used to protect variables in this file:
 *
 *	I	immutable after initialization
 *	f	futex_slpque fsq_lock
 */

/*
 * Kernel representation of a futex.
 *
 * The userland address that the futex is waiting on is represented by
 * ft_ps, ft_obj, ft_amap, and ft_off.
 *
 * Whether the futex is waiting or woken up is represented by the
 * ft_proc pointer being set (ie, not NULL) or not (ie, NULL) respectively.
 * When the futex is waiting it is referenced by the list in a
 * futex_slpque. When the futex gets woken up, it is removed from the
 * list and the ft_proc pointer is cleared to indicate that the reference
 * held by the list has been released. Only a thread holding the lock
 * may remove the futex from the list and clear ft_proc. This is true
 * even for futex_wait().
 *
 * However, futex_wait() may read ft_proc without the lock so it can
 * avoid contending with the thread that just woke it up. This means
 * that once ft_proc is cleared, futex_wait() may return, the struct
 * futex will no longer exist, and it is no longer safe to access it
 * from the wakeup side.
 *
 * tl;dr: the thread holding the slpque lock "owns" the references
 * to the futexes on the list until it clears ft_proc.
 */

struct futex_slpque;

struct futex {
	struct futex_slpque * volatile
				 ft_fsq;	/* [f] current futex_slpque */
	TAILQ_ENTRY(futex)	 ft_entry;	/* [f] entry on futex_slpque */

	struct process		*ft_ps;		/* [I] for private futexes */
	struct uvm_object	*ft_obj;	/* [f] UVM object */
	struct vm_amap		*ft_amap;	/* [f] UVM amap */
	volatile voff_t		 ft_off;	/* [f] UVM offset */

	struct proc * volatile	 ft_proc;	/* [f] waiting thread */
};

static int
futex_is_eq(const struct futex *a, const struct futex *b)
{
	return (a->ft_off == b->ft_off &&
	    a->ft_ps == b->ft_ps &&
	    a->ft_obj == b->ft_obj &&
	    a->ft_amap == b->ft_amap);
}

TAILQ_HEAD(futex_list, futex);

struct futex_slpque {
	struct futex_list	fsq_list;	/* [F] */
	struct rwlock		fsq_lock;
	uint32_t		fsq_id;		/* [I] for lock ordering */
} __aligned(CACHELINESIZE);

/* Syscall helpers. */
static int	futex_wait(struct proc *, uint32_t *, uint32_t,
		    const struct timespec *, int);
static int	futex_wake(struct proc *, uint32_t *, uint32_t, int,
		    register_t *);
static int	futex_requeue(struct proc *, uint32_t *, uint32_t,
		    uint32_t *, uint32_t, int, register_t *);

/* Flags for futex_get(). kernel private flags sit in FUTEX_OP_MASK space */
#define FT_PRIVATE	FUTEX_PRIVATE_FLAG	/* Futex is process-private. */

#define FUTEX_SLPQUES_BITS	6
#define FUTEX_SLPQUES_SIZE	(1U << FUTEX_SLPQUES_BITS)
#define FUTEX_SLPQUES_MASK	(FUTEX_SLPQUES_SIZE - 1)

static struct futex_slpque futex_slpques[FUTEX_SLPQUES_SIZE];

void
futex_init(void)
{
	struct futex_slpque *fsq;
	unsigned int i;

	for (i = 0; i < nitems(futex_slpques); i++) {
		fsq = &futex_slpques[i];

		TAILQ_INIT(&fsq->fsq_list);
		rw_init(&fsq->fsq_lock, "futexlk");

		fsq->fsq_id = arc4random();
		fsq->fsq_id &= ~FUTEX_SLPQUES_MASK;
		fsq->fsq_id |= i;
	}
}

int
sys_futex(struct proc *p, void *v, register_t *retval)
{
	struct sys_futex_args /* {
		syscallarg(uint32_t *) f;
		syscallarg(int) op;
		syscallarg(inr) val;
		syscallarg(const struct timespec *) timeout;
		syscallarg(uint32_t *) g;
	} */ *uap = v;
	uint32_t *uaddr = SCARG(uap, f);
	int op = SCARG(uap, op);
	uint32_t val = SCARG(uap, val);
	const struct timespec *timeout = SCARG(uap, timeout);
	void *g = SCARG(uap, g);
	int flags = op & FUTEX_FLAG_MASK;
	int error = 0;

	switch (op & FUTEX_OP_MASK) {
	case FUTEX_WAIT:
		error = futex_wait(p, uaddr, val, timeout, flags);
		break;
	case FUTEX_WAKE:
		error = futex_wake(p, uaddr, val, flags, retval);
		break;
	case FUTEX_REQUEUE:
		error = futex_requeue(p, uaddr, val, g,
		    (u_long)timeout, flags, retval);
		break;
	default:
		error = ENOSYS;
		break;
	}

	return error;
}

static void
futex_addrs(struct proc *p, struct futex *f, uint32_t *uaddr, int flags)
{
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	struct uvm_object *obj = NULL;
	struct vm_amap *amap = NULL;
	voff_t off = (vaddr_t)uaddr;
	struct process *ps;

	if (ISSET(flags, FT_PRIVATE))
		ps = p->p_p;
	else {
		ps = NULL;

		vm_map_lock_read(map);
		if (uvm_map_lookup_entry(map, (vaddr_t)uaddr, &entry) &&
		    entry->inheritance == MAP_INHERIT_SHARE) {
			if (UVM_ET_ISOBJ(entry)) {
				obj = entry->object.uvm_obj;
				off = entry->offset +
				    ((vaddr_t)uaddr - entry->start);
			} else if (entry->aref.ar_amap) {
				amap = entry->aref.ar_amap;
				off = ptoa(entry->aref.ar_pageoff) +
				    ((vaddr_t)uaddr - entry->start);
			}
		}
		vm_map_unlock_read(map);
	}

	f->ft_ps = ps;
	f->ft_obj = obj;
	f->ft_amap = amap;
	f->ft_off = off;
}

static inline struct futex_slpque *
futex_get_slpque(struct futex *f)
{
	uint32_t key = f->ft_off >> 3; /* watevs */
	key ^= key >> FUTEX_SLPQUES_BITS;

	return (&futex_slpques[key & FUTEX_SLPQUES_MASK]);
}

static int
futex_unwait(struct futex_slpque *ofsq, struct futex *f)
{
	struct futex_slpque *fsq;
	int rv;

	/*
	 * REQUEUE can move a futex between buckets, so follow it if needed.
	 */

	for (;;) {
		rw_enter_write(&ofsq->fsq_lock);
		fsq = f->ft_fsq;
		if (ofsq == fsq)
			break;

		rw_exit_write(&ofsq->fsq_lock);
		ofsq = fsq;
	}

	rv = f->ft_proc != NULL;
	if (rv)
		TAILQ_REMOVE(&fsq->fsq_list, f, ft_entry);
	rw_exit_write(&fsq->fsq_lock);

	return (rv);
}

/*
 * Put the current thread on the sleep queue of the futex at address
 * ``uaddr''.  Let it sleep for the specified ``timeout'' time, or
 * indefinitely if the argument is NULL.
 */
static int
futex_wait(struct proc *p, uint32_t *uaddr, uint32_t val,
    const struct timespec *timeout, int flags)
{
	struct futex f;
	struct futex_slpque *fsq;
	uint64_t nsecs = INFSLP;
	uint32_t cval;
	int error;

	if (timeout != NULL) {
		struct timespec ts;

		if ((error = copyin(timeout, &ts, sizeof(ts))))
			return error;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrreltimespec(p, &ts);
#endif
		if (ts.tv_sec < 0 || !timespecisvalid(&ts))
			return EINVAL;

		nsecs = MIN(TIMESPEC_TO_NSEC(&ts), MAXTSLP);
		if (nsecs == 0)
			return ETIMEDOUT;
	}

	futex_addrs(p, &f, uaddr, flags);
	fsq = futex_get_slpque(&f);

	/* Mark futex as waiting. */
	f.ft_fsq = fsq;
	f.ft_proc = p;
	rw_enter_write(&fsq->fsq_lock);
	/* Make the waiting futex visible to wake/requeue */
	TAILQ_INSERT_TAIL(&fsq->fsq_list, &f, ft_entry);
	rw_exit_write(&fsq->fsq_lock);

	/*
	 * Do not return before f has been removed from the slpque!
	 */

	/*
	 * Read user space futex value
	 */
	if ((error = copyin32(uaddr, &cval)) != 0)
		goto exit;

	/* If the value changed, stop here. */
	if (cval != val) {
		error = EAGAIN;
		goto exit;
	}

	sleep_setup(&f, PWAIT|PCATCH, "fsleep");
	error = sleep_finish(nsecs, f.ft_proc != NULL);
	/* Remove ourself if we haven't been awaken. */
	if (error != 0 || f.ft_proc != NULL) {
		if (futex_unwait(fsq, &f) == 0)
			error = 0;

		switch (error) {
		case ERESTART:
			error = ECANCELED;
			break;
		case EWOULDBLOCK:
			error = ETIMEDOUT;
			break;
		default:
			break;
		}
	}

	return error;
exit:
	if (f.ft_proc != NULL)
		futex_unwait(fsq, &f);
	return error;
}

static void
futex_list_wakeup(struct futex_list *fl)
{
	struct futex *f, *nf;
	struct proc *p;

	/*
	 * Setting ft_proc to NULL releases the futex reference
	 * currently held via the slpque lock.
	 *
	 * SCHED_LOCK is only needed to call wakeup_proc.
	 */

	SCHED_LOCK();
	TAILQ_FOREACH_SAFE(f, fl, ft_entry, nf) {
		p = f->ft_proc;
		f->ft_proc = NULL;
		wakeup_proc(p);
	}
	SCHED_UNLOCK();
}

/*
 * Wakeup at most ``n'' sibling threads sleeping on a futex at address
 * ``uaddr'' and requeue at most ``m'' sibling threads on a futex at
 * address ``uaddr2''.
 */
static int
futex_requeue(struct proc *p, uint32_t *uaddr, uint32_t n,
    uint32_t *uaddr2, uint32_t m, int flags, register_t *retval)
{
	struct futex_list fl = TAILQ_HEAD_INITIALIZER(fl);
	struct futex okey, nkey;
	struct futex *f, *nf, *mf = NULL;
	struct futex_slpque *ofsq, *nfsq;
	uint32_t count = 0;

	if (m == 0)
		return futex_wake(p, uaddr, n, flags, retval);

	futex_addrs(p, &okey, uaddr, flags);
	ofsq = futex_get_slpque(&okey);
	futex_addrs(p, &nkey, uaddr2, flags);
	nfsq = futex_get_slpque(&nkey);

	if (ofsq->fsq_id < nfsq->fsq_id) {
		rw_enter_write(&ofsq->fsq_lock);
		rw_enter_write(&nfsq->fsq_lock);
	} else if (ofsq->fsq_id > nfsq->fsq_id) {
		rw_enter_write(&nfsq->fsq_lock);
		rw_enter_write(&ofsq->fsq_lock);
	} else
		rw_enter_write(&ofsq->fsq_lock);

	TAILQ_FOREACH_SAFE(f, &ofsq->fsq_list, ft_entry, nf) {
		/* __builtin_prefetch(nf, 1); */
		KASSERT(f->ft_proc != NULL);

		if (!futex_is_eq(f, &okey))
			continue;

		TAILQ_REMOVE(&ofsq->fsq_list, f, ft_entry);
		TAILQ_INSERT_TAIL(&fl, f, ft_entry);

		if (++count == n) {
			mf = nf;
			break;
		}
	}

	if (!TAILQ_EMPTY(&fl))
		futex_list_wakeup(&fl);

	/* update matching futexes */
	if (mf != NULL) {
		/*
		 * only iterate from the current entry to the tail
		 * of the list as it is now in case we're requeueing
		 * on the end of the same list.
		 */
		nf = TAILQ_LAST(&ofsq->fsq_list, futex_list);
		do {
			f = mf;
			mf = TAILQ_NEXT(f, ft_entry);
			/* __builtin_prefetch(mf, 1); */

			KASSERT(f->ft_proc != NULL);

			if (!futex_is_eq(f, &okey))
				continue;

			TAILQ_REMOVE(&ofsq->fsq_list, f, ft_entry);
			f->ft_fsq = nfsq;
			f->ft_ps = nkey.ft_ps;
			f->ft_obj = nkey.ft_obj;
			f->ft_amap = nkey.ft_amap;
			f->ft_off = nkey.ft_off;
			TAILQ_INSERT_TAIL(&nfsq->fsq_list, f, ft_entry);

			if (--m == 0)
				break;
		} while (f != nf);
	}

	if (ofsq->fsq_id != nfsq->fsq_id)
		rw_exit_write(&nfsq->fsq_lock);
	rw_exit_write(&ofsq->fsq_lock);

	*retval = count;
	return 0;
}

/*
 * Wakeup at most ``n'' sibling threads sleeping on a futex at address
 * ``uaddr''.
 */
static int
futex_wake(struct proc *p, uint32_t *uaddr, uint32_t n, int flags,
    register_t *retval)
{
	struct futex_list fl = TAILQ_HEAD_INITIALIZER(fl);
	struct futex key;
	struct futex *f, *nf;
	struct futex_slpque *fsq;
	int count = 0;

	if (n == 0) {
		*retval = 0;
		return 0;
	}

	futex_addrs(p, &key, uaddr, flags);
	fsq = futex_get_slpque(&key);

	rw_enter_write(&fsq->fsq_lock);

	TAILQ_FOREACH_SAFE(f, &fsq->fsq_list, ft_entry, nf) {
		/* __builtin_prefetch(nf, 1); */
		KASSERT(f->ft_proc != NULL);

		if (!futex_is_eq(f, &key))
			continue;

		TAILQ_REMOVE(&fsq->fsq_list, f, ft_entry);
		TAILQ_INSERT_TAIL(&fl, f, ft_entry);

		if (++count == n)
			break;
	}

	if (!TAILQ_EMPTY(&fl))
		futex_list_wakeup(&fl);

	rw_exit_write(&fsq->fsq_lock);

	*retval = count;
	return 0;
}

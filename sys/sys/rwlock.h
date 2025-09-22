/*	$OpenBSD: rwlock.h,v 1.34 2025/07/21 20:36:41 bluhm Exp $	*/
/*
 * Copyright (c) 2002 Artur Grabowski <art@openbsd.org>
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

/*
 * Multiple readers, single writer lock.
 *
 * Simplistic implementation modelled after rw locks in Solaris.
 *
 * The rwl_owner has the following layout:
 * [ owner or count of readers | wrlock | wrwant | wait ]
 *
 * When the WAIT bit is set (bit 0), the lock has waiters sleeping on it.
 * When the WRWANT bit is set (bit 1), at least one waiter wants a write lock.
 * When the WRLOCK bit is set (bit 2) the lock is currently write-locked.
 *
 * When write locked, the upper bits contain the struct proc * pointer to
 * the writer, otherwise they count the number of readers.
 *
 * We provide a simple machine independent implementation:
 *
 * void rw_enter_read(struct rwlock *)
 *  atomically test for RWLOCK_WRLOCK and if not set, increment the lock
 *  by RWLOCK_READ_INCR. While RWLOCK_WRLOCK is set, loop into rw_enter_wait.
 *
 * void rw_enter_write(struct rwlock *);
 *  atomically test for the lock being 0 (it's not possible to have
 *  owner/read count unset and waiter bits set) and if 0 set the owner to
 *  the proc and RWLOCK_WRLOCK. While not zero, loop into rw_enter_wait.
 *
 * void rw_exit_read(struct rwlock *);
 *  atomically decrement lock by RWLOCK_READ_INCR and unset RWLOCK_WAIT and
 *  RWLOCK_WRWANT remembering the old value of lock and if RWLOCK_WAIT was set,
 *  call rw_exit_waiters with the old contents of the lock.
 *
 * void rw_exit_write(struct rwlock *);
 *  atomically swap the contents of the lock with 0 and if RWLOCK_WAIT was
 *  set, call rw_exit_waiters with the old contents of the lock.
 */

#ifndef _SYS_RWLOCK_H
#define _SYS_RWLOCK_H

#include <sys/_lock.h>

struct proc;

struct rwlock {
	volatile unsigned long	 rwl_owner;
	volatile unsigned int	 rwl_waiters;
	volatile unsigned int	 rwl_readers;
	const char		*rwl_name;
#ifdef WITNESS
	struct lock_object	 rwl_lock_obj;
#endif
	int			 rwl_traceidx;
};

#define RWLOCK_LO_FLAGS(flags) \
	((ISSET(flags, RWL_DUPOK) ? LO_DUPOK : 0) |			\
	 (ISSET(flags, RWL_NOWITNESS) ? 0 : LO_WITNESS) |		\
	 (ISSET(flags, RWL_IS_VNODE) ? LO_IS_VNODE : 0) |		\
	 LO_INITIALIZED | LO_SLEEPABLE | LO_UPGRADABLE |		\
	 (LO_CLASS_RWLOCK << LO_CLASSSHIFT))

#define RRWLOCK_LO_FLAGS(flags) \
	((ISSET(flags, RWL_DUPOK) ? LO_DUPOK : 0) |			\
	 (ISSET(flags, RWL_NOWITNESS) ? 0 : LO_WITNESS) |		\
	 (ISSET(flags, RWL_IS_VNODE) ? LO_IS_VNODE : 0) |		\
	 LO_INITIALIZED | LO_RECURSABLE | LO_SLEEPABLE | LO_UPGRADABLE | \
	 (LO_CLASS_RRWLOCK << LO_CLASSSHIFT))

#define RWLOCK_LO_INITIALIZER(name, flags) \
	{ .lo_type = &(const struct lock_type){ .lt_name = name },	\
	  .lo_name = (name),						\
	  .lo_flags = RWLOCK_LO_FLAGS(flags) }

#define RWL_DUPOK		0x01
#define RWL_NOWITNESS		0x02
#define RWL_IS_VNODE		0x04

#ifdef WITNESS
#define RWLOCK_INITIALIZER(name) \
	{ 0, 0, 0, name, .rwl_lock_obj = RWLOCK_LO_INITIALIZER(name, 0), 0 }
#define RWLOCK_INITIALIZER_TRACE(name, trace) \
	{ 0, 0, 0, name, .rwl_lock_obj = RWLOCK_LO_INITIALIZER(name, 0), trace }
#else
#define RWLOCK_INITIALIZER(name) \
	{ 0, 0, 0, name, 0 }
#define RWLOCK_INITIALIZER_TRACE(name, trace) \
	{ 0, 0, 0, name, trace }
#endif

#define RWLOCK_WRLOCK		0x04UL
#define RWLOCK_MASK		0x07UL

#define RWLOCK_OWNER(rwl)	((struct proc *)((rwl)->rwl_owner & ~RWLOCK_MASK))

#define RWLOCK_READER_SHIFT	3UL
#define RWLOCK_READ_INCR	(1UL << RWLOCK_READER_SHIFT)

#define RW_WRITE		0x0001UL /* exclusive lock */
#define RW_READ			0x0002UL /* shared lock */
#define RW_DOWNGRADE		0x0004UL /* downgrade exclusive to shared */
#define RW_UPGRADE		0x0005UL
#define RW_OPMASK		0x0007UL

#define RW_INTR			0x0010UL /* interruptible sleep */
#define RW_NOSLEEP		0x0040UL /* don't wait for the lock */
#define RW_RECURSEFAIL		0x0080UL /* Fail on recursion for RRW locks. */
#define RW_DUPOK		0x0100UL /* Permit duplicate lock */

/*
 * for rw_status() and rrw_status() only: exclusive lock held by
 * some other thread
 */
#define RW_WRITE_OTHER		0x0100UL

/* recursive rwlocks; */
struct rrwlock {
	struct rwlock		 rrwl_lock;
	uint32_t		 rrwl_wcnt; /* # writers. */
};

#ifdef _KERNEL

void	_rw_init_flags(struct rwlock *, const char *, int,
	    const struct lock_type *, int);

#ifdef WITNESS
#define rw_init_flags_trace(rwl, name, flags, trace) do {		\
	static const struct lock_type __lock_type = { .lt_name = #rwl };\
	_rw_init_flags(rwl, name, flags, &__lock_type, trace);		\
} while (0)
#define rw_init_flags(rwl, name, flags) do {				\
	static const struct lock_type __lock_type = { .lt_name = #rwl };\
	_rw_init_flags(rwl, name, flags, &__lock_type, 0);	\
} while (0)
#define rw_init(rwl, name)		rw_init_flags(rwl, name, 0)
#else /* WITNESS */
#define rw_init_flags_trace(rwl, name, flags, trace) \
				_rw_init_flags(rwl, name, flags, NULL, trace)
#define rw_init_flags(rwl, name, flags) \
				_rw_init_flags(rwl, name, flags, NULL, 0)
#define rw_init(rwl, name)	_rw_init_flags(rwl, name, 0, NULL, 0)
#endif /* WITNESS */

void	rw_enter_read(struct rwlock *);
void	rw_enter_write(struct rwlock *);
void	rw_exit_read(struct rwlock *);
void	rw_exit_write(struct rwlock *);

#ifdef DIAGNOSTIC
void	rw_assert_wrlock(struct rwlock *);
void	rw_assert_rdlock(struct rwlock *);
void	rw_assert_anylock(struct rwlock *);
void	rw_assert_unlocked(struct rwlock *);
#else
#define rw_assert_wrlock(rwl)	((void)0)
#define rw_assert_rdlock(rwl)	((void)0)
#define rw_assert_anylock(rwl)	((void)0)
#define rw_assert_unlocked(rwl)	((void)0)
#endif

int	rw_enter(struct rwlock *, int);
void	rw_exit(struct rwlock *);
int	rw_status(struct rwlock *);

static inline int
rw_read_held(struct rwlock *rwl)
{
	return (rw_status(rwl) == RW_READ);
}

static inline int
rw_write_held(struct rwlock *rwl)
{
	return (rw_status(rwl) == RW_WRITE);
}

static inline int
rw_lock_held(struct rwlock *rwl)
{
	int status;

	status = rw_status(rwl);

	return (status == RW_READ || status == RW_WRITE);
}


void	_rrw_init_flags(struct rrwlock *, const char *, int,
	    const struct lock_type *);
int	rrw_enter(struct rrwlock *, int);
void	rrw_exit(struct rrwlock *);
int	rrw_status(struct rrwlock *);

#ifdef WITNESS
#define rrw_init_flags(rrwl, name, flags) do {				\
	static const struct lock_type __lock_type = { .lt_name = #rrwl };\
	_rrw_init_flags(rrwl, name, flags, &__lock_type);		\
} while (0)
#define rrw_init(rrwl, name)	rrw_init_flags(rrwl, name, 0)
#else /* WITNESS */
#define rrw_init_flags(rrwl, name, flags) \
				_rrw_init_flags(rrwl, name, 0, NULL)
#define rrw_init(rrwl, name)	_rrw_init_flags(rrwl, name, 0, NULL)
#endif /* WITNESS */


/*
 * Allocated, reference-counted rwlocks
 */

#ifdef WITNESS
#define rw_obj_alloc_flags(rwl, name, flags) do {			\
	static struct lock_type __lock_type = { .lt_name = #rwl };	\
	_rw_obj_alloc_flags(rwl, name, flags, &__lock_type);		\
} while (0)
#else
#define rw_obj_alloc_flags(rwl, name, flags) \
			_rw_obj_alloc_flags(rwl, name, flags, NULL)
#endif
#define rw_obj_alloc(rwl, name)		rw_obj_alloc_flags(rwl, name, 0)

void	rw_obj_init(void);
void	_rw_obj_alloc_flags(struct rwlock **, const char *, int,
		struct lock_type *);
void	rw_obj_hold(struct rwlock *);
int	rw_obj_free(struct rwlock *);

/* sorted alphabetically, keep in sync with dev/dt/dt_prov_static.c */
#define DT_RWLOCK_IDX_NETLOCK	1
#define DT_RWLOCK_IDX_SOLOCK	2

#endif /* _KERNEL */

#endif /* _SYS_RWLOCK_H */

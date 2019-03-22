/* SPDX-License-Identifier: GPL-2.0 */
/* rwsem.h: R/W semaphores, public interface
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/err.h>
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
#include <linux/osq_lock.h>
#endif

struct rw_semaphore;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
#include <linux/rwsem-spinlock.h> /* use a generic implementation */
#define __RWSEM_INIT_COUNT(name)	.count = RWSEM_UNLOCKED_VALUE
#else
/* All arch specific implementations share the same struct */
struct rw_semaphore {
	atomic_long_t count;
	struct list_head wait_list;
	raw_spinlock_t wait_lock;
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	struct optimistic_spin_queue osq; /* spinner MCS lock */
	/*
	 * Write owner. Used as a speculative check to see
	 * if the owner is running on the cpu.
	 */
	struct task_struct *owner;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

/*
 * Setting bit 1 of the owner field but not bit 0 will indicate
 * that the rwsem is writer-owned with an unknown owner.
 */
#define RWSEM_OWNER_UNKNOWN	((struct task_struct *)-2L)

extern struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_read_failed_killable(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed_killable(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_wake(struct rw_semaphore *);
extern struct rw_semaphore *rwsem_downgrade_wake(struct rw_semaphore *sem);

/* In all implementations count != 0 means locked */
static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) != 0;
}

#define RWSEM_UNLOCKED_VALUE		0L
#define __RWSEM_INIT_COUNT(name)	.count = ATOMIC_LONG_INIT(RWSEM_UNLOCKED_VALUE)
#endif

/* Common initializer macros and functions */

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __RWSEM_DEP_MAP_INIT(lockname) , .dep_map = { .name = #lockname }
#else
# define __RWSEM_DEP_MAP_INIT(lockname)
#endif

#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
#define __RWSEM_OPT_INIT(lockname) , .osq = OSQ_LOCK_UNLOCKED, .owner = NULL
#else
#define __RWSEM_OPT_INIT(lockname)
#endif

#define __RWSEM_INITIALIZER(name)				\
	{ __RWSEM_INIT_COUNT(name),				\
	  .wait_list = LIST_HEAD_INIT((name).wait_list),	\
	  .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name.wait_lock)	\
	  __RWSEM_OPT_INIT(name)				\
	  __RWSEM_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_rwsem((sem), #sem, &__key);			\
} while (0)

/*
 * This is the same regardless of which rwsem implementation that is being used.
 * It is just a heuristic meant to be called by somebody alreadying holding the
 * rwsem to see if somebody from an incompatible type is wanting access to the
 * lock.
 */
static inline int rwsem_is_contended(struct rw_semaphore *sem)
{
	return !list_empty(&sem->wait_list);
}

/*
 * lock for reading
 */
extern void down_read(struct rw_semaphore *sem);
extern int __must_check down_read_killable(struct rw_semaphore *sem);

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
extern int down_read_trylock(struct rw_semaphore *sem);

/*
 * lock for writing
 */
extern void down_write(struct rw_semaphore *sem);
extern int __must_check down_write_killable(struct rw_semaphore *sem);

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
extern int down_write_trylock(struct rw_semaphore *sem);

/*
 * release a read lock
 */
extern void up_read(struct rw_semaphore *sem);

/*
 * release a write lock
 */
extern void up_write(struct rw_semaphore *sem);

/*
 * downgrade write lock to read lock
 */
extern void downgrade_write(struct rw_semaphore *sem);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
/*
 * nested locking. NOTE: rwsems are not allowed to recurse
 * (which occurs if the same task tries to acquire the same
 * lock instance multiple times), but multiple locks of the
 * same lock class might be taken, if the order of the locks
 * is always the same. This ordering rule can be expressed
 * to lockdep via the _nested() APIs, but enumerating the
 * subclasses that are used. (If the nesting relationship is
 * static then another method for expressing nested locking is
 * the explicit definition of lock class keys and the use of
 * lockdep_set_class() at lock initialization time.
 * See Documentation/locking/lockdep-design.txt for more details.)
 */
extern void down_read_nested(struct rw_semaphore *sem, int subclass);
extern void down_write_nested(struct rw_semaphore *sem, int subclass);
extern int down_write_killable_nested(struct rw_semaphore *sem, int subclass);
extern void _down_write_nest_lock(struct rw_semaphore *sem, struct lockdep_map *nest_lock);

# define down_write_nest_lock(sem, nest_lock)			\
do {								\
	typecheck(struct lockdep_map *, &(nest_lock)->dep_map);	\
	_down_write_nest_lock(sem, &(nest_lock)->dep_map);	\
} while (0);

/*
 * Take/release a lock when not the owner will release it.
 *
 * [ This API should be avoided as much as possible - the
 *   proper abstraction for this case is completions. ]
 */
extern void down_read_non_owner(struct rw_semaphore *sem);
extern void up_read_non_owner(struct rw_semaphore *sem);
#else
# define down_read_nested(sem, subclass)		down_read(sem)
# define down_write_nest_lock(sem, nest_lock)	down_write(sem)
# define down_write_nested(sem, subclass)	down_write(sem)
# define down_write_killable_nested(sem, subclass)	down_write_killable(sem)
# define down_read_non_owner(sem)		down_read(sem)
# define up_read_non_owner(sem)			up_read(sem)
#endif

#endif /* _LINUX_RWSEM_H */

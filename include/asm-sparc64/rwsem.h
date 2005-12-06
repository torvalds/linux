/* $Id: rwsem.h,v 1.5 2001/11/18 00:12:56 davem Exp $
 * rwsem.h: R/W semaphores implemented using CAS
 *
 * Written by David S. Miller (davem@redhat.com), 2001.
 * Derived from asm-i386/rwsem.h
 */
#ifndef _SPARC64_RWSEM_H
#define _SPARC64_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "please don't include asm/rwsem.h directly, use linux/rwsem.h instead"
#endif

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/rwsem-const.h>

struct rwsem_waiter;

struct rw_semaphore {
	signed int count;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
};

#define __RWSEM_INITIALIZER(name) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static __inline__ void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
}

extern void __down_read(struct rw_semaphore *sem);
extern int __down_read_trylock(struct rw_semaphore *sem);
extern void __down_write(struct rw_semaphore *sem);
extern int __down_write_trylock(struct rw_semaphore *sem);
extern void __up_read(struct rw_semaphore *sem);
extern void __up_write(struct rw_semaphore *sem);
extern void __downgrade_write(struct rw_semaphore *sem);

static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	return atomic_add_return(delta, (atomic_t *)(&sem->count));
}

static inline void rwsem_atomic_add(int delta, struct rw_semaphore *sem)
{
	atomic_add(delta, (atomic_t *)(&sem->count));
}

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return (sem->count != 0);
}

#endif /* __KERNEL__ */

#endif /* _SPARC64_RWSEM_H */

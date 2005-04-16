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

static __inline__ int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int tmp = delta;

	__asm__ __volatile__(
		"1:\tlduw	[%2], %%g1\n\t"
		"add		%%g1, %1, %%g7\n\t"
		"cas		[%2], %%g1, %%g7\n\t"
		"cmp		%%g1, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" membar	#StoreLoad | #StoreStore\n\t"
		"mov		%%g7, %0\n\t"
		: "=&r" (tmp)
		: "0" (tmp), "r" (sem)
		: "g1", "g7", "memory", "cc");

	return tmp + delta;
}

#define rwsem_atomic_add rwsem_atomic_update

static __inline__ __u16 rwsem_cmpxchgw(struct rw_semaphore *sem, __u16 __old, __u16 __new)
{
	u32 old = (sem->count & 0xffff0000) | (u32) __old;
	u32 new = (old & 0xffff0000) | (u32) __new;
	u32 prev;

again:
	__asm__ __volatile__("cas	[%2], %3, %0\n\t"
			     "membar	#StoreLoad | #StoreStore"
			     : "=&r" (prev)
			     : "0" (new), "r" (sem), "r" (old)
			     : "memory");

	/* To give the same semantics as x86 cmpxchgw, keep trying
	 * if only the upper 16-bits changed.
	 */
	if (prev != old &&
	    ((prev & 0xffff) == (old & 0xffff)))
		goto again;

	return prev & 0xffff;
}

static __inline__ signed long rwsem_cmpxchg(struct rw_semaphore *sem, signed long old, signed long new)
{
	return cmpxchg(&sem->count,old,new);
}

#endif /* __KERNEL__ */

#endif /* _SPARC64_RWSEM_H */

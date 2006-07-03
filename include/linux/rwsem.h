/* rwsem.h: R/W semaphores, public interface
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/atomic.h>

struct rw_semaphore;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
#include <linux/rwsem-spinlock.h> /* use a generic implementation */
#else
#include <asm/rwsem.h> /* use an arch-specific implementation */
#endif

/*
 * lock for reading
 */
static inline void down_read(struct rw_semaphore *sem)
{
	might_sleep();
	__down_read(sem);
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int down_read_trylock(struct rw_semaphore *sem)
{
	int ret;
	ret = __down_read_trylock(sem);
	return ret;
}

/*
 * lock for writing
 */
static inline void down_write(struct rw_semaphore *sem)
{
	might_sleep();
	__down_write(sem);
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int down_write_trylock(struct rw_semaphore *sem)
{
	int ret;
	ret = __down_write_trylock(sem);
	return ret;
}

/*
 * release a read lock
 */
static inline void up_read(struct rw_semaphore *sem)
{
	__up_read(sem);
}

/*
 * release a write lock
 */
static inline void up_write(struct rw_semaphore *sem)
{
	__up_write(sem);
}

/*
 * downgrade write lock to read lock
 */
static inline void downgrade_write(struct rw_semaphore *sem)
{
	__downgrade_write(sem);
}

#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_H */

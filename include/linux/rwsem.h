/* rwsem.h: R/W semaphores, public interface
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#define RWSEM_DEBUG 0

#ifdef __KERNEL__

#include <linux/config.h>
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

#ifndef rwsemtrace
#if RWSEM_DEBUG
extern void FASTCALL(rwsemtrace(struct rw_semaphore *sem, const char *str));
#else
#define rwsemtrace(SEM,FMT)
#endif
#endif

/*
 * lock for reading
 */
static inline void down_read(struct rw_semaphore *sem)
{
	might_sleep();
	rwsemtrace(sem,"Entering down_read");
	__down_read(sem);
	rwsemtrace(sem,"Leaving down_read");
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int down_read_trylock(struct rw_semaphore *sem)
{
	int ret;
	rwsemtrace(sem,"Entering down_read_trylock");
	ret = __down_read_trylock(sem);
	rwsemtrace(sem,"Leaving down_read_trylock");
	return ret;
}

/*
 * lock for writing
 */
static inline void down_write(struct rw_semaphore *sem)
{
	might_sleep();
	rwsemtrace(sem,"Entering down_write");
	__down_write(sem);
	rwsemtrace(sem,"Leaving down_write");
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int down_write_trylock(struct rw_semaphore *sem)
{
	int ret;
	rwsemtrace(sem,"Entering down_write_trylock");
	ret = __down_write_trylock(sem);
	rwsemtrace(sem,"Leaving down_write_trylock");
	return ret;
}

/*
 * release a read lock
 */
static inline void up_read(struct rw_semaphore *sem)
{
	rwsemtrace(sem,"Entering up_read");
	__up_read(sem);
	rwsemtrace(sem,"Leaving up_read");
}

/*
 * release a write lock
 */
static inline void up_write(struct rw_semaphore *sem)
{
	rwsemtrace(sem,"Entering up_write");
	__up_write(sem);
	rwsemtrace(sem,"Leaving up_write");
}

/*
 * downgrade write lock to read lock
 */
static inline void downgrade_write(struct rw_semaphore *sem)
{
	rwsemtrace(sem,"Entering downgrade_write");
	__downgrade_write(sem);
	rwsemtrace(sem,"Leaving downgrade_write");
}

#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_H */

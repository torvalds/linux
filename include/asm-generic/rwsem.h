#ifndef _ASM_GENERIC_RWSEM_H
#define _ASM_GENERIC_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "Please don't include <asm/rwsem.h> directly, use <linux/rwsem.h> instead."
#endif

#ifdef __KERNEL__

/*
 * R/W semaphores originally for PPC using the stuff in lib/rwsem.c.
 * Adapted largely from include/asm-i386/rwsem.h
 * by Paul Mackerras <paulus@samba.org>.
 */

/*
 * the semaphore definition
 */
#ifdef CONFIG_64BIT
# define RWSEM_ACTIVE_MASK		0xffffffffL
#else
# define RWSEM_ACTIVE_MASK		0x0000ffffL
#endif

#define RWSEM_UNLOCKED_VALUE		0x00000000L
#define RWSEM_ACTIVE_BIAS		0x00000001L
#define RWSEM_WAITING_BIAS		(-RWSEM_ACTIVE_MASK-1)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	if (unlikely(atomic_long_inc_return_acquire((atomic_long_t *)&sem->count) <= 0))
		rwsem_down_read_failed(sem);
}

static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	long tmp;

	while ((tmp = sem->count) >= 0) {
		if (tmp == cmpxchg_acquire(&sem->count, tmp,
				   tmp + RWSEM_ACTIVE_READ_BIAS)) {
			return 1;
		}
	}
	return 0;
}

/*
 * lock for writing
 */
static inline void __down_write_nested(struct rw_semaphore *sem, int subclass)
{
	long tmp;

	tmp = atomic_long_add_return_acquire(RWSEM_ACTIVE_WRITE_BIAS,
				     (atomic_long_t *)&sem->count);
	if (unlikely(tmp != RWSEM_ACTIVE_WRITE_BIAS))
		rwsem_down_write_failed(sem);
}

static inline void __down_write(struct rw_semaphore *sem)
{
	__down_write_nested(sem, 0);
}

static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	long tmp;

	tmp = cmpxchg_acquire(&sem->count, RWSEM_UNLOCKED_VALUE,
		      RWSEM_ACTIVE_WRITE_BIAS);
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	long tmp;

	tmp = atomic_long_dec_return_release((atomic_long_t *)&sem->count);
	if (unlikely(tmp < -1 && (tmp & RWSEM_ACTIVE_MASK) == 0))
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	if (unlikely(atomic_long_sub_return_release(RWSEM_ACTIVE_WRITE_BIAS,
				 (atomic_long_t *)&sem->count) < 0))
		rwsem_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(long delta, struct rw_semaphore *sem)
{
	atomic_long_add(delta, (atomic_long_t *)&sem->count);
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	long tmp;

	/*
	 * When downgrading from exclusive to shared ownership,
	 * anything inside the write-locked region cannot leak
	 * into the read side. In contrast, anything in the
	 * read-locked region is ok to be re-ordered into the
	 * write side. As such, rely on RELEASE semantics.
	 */
	tmp = atomic_long_add_return_release(-RWSEM_WAITING_BIAS,
				     (atomic_long_t *)&sem->count);
	if (tmp < 0)
		rwsem_downgrade_wake(sem);
}

/*
 * implement exchange and add functionality
 */
static inline long rwsem_atomic_update(long delta, struct rw_semaphore *sem)
{
	return atomic_long_add_return(delta, (atomic_long_t *)&sem->count);
}

#endif	/* __KERNEL__ */
#endif	/* _ASM_GENERIC_RWSEM_H */

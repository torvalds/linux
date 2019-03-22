/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The least significant 2 bits of the owner value has the following
 * meanings when set.
 *  - RWSEM_READER_OWNED (bit 0): The rwsem is owned by readers
 *  - RWSEM_ANONYMOUSLY_OWNED (bit 1): The rwsem is anonymously owned,
 *    i.e. the owner(s) cannot be readily determined. It can be reader
 *    owned or the owning writer is indeterminate.
 *
 * When a writer acquires a rwsem, it puts its task_struct pointer
 * into the owner field. It is cleared after an unlock.
 *
 * When a reader acquires a rwsem, it will also puts its task_struct
 * pointer into the owner field with both the RWSEM_READER_OWNED and
 * RWSEM_ANONYMOUSLY_OWNED bits set. On unlock, the owner field will
 * largely be left untouched. So for a free or reader-owned rwsem,
 * the owner value may contain information about the last reader that
 * acquires the rwsem. The anonymous bit is set because that particular
 * reader may or may not still own the lock.
 *
 * That information may be helpful in debugging cases where the system
 * seems to hang on a reader owned rwsem especially if only one reader
 * is involved. Ideally we would like to track all the readers that own
 * a rwsem, but the overhead is simply too big.
 */
#define RWSEM_READER_OWNED	(1UL << 0)
#define RWSEM_ANONYMOUSLY_OWNED	(1UL << 1)

#ifdef CONFIG_DEBUG_RWSEMS
# define DEBUG_RWSEMS_WARN_ON(c)	DEBUG_LOCKS_WARN_ON(c)
#else
# define DEBUG_RWSEMS_WARN_ON(c)
#endif

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

#define RWSEM_ACTIVE_BIAS		0x00000001L
#define RWSEM_WAITING_BIAS		(-RWSEM_ACTIVE_MASK-1)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
/*
 * All writes to owner are protected by WRITE_ONCE() to make sure that
 * store tearing can't happen as optimistic spinners may read and use
 * the owner value concurrently without lock. Read from owner, however,
 * may not need READ_ONCE() as long as the pointer value is only used
 * for comparison and isn't being dereferenced.
 */
static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
	WRITE_ONCE(sem->owner, current);
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
	WRITE_ONCE(sem->owner, NULL);
}

/*
 * The task_struct pointer of the last owning reader will be left in
 * the owner field.
 *
 * Note that the owner value just indicates the task has owned the rwsem
 * previously, it may not be the real owner or one of the real owners
 * anymore when that field is examined, so take it with a grain of salt.
 */
static inline void __rwsem_set_reader_owned(struct rw_semaphore *sem,
					    struct task_struct *owner)
{
	unsigned long val = (unsigned long)owner | RWSEM_READER_OWNED
						 | RWSEM_ANONYMOUSLY_OWNED;

	WRITE_ONCE(sem->owner, (struct task_struct *)val);
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
	__rwsem_set_reader_owned(sem, current);
}

/*
 * Return true if the a rwsem waiter can spin on the rwsem's owner
 * and steal the lock, i.e. the lock is not anonymously owned.
 * N.B. !owner is considered spinnable.
 */
static inline bool is_rwsem_owner_spinnable(struct task_struct *owner)
{
	return !((unsigned long)owner & RWSEM_ANONYMOUSLY_OWNED);
}

/*
 * Return true if rwsem is owned by an anonymous writer or readers.
 */
static inline bool rwsem_has_anonymous_owner(struct task_struct *owner)
{
	return (unsigned long)owner & RWSEM_ANONYMOUSLY_OWNED;
}

#ifdef CONFIG_DEBUG_RWSEMS
/*
 * With CONFIG_DEBUG_RWSEMS configured, it will make sure that if there
 * is a task pointer in owner of a reader-owned rwsem, it will be the
 * real owner or one of the real owners. The only exception is when the
 * unlock is done by up_read_non_owner().
 */
#define rwsem_clear_reader_owned rwsem_clear_reader_owned
static inline void rwsem_clear_reader_owned(struct rw_semaphore *sem)
{
	unsigned long val = (unsigned long)current | RWSEM_READER_OWNED
						   | RWSEM_ANONYMOUSLY_OWNED;
	if (READ_ONCE(sem->owner) == (struct task_struct *)val)
		cmpxchg_relaxed((unsigned long *)&sem->owner, val,
				RWSEM_READER_OWNED | RWSEM_ANONYMOUSLY_OWNED);
}
#endif

#else
static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
}

static inline void __rwsem_set_reader_owned(struct rw_semaphore *sem,
					   struct task_struct *owner)
{
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
}
#endif

#ifndef rwsem_clear_reader_owned
static inline void rwsem_clear_reader_owned(struct rw_semaphore *sem)
{
}
#endif

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	if (unlikely(atomic_long_inc_return_acquire(&sem->count) <= 0))
		rwsem_down_read_failed(sem);
}

static inline int __down_read_killable(struct rw_semaphore *sem)
{
	if (unlikely(atomic_long_inc_return_acquire(&sem->count) <= 0)) {
		if (IS_ERR(rwsem_down_read_failed_killable(sem)))
			return -EINTR;
	}

	return 0;
}

static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	long tmp;

	while ((tmp = atomic_long_read(&sem->count)) >= 0) {
		if (tmp == atomic_long_cmpxchg_acquire(&sem->count, tmp,
				   tmp + RWSEM_ACTIVE_READ_BIAS)) {
			return 1;
		}
	}
	return 0;
}

/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	long tmp;

	tmp = atomic_long_add_return_acquire(RWSEM_ACTIVE_WRITE_BIAS,
					     &sem->count);
	if (unlikely(tmp != RWSEM_ACTIVE_WRITE_BIAS))
		rwsem_down_write_failed(sem);
}

static inline int __down_write_killable(struct rw_semaphore *sem)
{
	long tmp;

	tmp = atomic_long_add_return_acquire(RWSEM_ACTIVE_WRITE_BIAS,
					     &sem->count);
	if (unlikely(tmp != RWSEM_ACTIVE_WRITE_BIAS))
		if (IS_ERR(rwsem_down_write_failed_killable(sem)))
			return -EINTR;
	return 0;
}

static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	long tmp;

	tmp = atomic_long_cmpxchg_acquire(&sem->count, RWSEM_UNLOCKED_VALUE,
		      RWSEM_ACTIVE_WRITE_BIAS);
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	long tmp;

	tmp = atomic_long_dec_return_release(&sem->count);
	if (unlikely(tmp < -1 && (tmp & RWSEM_ACTIVE_MASK) == 0))
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	if (unlikely(atomic_long_sub_return_release(RWSEM_ACTIVE_WRITE_BIAS,
						    &sem->count) < 0))
		rwsem_wake(sem);
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
	tmp = atomic_long_add_return_release(-RWSEM_WAITING_BIAS, &sem->count);
	if (tmp < 0)
		rwsem_downgrade_wake(sem);
}

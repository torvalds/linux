/*
 * include/asm-ppc/rwsem.h: R/W semaphores for SH using the stuff
 * in lib/rwsem.c.
 */

#ifndef _ASM_SH_RWSEM_H
#define _ASM_SH_RWSEM_H

#ifdef __KERNEL__
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/system.h>

/*
 * the semaphore definition
 */
struct rw_semaphore {
	long		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __RWSEM_DEP_MAP_INIT(lockname) , .dep_map = { .name = #lockname }
#else
# define __RWSEM_DEP_MAP_INIT(lockname)
#endif

#define __RWSEM_INITIALIZER(name) \
	{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, \
	  LIST_HEAD_INIT((name).wait_list) \
	  __RWSEM_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(name)		\
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

extern struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_downgrade_wake(struct rw_semaphore *sem);

extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)				\
do {						\
	static struct lock_class_key __key;	\
						\
	__init_rwsem((sem), #sem, &__key);	\
} while (0)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	if (atomic_inc_return((atomic_t *)(&sem->count)) > 0)
		smp_wmb();
	else
		rwsem_down_read_failed(sem);
}

static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	int tmp;

	while ((tmp = sem->count) >= 0) {
		if (tmp == cmpxchg(&sem->count, tmp,
				   tmp + RWSEM_ACTIVE_READ_BIAS)) {
			smp_wmb();
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
	int tmp;

	tmp = atomic_add_return(RWSEM_ACTIVE_WRITE_BIAS,
				(atomic_t *)(&sem->count));
	if (tmp == RWSEM_ACTIVE_WRITE_BIAS)
		smp_wmb();
	else
		rwsem_down_write_failed(sem);
}

static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	int tmp;

	tmp = cmpxchg(&sem->count, RWSEM_UNLOCKED_VALUE,
		      RWSEM_ACTIVE_WRITE_BIAS);
	smp_wmb();
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	int tmp;

	smp_wmb();
	tmp = atomic_dec_return((atomic_t *)(&sem->count));
	if (tmp < -1 && (tmp & RWSEM_ACTIVE_MASK) == 0)
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	smp_wmb();
	if (atomic_sub_return(RWSEM_ACTIVE_WRITE_BIAS,
			      (atomic_t *)(&sem->count)) < 0)
		rwsem_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(int delta, struct rw_semaphore *sem)
{
	atomic_add(delta, (atomic_t *)(&sem->count));
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	int tmp;

	smp_wmb();
	tmp = atomic_add_return(-RWSEM_WAITING_BIAS, (atomic_t *)(&sem->count));
	if (tmp < 0)
		rwsem_downgrade_wake(sem);
}

static inline void __down_write_nested(struct rw_semaphore *sem, int subclass)
{
	__down_write(sem);
}

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	smp_mb();
	return atomic_add_return(delta, (atomic_t *)(&sem->count));
}

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return (sem->count != 0);
}

#endif /* __KERNEL__ */
#endif /* _ASM_SH_RWSEM_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/rcuwait.h>
#include <linux/rcu_sync.h>
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*read_count;
	struct rw_semaphore	rw_sem; /* slowpath */
	struct rcuwait          writer; /* blocked writer */
	int			readers_block;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define __PERCPU_RWSEM_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname },
#else
#define __PERCPU_RWSEM_DEP_MAP_INIT(lockname)
#endif

#define __DEFINE_PERCPU_RWSEM(name, is_static)				\
static DEFINE_PER_CPU(unsigned int, __percpu_rwsem_rc_##name);		\
is_static struct percpu_rw_semaphore name = {				\
	.rss = __RCU_SYNC_INITIALIZER(name.rss),			\
	.read_count = &__percpu_rwsem_rc_##name,			\
	.rw_sem = __RWSEM_INITIALIZER(name.rw_sem),			\
	.writer = __RCUWAIT_INITIALIZER(name.writer),			\
	__PERCPU_RWSEM_DEP_MAP_INIT(name)				\
}

#define DEFINE_PERCPU_RWSEM(name)		\
	__DEFINE_PERCPU_RWSEM(name, /* not static */)
#define DEFINE_STATIC_PERCPU_RWSEM(name)	\
	__DEFINE_PERCPU_RWSEM(name, static)

extern bool __percpu_down_read(struct percpu_rw_semaphore *, bool);
extern void __percpu_up_read(struct percpu_rw_semaphore *);

static inline void percpu_down_read(struct percpu_rw_semaphore *sem)
{
	might_sleep();

	rwsem_acquire_read(&sem->dep_map, 0, 0, _RET_IP_);

	preempt_disable();
	/*
	 * We are in an RCU-sched read-side critical section, so the writer
	 * cannot both change sem->state from readers_fast and start checking
	 * counters while we are here. So if we see !sem->state, we know that
	 * the writer won't be checking until we're past the preempt_enable()
	 * and that once the synchronize_rcu() is done, the writer will see
	 * anything we did within this RCU-sched read-size critical section.
	 */
	__this_cpu_inc(*sem->read_count);
	if (unlikely(!rcu_sync_is_idle(&sem->rss)))
		__percpu_down_read(sem, false); /* Unconditional memory barrier */
	/*
	 * The preempt_enable() prevents the compiler from
	 * bleeding the critical section out.
	 */
	preempt_enable();
}

static inline bool percpu_down_read_trylock(struct percpu_rw_semaphore *sem)
{
	bool ret = true;

	preempt_disable();
	/*
	 * Same as in percpu_down_read().
	 */
	__this_cpu_inc(*sem->read_count);
	if (unlikely(!rcu_sync_is_idle(&sem->rss)))
		ret = __percpu_down_read(sem, true); /* Unconditional memory barrier */
	preempt_enable();
	/*
	 * The barrier() from preempt_enable() prevents the compiler from
	 * bleeding the critical section out.
	 */

	if (ret)
		rwsem_acquire_read(&sem->dep_map, 0, 1, _RET_IP_);

	return ret;
}

static inline void percpu_up_read(struct percpu_rw_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);

	preempt_disable();
	/*
	 * Same as in percpu_down_read().
	 */
	if (likely(rcu_sync_is_idle(&sem->rss)))
		__this_cpu_dec(*sem->read_count);
	else
		__percpu_up_read(sem); /* Unconditional memory barrier */
	preempt_enable();
}

extern void percpu_down_write(struct percpu_rw_semaphore *);
extern void percpu_up_write(struct percpu_rw_semaphore *);

extern int __percpu_init_rwsem(struct percpu_rw_semaphore *,
				const char *, struct lock_class_key *);

extern void percpu_free_rwsem(struct percpu_rw_semaphore *);

#define percpu_init_rwsem(sem)					\
({								\
	static struct lock_class_key rwsem_key;			\
	__percpu_init_rwsem(sem, #sem, &rwsem_key);		\
})

#define percpu_rwsem_is_held(sem)	lockdep_is_held(sem)
#define percpu_rwsem_assert_held(sem)	lockdep_assert_held(sem)

static inline void percpu_rwsem_release(struct percpu_rw_semaphore *sem,
					bool read, unsigned long ip)
{
	lock_release(&sem->dep_map, ip);
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	if (!read)
		atomic_long_set(&sem->rw_sem.owner, RWSEM_OWNER_UNKNOWN);
#endif
}

static inline void percpu_rwsem_acquire(struct percpu_rw_semaphore *sem,
					bool read, unsigned long ip)
{
	lock_acquire(&sem->dep_map, 0, 1, read, 1, NULL, ip);
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	if (!read)
		atomic_long_set(&sem->rw_sem.owner, (long)current);
#endif
}

#endif

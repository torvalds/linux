/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOCAL_LOCK_H
# error "Do not include directly, include linux/local_lock.h"
#endif

#include <linux/percpu-defs.h>
#include <linux/lockdep.h>

typedef struct {
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
	struct task_struct	*owner;
#endif
} local_lock_t;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define LL_DEP_MAP_INIT(lockname)			\
	.dep_map = {					\
		.name = #lockname,			\
		.wait_type_inner = LD_WAIT_CONFIG,	\
	}
#else
# define LL_DEP_MAP_INIT(lockname)
#endif

#define INIT_LOCAL_LOCK(lockname)	{ LL_DEP_MAP_INIT(lockname) }

#define __local_lock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));\
	lockdep_init_map_wait(&(lock)->dep_map, #lock, &__key, 0, LD_WAIT_CONFIG);\
} while (0)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static inline void local_lock_acquire(local_lock_t *l)
{
	lock_map_acquire(&l->dep_map);
	DEBUG_LOCKS_WARN_ON(l->owner);
	l->owner = current;
}

static inline void local_lock_release(local_lock_t *l)
{
	DEBUG_LOCKS_WARN_ON(l->owner != current);
	l->owner = NULL;
	lock_map_release(&l->dep_map);
}

#else /* CONFIG_DEBUG_LOCK_ALLOC */
static inline void local_lock_acquire(local_lock_t *l) { }
static inline void local_lock_release(local_lock_t *l) { }
#endif /* !CONFIG_DEBUG_LOCK_ALLOC */

#define __local_lock(lock)					\
	do {							\
		preempt_disable();				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_lock_irq(lock)					\
	do {							\
		local_irq_disable();				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_lock_irqsave(lock, flags)			\
	do {							\
		local_irq_save(flags);				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_unlock(lock)					\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		preempt_enable();				\
	} while (0)

#define __local_unlock_irq(lock)				\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		local_irq_enable();				\
	} while (0)

#define __local_unlock_irqrestore(lock, flags)			\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		local_irq_restore(flags);			\
	} while (0)

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOCAL_LOCK_H
# error "Do not include directly, include linux/local_lock.h"
#endif

#include <linux/percpu-defs.h>
#include <linux/lockdep.h>

#ifndef CONFIG_PREEMPT_RT

typedef struct {
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
	struct task_struct	*owner;
#endif
} local_lock_t;

/* local_trylock() and local_trylock_irqsave() only work with local_trylock_t */
typedef struct {
	local_lock_t	llock;
	u8		acquired;
} local_trylock_t;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define LOCAL_LOCK_DEBUG_INIT(lockname)		\
	.dep_map = {					\
		.name = #lockname,			\
		.wait_type_inner = LD_WAIT_CONFIG,	\
		.lock_type = LD_LOCK_PERCPU,		\
	},						\
	.owner = NULL,

# define LOCAL_TRYLOCK_DEBUG_INIT(lockname)		\
	.llock = { LOCAL_LOCK_DEBUG_INIT((lockname).llock) },

static inline void local_lock_acquire(local_lock_t *l)
{
	lock_map_acquire(&l->dep_map);
	DEBUG_LOCKS_WARN_ON(l->owner);
	l->owner = current;
}

static inline void local_trylock_acquire(local_lock_t *l)
{
	lock_map_acquire_try(&l->dep_map);
	DEBUG_LOCKS_WARN_ON(l->owner);
	l->owner = current;
}

static inline void local_lock_release(local_lock_t *l)
{
	DEBUG_LOCKS_WARN_ON(l->owner != current);
	l->owner = NULL;
	lock_map_release(&l->dep_map);
}

static inline void local_lock_debug_init(local_lock_t *l)
{
	l->owner = NULL;
}
#else /* CONFIG_DEBUG_LOCK_ALLOC */
# define LOCAL_LOCK_DEBUG_INIT(lockname)
# define LOCAL_TRYLOCK_DEBUG_INIT(lockname)
static inline void local_lock_acquire(local_lock_t *l) { }
static inline void local_trylock_acquire(local_lock_t *l) { }
static inline void local_lock_release(local_lock_t *l) { }
static inline void local_lock_debug_init(local_lock_t *l) { }
#endif /* !CONFIG_DEBUG_LOCK_ALLOC */

#define INIT_LOCAL_LOCK(lockname)	{ LOCAL_LOCK_DEBUG_INIT(lockname) }
#define INIT_LOCAL_TRYLOCK(lockname)	{ LOCAL_TRYLOCK_DEBUG_INIT(lockname) }

#define __local_lock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));\
	lockdep_init_map_type(&(lock)->dep_map, #lock, &__key,  \
			      0, LD_WAIT_CONFIG, LD_WAIT_INV,	\
			      LD_LOCK_PERCPU);			\
	local_lock_debug_init(lock);				\
} while (0)

#define __local_trylock_init(lock) __local_lock_init(lock.llock)

#define __spinlock_nested_bh_init(lock)				\
do {								\
	static struct lock_class_key __key;			\
								\
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));\
	lockdep_init_map_type(&(lock)->dep_map, #lock, &__key,  \
			      0, LD_WAIT_CONFIG, LD_WAIT_INV,	\
			      LD_LOCK_NORMAL);			\
	local_lock_debug_init(lock);				\
} while (0)

#define __local_lock_acquire(lock)					\
	do {								\
		local_trylock_t *tl;					\
		local_lock_t *l;					\
									\
		l = (local_lock_t *)this_cpu_ptr(lock);			\
		tl = (local_trylock_t *)l;				\
		_Generic((lock),					\
			__percpu local_trylock_t *: ({			\
				lockdep_assert(tl->acquired == 0);	\
				WRITE_ONCE(tl->acquired, 1);		\
			}),						\
			__percpu local_lock_t *: (void)0);		\
		local_lock_acquire(l);					\
	} while (0)

#define __local_lock(lock)					\
	do {							\
		preempt_disable();				\
		__local_lock_acquire(lock);			\
	} while (0)

#define __local_lock_irq(lock)					\
	do {							\
		local_irq_disable();				\
		__local_lock_acquire(lock);			\
	} while (0)

#define __local_lock_irqsave(lock, flags)			\
	do {							\
		local_irq_save(flags);				\
		__local_lock_acquire(lock);			\
	} while (0)

#define __local_trylock(lock)					\
	({							\
		local_trylock_t *tl;				\
								\
		preempt_disable();				\
		tl = this_cpu_ptr(lock);			\
		if (READ_ONCE(tl->acquired)) {			\
			preempt_enable();			\
			tl = NULL;				\
		} else {					\
			WRITE_ONCE(tl->acquired, 1);		\
			local_trylock_acquire(			\
				(local_lock_t *)tl);		\
		}						\
		!!tl;						\
	})

#define __local_trylock_irqsave(lock, flags)			\
	({							\
		local_trylock_t *tl;				\
								\
		local_irq_save(flags);				\
		tl = this_cpu_ptr(lock);			\
		if (READ_ONCE(tl->acquired)) {			\
			local_irq_restore(flags);		\
			tl = NULL;				\
		} else {					\
			WRITE_ONCE(tl->acquired, 1);		\
			local_trylock_acquire(			\
				(local_lock_t *)tl);		\
		}						\
		!!tl;						\
	})

#define __local_lock_release(lock)					\
	do {								\
		local_trylock_t *tl;					\
		local_lock_t *l;					\
									\
		l = (local_lock_t *)this_cpu_ptr(lock);			\
		tl = (local_trylock_t *)l;				\
		local_lock_release(l);					\
		_Generic((lock),					\
			__percpu local_trylock_t *: ({			\
				lockdep_assert(tl->acquired == 1);	\
				WRITE_ONCE(tl->acquired, 0);		\
			}),						\
			__percpu local_lock_t *: (void)0);		\
	} while (0)

#define __local_unlock(lock)					\
	do {							\
		__local_lock_release(lock);			\
		preempt_enable();				\
	} while (0)

#define __local_unlock_irq(lock)				\
	do {							\
		__local_lock_release(lock);			\
		local_irq_enable();				\
	} while (0)

#define __local_unlock_irqrestore(lock, flags)			\
	do {							\
		__local_lock_release(lock);			\
		local_irq_restore(flags);			\
	} while (0)

#define __local_lock_nested_bh(lock)				\
	do {							\
		lockdep_assert_in_softirq();			\
		local_lock_acquire(this_cpu_ptr(lock));	\
	} while (0)

#define __local_unlock_nested_bh(lock)				\
	local_lock_release(this_cpu_ptr(lock))

#else /* !CONFIG_PREEMPT_RT */

/*
 * On PREEMPT_RT local_lock maps to a per CPU spinlock, which protects the
 * critical section while staying preemptible.
 */
typedef spinlock_t local_lock_t;
typedef spinlock_t local_trylock_t;

#define INIT_LOCAL_LOCK(lockname) __LOCAL_SPIN_LOCK_UNLOCKED((lockname))
#define INIT_LOCAL_TRYLOCK(lockname) __LOCAL_SPIN_LOCK_UNLOCKED((lockname))

#define __local_lock_init(l)					\
	do {							\
		local_spin_lock_init((l));			\
	} while (0)

#define __local_trylock_init(l)			__local_lock_init(l)

#define __local_lock(__lock)					\
	do {							\
		migrate_disable();				\
		spin_lock(this_cpu_ptr((__lock)));		\
	} while (0)

#define __local_lock_irq(lock)			__local_lock(lock)

#define __local_lock_irqsave(lock, flags)			\
	do {							\
		typecheck(unsigned long, flags);		\
		flags = 0;					\
		__local_lock(lock);				\
	} while (0)

#define __local_unlock(__lock)					\
	do {							\
		spin_unlock(this_cpu_ptr((__lock)));		\
		migrate_enable();				\
	} while (0)

#define __local_unlock_irq(lock)		__local_unlock(lock)

#define __local_unlock_irqrestore(lock, flags)	__local_unlock(lock)

#define __local_lock_nested_bh(lock)				\
do {								\
	lockdep_assert_in_softirq_func();			\
	spin_lock(this_cpu_ptr(lock));				\
} while (0)

#define __local_unlock_nested_bh(lock)				\
do {								\
	spin_unlock(this_cpu_ptr((lock)));			\
} while (0)

#define __local_trylock(lock)					\
	({							\
		int __locked;					\
								\
		if (in_nmi() | in_hardirq()) {			\
			__locked = 0;				\
		} else {					\
			migrate_disable();			\
			__locked = spin_trylock(this_cpu_ptr((lock)));	\
			if (!__locked)				\
				migrate_enable();		\
		}						\
		__locked;					\
	})

#define __local_trylock_irqsave(lock, flags)			\
	({							\
		typecheck(unsigned long, flags);		\
		flags = 0;					\
		__local_trylock(lock);				\
	})

#endif /* CONFIG_PREEMPT_RT */

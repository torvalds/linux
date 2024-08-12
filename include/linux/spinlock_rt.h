// SPDX-License-Identifier: GPL-2.0-only
#ifndef __LINUX_SPINLOCK_RT_H
#define __LINUX_SPINLOCK_RT_H

#ifndef __LINUX_INSIDE_SPINLOCK_H
#error Do not include directly. Use spinlock.h
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern void __rt_spin_lock_init(spinlock_t *lock, const char *name,
				struct lock_class_key *key, bool percpu);
#else
static inline void __rt_spin_lock_init(spinlock_t *lock, const char *name,
				struct lock_class_key *key, bool percpu)
{
}
#endif

#define spin_lock_init(slock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	rt_mutex_base_init(&(slock)->lock);			\
	__rt_spin_lock_init(slock, #slock, &__key, false);	\
} while (0)

#define local_spin_lock_init(slock)				\
do {								\
	static struct lock_class_key __key;			\
								\
	rt_mutex_base_init(&(slock)->lock);			\
	__rt_spin_lock_init(slock, #slock, &__key, true);	\
} while (0)

extern void rt_spin_lock(spinlock_t *lock) __acquires(lock);
extern void rt_spin_lock_nested(spinlock_t *lock, int subclass)	__acquires(lock);
extern void rt_spin_lock_nest_lock(spinlock_t *lock, struct lockdep_map *nest_lock) __acquires(lock);
extern void rt_spin_unlock(spinlock_t *lock)	__releases(lock);
extern void rt_spin_lock_unlock(spinlock_t *lock);
extern int rt_spin_trylock_bh(spinlock_t *lock);
extern int rt_spin_trylock(spinlock_t *lock);

static __always_inline void spin_lock(spinlock_t *lock)
{
	rt_spin_lock(lock);
}

#ifdef CONFIG_LOCKDEP
# define __spin_lock_nested(lock, subclass)				\
	rt_spin_lock_nested(lock, subclass)

# define __spin_lock_nest_lock(lock, nest_lock)				\
	do {								\
		typecheck(struct lockdep_map *, &(nest_lock)->dep_map);	\
		rt_spin_lock_nest_lock(lock, &(nest_lock)->dep_map);	\
	} while (0)
# define __spin_lock_irqsave_nested(lock, flags, subclass)	\
	do {							\
		typecheck(unsigned long, flags);		\
		flags = 0;					\
		__spin_lock_nested(lock, subclass);		\
	} while (0)

#else
 /*
  * Always evaluate the 'subclass' argument to avoid that the compiler
  * warns about set-but-not-used variables when building with
  * CONFIG_DEBUG_LOCK_ALLOC=n and with W=1.
  */
# define __spin_lock_nested(lock, subclass)	spin_lock(((void)(subclass), (lock)))
# define __spin_lock_nest_lock(lock, subclass)	spin_lock(((void)(subclass), (lock)))
# define __spin_lock_irqsave_nested(lock, flags, subclass)	\
	spin_lock_irqsave(((void)(subclass), (lock)), flags)
#endif

#define spin_lock_nested(lock, subclass)		\
	__spin_lock_nested(lock, subclass)

#define spin_lock_nest_lock(lock, nest_lock)		\
	__spin_lock_nest_lock(lock, nest_lock)

#define spin_lock_irqsave_nested(lock, flags, subclass)	\
	__spin_lock_irqsave_nested(lock, flags, subclass)

static __always_inline void spin_lock_bh(spinlock_t *lock)
{
	/* Investigate: Drop bh when blocking ? */
	local_bh_disable();
	rt_spin_lock(lock);
}

static __always_inline void spin_lock_irq(spinlock_t *lock)
{
	rt_spin_lock(lock);
}

#define spin_lock_irqsave(lock, flags)			 \
	do {						 \
		typecheck(unsigned long, flags);	 \
		flags = 0;				 \
		spin_lock(lock);			 \
	} while (0)

static __always_inline void spin_unlock(spinlock_t *lock)
{
	rt_spin_unlock(lock);
}

static __always_inline void spin_unlock_bh(spinlock_t *lock)
{
	rt_spin_unlock(lock);
	local_bh_enable();
}

static __always_inline void spin_unlock_irq(spinlock_t *lock)
{
	rt_spin_unlock(lock);
}

static __always_inline void spin_unlock_irqrestore(spinlock_t *lock,
						   unsigned long flags)
{
	rt_spin_unlock(lock);
}

#define spin_trylock(lock)				\
	__cond_lock(lock, rt_spin_trylock(lock))

#define spin_trylock_bh(lock)				\
	__cond_lock(lock, rt_spin_trylock_bh(lock))

#define spin_trylock_irq(lock)				\
	__cond_lock(lock, rt_spin_trylock(lock))

#define __spin_trylock_irqsave(lock, flags)		\
({							\
	int __locked;					\
							\
	typecheck(unsigned long, flags);		\
	flags = 0;					\
	__locked = spin_trylock(lock);			\
	__locked;					\
})

#define spin_trylock_irqsave(lock, flags)		\
	__cond_lock(lock, __spin_trylock_irqsave(lock, flags))

#define spin_is_contended(lock)		(((void)(lock), 0))

static inline int spin_is_locked(spinlock_t *lock)
{
	return rt_mutex_base_is_locked(&lock->lock);
}

#define assert_spin_locked(lock) BUG_ON(!spin_is_locked(lock))

#include <linux/rwlock_rt.h>

#endif

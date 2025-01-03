// SPDX-License-Identifier: GPL-2.0-only
#ifndef __LINUX_RWLOCK_RT_H
#define __LINUX_RWLOCK_RT_H

#ifndef __LINUX_SPINLOCK_RT_H
#error Do not #include directly. Use <linux/spinlock.h>.
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern void __rt_rwlock_init(rwlock_t *rwlock, const char *name,
			     struct lock_class_key *key);
#else
static inline void __rt_rwlock_init(rwlock_t *rwlock, char *name,
				    struct lock_class_key *key)
{
}
#endif

#define rwlock_init(rwl)				\
do {							\
	static struct lock_class_key __key;		\
							\
	init_rwbase_rt(&(rwl)->rwbase);			\
	__rt_rwlock_init(rwl, #rwl, &__key);		\
} while (0)

extern void rt_read_lock(rwlock_t *rwlock)	__acquires(rwlock);
extern int rt_read_trylock(rwlock_t *rwlock);
extern void rt_read_unlock(rwlock_t *rwlock)	__releases(rwlock);
extern void rt_write_lock(rwlock_t *rwlock)	__acquires(rwlock);
extern void rt_write_lock_nested(rwlock_t *rwlock, int subclass)	__acquires(rwlock);
extern int rt_write_trylock(rwlock_t *rwlock);
extern void rt_write_unlock(rwlock_t *rwlock)	__releases(rwlock);

static __always_inline void read_lock(rwlock_t *rwlock)
{
	rt_read_lock(rwlock);
}

static __always_inline void read_lock_bh(rwlock_t *rwlock)
{
	local_bh_disable();
	rt_read_lock(rwlock);
}

static __always_inline void read_lock_irq(rwlock_t *rwlock)
{
	rt_read_lock(rwlock);
}

#define read_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		rt_read_lock(lock);			\
		flags = 0;				\
	} while (0)

#define read_trylock(lock)	__cond_lock(lock, rt_read_trylock(lock))

static __always_inline void read_unlock(rwlock_t *rwlock)
{
	rt_read_unlock(rwlock);
}

static __always_inline void read_unlock_bh(rwlock_t *rwlock)
{
	rt_read_unlock(rwlock);
	local_bh_enable();
}

static __always_inline void read_unlock_irq(rwlock_t *rwlock)
{
	rt_read_unlock(rwlock);
}

static __always_inline void read_unlock_irqrestore(rwlock_t *rwlock,
						   unsigned long flags)
{
	rt_read_unlock(rwlock);
}

static __always_inline void write_lock(rwlock_t *rwlock)
{
	rt_write_lock(rwlock);
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static __always_inline void write_lock_nested(rwlock_t *rwlock, int subclass)
{
	rt_write_lock_nested(rwlock, subclass);
}
#else
#define write_lock_nested(lock, subclass)	rt_write_lock(((void)(subclass), (lock)))
#endif

static __always_inline void write_lock_bh(rwlock_t *rwlock)
{
	local_bh_disable();
	rt_write_lock(rwlock);
}

static __always_inline void write_lock_irq(rwlock_t *rwlock)
{
	rt_write_lock(rwlock);
}

#define write_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		rt_write_lock(lock);			\
		flags = 0;				\
	} while (0)

#define write_trylock(lock)	__cond_lock(lock, rt_write_trylock(lock))

#define write_trylock_irqsave(lock, flags)		\
({							\
	int __locked;					\
							\
	typecheck(unsigned long, flags);		\
	flags = 0;					\
	__locked = write_trylock(lock);			\
	__locked;					\
})

static __always_inline void write_unlock(rwlock_t *rwlock)
{
	rt_write_unlock(rwlock);
}

static __always_inline void write_unlock_bh(rwlock_t *rwlock)
{
	rt_write_unlock(rwlock);
	local_bh_enable();
}

static __always_inline void write_unlock_irq(rwlock_t *rwlock)
{
	rt_write_unlock(rwlock);
}

static __always_inline void write_unlock_irqrestore(rwlock_t *rwlock,
						    unsigned long flags)
{
	rt_write_unlock(rwlock);
}

#define rwlock_is_contended(lock)		(((void)(lock), 0))

#endif /* __LINUX_RWLOCK_RT_H */

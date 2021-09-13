#ifndef __LINUX_RWLOCK_TYPES_H
#define __LINUX_RWLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H)
# error "Do not include directly, include spinlock_types.h"
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define RW_DEP_MAP_INIT(lockname)					\
	.dep_map = {							\
		.name = #lockname,					\
		.wait_type_inner = LD_WAIT_CONFIG,			\
	}
#else
# define RW_DEP_MAP_INIT(lockname)
#endif

#ifndef CONFIG_PREEMPT_RT
/*
 * generic rwlock type definitions and initializers
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */
typedef struct {
	arch_rwlock_t raw_lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned int magic, owner_cpu;
	void *owner;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} rwlock_t;

#define RWLOCK_MAGIC		0xdeaf1eed

#ifdef CONFIG_DEBUG_SPINLOCK
#define __RW_LOCK_UNLOCKED(lockname)					\
	(rwlock_t)	{	.raw_lock = __ARCH_RW_LOCK_UNLOCKED,	\
				.magic = RWLOCK_MAGIC,			\
				.owner = SPINLOCK_OWNER_INIT,		\
				.owner_cpu = -1,			\
				RW_DEP_MAP_INIT(lockname) }
#else
#define __RW_LOCK_UNLOCKED(lockname) \
	(rwlock_t)	{	.raw_lock = __ARCH_RW_LOCK_UNLOCKED,	\
				RW_DEP_MAP_INIT(lockname) }
#endif

#define DEFINE_RWLOCK(x)	rwlock_t x = __RW_LOCK_UNLOCKED(x)

#else /* !CONFIG_PREEMPT_RT */

#include <linux/rwbase_rt.h>

typedef struct {
	struct rwbase_rt	rwbase;
	atomic_t		readers;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} rwlock_t;

#define __RWLOCK_RT_INITIALIZER(name)					\
{									\
	.rwbase = __RWBASE_INITIALIZER(name),				\
	RW_DEP_MAP_INIT(name)						\
}

#define __RW_LOCK_UNLOCKED(name) __RWLOCK_RT_INITIALIZER(name)

#define DEFINE_RWLOCK(name)						\
	rwlock_t name = __RW_LOCK_UNLOCKED(name)

#endif /* CONFIG_PREEMPT_RT */

#endif /* __LINUX_RWLOCK_TYPES_H */

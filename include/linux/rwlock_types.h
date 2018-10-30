#ifndef __LINUX_RWLOCK_TYPES_H
#define __LINUX_RWLOCK_TYPES_H

/*
 * include/linux/rwlock_types.h - generic rwlock type definitions
 *				  and initializers
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

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define RW_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
#else
# define RW_DEP_MAP_INIT(lockname)
#endif

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

#endif /* __LINUX_RWLOCK_TYPES_H */

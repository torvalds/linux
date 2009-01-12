/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains mutex debugging related internal prototypes, for the
 * !CONFIG_DEBUG_MUTEXES case. Most of them are NOPs:
 */

#define spin_lock_mutex(lock, flags) \
		do { spin_lock(lock); (void)(flags); } while (0)
#define spin_unlock_mutex(lock, flags) \
		do { spin_unlock(lock); (void)(flags); } while (0)
#define mutex_remove_waiter(lock, waiter, ti) \
		__list_del((waiter)->list.prev, (waiter)->list.next)

#ifdef CONFIG_SMP
static inline void mutex_set_owner(struct mutex *lock)
{
	lock->owner = current_thread_info();
}

static inline void mutex_clear_owner(struct mutex *lock)
{
	lock->owner = NULL;
}
#else
static inline void mutex_set_owner(struct mutex *lock)
{
}

static inline void mutex_clear_owner(struct mutex *lock)
{
}
#endif

#define debug_mutex_wake_waiter(lock, waiter)		do { } while (0)
#define debug_mutex_free_waiter(waiter)			do { } while (0)
#define debug_mutex_add_waiter(lock, waiter, ti)	do { } while (0)
#define debug_mutex_unlock(lock)			do { } while (0)
#define debug_mutex_init(lock, name, key)		do { } while (0)

static inline void
debug_mutex_lock_common(struct mutex *lock, struct mutex_waiter *waiter)
{
}

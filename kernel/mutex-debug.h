/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains mutex debugging related internal declarations,
 * prototypes and inline functions, for the CONFIG_DEBUG_MUTEXES case.
 * More details are in kernel/mutex-debug.c.
 */

extern spinlock_t debug_mutex_lock;
extern struct list_head debug_mutex_held_locks;
extern int debug_mutex_on;

/*
 * In the debug case we carry the caller's instruction pointer into
 * other functions, but we dont want the function argument overhead
 * in the nondebug case - hence these macros:
 */
#define __IP_DECL__		, unsigned long ip
#define __IP__			, ip
#define __RET_IP__		, (unsigned long)__builtin_return_address(0)

/*
 * This must be called with lock->wait_lock held.
 */
extern void debug_mutex_set_owner(struct mutex *lock,
				  struct thread_info *new_owner __IP_DECL__);

static inline void debug_mutex_clear_owner(struct mutex *lock)
{
	lock->owner = NULL;
}

extern void debug_mutex_init_waiter(struct mutex_waiter *waiter);
extern void debug_mutex_wake_waiter(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void debug_mutex_free_waiter(struct mutex_waiter *waiter);
extern void debug_mutex_add_waiter(struct mutex *lock,
				   struct mutex_waiter *waiter,
				   struct thread_info *ti __IP_DECL__);
extern void mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
				struct thread_info *ti);
extern void debug_mutex_unlock(struct mutex *lock);
extern void debug_mutex_init(struct mutex *lock, const char *name);

#define debug_spin_lock(lock)				\
	do {						\
		local_irq_disable();			\
		if (debug_mutex_on)			\
			spin_lock(lock);		\
	} while (0)

#define debug_spin_unlock(lock)				\
	do {						\
		if (debug_mutex_on)			\
			spin_unlock(lock);		\
		local_irq_enable();			\
		preempt_check_resched();		\
	} while (0)

#define debug_spin_lock_save(lock, flags)		\
	do {						\
		local_irq_save(flags);			\
		if (debug_mutex_on)			\
			spin_lock(lock);		\
	} while (0)

#define debug_spin_lock_restore(lock, flags)		\
	do {						\
		if (debug_mutex_on)			\
			spin_unlock(lock);		\
		local_irq_restore(flags);		\
		preempt_check_resched();		\
	} while (0)

#define spin_lock_mutex(lock)				\
	do {						\
		struct mutex *l = container_of(lock, struct mutex, wait_lock); \
							\
		DEBUG_WARN_ON(in_interrupt());		\
		debug_spin_lock(&debug_mutex_lock);	\
		spin_lock(lock);			\
		DEBUG_WARN_ON(l->magic != l);		\
	} while (0)

#define spin_unlock_mutex(lock)				\
	do {						\
		spin_unlock(lock);			\
		debug_spin_unlock(&debug_mutex_lock);	\
	} while (0)

#define DEBUG_OFF()					\
do {							\
	if (debug_mutex_on) {				\
		debug_mutex_on = 0;			\
		console_verbose();			\
		if (spin_is_locked(&debug_mutex_lock))	\
			spin_unlock(&debug_mutex_lock);	\
	}						\
} while (0)

#define DEBUG_BUG()					\
do {							\
	if (debug_mutex_on) {				\
		DEBUG_OFF();				\
		BUG();					\
	}						\
} while (0)

#define DEBUG_WARN_ON(c)				\
do {							\
	if (unlikely(c && debug_mutex_on)) {		\
		DEBUG_OFF();				\
		WARN_ON(1);				\
	}						\
} while (0)

# define DEBUG_BUG_ON(c)				\
do {							\
	if (unlikely(c))				\
		DEBUG_BUG();				\
} while (0)

#ifdef CONFIG_SMP
# define SMP_DEBUG_WARN_ON(c)			DEBUG_WARN_ON(c)
# define SMP_DEBUG_BUG_ON(c)			DEBUG_BUG_ON(c)
#else
# define SMP_DEBUG_WARN_ON(c)			do { } while (0)
# define SMP_DEBUG_BUG_ON(c)			do { } while (0)
#endif


/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */

/*
 * This is the control structure for tasks blocked on mutex, which resides
 * on the blocked task's kernel stack:
 */
struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
	struct ww_acquire_ctx	*ww_ctx;
#ifdef CONFIG_DEBUG_MUTEXES
	void			*magic;
#endif
};

/*
 * @owner: contains: 'struct task_struct *' to the current lock owner,
 * NULL means not owned. Since task_struct pointers are aligned at
 * at least L1_CACHE_BYTES, we have low bits to store extra state.
 *
 * Bit0 indicates a non-empty waiter list; unlock must issue a wakeup.
 * Bit1 indicates unlock needs to hand the lock to the top-waiter
 * Bit2 indicates handoff has been done and we're waiting for pickup.
 */
#define MUTEX_FLAG_WAITERS	0x01
#define MUTEX_FLAG_HANDOFF	0x02
#define MUTEX_FLAG_PICKUP	0x04

#define MUTEX_FLAGS		0x07

/*
 * Internal helper function; C doesn't allow us to hide it :/
 *
 * DO NOT USE (outside of mutex & scheduler code).
 */
static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	if (!lock)
		return NULL;
	return (struct task_struct *)(atomic_long_read(&lock->owner) & ~MUTEX_FLAGS);
}

#ifdef CONFIG_DEBUG_MUTEXES
extern void debug_mutex_lock_common(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void debug_mutex_wake_waiter(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void debug_mutex_free_waiter(struct mutex_waiter *waiter);
extern void debug_mutex_add_waiter(struct mutex *lock,
				   struct mutex_waiter *waiter,
				   struct task_struct *task);
extern void debug_mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
				      struct task_struct *task);
extern void debug_mutex_unlock(struct mutex *lock);
extern void debug_mutex_init(struct mutex *lock, const char *name,
			     struct lock_class_key *key);
#else /* CONFIG_DEBUG_MUTEXES */
# define debug_mutex_lock_common(lock, waiter)		do { } while (0)
# define debug_mutex_wake_waiter(lock, waiter)		do { } while (0)
# define debug_mutex_free_waiter(waiter)		do { } while (0)
# define debug_mutex_add_waiter(lock, waiter, ti)	do { } while (0)
# define debug_mutex_remove_waiter(lock, waiter, ti)	do { } while (0)
# define debug_mutex_unlock(lock)			do { } while (0)
# define debug_mutex_init(lock, name, key)		do { } while (0)
#endif /* !CONFIG_DEBUG_MUTEXES */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains mutex deging related internal declarations,
 * prototypes and inline functions, for the CONFIG_DE_MUTEXES case.
 * More details are in kernel/mutex-de.c.
 */

/*
 * This must be called with lock->wait_lock held.
 */
extern void de_mutex_lock_common(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void de_mutex_wake_waiter(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void de_mutex_free_waiter(struct mutex_waiter *waiter);
extern void de_mutex_add_waiter(struct mutex *lock,
				   struct mutex_waiter *waiter,
				   struct task_struct *task);
extern void mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
				struct task_struct *task);
extern void de_mutex_unlock(struct mutex *lock);
extern void de_mutex_init(struct mutex *lock, const char *name,
			     struct lock_class_key *key);

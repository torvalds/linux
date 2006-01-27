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

#define spin_lock_mutex(lock)			spin_lock(lock)
#define spin_unlock_mutex(lock)			spin_unlock(lock)
#define mutex_remove_waiter(lock, waiter, ti) \
		__list_del((waiter)->list.prev, (waiter)->list.next)

#define DEBUG_WARN_ON(c)				do { } while (0)
#define debug_mutex_set_owner(lock, new_owner)		do { } while (0)
#define debug_mutex_clear_owner(lock)			do { } while (0)
#define debug_mutex_init_waiter(waiter)			do { } while (0)
#define debug_mutex_wake_waiter(lock, waiter)		do { } while (0)
#define debug_mutex_free_waiter(waiter)			do { } while (0)
#define debug_mutex_add_waiter(lock, waiter, ti, ip)	do { } while (0)
#define debug_mutex_unlock(lock)			do { } while (0)
#define debug_mutex_init(lock, name)			do { } while (0)

/*
 * Return-address parameters/declarations. They are very useful for
 * debugging, but add overhead in the !DEBUG case - so we go the
 * trouble of using this not too elegant but zero-cost solution:
 */
#define __IP_DECL__
#define __IP__
#define __RET_IP__


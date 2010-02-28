/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

struct srcu_struct_array {
	int c[2];
};

struct srcu_struct {
	int completed;
	struct srcu_struct_array *per_cpu_ref;
	struct mutex mutex;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

#ifndef CONFIG_PREEMPT
#define srcu_barrier() barrier()
#else /* #ifndef CONFIG_PREEMPT */
#define srcu_barrier()
#endif /* #else #ifndef CONFIG_PREEMPT */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *sp, const char *name,
		       struct lock_class_key *key);

#define init_srcu_struct(sp) \
({ \
	static struct lock_class_key __srcu_key; \
	\
	__init_srcu_struct((sp), #sp, &__srcu_key); \
})

# define srcu_read_acquire(sp) \
		lock_acquire(&(sp)->dep_map, 0, 0, 2, 1, NULL, _THIS_IP_)
# define srcu_read_release(sp) \
		lock_release(&(sp)->dep_map, 1, _THIS_IP_)

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

int init_srcu_struct(struct srcu_struct *sp);

# define srcu_read_acquire(sp)  do { } while (0)
# define srcu_read_release(sp)  do { } while (0)

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

void cleanup_srcu_struct(struct srcu_struct *sp);
int __srcu_read_lock(struct srcu_struct *sp) __acquires(sp);
void __srcu_read_unlock(struct srcu_struct *sp, int idx) __releases(sp);
void synchronize_srcu(struct srcu_struct *sp);
void synchronize_srcu_expedited(struct srcu_struct *sp);
long srcu_batches_completed(struct srcu_struct *sp);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

/**
 * srcu_read_lock_held - might we be in SRCU read-side critical section?
 *
 * If CONFIG_PROVE_LOCKING is selected and enabled, returns nonzero iff in
 * an SRCU read-side critical section.  In absence of CONFIG_PROVE_LOCKING,
 * this assumes we are in an SRCU read-side critical section unless it can
 * prove otherwise.
 */
static inline int srcu_read_lock_held(struct srcu_struct *sp)
{
	if (debug_locks)
		return lock_is_held(&sp->dep_map);
	return 1;
}

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

static inline int srcu_read_lock_held(struct srcu_struct *sp)
{
	return 1;
}

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/**
 * srcu_dereference - fetch SRCU-protected pointer with checking
 *
 * Makes rcu_dereference_check() do the dirty work.
 */
#define srcu_dereference(p, sp) \
		rcu_dereference_check(p, srcu_read_lock_held(sp))

/**
 * srcu_read_lock - register a new reader for an SRCU-protected structure.
 * @sp: srcu_struct in which to register the new reader.
 *
 * Enter an SRCU read-side critical section.  Note that SRCU read-side
 * critical sections may be nested.
 */
static inline int srcu_read_lock(struct srcu_struct *sp) __acquires(sp)
{
	int retval = __srcu_read_lock(sp);

	srcu_read_acquire(sp);
	return retval;
}

/**
 * srcu_read_unlock - unregister a old reader from an SRCU-protected structure.
 * @sp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Exit an SRCU read-side critical section.
 */
static inline void srcu_read_unlock(struct srcu_struct *sp, int idx)
	__releases(sp)
{
	srcu_read_release(sp);
	__srcu_read_unlock(sp, idx);
}

#endif

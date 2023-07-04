/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion
 *
 * Copyright (C) IBM Corporation, 2006
 * Copyright (C) Fujitsu, 2012
 *
 * Author: Paul McKenney <paulmck@linux.ibm.com>
 *	   Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU/ *.txt
 *
 */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/rcu_segcblist.h>

struct srcu_struct;

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *ssp, const char *name,
		       struct lock_class_key *key);

#define init_srcu_struct(ssp) \
({ \
	static struct lock_class_key __srcu_key; \
	\
	__init_srcu_struct((ssp), #ssp, &__srcu_key); \
})

#define __SRCU_DEP_MAP_INIT(srcu_name)	.dep_map = { .name = #srcu_name },
#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

int init_srcu_struct(struct srcu_struct *ssp);

#define __SRCU_DEP_MAP_INIT(srcu_name)
#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_TINY_SRCU
#include <linux/srcutiny.h>
#elif defined(CONFIG_TREE_SRCU)
#include <linux/srcutree.h>
#else
#error "Unknown SRCU implementation specified to kernel configuration"
#endif

void call_srcu(struct srcu_struct *ssp, struct rcu_head *head,
		void (*func)(struct rcu_head *head));
void cleanup_srcu_struct(struct srcu_struct *ssp);
int __srcu_read_lock(struct srcu_struct *ssp) __acquires(ssp);
void __srcu_read_unlock(struct srcu_struct *ssp, int idx) __releases(ssp);
void synchronize_srcu(struct srcu_struct *ssp);
unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp);
unsigned long start_poll_synchronize_srcu(struct srcu_struct *ssp);
bool poll_state_synchronize_srcu(struct srcu_struct *ssp, unsigned long cookie);

#ifdef CONFIG_NEED_SRCU_NMI_SAFE
int __srcu_read_lock_nmisafe(struct srcu_struct *ssp) __acquires(ssp);
void __srcu_read_unlock_nmisafe(struct srcu_struct *ssp, int idx) __releases(ssp);
#else
static inline int __srcu_read_lock_nmisafe(struct srcu_struct *ssp)
{
	return __srcu_read_lock(ssp);
}
static inline void __srcu_read_unlock_nmisafe(struct srcu_struct *ssp, int idx)
{
	__srcu_read_unlock(ssp, idx);
}
#endif /* CONFIG_NEED_SRCU_NMI_SAFE */

void srcu_init(void);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

/**
 * srcu_read_lock_held - might we be in SRCU read-side critical section?
 * @ssp: The srcu_struct structure to check
 *
 * If CONFIG_DEBUG_LOCK_ALLOC is selected, returns nonzero iff in an SRCU
 * read-side critical section.  In absence of CONFIG_DEBUG_LOCK_ALLOC,
 * this assumes we are in an SRCU read-side critical section unless it can
 * prove otherwise.
 *
 * Checks debug_lockdep_rcu_enabled() to prevent false positives during boot
 * and while lockdep is disabled.
 *
 * Note that SRCU is based on its own statemachine and it doesn't
 * relies on normal RCU, it can be called from the CPU which
 * is in the idle loop from an RCU point of view or offline.
 */
static inline int srcu_read_lock_held(const struct srcu_struct *ssp)
{
	if (!debug_lockdep_rcu_enabled())
		return 1;
	return lock_is_held(&ssp->dep_map);
}

/*
 * Annotations provide deadlock detection for SRCU.
 *
 * Similar to other lockdep annotations, except there is an additional
 * srcu_lock_sync(), which is basically an empty *write*-side critical section,
 * see lock_sync() for more information.
 */

/* Annotates a srcu_read_lock() */
static inline void srcu_lock_acquire(struct lockdep_map *map)
{
	lock_map_acquire_read(map);
}

/* Annotates a srcu_read_lock() */
static inline void srcu_lock_release(struct lockdep_map *map)
{
	lock_map_release(map);
}

/* Annotates a synchronize_srcu() */
static inline void srcu_lock_sync(struct lockdep_map *map)
{
	lock_map_sync(map);
}

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

static inline int srcu_read_lock_held(const struct srcu_struct *ssp)
{
	return 1;
}

#define srcu_lock_acquire(m) do { } while (0)
#define srcu_lock_release(m) do { } while (0)
#define srcu_lock_sync(m) do { } while (0)

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#define SRCU_NMI_UNKNOWN	0x0
#define SRCU_NMI_UNSAFE		0x1
#define SRCU_NMI_SAFE		0x2

#if defined(CONFIG_PROVE_RCU) && defined(CONFIG_TREE_SRCU)
void srcu_check_nmi_safety(struct srcu_struct *ssp, bool nmi_safe);
#else
static inline void srcu_check_nmi_safety(struct srcu_struct *ssp,
					 bool nmi_safe) { }
#endif


/**
 * srcu_dereference_check - fetch SRCU-protected pointer for later dereferencing
 * @p: the pointer to fetch and protect for later dereferencing
 * @ssp: pointer to the srcu_struct, which is used to check that we
 *	really are in an SRCU read-side critical section.
 * @c: condition to check for update-side use
 *
 * If PROVE_RCU is enabled, invoking this outside of an RCU read-side
 * critical section will result in an RCU-lockdep splat, unless @c evaluates
 * to 1.  The @c argument will normally be a logical expression containing
 * lockdep_is_held() calls.
 */
#define srcu_dereference_check(p, ssp, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || srcu_read_lock_held(ssp), __rcu)

/**
 * srcu_dereference - fetch SRCU-protected pointer for later dereferencing
 * @p: the pointer to fetch and protect for later dereferencing
 * @ssp: pointer to the srcu_struct, which is used to check that we
 *	really are in an SRCU read-side critical section.
 *
 * Makes rcu_dereference_check() do the dirty work.  If PROVE_RCU
 * is enabled, invoking this outside of an RCU read-side critical
 * section will result in an RCU-lockdep splat.
 */
#define srcu_dereference(p, ssp) srcu_dereference_check((p), (ssp), 0)

/**
 * srcu_dereference_notrace - no tracing and no lockdep calls from here
 * @p: the pointer to fetch and protect for later dereferencing
 * @ssp: pointer to the srcu_struct, which is used to check that we
 *	really are in an SRCU read-side critical section.
 */
#define srcu_dereference_notrace(p, ssp) srcu_dereference_check((p), (ssp), 1)

/**
 * srcu_read_lock - register a new reader for an SRCU-protected structure.
 * @ssp: srcu_struct in which to register the new reader.
 *
 * Enter an SRCU read-side critical section.  Note that SRCU read-side
 * critical sections may be nested.  However, it is illegal to
 * call anything that waits on an SRCU grace period for the same
 * srcu_struct, whether directly or indirectly.  Please note that
 * one way to indirectly wait on an SRCU grace period is to acquire
 * a mutex that is held elsewhere while calling synchronize_srcu() or
 * synchronize_srcu_expedited().
 *
 * Note that srcu_read_lock() and the matching srcu_read_unlock() must
 * occur in the same context, for example, it is illegal to invoke
 * srcu_read_unlock() in an irq handler if the matching srcu_read_lock()
 * was invoked in process context.
 */
static inline int srcu_read_lock(struct srcu_struct *ssp) __acquires(ssp)
{
	int retval;

	srcu_check_nmi_safety(ssp, false);
	retval = __srcu_read_lock(ssp);
	srcu_lock_acquire(&ssp->dep_map);
	return retval;
}

/**
 * srcu_read_lock_nmisafe - register a new reader for an SRCU-protected structure.
 * @ssp: srcu_struct in which to register the new reader.
 *
 * Enter an SRCU read-side critical section, but in an NMI-safe manner.
 * See srcu_read_lock() for more information.
 */
static inline int srcu_read_lock_nmisafe(struct srcu_struct *ssp) __acquires(ssp)
{
	int retval;

	srcu_check_nmi_safety(ssp, true);
	retval = __srcu_read_lock_nmisafe(ssp);
	rcu_lock_acquire(&ssp->dep_map);
	return retval;
}

/* Used by tracing, cannot be traced and cannot invoke lockdep. */
static inline notrace int
srcu_read_lock_notrace(struct srcu_struct *ssp) __acquires(ssp)
{
	int retval;

	srcu_check_nmi_safety(ssp, false);
	retval = __srcu_read_lock(ssp);
	return retval;
}

/**
 * srcu_down_read - register a new reader for an SRCU-protected structure.
 * @ssp: srcu_struct in which to register the new reader.
 *
 * Enter a semaphore-like SRCU read-side critical section.  Note that
 * SRCU read-side critical sections may be nested.  However, it is
 * illegal to call anything that waits on an SRCU grace period for the
 * same srcu_struct, whether directly or indirectly.  Please note that
 * one way to indirectly wait on an SRCU grace period is to acquire
 * a mutex that is held elsewhere while calling synchronize_srcu() or
 * synchronize_srcu_expedited().  But if you want lockdep to help you
 * keep this stuff straight, you should instead use srcu_read_lock().
 *
 * The semaphore-like nature of srcu_down_read() means that the matching
 * srcu_up_read() can be invoked from some other context, for example,
 * from some other task or from an irq handler.  However, neither
 * srcu_down_read() nor srcu_up_read() may be invoked from an NMI handler.
 *
 * Calls to srcu_down_read() may be nested, similar to the manner in
 * which calls to down_read() may be nested.
 */
static inline int srcu_down_read(struct srcu_struct *ssp) __acquires(ssp)
{
	WARN_ON_ONCE(in_nmi());
	srcu_check_nmi_safety(ssp, false);
	return __srcu_read_lock(ssp);
}

/**
 * srcu_read_unlock - unregister a old reader from an SRCU-protected structure.
 * @ssp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Exit an SRCU read-side critical section.
 */
static inline void srcu_read_unlock(struct srcu_struct *ssp, int idx)
	__releases(ssp)
{
	WARN_ON_ONCE(idx & ~0x1);
	srcu_check_nmi_safety(ssp, false);
	srcu_lock_release(&ssp->dep_map);
	__srcu_read_unlock(ssp, idx);
}

/**
 * srcu_read_unlock_nmisafe - unregister a old reader from an SRCU-protected structure.
 * @ssp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Exit an SRCU read-side critical section, but in an NMI-safe manner.
 */
static inline void srcu_read_unlock_nmisafe(struct srcu_struct *ssp, int idx)
	__releases(ssp)
{
	WARN_ON_ONCE(idx & ~0x1);
	srcu_check_nmi_safety(ssp, true);
	rcu_lock_release(&ssp->dep_map);
	__srcu_read_unlock_nmisafe(ssp, idx);
}

/* Used by tracing, cannot be traced and cannot call lockdep. */
static inline notrace void
srcu_read_unlock_notrace(struct srcu_struct *ssp, int idx) __releases(ssp)
{
	srcu_check_nmi_safety(ssp, false);
	__srcu_read_unlock(ssp, idx);
}

/**
 * srcu_up_read - unregister a old reader from an SRCU-protected structure.
 * @ssp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Exit an SRCU read-side critical section, but not necessarily from
 * the same context as the maching srcu_down_read().
 */
static inline void srcu_up_read(struct srcu_struct *ssp, int idx)
	__releases(ssp)
{
	WARN_ON_ONCE(idx & ~0x1);
	WARN_ON_ONCE(in_nmi());
	srcu_check_nmi_safety(ssp, false);
	__srcu_read_unlock(ssp, idx);
}

/**
 * smp_mb__after_srcu_read_unlock - ensure full ordering after srcu_read_unlock
 *
 * Converts the preceding srcu_read_unlock into a two-way memory barrier.
 *
 * Call this after srcu_read_unlock, to guarantee that all memory operations
 * that occur after smp_mb__after_srcu_read_unlock will appear to happen after
 * the preceding srcu_read_unlock.
 */
static inline void smp_mb__after_srcu_read_unlock(void)
{
	/* __srcu_read_unlock has smp_mb() internally so nothing to do here. */
}

DEFINE_LOCK_GUARD_1(srcu, struct srcu_struct,
		    _T->idx = srcu_read_lock(_T->lock),
		    srcu_read_unlock(_T->lock, _T->idx),
		    int idx)

#endif

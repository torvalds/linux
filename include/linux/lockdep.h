/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime locking correctness validator
 *
 *  Copyright (C) 2006,2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 * see Documentation/locking/lockdep-design.txt for more details.
 */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

struct task_struct;
struct lockdep_map;

/* for sysctl */
extern int prove_locking;
extern int lock_stat;

#define MAX_LOCKDEP_SUBCLASSES		8UL

#include <linux/types.h>

#ifdef CONFIG_LOCKDEP

#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/debug_locks.h>
#include <linux/stacktrace.h>

/*
 * We'd rather not expose kernel/lockdep_states.h this wide, but we do need
 * the total number of states... :-(
 */
#define XXX_LOCK_USAGE_STATES		(1+2*4)

/*
 * NR_LOCKDEP_CACHING_CLASSES ... Number of classes
 * cached in the instance of lockdep_map
 *
 * Currently main class (subclass == 0) and signle depth subclass
 * are cached in lockdep_map. This optimization is mainly targeting
 * on rq->lock. double_rq_lock() acquires this highly competitive with
 * single depth.
 */
#define NR_LOCKDEP_CACHING_CLASSES	2

/*
 * Lock-classes are keyed via unique addresses, by embedding the
 * lockclass-key into the kernel (or module) .data section. (For
 * static locks we use the lock address itself as the key.)
 */
struct lockdep_subclass_key {
	char __one_byte;
} __attribute__ ((__packed__));

struct lock_class_key {
	struct lockdep_subclass_key	subkeys[MAX_LOCKDEP_SUBCLASSES];
};

extern struct lock_class_key __lockdep_no_validate__;

#define LOCKSTAT_POINTS		4

/*
 * The lock-class itself:
 */
struct lock_class {
	/*
	 * class-hash:
	 */
	struct hlist_node		hash_entry;

	/*
	 * global list of all lock-classes:
	 */
	struct list_head		lock_entry;

	struct lockdep_subclass_key	*key;
	unsigned int			subclass;
	unsigned int			dep_gen_id;

	/*
	 * IRQ/softirq usage tracking bits:
	 */
	unsigned long			usage_mask;
	struct stack_trace		usage_traces[XXX_LOCK_USAGE_STATES];

	/*
	 * These fields represent a directed graph of lock dependencies,
	 * to every node we attach a list of "forward" and a list of
	 * "backward" graph nodes.
	 */
	struct list_head		locks_after, locks_before;

	/*
	 * Generation counter, when doing certain classes of graph walking,
	 * to ensure that we check one node only once:
	 */
	unsigned int			version;

	/*
	 * Statistics counter:
	 */
	unsigned long			ops;

	const char			*name;
	int				name_version;

#ifdef CONFIG_LOCK_STAT
	unsigned long			contention_point[LOCKSTAT_POINTS];
	unsigned long			contending_point[LOCKSTAT_POINTS];
#endif
};

#ifdef CONFIG_LOCK_STAT
struct lock_time {
	s64				min;
	s64				max;
	s64				total;
	unsigned long			nr;
};

enum bounce_type {
	bounce_acquired_write,
	bounce_acquired_read,
	bounce_contended_write,
	bounce_contended_read,
	nr_bounce_types,

	bounce_acquired = bounce_acquired_write,
	bounce_contended = bounce_contended_write,
};

struct lock_class_stats {
	unsigned long			contention_point[LOCKSTAT_POINTS];
	unsigned long			contending_point[LOCKSTAT_POINTS];
	struct lock_time		read_waittime;
	struct lock_time		write_waittime;
	struct lock_time		read_holdtime;
	struct lock_time		write_holdtime;
	unsigned long			bounces[nr_bounce_types];
};

struct lock_class_stats lock_stats(struct lock_class *class);
void clear_lock_stats(struct lock_class *class);
#endif

/*
 * Map the lock object (the lock instance) to the lock-class object.
 * This is embedded into specific lock instances:
 */
struct lockdep_map {
	struct lock_class_key		*key;
	struct lock_class		*class_cache[NR_LOCKDEP_CACHING_CLASSES];
	const char			*name;
#ifdef CONFIG_LOCK_STAT
	int				cpu;
	unsigned long			ip;
#endif
};

static inline void lockdep_copy_map(struct lockdep_map *to,
				    struct lockdep_map *from)
{
	int i;

	*to = *from;
	/*
	 * Since the class cache can be modified concurrently we could observe
	 * half pointers (64bit arch using 32bit copy insns). Therefore clear
	 * the caches and take the performance hit.
	 *
	 * XXX it doesn't work well with lockdep_set_class_and_subclass(), since
	 *     that relies on cache abuse.
	 */
	for (i = 0; i < NR_LOCKDEP_CACHING_CLASSES; i++)
		to->class_cache[i] = NULL;
}

/*
 * Every lock has a list of other locks that were taken after it.
 * We only grow the list, never remove from it:
 */
struct lock_list {
	struct list_head		entry;
	struct lock_class		*class;
	struct stack_trace		trace;
	int				distance;

	/*
	 * The parent field is used to implement breadth-first search, and the
	 * bit 0 is reused to indicate if the lock has been accessed in BFS.
	 */
	struct lock_list		*parent;
};

/*
 * We record lock dependency chains, so that we can cache them:
 */
struct lock_chain {
	/* see BUILD_BUG_ON()s in lookup_chain_cache() */
	unsigned int			irq_context :  2,
					depth       :  6,
					base	    : 24;
	/* 4 byte hole */
	struct hlist_node		entry;
	u64				chain_key;
};

#define MAX_LOCKDEP_KEYS_BITS		13
/*
 * Subtract one because we offset hlock->class_idx by 1 in order
 * to make 0 mean no class. This avoids overflowing the class_idx
 * bitfield and hitting the BUG in hlock_class().
 */
#define MAX_LOCKDEP_KEYS		((1UL << MAX_LOCKDEP_KEYS_BITS) - 1)

struct held_lock {
	/*
	 * One-way hash of the dependency chain up to this point. We
	 * hash the hashes step by step as the dependency chain grows.
	 *
	 * We use it for dependency-caching and we skip detection
	 * passes and dependency-updates if there is a cache-hit, so
	 * it is absolutely critical for 100% coverage of the validator
	 * to have a unique key value for every unique dependency path
	 * that can occur in the system, to make a unique hash value
	 * as likely as possible - hence the 64-bit width.
	 *
	 * The task struct holds the current hash value (initialized
	 * with zero), here we store the previous hash value:
	 */
	u64				prev_chain_key;
	unsigned long			acquire_ip;
	struct lockdep_map		*instance;
	struct lockdep_map		*nest_lock;
#ifdef CONFIG_LOCK_STAT
	u64 				waittime_stamp;
	u64				holdtime_stamp;
#endif
	unsigned int			class_idx:MAX_LOCKDEP_KEYS_BITS;
	/*
	 * The lock-stack is unified in that the lock chains of interrupt
	 * contexts nest ontop of process context chains, but we 'separate'
	 * the hashes by starting with 0 if we cross into an interrupt
	 * context, and we also keep do not add cross-context lock
	 * dependencies - the lock usage graph walking covers that area
	 * anyway, and we'd just unnecessarily increase the number of
	 * dependencies otherwise. [Note: hardirq and softirq contexts
	 * are separated from each other too.]
	 *
	 * The following field is used to detect when we cross into an
	 * interrupt context:
	 */
	unsigned int irq_context:2; /* bit 0 - soft, bit 1 - hard */
	unsigned int trylock:1;						/* 16 bits */

	unsigned int read:2;        /* see lock_acquire() comment */
	unsigned int check:1;       /* see lock_acquire() comment */
	unsigned int hardirqs_off:1;
	unsigned int references:12;					/* 32 bits */
	unsigned int pin_count;
};

/*
 * Initialization, self-test and debugging-output methods:
 */
extern void lockdep_info(void);
extern void lockdep_reset(void);
extern void lockdep_reset_lock(struct lockdep_map *lock);
extern void lockdep_free_key_range(void *start, unsigned long size);
extern asmlinkage void lockdep_sys_exit(void);

extern void lockdep_off(void);
extern void lockdep_on(void);

/*
 * These methods are used by specific locking variants (spinlocks,
 * rwlocks, mutexes and rwsems) to pass init/acquire/release events
 * to lockdep:
 */

extern void lockdep_init_map(struct lockdep_map *lock, const char *name,
			     struct lock_class_key *key, int subclass);

/*
 * Reinitialize a lock key - for cases where there is special locking or
 * special initialization of locks so that the validator gets the scope
 * of dependencies wrong: they are either too broad (they need a class-split)
 * or they are too narrow (they suffer from a false class-split):
 */
#define lockdep_set_class(lock, key) \
		lockdep_init_map(&(lock)->dep_map, #key, key, 0)
#define lockdep_set_class_and_name(lock, key, name) \
		lockdep_init_map(&(lock)->dep_map, name, key, 0)
#define lockdep_set_class_and_subclass(lock, key, sub) \
		lockdep_init_map(&(lock)->dep_map, #key, key, sub)
#define lockdep_set_subclass(lock, sub)	\
		lockdep_init_map(&(lock)->dep_map, #lock, \
				 (lock)->dep_map.key, sub)

#define lockdep_set_novalidate_class(lock) \
	lockdep_set_class_and_name(lock, &__lockdep_no_validate__, #lock)
/*
 * Compare locking classes
 */
#define lockdep_match_class(lock, key) lockdep_match_key(&(lock)->dep_map, key)

static inline int lockdep_match_key(struct lockdep_map *lock,
				    struct lock_class_key *key)
{
	return lock->key == key;
}

/*
 * Acquire a lock.
 *
 * Values for "read":
 *
 *   0: exclusive (write) acquire
 *   1: read-acquire (no recursion allowed)
 *   2: read-acquire with same-instance recursion allowed
 *
 * Values for check:
 *
 *   0: simple checks (freeing, held-at-exit-time, etc.)
 *   1: full validation
 */
extern void lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			 int trylock, int read, int check,
			 struct lockdep_map *nest_lock, unsigned long ip);

extern void lock_release(struct lockdep_map *lock, int nested,
			 unsigned long ip);

/*
 * Same "read" as for lock_acquire(), except -1 means any.
 */
extern int lock_is_held_type(struct lockdep_map *lock, int read);

static inline int lock_is_held(struct lockdep_map *lock)
{
	return lock_is_held_type(lock, -1);
}

#define lockdep_is_held(lock)		lock_is_held(&(lock)->dep_map)
#define lockdep_is_held_type(lock, r)	lock_is_held_type(&(lock)->dep_map, (r))

extern void lock_set_class(struct lockdep_map *lock, const char *name,
			   struct lock_class_key *key, unsigned int subclass,
			   unsigned long ip);

static inline void lock_set_subclass(struct lockdep_map *lock,
		unsigned int subclass, unsigned long ip)
{
	lock_set_class(lock, lock->name, lock->key, subclass, ip);
}

extern void lock_downgrade(struct lockdep_map *lock, unsigned long ip);

struct pin_cookie { unsigned int val; };

#define NIL_COOKIE (struct pin_cookie){ .val = 0U, }

extern struct pin_cookie lock_pin_lock(struct lockdep_map *lock);
extern void lock_repin_lock(struct lockdep_map *lock, struct pin_cookie);
extern void lock_unpin_lock(struct lockdep_map *lock, struct pin_cookie);

# define INIT_LOCKDEP				.lockdep_recursion = 0,

#define lockdep_depth(tsk)	(debug_locks ? (tsk)->lockdep_depth : 0)

#define lockdep_assert_held(l)	do {				\
		WARN_ON(debug_locks && !lockdep_is_held(l));	\
	} while (0)

#define lockdep_assert_held_exclusive(l)	do {			\
		WARN_ON(debug_locks && !lockdep_is_held_type(l, 0));	\
	} while (0)

#define lockdep_assert_held_read(l)	do {				\
		WARN_ON(debug_locks && !lockdep_is_held_type(l, 1));	\
	} while (0)

#define lockdep_assert_held_once(l)	do {				\
		WARN_ON_ONCE(debug_locks && !lockdep_is_held(l));	\
	} while (0)

#define lockdep_recursing(tsk)	((tsk)->lockdep_recursion)

#define lockdep_pin_lock(l)	lock_pin_lock(&(l)->dep_map)
#define lockdep_repin_lock(l,c)	lock_repin_lock(&(l)->dep_map, (c))
#define lockdep_unpin_lock(l,c)	lock_unpin_lock(&(l)->dep_map, (c))

#else /* !CONFIG_LOCKDEP */

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}

# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, n, i)			do { } while (0)
# define lock_downgrade(l, i)			do { } while (0)
# define lock_set_class(l, n, k, s, i)		do { } while (0)
# define lock_set_subclass(l, s, i)		do { } while (0)
# define lockdep_info()				do { } while (0)
# define lockdep_init_map(lock, name, key, sub) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_set_class(lock, key)		do { (void)(key); } while (0)
# define lockdep_set_class_and_name(lock, key, name) \
		do { (void)(key); (void)(name); } while (0)
#define lockdep_set_class_and_subclass(lock, key, sub) \
		do { (void)(key); } while (0)
#define lockdep_set_subclass(lock, sub)		do { } while (0)

#define lockdep_set_novalidate_class(lock) do { } while (0)

/*
 * We don't define lockdep_match_class() and lockdep_match_key() for !LOCKDEP
 * case since the result is not well defined and the caller should rather
 * #ifdef the call himself.
 */

# define INIT_LOCKDEP
# define lockdep_reset()		do { debug_locks = 1; } while (0)
# define lockdep_free_key_range(start, size)	do { } while (0)
# define lockdep_sys_exit() 			do { } while (0)
/*
 * The class key takes no space if lockdep is disabled:
 */
struct lock_class_key { };

/*
 * The lockdep_map takes no space if lockdep is disabled:
 */
struct lockdep_map { };

#define lockdep_depth(tsk)	(0)

#define lockdep_is_held_type(l, r)		(1)

#define lockdep_assert_held(l)			do { (void)(l); } while (0)
#define lockdep_assert_held_exclusive(l)	do { (void)(l); } while (0)
#define lockdep_assert_held_read(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_once(l)		do { (void)(l); } while (0)

#define lockdep_recursing(tsk)			(0)

struct pin_cookie { };

#define NIL_COOKIE (struct pin_cookie){ }

#define lockdep_pin_lock(l)			({ struct pin_cookie cookie; cookie; })
#define lockdep_repin_lock(l, c)		do { (void)(l); (void)(c); } while (0)
#define lockdep_unpin_lock(l, c)		do { (void)(l); (void)(c); } while (0)

#endif /* !LOCKDEP */

enum xhlock_context_t {
	XHLOCK_HARD,
	XHLOCK_SOFT,
	XHLOCK_CTX_NR,
};

#define lockdep_init_map_crosslock(m, n, k, s) do {} while (0)
/*
 * To initialize a lockdep_map statically use this macro.
 * Note that _name must not be NULL.
 */
#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

static inline void lockdep_invariant_state(bool force) {}
static inline void lockdep_init_task(struct task_struct *task) {}
static inline void lockdep_free_task(struct task_struct *task) {}

#ifdef CONFIG_LOCK_STAT

extern void lock_contended(struct lockdep_map *lock, unsigned long ip);
extern void lock_acquired(struct lockdep_map *lock, unsigned long ip);

#define LOCK_CONTENDED(_lock, try, lock)			\
do {								\
	if (!try(_lock)) {					\
		lock_contended(&(_lock)->dep_map, _RET_IP_);	\
		lock(_lock);					\
	}							\
	lock_acquired(&(_lock)->dep_map, _RET_IP_);			\
} while (0)

#define LOCK_CONTENDED_RETURN(_lock, try, lock)			\
({								\
	int ____err = 0;					\
	if (!try(_lock)) {					\
		lock_contended(&(_lock)->dep_map, _RET_IP_);	\
		____err = lock(_lock);				\
	}							\
	if (!____err)						\
		lock_acquired(&(_lock)->dep_map, _RET_IP_);	\
	____err;						\
})

#else /* CONFIG_LOCK_STAT */

#define lock_contended(lockdep_map, ip) do {} while (0)
#define lock_acquired(lockdep_map, ip) do {} while (0)

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#define LOCK_CONTENDED_RETURN(_lock, try, lock) \
	lock(_lock)

#endif /* CONFIG_LOCK_STAT */

#ifdef CONFIG_LOCKDEP

/*
 * On lockdep we dont want the hand-coded irq-enable of
 * _raw_*_lock_flags() code, because lockdep assumes
 * that interrupts are not re-enabled during lock-acquire:
 */
#define LOCK_CONTENDED_FLAGS(_lock, try, lock, lockfl, flags) \
	LOCK_CONTENDED((_lock), (try), (lock))

#else /* CONFIG_LOCKDEP */

#define LOCK_CONTENDED_FLAGS(_lock, try, lock, lockfl, flags) \
	lockfl((_lock), (flags))

#endif /* CONFIG_LOCKDEP */

#ifdef CONFIG_TRACE_IRQFLAGS
extern void print_irqtrace_events(struct task_struct *curr);
#else
static inline void print_irqtrace_events(struct task_struct *curr)
{
}
#endif

/*
 * For trivial one-depth nesting of a lock-class, the following
 * global define can be used. (Subsystems with multiple levels
 * of nesting should define their own lock-nesting subclasses.)
 */
#define SINGLE_DEPTH_NESTING			1

/*
 * Map the dependency ops to NOP or to real lockdep ops, depending
 * on the per lock-class debug mode:
 */

#define lock_acquire_exclusive(l, s, t, n, i)		lock_acquire(l, s, t, 0, 1, n, i)
#define lock_acquire_shared(l, s, t, n, i)		lock_acquire(l, s, t, 1, 1, n, i)
#define lock_acquire_shared_recursive(l, s, t, n, i)	lock_acquire(l, s, t, 2, 1, n, i)

#define spin_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define spin_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define spin_release(l, n, i)			lock_release(l, n, i)

#define rwlock_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define rwlock_acquire_read(l, s, t, i)		lock_acquire_shared_recursive(l, s, t, NULL, i)
#define rwlock_release(l, n, i)			lock_release(l, n, i)

#define seqcount_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define seqcount_acquire_read(l, s, t, i)	lock_acquire_shared_recursive(l, s, t, NULL, i)
#define seqcount_release(l, n, i)		lock_release(l, n, i)

#define mutex_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define mutex_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define mutex_release(l, n, i)			lock_release(l, n, i)

#define rwsem_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define rwsem_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define rwsem_acquire_read(l, s, t, i)		lock_acquire_shared(l, s, t, NULL, i)
#define rwsem_release(l, n, i)			lock_release(l, n, i)

#define lock_map_acquire(l)			lock_acquire_exclusive(l, 0, 0, NULL, _THIS_IP_)
#define lock_map_acquire_read(l)		lock_acquire_shared_recursive(l, 0, 0, NULL, _THIS_IP_)
#define lock_map_acquire_tryread(l)		lock_acquire_shared_recursive(l, 0, 1, NULL, _THIS_IP_)
#define lock_map_release(l)			lock_release(l, 1, _THIS_IP_)

#ifdef CONFIG_PROVE_LOCKING
# define might_lock(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 0, 1, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)
# define might_lock_read(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 1, 1, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)

#define lockdep_assert_irqs_enabled()	do {				\
		WARN_ONCE(debug_locks && !current->lockdep_recursion &&	\
			  !current->hardirqs_enabled,			\
			  "IRQs not enabled as expected\n");		\
	} while (0)

#define lockdep_assert_irqs_disabled()	do {				\
		WARN_ONCE(debug_locks && !current->lockdep_recursion &&	\
			  current->hardirqs_enabled,			\
			  "IRQs not disabled as expected\n");		\
	} while (0)

#else
# define might_lock(lock) do { } while (0)
# define might_lock_read(lock) do { } while (0)
# define lockdep_assert_irqs_enabled() do { } while (0)
# define lockdep_assert_irqs_disabled() do { } while (0)
#endif

#ifdef CONFIG_LOCKDEP
void lockdep_rcu_suspicious(const char *file, const int line, const char *s);
#else
static inline void
lockdep_rcu_suspicious(const char *file, const int line, const char *s)
{
}
#endif

#endif /* __LINUX_LOCKDEP_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime locking correctness validator
 *
 *  Copyright (C) 2006,2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 * see Documentation/locking/lockdep-design.rst for more details.
 */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#include <linux/lockdep_types.h>
#include <linux/smp.h>
#include <asm/percpu.h>

struct task_struct;

#ifdef CONFIG_LOCKDEP

#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/debug_locks.h>
#include <linux/stacktrace.h>

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
	struct lock_class		*links_to;
	const struct lock_trace		*trace;
	u16				distance;
	/* bitmap of different dependencies from head to this */
	u8				dep;
	/* used by BFS to record whether "prev -> this" only has -(*R)-> */
	u8				only_xr;

	/*
	 * The parent field is used to implement breadth-first search, and the
	 * bit 0 is reused to indicate if the lock has been accessed in BFS.
	 */
	struct lock_list		*parent;
};

/**
 * struct lock_chain - lock dependency chain record
 *
 * @irq_context: the same as irq_context in held_lock below
 * @depth:       the number of held locks in this chain
 * @base:        the index in chain_hlocks for this chain
 * @entry:       the collided lock chains in lock_chain hash list
 * @chain_key:   the hash key of this lock_chain
 */
struct lock_chain {
	/* see BUILD_BUG_ON()s in add_chain_cache() */
	unsigned int			irq_context :  2,
					depth       :  6,
					base	    : 24;
	/* 4 byte hole */
	struct hlist_node		entry;
	u64				chain_key;
};

#define MAX_LOCKDEP_KEYS_BITS		13
#define MAX_LOCKDEP_KEYS		(1UL << MAX_LOCKDEP_KEYS_BITS)
#define INITIAL_CHAIN_KEY		-1

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
	/*
	 * class_idx is zero-indexed; it points to the element in
	 * lock_classes this held lock instance belongs to. class_idx is in
	 * the range from 0 to (MAX_LOCKDEP_KEYS-1) inclusive.
	 */
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
extern void lockdep_init(void);
extern void lockdep_reset(void);
extern void lockdep_reset_lock(struct lockdep_map *lock);
extern void lockdep_free_key_range(void *start, unsigned long size);
extern asmlinkage void lockdep_sys_exit(void);
extern void lockdep_set_selftest_task(struct task_struct *task);

extern void lockdep_init_task(struct task_struct *task);

/*
 * Split the recursion counter in two to readily detect 'off' vs recursion.
 */
#define LOCKDEP_RECURSION_BITS	16
#define LOCKDEP_OFF		(1U << LOCKDEP_RECURSION_BITS)
#define LOCKDEP_RECURSION_MASK	(LOCKDEP_OFF - 1)

/*
 * lockdep_{off,on}() are macros to avoid tracing and kprobes; not inlines due
 * to header dependencies.
 */

#define lockdep_off()					\
do {							\
	current->lockdep_recursion += LOCKDEP_OFF;	\
} while (0)

#define lockdep_on()					\
do {							\
	current->lockdep_recursion -= LOCKDEP_OFF;	\
} while (0)

extern void lockdep_register_key(struct lock_class_key *key);
extern void lockdep_unregister_key(struct lock_class_key *key);

/*
 * These methods are used by specific locking variants (spinlocks,
 * rwlocks, mutexes and rwsems) to pass init/acquire/release events
 * to lockdep:
 */

extern void lockdep_init_map_type(struct lockdep_map *lock, const char *name,
	struct lock_class_key *key, int subclass, u8 inner, u8 outer, u8 lock_type);

static inline void
lockdep_init_map_waits(struct lockdep_map *lock, const char *name,
		       struct lock_class_key *key, int subclass, u8 inner, u8 outer)
{
	lockdep_init_map_type(lock, name, key, subclass, inner, outer, LD_LOCK_NORMAL);
}

static inline void
lockdep_init_map_wait(struct lockdep_map *lock, const char *name,
		      struct lock_class_key *key, int subclass, u8 inner)
{
	lockdep_init_map_waits(lock, name, key, subclass, inner, LD_WAIT_INV);
}

static inline void lockdep_init_map(struct lockdep_map *lock, const char *name,
			     struct lock_class_key *key, int subclass)
{
	lockdep_init_map_wait(lock, name, key, subclass, LD_WAIT_INV);
}

/*
 * Reinitialize a lock key - for cases where there is special locking or
 * special initialization of locks so that the validator gets the scope
 * of dependencies wrong: they are either too broad (they need a class-split)
 * or they are too narrow (they suffer from a false class-split):
 */
#define lockdep_set_class(lock, key)				\
	lockdep_init_map_type(&(lock)->dep_map, #key, key, 0,	\
			      (lock)->dep_map.wait_type_inner,	\
			      (lock)->dep_map.wait_type_outer,	\
			      (lock)->dep_map.lock_type)

#define lockdep_set_class_and_name(lock, key, name)		\
	lockdep_init_map_type(&(lock)->dep_map, name, key, 0,	\
			      (lock)->dep_map.wait_type_inner,	\
			      (lock)->dep_map.wait_type_outer,	\
			      (lock)->dep_map.lock_type)

#define lockdep_set_class_and_subclass(lock, key, sub)		\
	lockdep_init_map_type(&(lock)->dep_map, #key, key, sub,	\
			      (lock)->dep_map.wait_type_inner,	\
			      (lock)->dep_map.wait_type_outer,	\
			      (lock)->dep_map.lock_type)

#define lockdep_set_subclass(lock, sub)					\
	lockdep_init_map_type(&(lock)->dep_map, #lock, (lock)->dep_map.key, sub,\
			      (lock)->dep_map.wait_type_inner,		\
			      (lock)->dep_map.wait_type_outer,		\
			      (lock)->dep_map.lock_type)

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

extern void lock_release(struct lockdep_map *lock, unsigned long ip);

/* lock_is_held_type() returns */
#define LOCK_STATE_UNKNOWN	-1
#define LOCK_STATE_NOT_HELD	0
#define LOCK_STATE_HELD		1

/*
 * Same "read" as for lock_acquire(), except -1 means any.
 */
extern int lock_is_held_type(const struct lockdep_map *lock, int read);

static inline int lock_is_held(const struct lockdep_map *lock)
{
	return lock_is_held_type(lock, -1);
}

#define lockdep_is_held(lock)		lock_is_held(&(lock)->dep_map)
#define lockdep_is_held_type(lock, r)	lock_is_held_type(&(lock)->dep_map, (r))

extern void lock_set_class(struct lockdep_map *lock, const char *name,
			   struct lock_class_key *key, unsigned int subclass,
			   unsigned long ip);

#define lock_set_novalidate_class(l, n, i) \
	lock_set_class(l, n, &__lockdep_no_validate__, 0, i)

static inline void lock_set_subclass(struct lockdep_map *lock,
		unsigned int subclass, unsigned long ip)
{
	lock_set_class(lock, lock->name, lock->key, subclass, ip);
}

extern void lock_downgrade(struct lockdep_map *lock, unsigned long ip);

#define NIL_COOKIE (struct pin_cookie){ .val = 0U, }

extern struct pin_cookie lock_pin_lock(struct lockdep_map *lock);
extern void lock_repin_lock(struct lockdep_map *lock, struct pin_cookie);
extern void lock_unpin_lock(struct lockdep_map *lock, struct pin_cookie);

#define lockdep_depth(tsk)	(debug_locks ? (tsk)->lockdep_depth : 0)

#define lockdep_assert(cond)		\
	do { WARN_ON(debug_locks && !(cond)); } while (0)

#define lockdep_assert_once(cond)	\
	do { WARN_ON_ONCE(debug_locks && !(cond)); } while (0)

#define lockdep_assert_held(l)		\
	lockdep_assert(lockdep_is_held(l) != LOCK_STATE_NOT_HELD)

#define lockdep_assert_not_held(l)	\
	lockdep_assert(lockdep_is_held(l) != LOCK_STATE_HELD)

#define lockdep_assert_held_write(l)	\
	lockdep_assert(lockdep_is_held_type(l, 0))

#define lockdep_assert_held_read(l)	\
	lockdep_assert(lockdep_is_held_type(l, 1))

#define lockdep_assert_held_once(l)		\
	lockdep_assert_once(lockdep_is_held(l) != LOCK_STATE_NOT_HELD)

#define lockdep_assert_none_held_once()		\
	lockdep_assert_once(!current->lockdep_depth)

#define lockdep_recursing(tsk)	((tsk)->lockdep_recursion)

#define lockdep_pin_lock(l)	lock_pin_lock(&(l)->dep_map)
#define lockdep_repin_lock(l,c)	lock_repin_lock(&(l)->dep_map, (c))
#define lockdep_unpin_lock(l,c)	lock_unpin_lock(&(l)->dep_map, (c))

/*
 * Must use lock_map_aquire_try() with override maps to avoid
 * lockdep thinking they participate in the block chain.
 */
#define DEFINE_WAIT_OVERRIDE_MAP(_name, _wait_type)	\
	struct lockdep_map _name = {			\
		.name = #_name "-wait-type-override",	\
		.wait_type_inner = _wait_type,		\
		.lock_type = LD_LOCK_WAIT_OVERRIDE, }

#else /* !CONFIG_LOCKDEP */

static inline void lockdep_init_task(struct task_struct *task)
{
}

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}

static inline void lockdep_set_selftest_task(struct task_struct *task)
{
}

# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, i)			do { } while (0)
# define lock_downgrade(l, i)			do { } while (0)
# define lock_set_class(l, n, key, s, i)	do { (void)(key); } while (0)
# define lock_set_novalidate_class(l, n, i)	do { } while (0)
# define lock_set_subclass(l, s, i)		do { } while (0)
# define lockdep_init()				do { } while (0)
# define lockdep_init_map_type(lock, name, key, sub, inner, outer, type) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_init_map_waits(lock, name, key, sub, inner, outer) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_init_map_wait(lock, name, key, sub, inner) \
		do { (void)(name); (void)(key); } while (0)
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

# define lockdep_reset()		do { debug_locks = 1; } while (0)
# define lockdep_free_key_range(start, size)	do { } while (0)
# define lockdep_sys_exit() 			do { } while (0)

static inline void lockdep_register_key(struct lock_class_key *key)
{
}

static inline void lockdep_unregister_key(struct lock_class_key *key)
{
}

#define lockdep_depth(tsk)	(0)

/*
 * Dummy forward declarations, allow users to write less ifdef-y code
 * and depend on dead code elimination.
 */
extern int lock_is_held(const void *);
extern int lockdep_is_held(const void *);
#define lockdep_is_held_type(l, r)		(1)

#define lockdep_assert(c)			do { } while (0)
#define lockdep_assert_once(c)			do { } while (0)

#define lockdep_assert_held(l)			do { (void)(l); } while (0)
#define lockdep_assert_not_held(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_write(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_read(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_once(l)		do { (void)(l); } while (0)
#define lockdep_assert_none_held_once()	do { } while (0)

#define lockdep_recursing(tsk)			(0)

#define NIL_COOKIE (struct pin_cookie){ }

#define lockdep_pin_lock(l)			({ struct pin_cookie cookie = { }; cookie; })
#define lockdep_repin_lock(l, c)		do { (void)(l); (void)(c); } while (0)
#define lockdep_unpin_lock(l, c)		do { (void)(l); (void)(c); } while (0)

#define DEFINE_WAIT_OVERRIDE_MAP(_name, _wait_type)	\
	struct lockdep_map __maybe_unused _name = {}

#endif /* !LOCKDEP */

enum xhlock_context_t {
	XHLOCK_HARD,
	XHLOCK_SOFT,
	XHLOCK_CTX_NR,
};

/*
 * To initialize a lockdep_map statically use this macro.
 * Note that _name must not be NULL.
 */
#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

static inline void lockdep_invariant_state(bool force) {}
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

#ifdef CONFIG_PROVE_LOCKING
extern void print_irqtrace_events(struct task_struct *curr);
#else
static inline void print_irqtrace_events(struct task_struct *curr)
{
}
#endif

/* Variable used to make lockdep treat read_lock() as recursive in selftests */
#ifdef CONFIG_DEBUG_LOCKING_API_SELFTESTS
extern unsigned int force_read_lock_recursive;
#else /* CONFIG_DEBUG_LOCKING_API_SELFTESTS */
#define force_read_lock_recursive 0
#endif /* CONFIG_DEBUG_LOCKING_API_SELFTESTS */

#ifdef CONFIG_LOCKDEP
extern bool read_lock_is_recursive(void);
#else /* CONFIG_LOCKDEP */
/* If !LOCKDEP, the value is meaningless */
#define read_lock_is_recursive() 0
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
#define spin_release(l, i)			lock_release(l, i)

#define rwlock_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define rwlock_acquire_read(l, s, t, i)					\
do {									\
	if (read_lock_is_recursive())					\
		lock_acquire_shared_recursive(l, s, t, NULL, i);	\
	else								\
		lock_acquire_shared(l, s, t, NULL, i);			\
} while (0)

#define rwlock_release(l, i)			lock_release(l, i)

#define seqcount_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define seqcount_acquire_read(l, s, t, i)	lock_acquire_shared_recursive(l, s, t, NULL, i)
#define seqcount_release(l, i)			lock_release(l, i)

#define mutex_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define mutex_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define mutex_release(l, i)			lock_release(l, i)

#define rwsem_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define rwsem_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define rwsem_acquire_read(l, s, t, i)		lock_acquire_shared(l, s, t, NULL, i)
#define rwsem_release(l, i)			lock_release(l, i)

#define lock_map_acquire(l)			lock_acquire_exclusive(l, 0, 0, NULL, _THIS_IP_)
#define lock_map_acquire_try(l)			lock_acquire_exclusive(l, 0, 1, NULL, _THIS_IP_)
#define lock_map_acquire_read(l)		lock_acquire_shared_recursive(l, 0, 0, NULL, _THIS_IP_)
#define lock_map_acquire_tryread(l)		lock_acquire_shared_recursive(l, 0, 1, NULL, _THIS_IP_)
#define lock_map_release(l)			lock_release(l, _THIS_IP_)

#ifdef CONFIG_PROVE_LOCKING
# define might_lock(lock)						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 0, 1, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, _THIS_IP_);			\
} while (0)
# define might_lock_read(lock)						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 1, 1, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, _THIS_IP_);			\
} while (0)
# define might_lock_nested(lock, subclass)				\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, subclass, 0, 1, 1, NULL,		\
		     _THIS_IP_);					\
	lock_release(&(lock)->dep_map, _THIS_IP_);			\
} while (0)

DECLARE_PER_CPU(int, hardirqs_enabled);
DECLARE_PER_CPU(int, hardirq_context);
DECLARE_PER_CPU(unsigned int, lockdep_recursion);

#define __lockdep_enabled	(debug_locks && !this_cpu_read(lockdep_recursion))

#define lockdep_assert_irqs_enabled()					\
do {									\
	WARN_ON_ONCE(__lockdep_enabled && !this_cpu_read(hardirqs_enabled)); \
} while (0)

#define lockdep_assert_irqs_disabled()					\
do {									\
	WARN_ON_ONCE(__lockdep_enabled && this_cpu_read(hardirqs_enabled)); \
} while (0)

#define lockdep_assert_in_irq()						\
do {									\
	WARN_ON_ONCE(__lockdep_enabled && !this_cpu_read(hardirq_context)); \
} while (0)

#define lockdep_assert_preemption_enabled()				\
do {									\
	WARN_ON_ONCE(IS_ENABLED(CONFIG_PREEMPT_COUNT)	&&		\
		     __lockdep_enabled			&&		\
		     (preempt_count() != 0		||		\
		      !this_cpu_read(hardirqs_enabled)));		\
} while (0)

#define lockdep_assert_preemption_disabled()				\
do {									\
	WARN_ON_ONCE(IS_ENABLED(CONFIG_PREEMPT_COUNT)	&&		\
		     __lockdep_enabled			&&		\
		     (preempt_count() == 0		&&		\
		      this_cpu_read(hardirqs_enabled)));		\
} while (0)

/*
 * Acceptable for protecting per-CPU resources accessed from BH.
 * Much like in_softirq() - semantics are ambiguous, use carefully.
 */
#define lockdep_assert_in_softirq()					\
do {									\
	WARN_ON_ONCE(__lockdep_enabled			&&		\
		     (!in_softirq() || in_irq() || in_nmi()));		\
} while (0)

#else
# define might_lock(lock) do { } while (0)
# define might_lock_read(lock) do { } while (0)
# define might_lock_nested(lock, subclass) do { } while (0)

# define lockdep_assert_irqs_enabled() do { } while (0)
# define lockdep_assert_irqs_disabled() do { } while (0)
# define lockdep_assert_in_irq() do { } while (0)

# define lockdep_assert_preemption_enabled() do { } while (0)
# define lockdep_assert_preemption_disabled() do { } while (0)
# define lockdep_assert_in_softirq() do { } while (0)
#endif

#ifdef CONFIG_PROVE_RAW_LOCK_NESTING

# define lockdep_assert_RT_in_threaded_ctx() do {			\
		WARN_ONCE(debug_locks && !current->lockdep_recursion &&	\
			  lockdep_hardirq_context() &&			\
			  !(current->hardirq_threaded || current->irq_config),	\
			  "Not in threaded context on PREEMPT_RT as expected\n");	\
} while (0)

#else

# define lockdep_assert_RT_in_threaded_ctx() do { } while (0)

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

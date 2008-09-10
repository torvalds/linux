/*
 * Runtime locking correctness validator
 *
 *  Copyright (C) 2006,2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * see Documentation/lockdep-design.txt for more details.
 */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

struct task_struct;
struct lockdep_map;

#ifdef CONFIG_LOCKDEP

#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/debug_locks.h>
#include <linux/stacktrace.h>

/*
 * Lock-class usage-state bits:
 */
enum lock_usage_bit
{
	LOCK_USED = 0,
	LOCK_USED_IN_HARDIRQ,
	LOCK_USED_IN_SOFTIRQ,
	LOCK_ENABLED_SOFTIRQS,
	LOCK_ENABLED_HARDIRQS,
	LOCK_USED_IN_HARDIRQ_READ,
	LOCK_USED_IN_SOFTIRQ_READ,
	LOCK_ENABLED_SOFTIRQS_READ,
	LOCK_ENABLED_HARDIRQS_READ,
	LOCK_USAGE_STATES
};

/*
 * Usage-state bitmasks:
 */
#define LOCKF_USED			(1 << LOCK_USED)
#define LOCKF_USED_IN_HARDIRQ		(1 << LOCK_USED_IN_HARDIRQ)
#define LOCKF_USED_IN_SOFTIRQ		(1 << LOCK_USED_IN_SOFTIRQ)
#define LOCKF_ENABLED_HARDIRQS		(1 << LOCK_ENABLED_HARDIRQS)
#define LOCKF_ENABLED_SOFTIRQS		(1 << LOCK_ENABLED_SOFTIRQS)

#define LOCKF_ENABLED_IRQS (LOCKF_ENABLED_HARDIRQS | LOCKF_ENABLED_SOFTIRQS)
#define LOCKF_USED_IN_IRQ (LOCKF_USED_IN_HARDIRQ | LOCKF_USED_IN_SOFTIRQ)

#define LOCKF_USED_IN_HARDIRQ_READ	(1 << LOCK_USED_IN_HARDIRQ_READ)
#define LOCKF_USED_IN_SOFTIRQ_READ	(1 << LOCK_USED_IN_SOFTIRQ_READ)
#define LOCKF_ENABLED_HARDIRQS_READ	(1 << LOCK_ENABLED_HARDIRQS_READ)
#define LOCKF_ENABLED_SOFTIRQS_READ	(1 << LOCK_ENABLED_SOFTIRQS_READ)

#define LOCKF_ENABLED_IRQS_READ \
		(LOCKF_ENABLED_HARDIRQS_READ | LOCKF_ENABLED_SOFTIRQS_READ)
#define LOCKF_USED_IN_IRQ_READ \
		(LOCKF_USED_IN_HARDIRQ_READ | LOCKF_USED_IN_SOFTIRQ_READ)

#define MAX_LOCKDEP_SUBCLASSES		8UL

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

/*
 * The lock-class itself:
 */
struct lock_class {
	/*
	 * class-hash:
	 */
	struct list_head		hash_entry;

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
	struct stack_trace		usage_traces[LOCK_USAGE_STATES];

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
	unsigned long			contention_point[4];
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
	unsigned long			contention_point[4];
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
	struct lock_class		*class_cache;
	const char			*name;
#ifdef CONFIG_LOCK_STAT
	int				cpu;
#endif
};

/*
 * Every lock has a list of other locks that were taken after it.
 * We only grow the list, never remove from it:
 */
struct lock_list {
	struct list_head		entry;
	struct lock_class		*class;
	struct stack_trace		trace;
	int				distance;
};

/*
 * We record lock dependency chains, so that we can cache them:
 */
struct lock_chain {
	u8				irq_context;
	u8				depth;
	u16				base;
	struct list_head		entry;
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
	unsigned int trylock:1;
	unsigned int read:2;        /* see lock_acquire() comment */
	unsigned int check:2;       /* see lock_acquire() comment */
	unsigned int hardirqs_off:1;
};

/*
 * Initialization, self-test and debugging-output methods:
 */
extern void lockdep_init(void);
extern void lockdep_info(void);
extern void lockdep_reset(void);
extern void lockdep_reset_lock(struct lockdep_map *lock);
extern void lockdep_free_key_range(void *start, unsigned long size);
extern void lockdep_sys_exit(void);

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
 * To initialize a lockdep_map statically use this macro.
 * Note that _name must not be NULL.
 */
#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

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
 *   0: disabled
 *   1: simple checks (freeing, held-at-exit-time, etc.)
 *   2: full validation
 */
extern void lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			 int trylock, int read, int check,
			 struct lockdep_map *nest_lock, unsigned long ip);

extern void lock_release(struct lockdep_map *lock, int nested,
			 unsigned long ip);

extern void lock_set_subclass(struct lockdep_map *lock, unsigned int subclass,
			      unsigned long ip);

# define INIT_LOCKDEP				.lockdep_recursion = 0,

#define lockdep_depth(tsk)	(debug_locks ? (tsk)->lockdep_depth : 0)

#else /* !LOCKDEP */

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}

# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, n, i)			do { } while (0)
# define lock_set_subclass(l, s, i)		do { } while (0)
# define lockdep_init()				do { } while (0)
# define lockdep_info()				do { } while (0)
# define lockdep_init_map(lock, name, key, sub)	do { (void)(key); } while (0)
# define lockdep_set_class(lock, key)		do { (void)(key); } while (0)
# define lockdep_set_class_and_name(lock, key, name) \
		do { (void)(key); } while (0)
#define lockdep_set_class_and_subclass(lock, key, sub) \
		do { (void)(key); } while (0)
#define lockdep_set_subclass(lock, sub)		do { } while (0)

# define INIT_LOCKDEP
# define lockdep_reset()		do { debug_locks = 1; } while (0)
# define lockdep_free_key_range(start, size)	do { } while (0)
# define lockdep_sys_exit() 			do { } while (0)
/*
 * The class key takes no space if lockdep is disabled:
 */
struct lock_class_key { };

#define lockdep_depth(tsk)	(0)

#endif /* !LOCKDEP */

#ifdef CONFIG_LOCK_STAT

extern void lock_contended(struct lockdep_map *lock, unsigned long ip);
extern void lock_acquired(struct lockdep_map *lock);

#define LOCK_CONTENDED(_lock, try, lock)			\
do {								\
	if (!try(_lock)) {					\
		lock_contended(&(_lock)->dep_map, _RET_IP_);	\
		lock(_lock);					\
	}							\
	lock_acquired(&(_lock)->dep_map);			\
} while (0)

#else /* CONFIG_LOCK_STAT */

#define lock_contended(lockdep_map, ip) do {} while (0)
#define lock_acquired(lockdep_map) do {} while (0)

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#endif /* CONFIG_LOCK_STAT */

#if defined(CONFIG_TRACE_IRQFLAGS) && defined(CONFIG_GENERIC_HARDIRQS)
extern void early_init_irq_lock_class(void);
#else
static inline void early_init_irq_lock_class(void)
{
}
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
extern void early_boot_irqs_off(void);
extern void early_boot_irqs_on(void);
extern void print_irqtrace_events(struct task_struct *curr);
#else
static inline void early_boot_irqs_off(void)
{
}
static inline void early_boot_irqs_on(void)
{
}
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

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# ifdef CONFIG_PROVE_LOCKING
#  define spin_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define spin_acquire_nest(l, s, t, n, i)	lock_acquire(l, s, t, 0, 2, n, i)
# else
#  define spin_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define spin_acquire_nest(l, s, t, n, i)	lock_acquire(l, s, t, 0, 1, NULL, i)
# endif
# define spin_release(l, n, i)			lock_release(l, n, i)
#else
# define spin_acquire(l, s, t, i)		do { } while (0)
# define spin_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# ifdef CONFIG_PROVE_LOCKING
#  define rwlock_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define rwlock_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 2, 2, NULL, i)
# else
#  define rwlock_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define rwlock_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 2, 1, NULL, i)
# endif
# define rwlock_release(l, n, i)		lock_release(l, n, i)
#else
# define rwlock_acquire(l, s, t, i)		do { } while (0)
# define rwlock_acquire_read(l, s, t, i)	do { } while (0)
# define rwlock_release(l, n, i)		do { } while (0)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# ifdef CONFIG_PROVE_LOCKING
#  define mutex_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
# else
#  define mutex_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
# endif
# define mutex_release(l, n, i)			lock_release(l, n, i)
#else
# define mutex_acquire(l, s, t, i)		do { } while (0)
# define mutex_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# ifdef CONFIG_PROVE_LOCKING
#  define rwsem_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define rwsem_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 1, 2, NULL, i)
# else
#  define rwsem_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define rwsem_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 1, 1, NULL, i)
# endif
# define rwsem_release(l, n, i)			lock_release(l, n, i)
#else
# define rwsem_acquire(l, s, t, i)		do { } while (0)
# define rwsem_acquire_read(l, s, t, i)		do { } while (0)
# define rwsem_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# ifdef CONFIG_PROVE_LOCKING
#  define lock_map_acquire(l)		lock_acquire(l, 0, 0, 0, 2, NULL, _THIS_IP_)
# else
#  define lock_map_acquire(l)		lock_acquire(l, 0, 0, 0, 1, NULL, _THIS_IP_)
# endif
# define lock_map_release(l)			lock_release(l, 1, _THIS_IP_)
#else
# define lock_map_acquire(l)			do { } while (0)
# define lock_map_release(l)			do { } while (0)
#endif

#ifdef CONFIG_PROVE_LOCKING
# define might_lock(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 0, 2, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)
# define might_lock_read(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 1, 2, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)
#else
# define might_lock(lock) do { } while (0)
# define might_lock_read(lock) do { } while (0)
#endif

#endif /* __LINUX_LOCKDEP_H */

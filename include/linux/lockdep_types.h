/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime locking correctness validator
 *
 *  Copyright (C) 2006,2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 * see Documentation/locking/lockdep-design.rst for more details.
 */
#ifndef __LINUX_LOCKDEP_TYPES_H
#define __LINUX_LOCKDEP_TYPES_H

#include <linux/types.h>

#define MAX_LOCKDEP_SUBCLASSES		8UL

enum lockdep_wait_type {
	LD_WAIT_INV = 0,	/* not checked, catch all */

	LD_WAIT_FREE,		/* wait free, rcu etc.. */
	LD_WAIT_SPIN,		/* spin loops, raw_spinlock_t etc.. */

#ifdef CONFIG_PROVE_RAW_LOCK_NESTING
	LD_WAIT_CONFIG,		/* preemptible in PREEMPT_RT, spinlock_t etc.. */
#else
	LD_WAIT_CONFIG = LD_WAIT_SPIN,
#endif
	LD_WAIT_SLEEP,		/* sleeping locks, mutex_t etc.. */

	LD_WAIT_MAX,		/* must be last */
};

enum lockdep_lock_type {
	LD_LOCK_NORMAL = 0,	/* normal, catch all */
	LD_LOCK_PERCPU,		/* percpu */
	LD_LOCK_WAIT_OVERRIDE,	/* annotation */
	LD_LOCK_MAX,
};

#ifdef CONFIG_LOCKDEP

/*
 * We'd rather not expose kernel/lockdep_states.h this wide, but we do need
 * the total number of states... :-(
 *
 * XXX_LOCK_USAGE_STATES is the number of lines in lockdep_states.h, for each
 * of those we generates 4 states, Additionally we report on USED and USED_READ.
 */
#define XXX_LOCK_USAGE_STATES		2
#define LOCK_TRACE_STATES		(XXX_LOCK_USAGE_STATES*4 + 2)

/*
 * NR_LOCKDEP_CACHING_CLASSES ... Number of classes
 * cached in the instance of lockdep_map
 *
 * Currently main class (subclass == 0) and single depth subclass
 * are cached in lockdep_map. This optimization is mainly targeting
 * on rq->lock. double_rq_lock() acquires this highly competitive with
 * single depth.
 */
#define NR_LOCKDEP_CACHING_CLASSES	2

/*
 * A lockdep key is associated with each lock object. For static locks we use
 * the lock address itself as the key. Dynamically allocated lock objects can
 * have a statically or dynamically allocated key. Dynamically allocated lock
 * keys must be registered before being used and must be unregistered before
 * the key memory is freed.
 */
struct lockdep_subclass_key {
	char __one_byte;
} __attribute__ ((__packed__));

/* hash_entry is used to keep track of dynamically allocated keys. */
struct lock_class_key {
	union {
		struct hlist_node		hash_entry;
		struct lockdep_subclass_key	subkeys[MAX_LOCKDEP_SUBCLASSES];
	};
};

extern struct lock_class_key __lockdep_no_validate__;
extern struct lock_class_key __lockdep_no_track__;

struct lock_trace;

#define LOCKSTAT_POINTS		4

struct lockdep_map;
typedef int (*lock_cmp_fn)(const struct lockdep_map *a,
			   const struct lockdep_map *b);
typedef void (*lock_print_fn)(const struct lockdep_map *map);

/*
 * The lock-class itself. The order of the structure members matters.
 * reinit_class() zeroes the key member and all subsequent members.
 */
struct lock_class {
	/*
	 * class-hash:
	 */
	struct hlist_node		hash_entry;

	/*
	 * Entry in all_lock_classes when in use. Entry in free_lock_classes
	 * when not in use. Instances that are being freed are on one of the
	 * zapped_classes lists.
	 */
	struct list_head		lock_entry;

	/*
	 * These fields represent a directed graph of lock dependencies,
	 * to every node we attach a list of "forward" and a list of
	 * "backward" graph nodes.
	 */
	struct list_head		locks_after, locks_before;

	const struct lockdep_subclass_key *key;
	lock_cmp_fn			cmp_fn;
	lock_print_fn			print_fn;

	unsigned int			subclass;
	unsigned int			dep_gen_id;

	/*
	 * IRQ/softirq usage tracking bits:
	 */
	unsigned long			usage_mask;
	const struct lock_trace		*usage_traces[LOCK_TRACE_STATES];

	const char			*name;
	/*
	 * Generation counter, when doing certain classes of graph walking,
	 * to ensure that we check one node only once:
	 */
	int				name_version;

	u8				wait_type_inner;
	u8				wait_type_outer;
	u8				lock_type;
	/* u8				hole; */

#ifdef CONFIG_LOCK_STAT
	unsigned long			contention_point[LOCKSTAT_POINTS];
	unsigned long			contending_point[LOCKSTAT_POINTS];
#endif
} __no_randomize_layout;

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

void lock_stats(struct lock_class *class, struct lock_class_stats *stats);
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
	u8				wait_type_outer; /* can be taken in this context */
	u8				wait_type_inner; /* presents this context */
	u8				lock_type;
	/* u8				hole; */
#ifdef CONFIG_LOCK_STAT
	int				cpu;
	unsigned long			ip;
#endif
};

struct pin_cookie { unsigned int val; };

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
	unsigned int sync:1;
	unsigned int references:11;					/* 32 bits */
	unsigned int pin_count;
};

#else /* !CONFIG_LOCKDEP */

/*
 * The class key takes no space if lockdep is disabled:
 */
struct lock_class_key { };

/*
 * The lockdep_map takes no space if lockdep is disabled:
 */
struct lockdep_map { };

struct pin_cookie { };

#endif /* !LOCKDEP */

#endif /* __LINUX_LOCKDEP_TYPES_H */

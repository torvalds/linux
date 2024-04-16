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

struct lock_trace;

#define LOCKSTAT_POINTS		4

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
	unsigned int			subclass;
	unsigned int			dep_gen_id;

	/*
	 * IRQ/softirq usage tracking bits:
	 */
	unsigned long			usage_mask;
	const struct lock_trace		*usage_traces[LOCK_TRACE_STATES];

	/*
	 * Generation counter, when doing certain classes of graph walking,
	 * to ensure that we check one node only once:
	 */
	int				name_version;
	const char			*name;

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

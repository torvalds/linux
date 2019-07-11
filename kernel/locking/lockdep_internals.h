/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kernel/lockdep_internals.h
 *
 * Runtime locking correctness validator
 *
 * lockdep subsystem internal functions and variables.
 */

/*
 * Lock-class usage-state bits:
 */
enum lock_usage_bit {
#define LOCKDEP_STATE(__STATE)		\
	LOCK_USED_IN_##__STATE,		\
	LOCK_USED_IN_##__STATE##_READ,	\
	LOCK_ENABLED_##__STATE,		\
	LOCK_ENABLED_##__STATE##_READ,
#include "lockdep_states.h"
#undef LOCKDEP_STATE
	LOCK_USED,
	LOCK_USAGE_STATES
};

#define LOCK_USAGE_READ_MASK 1
#define LOCK_USAGE_DIR_MASK  2
#define LOCK_USAGE_STATE_MASK (~(LOCK_USAGE_READ_MASK | LOCK_USAGE_DIR_MASK))

/*
 * Usage-state bitmasks:
 */
#define __LOCKF(__STATE)	LOCKF_##__STATE = (1 << LOCK_##__STATE),

enum {
#define LOCKDEP_STATE(__STATE)						\
	__LOCKF(USED_IN_##__STATE)					\
	__LOCKF(USED_IN_##__STATE##_READ)				\
	__LOCKF(ENABLED_##__STATE)					\
	__LOCKF(ENABLED_##__STATE##_READ)
#include "lockdep_states.h"
#undef LOCKDEP_STATE
	__LOCKF(USED)
};

#define LOCKF_ENABLED_IRQ (LOCKF_ENABLED_HARDIRQ | LOCKF_ENABLED_SOFTIRQ)
#define LOCKF_USED_IN_IRQ (LOCKF_USED_IN_HARDIRQ | LOCKF_USED_IN_SOFTIRQ)

#define LOCKF_ENABLED_IRQ_READ \
		(LOCKF_ENABLED_HARDIRQ_READ | LOCKF_ENABLED_SOFTIRQ_READ)
#define LOCKF_USED_IN_IRQ_READ \
		(LOCKF_USED_IN_HARDIRQ_READ | LOCKF_USED_IN_SOFTIRQ_READ)

/*
 * CONFIG_LOCKDEP_SMALL is defined for sparc. Sparc requires .text,
 * .data and .bss to fit in required 32MB limit for the kernel. With
 * CONFIG_LOCKDEP we could go over this limit and cause system boot-up problems.
 * So, reduce the static allocations for lockdeps related structures so that
 * everything fits in current required size limit.
 */
#ifdef CONFIG_LOCKDEP_SMALL
/*
 * MAX_LOCKDEP_ENTRIES is the maximum number of lock dependencies
 * we track.
 *
 * We use the per-lock dependency maps in two ways: we grow it by adding
 * every to-be-taken lock to all currently held lock's own dependency
 * table (if it's not there yet), and we check it for lock order
 * conflicts and deadlocks.
 */
#define MAX_LOCKDEP_ENTRIES	16384UL
#define MAX_LOCKDEP_CHAINS_BITS	15
#define MAX_STACK_TRACE_ENTRIES	262144UL
#else
#define MAX_LOCKDEP_ENTRIES	32768UL

#define MAX_LOCKDEP_CHAINS_BITS	16

/*
 * Stack-trace: tightly packed array of stack backtrace
 * addresses. Protected by the hash_lock.
 */
#define MAX_STACK_TRACE_ENTRIES	524288UL
#endif

#define MAX_LOCKDEP_CHAINS	(1UL << MAX_LOCKDEP_CHAINS_BITS)

#define MAX_LOCKDEP_CHAIN_HLOCKS (MAX_LOCKDEP_CHAINS*5)

extern struct list_head all_lock_classes;
extern struct lock_chain lock_chains[];

#define LOCK_USAGE_CHARS (1+LOCK_USAGE_STATES/2)

extern void get_usage_chars(struct lock_class *class,
			    char usage[LOCK_USAGE_CHARS]);

extern const char * __get_key_name(struct lockdep_subclass_key *key, char *str);

struct lock_class *lock_chain_get_class(struct lock_chain *chain, int i);

extern unsigned long nr_lock_classes;
extern unsigned long nr_list_entries;
long lockdep_next_lockchain(long i);
unsigned long lock_chain_count(void);
extern int nr_chain_hlocks;
extern unsigned long nr_stack_trace_entries;

extern unsigned int nr_hardirq_chains;
extern unsigned int nr_softirq_chains;
extern unsigned int nr_process_chains;
extern unsigned int max_lockdep_depth;
extern unsigned int max_recursion_depth;

extern unsigned int max_bfs_queue_depth;

#ifdef CONFIG_PROVE_LOCKING
extern unsigned long lockdep_count_forward_deps(struct lock_class *);
extern unsigned long lockdep_count_backward_deps(struct lock_class *);
#else
static inline unsigned long
lockdep_count_forward_deps(struct lock_class *class)
{
	return 0;
}
static inline unsigned long
lockdep_count_backward_deps(struct lock_class *class)
{
	return 0;
}
#endif

#ifdef CONFIG_DEBUG_LOCKDEP

#include <asm/local.h>
/*
 * Various lockdep statistics.
 * We want them per cpu as they are often accessed in fast path
 * and we want to avoid too much cache bouncing.
 */
struct lockdep_stats {
	int	chain_lookup_hits;
	int	chain_lookup_misses;
	int	hardirqs_on_events;
	int	hardirqs_off_events;
	int	redundant_hardirqs_on;
	int	redundant_hardirqs_off;
	int	softirqs_on_events;
	int	softirqs_off_events;
	int	redundant_softirqs_on;
	int	redundant_softirqs_off;
	int	nr_unused_locks;
	int	nr_redundant_checks;
	int	nr_redundant;
	int	nr_cyclic_checks;
	int	nr_cyclic_check_recursions;
	int	nr_find_usage_forwards_checks;
	int	nr_find_usage_forwards_recursions;
	int	nr_find_usage_backwards_checks;
	int	nr_find_usage_backwards_recursions;

	/*
	 * Per lock class locking operation stat counts
	 */
	unsigned long lock_class_ops[MAX_LOCKDEP_KEYS];
};

DECLARE_PER_CPU(struct lockdep_stats, lockdep_stats);
extern struct lock_class lock_classes[MAX_LOCKDEP_KEYS];

#define __debug_atomic_inc(ptr)					\
	this_cpu_inc(lockdep_stats.ptr);

#define debug_atomic_inc(ptr)			{		\
	WARN_ON_ONCE(!irqs_disabled());				\
	__this_cpu_inc(lockdep_stats.ptr);			\
}

#define debug_atomic_dec(ptr)			{		\
	WARN_ON_ONCE(!irqs_disabled());				\
	__this_cpu_dec(lockdep_stats.ptr);			\
}

#define debug_atomic_read(ptr)		({				\
	struct lockdep_stats *__cpu_lockdep_stats;			\
	unsigned long long __total = 0;					\
	int __cpu;							\
	for_each_possible_cpu(__cpu) {					\
		__cpu_lockdep_stats = &per_cpu(lockdep_stats, __cpu);	\
		__total += __cpu_lockdep_stats->ptr;			\
	}								\
	__total;							\
})

static inline void debug_class_ops_inc(struct lock_class *class)
{
	int idx;

	idx = class - lock_classes;
	__debug_atomic_inc(lock_class_ops[idx]);
}

static inline unsigned long debug_class_ops_read(struct lock_class *class)
{
	int idx, cpu;
	unsigned long ops = 0;

	idx = class - lock_classes;
	for_each_possible_cpu(cpu)
		ops += per_cpu(lockdep_stats.lock_class_ops[idx], cpu);
	return ops;
}

#else
# define __debug_atomic_inc(ptr)	do { } while (0)
# define debug_atomic_inc(ptr)		do { } while (0)
# define debug_atomic_dec(ptr)		do { } while (0)
# define debug_atomic_read(ptr)		0
# define debug_class_ops_inc(ptr)	do { } while (0)
#endif

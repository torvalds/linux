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
	LOCK_USED = 0,
	LOCK_USED_IN_HARDIRQ,
	LOCK_USED_IN_SOFTIRQ,
	LOCK_USED_IN_RECLAIM_FS,
	LOCK_ENABLED_SOFTIRQ,
	LOCK_ENABLED_HARDIRQ,
	LOCK_ENABLED_RECLAIM_FS,
	LOCK_USED_IN_HARDIRQ_READ,
	LOCK_USED_IN_SOFTIRQ_READ,
	LOCK_USED_IN_RECLAIM_FS_READ,
	LOCK_ENABLED_SOFTIRQ_READ,
	LOCK_ENABLED_HARDIRQ_READ,
	LOCK_ENABLED_RECLAIM_FS_READ,
	LOCK_USAGE_STATES
};

/*
 * Usage-state bitmasks:
 */
#define LOCKF_USED			(1 << LOCK_USED)
#define LOCKF_USED_IN_HARDIRQ		(1 << LOCK_USED_IN_HARDIRQ)
#define LOCKF_USED_IN_SOFTIRQ		(1 << LOCK_USED_IN_SOFTIRQ)
#define LOCKF_USED_IN_RECLAIM_FS	(1 << LOCK_USED_IN_RECLAIM_FS)
#define LOCKF_ENABLED_HARDIRQ		(1 << LOCK_ENABLED_HARDIRQ)
#define LOCKF_ENABLED_SOFTIRQ		(1 << LOCK_ENABLED_SOFTIRQ)
#define LOCKF_ENABLED_RECLAIM_FS	(1 << LOCK_ENABLED_RECLAIM_FS)

#define LOCKF_ENABLED_IRQ (LOCKF_ENABLED_HARDIRQ | LOCKF_ENABLED_SOFTIRQ)
#define LOCKF_USED_IN_IRQ (LOCKF_USED_IN_HARDIRQ | LOCKF_USED_IN_SOFTIRQ)

#define LOCKF_USED_IN_HARDIRQ_READ	(1 << LOCK_USED_IN_HARDIRQ_READ)
#define LOCKF_USED_IN_SOFTIRQ_READ	(1 << LOCK_USED_IN_SOFTIRQ_READ)
#define LOCKF_USED_IN_RECLAIM_FS_READ	(1 << LOCK_USED_IN_RECLAIM_FS_READ)
#define LOCKF_ENABLED_HARDIRQ_READ	(1 << LOCK_ENABLED_HARDIRQ_READ)
#define LOCKF_ENABLED_SOFTIRQ_READ	(1 << LOCK_ENABLED_SOFTIRQ_READ)
#define LOCKF_ENABLED_RECLAIM_FS_READ	(1 << LOCK_ENABLED_RECLAIM_FS_READ)

#define LOCKF_ENABLED_IRQ_READ \
		(LOCKF_ENABLED_HARDIRQ_READ | LOCKF_ENABLED_SOFTIRQ_READ)
#define LOCKF_USED_IN_IRQ_READ \
		(LOCKF_USED_IN_HARDIRQ_READ | LOCKF_USED_IN_SOFTIRQ_READ)

/*
 * MAX_LOCKDEP_ENTRIES is the maximum number of lock dependencies
 * we track.
 *
 * We use the per-lock dependency maps in two ways: we grow it by adding
 * every to-be-taken lock to all currently held lock's own dependency
 * table (if it's not there yet), and we check it for lock order
 * conflicts and deadlocks.
 */
#define MAX_LOCKDEP_ENTRIES	8192UL

#define MAX_LOCKDEP_CHAINS_BITS	14
#define MAX_LOCKDEP_CHAINS	(1UL << MAX_LOCKDEP_CHAINS_BITS)

#define MAX_LOCKDEP_CHAIN_HLOCKS (MAX_LOCKDEP_CHAINS*5)

/*
 * Stack-trace: tightly packed array of stack backtrace
 * addresses. Protected by the hash_lock.
 */
#define MAX_STACK_TRACE_ENTRIES	262144UL

extern struct list_head all_lock_classes;
extern struct lock_chain lock_chains[];

extern void
get_usage_chars(struct lock_class *class, char *c1, char *c2, char *c3,
					char *c4, char *c5, char *c6);

extern const char * __get_key_name(struct lockdep_subclass_key *key, char *str);

struct lock_class *lock_chain_get_class(struct lock_chain *chain, int i);

extern unsigned long nr_lock_classes;
extern unsigned long nr_list_entries;
extern unsigned long nr_lock_chains;
extern int nr_chain_hlocks;
extern unsigned long nr_stack_trace_entries;

extern unsigned int nr_hardirq_chains;
extern unsigned int nr_softirq_chains;
extern unsigned int nr_process_chains;
extern unsigned int max_lockdep_depth;
extern unsigned int max_recursion_depth;

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
/*
 * Various lockdep statistics:
 */
extern atomic_t chain_lookup_hits;
extern atomic_t chain_lookup_misses;
extern atomic_t hardirqs_on_events;
extern atomic_t hardirqs_off_events;
extern atomic_t redundant_hardirqs_on;
extern atomic_t redundant_hardirqs_off;
extern atomic_t softirqs_on_events;
extern atomic_t softirqs_off_events;
extern atomic_t redundant_softirqs_on;
extern atomic_t redundant_softirqs_off;
extern atomic_t nr_unused_locks;
extern atomic_t nr_cyclic_checks;
extern atomic_t nr_cyclic_check_recursions;
extern atomic_t nr_find_usage_forwards_checks;
extern atomic_t nr_find_usage_forwards_recursions;
extern atomic_t nr_find_usage_backwards_checks;
extern atomic_t nr_find_usage_backwards_recursions;
# define debug_atomic_inc(ptr)		atomic_inc(ptr)
# define debug_atomic_dec(ptr)		atomic_dec(ptr)
# define debug_atomic_read(ptr)		atomic_read(ptr)
#else
# define debug_atomic_inc(ptr)		do { } while (0)
# define debug_atomic_dec(ptr)		do { } while (0)
# define debug_atomic_read(ptr)		0
#endif

/*
 * kernel/lockdep_internals.h
 *
 * Runtime locking correctness validator
 *
 * lockdep subsystem internal functions and variables.
 */

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

#define MAX_LOCKDEP_KEYS_BITS	11
#define MAX_LOCKDEP_KEYS	(1UL << MAX_LOCKDEP_KEYS_BITS)

#define MAX_LOCKDEP_CHAINS_BITS	14
#define MAX_LOCKDEP_CHAINS	(1UL << MAX_LOCKDEP_CHAINS_BITS)

/*
 * Stack-trace: tightly packed array of stack backtrace
 * addresses. Protected by the hash_lock.
 */
#define MAX_STACK_TRACE_ENTRIES	262144UL

extern struct list_head all_lock_classes;

extern void
get_usage_chars(struct lock_class *class, char *c1, char *c2, char *c3, char *c4);

extern const char * __get_key_name(struct lockdep_subclass_key *key, char *str);

extern unsigned long nr_lock_classes;
extern unsigned long nr_list_entries;
extern unsigned long nr_lock_chains;
extern unsigned long nr_stack_trace_entries;

extern unsigned int nr_hardirq_chains;
extern unsigned int nr_softirq_chains;
extern unsigned int nr_process_chains;
extern unsigned int max_lockdep_depth;
extern unsigned int max_recursion_depth;

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

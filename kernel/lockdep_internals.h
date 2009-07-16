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
#define MAX_LOCKDEP_CHAINS	(1UL << MAX_LOCKDEP_CHAINS_BITS)

#define MAX_LOCKDEP_CHAIN_HLOCKS (MAX_LOCKDEP_CHAINS*5)

/*
 * Stack-trace: tightly packed array of stack backtrace
 * addresses. Protected by the hash_lock.
 */
#define MAX_STACK_TRACE_ENTRIES	262144UL

extern struct list_head all_lock_classes;
extern struct lock_chain lock_chains[];

#define LOCK_USAGE_CHARS (1+LOCK_USAGE_STATES/2)

extern void get_usage_chars(struct lock_class *class,
			    char usage[LOCK_USAGE_CHARS]);

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


extern unsigned int max_bfs_queue_depth;
extern unsigned long nr_list_entries;
extern struct lock_list list_entries[MAX_LOCKDEP_ENTRIES];
extern unsigned long bfs_accessed[];

/*For good efficiency of modular, we use power of 2*/
#define  MAX_CIRCULAR_QUE_SIZE	    4096UL

/* The circular_queue and helpers is used to implement the
 * breadth-first search(BFS)algorithem, by which we can build
 * the shortest path from the next lock to be acquired to the
 * previous held lock if there is a circular between them.
 * */
struct circular_queue{
	unsigned long element[MAX_CIRCULAR_QUE_SIZE];
	unsigned int  front, rear;
};

static inline void __cq_init(struct circular_queue *cq)
{
	cq->front = cq->rear = 0;
	bitmap_zero(bfs_accessed, MAX_LOCKDEP_ENTRIES);
}

static inline int __cq_empty(struct circular_queue *cq)
{
	return (cq->front == cq->rear);
}

static inline int __cq_full(struct circular_queue *cq)
{
	return ((cq->rear + 1)&(MAX_CIRCULAR_QUE_SIZE-1))  == cq->front;
}

static inline int __cq_enqueue(struct circular_queue *cq, unsigned long elem)
{
	if (__cq_full(cq))
		return -1;

	cq->element[cq->rear] = elem;
	cq->rear = (cq->rear + 1)&(MAX_CIRCULAR_QUE_SIZE-1);
	return 0;
}

static inline int __cq_dequeue(struct circular_queue *cq, unsigned long *elem)
{
	if (__cq_empty(cq))
		return -1;

	*elem = cq->element[cq->front];
	cq->front = (cq->front + 1)&(MAX_CIRCULAR_QUE_SIZE-1);
	return 0;
}

static inline unsigned int  __cq_get_elem_count(struct circular_queue *cq)
{
	return (cq->rear - cq->front)&(MAX_CIRCULAR_QUE_SIZE-1);
}

static inline void mark_lock_accessed(struct lock_list *lock,
					struct lock_list *parent)
{
	unsigned long nr;
	nr = lock - list_entries;
	WARN_ON(nr >= nr_list_entries);
	lock->parent = parent;
	set_bit(nr, bfs_accessed);
}

static inline unsigned long lock_accessed(struct lock_list *lock)
{
	unsigned long nr;
	nr = lock - list_entries;
	WARN_ON(nr >= nr_list_entries);
	return test_bit(nr, bfs_accessed);
}

static inline struct lock_list *get_lock_parent(struct lock_list *child)
{
	return child->parent;
}

static inline int get_lock_depth(struct lock_list *child)
{
	int depth = 0;
	struct lock_list *parent;

	while ((parent = get_lock_parent(child))) {
		child = parent;
		depth++;
	}
	return depth;
}

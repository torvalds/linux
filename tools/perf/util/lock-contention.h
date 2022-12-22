// SPDX-License-Identifier: GPL-2.0
#ifndef PERF_LOCK_CONTENTION_H
#define PERF_LOCK_CONTENTION_H

#include <linux/list.h>
#include <linux/rbtree.h>

struct lock_filter {
	int			nr_types;
	int			nr_addrs;
	int			nr_syms;
	unsigned int		*types;
	unsigned long		*addrs;
	char			**syms;
};

struct lock_stat {
	struct hlist_node	hash_entry;
	struct rb_node		rb;		/* used for sorting */

	u64			addr;		/* address of lockdep_map, used as ID */
	char			*name;		/* for strcpy(), we cannot use const */
	u64			*callstack;

	unsigned int		nr_acquire;
	unsigned int		nr_acquired;
	unsigned int		nr_contended;
	unsigned int		nr_release;

	union {
		unsigned int	nr_readlock;
		unsigned int	flags;
	};
	unsigned int		nr_trylock;

	/* these times are in nano sec. */
	u64                     avg_wait_time;
	u64			wait_time_total;
	u64			wait_time_min;
	u64			wait_time_max;

	int			broken; /* flag of blacklist */
	int			combined;
};

/*
 * States of lock_seq_stat
 *
 * UNINITIALIZED is required for detecting first event of acquire.
 * As the nature of lock events, there is no guarantee
 * that the first event for the locks are acquire,
 * it can be acquired, contended or release.
 */
#define SEQ_STATE_UNINITIALIZED      0	       /* initial state */
#define SEQ_STATE_RELEASED	1
#define SEQ_STATE_ACQUIRING	2
#define SEQ_STATE_ACQUIRED	3
#define SEQ_STATE_READ_ACQUIRED	4
#define SEQ_STATE_CONTENDED	5

/*
 * MAX_LOCK_DEPTH
 * Imported from include/linux/sched.h.
 * Should this be synchronized?
 */
#define MAX_LOCK_DEPTH 48

/*
 * struct lock_seq_stat:
 * Place to put on state of one lock sequence
 * 1) acquire -> acquired -> release
 * 2) acquire -> contended -> acquired -> release
 * 3) acquire (with read or try) -> release
 * 4) Are there other patterns?
 */
struct lock_seq_stat {
	struct list_head        list;
	int			state;
	u64			prev_event_time;
	u64                     addr;

	int                     read_count;
};

struct thread_stat {
	struct rb_node		rb;

	u32                     tid;
	struct list_head        seq_list;
};

/*
 * CONTENTION_STACK_DEPTH
 * Number of stack trace entries to find callers
 */
#define CONTENTION_STACK_DEPTH  8

/*
 * CONTENTION_STACK_SKIP
 * Number of stack trace entries to skip when finding callers.
 * The first few entries belong to the locking implementation itself.
 */
#define CONTENTION_STACK_SKIP  4

/*
 * flags for lock:contention_begin
 * Imported from include/trace/events/lock.h.
 */
#define LCB_F_SPIN	(1U << 0)
#define LCB_F_READ	(1U << 1)
#define LCB_F_WRITE	(1U << 2)
#define LCB_F_RT	(1U << 3)
#define LCB_F_PERCPU	(1U << 4)
#define LCB_F_MUTEX	(1U << 5)

struct evlist;
struct machine;
struct target;

struct lock_contention {
	struct evlist *evlist;
	struct target *target;
	struct machine *machine;
	struct hlist_head *result;
	struct lock_filter *filters;
	unsigned long map_nr_entries;
	int lost;
	int max_stack;
	int stack_skip;
	int aggr_mode;
};

#ifdef HAVE_BPF_SKEL

int lock_contention_prepare(struct lock_contention *con);
int lock_contention_start(void);
int lock_contention_stop(void);
int lock_contention_read(struct lock_contention *con);
int lock_contention_finish(void);

#else  /* !HAVE_BPF_SKEL */

static inline int lock_contention_prepare(struct lock_contention *con __maybe_unused)
{
	return 0;
}

static inline int lock_contention_start(void) { return 0; }
static inline int lock_contention_stop(void) { return 0; }
static inline int lock_contention_finish(void) { return 0; }

static inline int lock_contention_read(struct lock_contention *con __maybe_unused)
{
	return 0;
}

#endif  /* HAVE_BPF_SKEL */

#endif  /* PERF_LOCK_CONTENTION_H */

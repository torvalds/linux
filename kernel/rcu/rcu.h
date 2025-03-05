/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update definitions shared among RCU implementations.
 *
 * Copyright IBM Corporation, 2011
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

#include <linux/slab.h>
#include <trace/events/rcu.h>

/*
 * Grace-period counter management.
 */

#define RCU_SEQ_CTR_SHIFT	2
#define RCU_SEQ_STATE_MASK	((1 << RCU_SEQ_CTR_SHIFT) - 1)

/* Low-order bit definition for polled grace-period APIs. */
#define RCU_GET_STATE_COMPLETED	0x1

extern int sysctl_sched_rt_runtime;

/*
 * Return the counter portion of a sequence number previously returned
 * by rcu_seq_snap() or rcu_seq_current().
 */
static inline unsigned long rcu_seq_ctr(unsigned long s)
{
	return s >> RCU_SEQ_CTR_SHIFT;
}

/*
 * Return the state portion of a sequence number previously returned
 * by rcu_seq_snap() or rcu_seq_current().
 */
static inline int rcu_seq_state(unsigned long s)
{
	return s & RCU_SEQ_STATE_MASK;
}

/*
 * Set the state portion of the pointed-to sequence number.
 * The caller is responsible for preventing conflicting updates.
 */
static inline void rcu_seq_set_state(unsigned long *sp, int newstate)
{
	WARN_ON_ONCE(newstate & ~RCU_SEQ_STATE_MASK);
	WRITE_ONCE(*sp, (*sp & ~RCU_SEQ_STATE_MASK) + newstate);
}

/* Adjust sequence number for start of update-side operation. */
static inline void rcu_seq_start(unsigned long *sp)
{
	WRITE_ONCE(*sp, *sp + 1);
	smp_mb(); /* Ensure update-side operation after counter increment. */
	WARN_ON_ONCE(rcu_seq_state(*sp) != 1);
}

/* Compute the end-of-grace-period value for the specified sequence number. */
static inline unsigned long rcu_seq_endval(unsigned long *sp)
{
	return (*sp | RCU_SEQ_STATE_MASK) + 1;
}

/* Adjust sequence number for end of update-side operation. */
static inline void rcu_seq_end(unsigned long *sp)
{
	smp_mb(); /* Ensure update-side operation before counter increment. */
	WARN_ON_ONCE(!rcu_seq_state(*sp));
	WRITE_ONCE(*sp, rcu_seq_endval(sp));
}

/*
 * rcu_seq_snap - Take a snapshot of the update side's sequence number.
 *
 * This function returns the earliest value of the grace-period sequence number
 * that will indicate that a full grace period has elapsed since the current
 * time.  Once the grace-period sequence number has reached this value, it will
 * be safe to invoke all callbacks that have been registered prior to the
 * current time. This value is the current grace-period number plus two to the
 * power of the number of low-order bits reserved for state, then rounded up to
 * the next value in which the state bits are all zero.
 */
static inline unsigned long rcu_seq_snap(unsigned long *sp)
{
	unsigned long s;

	s = (READ_ONCE(*sp) + 2 * RCU_SEQ_STATE_MASK + 1) & ~RCU_SEQ_STATE_MASK;
	smp_mb(); /* Above access must not bleed into critical section. */
	return s;
}

/* Return the current value the update side's sequence number, no ordering. */
static inline unsigned long rcu_seq_current(unsigned long *sp)
{
	return READ_ONCE(*sp);
}

/*
 * Given a snapshot from rcu_seq_snap(), determine whether or not the
 * corresponding update-side operation has started.
 */
static inline bool rcu_seq_started(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_LT((s - 1) & ~RCU_SEQ_STATE_MASK, READ_ONCE(*sp));
}

/*
 * Given a snapshot from rcu_seq_snap(), determine whether or not a
 * full update-side operation has occurred.
 */
static inline bool rcu_seq_done(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_GE(READ_ONCE(*sp), s);
}

/*
 * Given a snapshot from rcu_seq_snap(), determine whether or not a
 * full update-side operation has occurred, but do not allow the
 * (ULONG_MAX / 2) safety-factor/guard-band.
 */
static inline bool rcu_seq_done_exact(unsigned long *sp, unsigned long s)
{
	unsigned long cur_s = READ_ONCE(*sp);

	return ULONG_CMP_GE(cur_s, s) || ULONG_CMP_LT(cur_s, s - (2 * RCU_SEQ_STATE_MASK + 1));
}

/*
 * Has a grace period completed since the time the old gp_seq was collected?
 */
static inline bool rcu_seq_completed_gp(unsigned long old, unsigned long new)
{
	return ULONG_CMP_LT(old, new & ~RCU_SEQ_STATE_MASK);
}

/*
 * Has a grace period started since the time the old gp_seq was collected?
 */
static inline bool rcu_seq_new_gp(unsigned long old, unsigned long new)
{
	return ULONG_CMP_LT((old + RCU_SEQ_STATE_MASK) & ~RCU_SEQ_STATE_MASK,
			    new);
}

/*
 * Roughly how many full grace periods have elapsed between the collection
 * of the two specified grace periods?
 */
static inline unsigned long rcu_seq_diff(unsigned long new, unsigned long old)
{
	unsigned long rnd_diff;

	if (old == new)
		return 0;
	/*
	 * Compute the number of grace periods (still shifted up), plus
	 * one if either of new and old is not an exact grace period.
	 */
	rnd_diff = (new & ~RCU_SEQ_STATE_MASK) -
		   ((old + RCU_SEQ_STATE_MASK) & ~RCU_SEQ_STATE_MASK) +
		   ((new & RCU_SEQ_STATE_MASK) || (old & RCU_SEQ_STATE_MASK));
	if (ULONG_CMP_GE(RCU_SEQ_STATE_MASK, rnd_diff))
		return 1; /* Definitely no grace period has elapsed. */
	return ((rnd_diff - RCU_SEQ_STATE_MASK - 1) >> RCU_SEQ_CTR_SHIFT) + 2;
}

/*
 * debug_rcu_head_queue()/debug_rcu_head_unqueue() are used internally
 * by call_rcu() and rcu callback execution, and are therefore not part
 * of the RCU API. These are in rcupdate.h because they are used by all
 * RCU implementations.
 */

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
# define STATE_RCU_HEAD_READY	0
# define STATE_RCU_HEAD_QUEUED	1

extern const struct debug_obj_descr rcuhead_debug_descr;

static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	int r1;

	r1 = debug_object_activate(head, &rcuhead_debug_descr);
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_READY,
				  STATE_RCU_HEAD_QUEUED);
	return r1;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_QUEUED,
				  STATE_RCU_HEAD_READY);
	debug_object_deactivate(head, &rcuhead_debug_descr);
}
#else	/* !CONFIG_DEBUG_OBJECTS_RCU_HEAD */
static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	return 0;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}
#endif	/* #else !CONFIG_DEBUG_OBJECTS_RCU_HEAD */

static inline void debug_rcu_head_callback(struct rcu_head *rhp)
{
	if (unlikely(!rhp->func))
		kmem_dump_obj(rhp);
}

extern int rcu_cpu_stall_suppress_at_boot;

static inline bool rcu_stall_is_suppressed_at_boot(void)
{
	return rcu_cpu_stall_suppress_at_boot && !rcu_inkernel_boot_has_ended();
}

#ifdef CONFIG_RCU_STALL_COMMON

extern int rcu_cpu_stall_ftrace_dump;
extern int rcu_cpu_stall_suppress;
extern int rcu_cpu_stall_timeout;
extern int rcu_exp_cpu_stall_timeout;
int rcu_jiffies_till_stall_check(void);
int rcu_exp_jiffies_till_stall_check(void);

static inline bool rcu_stall_is_suppressed(void)
{
	return rcu_stall_is_suppressed_at_boot() || rcu_cpu_stall_suppress;
}

#define rcu_ftrace_dump_stall_suppress() \
do { \
	if (!rcu_cpu_stall_suppress) \
		rcu_cpu_stall_suppress = 3; \
} while (0)

#define rcu_ftrace_dump_stall_unsuppress() \
do { \
	if (rcu_cpu_stall_suppress == 3) \
		rcu_cpu_stall_suppress = 0; \
} while (0)

#else /* #endif #ifdef CONFIG_RCU_STALL_COMMON */

static inline bool rcu_stall_is_suppressed(void)
{
	return rcu_stall_is_suppressed_at_boot();
}
#define rcu_ftrace_dump_stall_suppress()
#define rcu_ftrace_dump_stall_unsuppress()
#endif /* #ifdef CONFIG_RCU_STALL_COMMON */

/*
 * Strings used in tracepoints need to be exported via the
 * tracing system such that tools like perf and trace-cmd can
 * translate the string address pointers to actual text.
 */
#define TPS(x)  tracepoint_string(x)

/*
 * Dump the ftrace buffer, but only one time per callsite per boot.
 */
#define rcu_ftrace_dump(oops_dump_mode) \
do { \
	static atomic_t ___rfd_beenhere = ATOMIC_INIT(0); \
	\
	if (!atomic_read(&___rfd_beenhere) && \
	    !atomic_xchg(&___rfd_beenhere, 1)) { \
		tracing_off(); \
		rcu_ftrace_dump_stall_suppress(); \
		ftrace_dump(oops_dump_mode); \
		rcu_ftrace_dump_stall_unsuppress(); \
	} \
} while (0)

void rcu_early_boot_tests(void);
void rcu_test_sync_prims(void);

/*
 * This function really isn't for public consumption, but RCU is special in
 * that context switches can allow the state machine to make progress.
 */
extern void resched_cpu(int cpu);

#if defined(CONFIG_SRCU) || !defined(CONFIG_TINY_RCU)

#include <linux/rcu_node_tree.h>

extern int rcu_num_lvls;
extern int num_rcu_lvl[];
extern int rcu_num_nodes;
static bool rcu_fanout_exact;
static int rcu_fanout_leaf;

/*
 * Compute the per-level fanout, either using the exact fanout specified
 * or balancing the tree, depending on the rcu_fanout_exact boot parameter.
 */
static inline void rcu_init_levelspread(int *levelspread, const int *levelcnt)
{
	int i;

	for (i = 0; i < RCU_NUM_LVLS; i++)
		levelspread[i] = INT_MIN;
	if (rcu_fanout_exact) {
		levelspread[rcu_num_lvls - 1] = rcu_fanout_leaf;
		for (i = rcu_num_lvls - 2; i >= 0; i--)
			levelspread[i] = RCU_FANOUT;
	} else {
		int ccur;
		int cprv;

		cprv = nr_cpu_ids;
		for (i = rcu_num_lvls - 1; i >= 0; i--) {
			ccur = levelcnt[i];
			levelspread[i] = (cprv + ccur - 1) / ccur;
			cprv = ccur;
		}
	}
}

extern void rcu_init_geometry(void);

/* Returns a pointer to the first leaf rcu_node structure. */
#define rcu_first_leaf_node() (rcu_state.level[rcu_num_lvls - 1])

/* Is this rcu_node a leaf? */
#define rcu_is_leaf_node(rnp) ((rnp)->level == rcu_num_lvls - 1)

/* Is this rcu_node the last leaf? */
#define rcu_is_last_leaf_node(rnp) ((rnp) == &rcu_state.node[rcu_num_nodes - 1])

/*
 * Do a full breadth-first scan of the {s,}rcu_node structures for the
 * specified state structure (for SRCU) or the only rcu_state structure
 * (for RCU).
 */
#define srcu_for_each_node_breadth_first(sp, rnp) \
	for ((rnp) = &(sp)->node[0]; \
	     (rnp) < &(sp)->node[rcu_num_nodes]; (rnp)++)
#define rcu_for_each_node_breadth_first(rnp) \
	srcu_for_each_node_breadth_first(&rcu_state, rnp)

/*
 * Scan the leaves of the rcu_node hierarchy for the rcu_state structure.
 * Note that if there is a singleton rcu_node tree with but one rcu_node
 * structure, this loop -will- visit the rcu_node structure.  It is still
 * a leaf node, even if it is also the root node.
 */
#define rcu_for_each_leaf_node(rnp) \
	for ((rnp) = rcu_first_leaf_node(); \
	     (rnp) < &rcu_state.node[rcu_num_nodes]; (rnp)++)

/*
 * Iterate over all possible CPUs in a leaf RCU node.
 */
#define for_each_leaf_node_possible_cpu(rnp, cpu) \
	for (WARN_ON_ONCE(!rcu_is_leaf_node(rnp)), \
	     (cpu) = cpumask_next((rnp)->grplo - 1, cpu_possible_mask); \
	     (cpu) <= rnp->grphi; \
	     (cpu) = cpumask_next((cpu), cpu_possible_mask))

/*
 * Iterate over all CPUs in a leaf RCU node's specified mask.
 */
#define rcu_find_next_bit(rnp, cpu, mask) \
	((rnp)->grplo + find_next_bit(&(mask), BITS_PER_LONG, (cpu)))
#define for_each_leaf_node_cpu_mask(rnp, cpu, mask) \
	for (WARN_ON_ONCE(!rcu_is_leaf_node(rnp)), \
	     (cpu) = rcu_find_next_bit((rnp), 0, (mask)); \
	     (cpu) <= rnp->grphi; \
	     (cpu) = rcu_find_next_bit((rnp), (cpu) + 1 - (rnp->grplo), (mask)))

/*
 * Wrappers for the rcu_node::lock acquire and release.
 *
 * Because the rcu_nodes form a tree, the tree traversal locking will observe
 * different lock values, this in turn means that an UNLOCK of one level
 * followed by a LOCK of another level does not imply a full memory barrier;
 * and most importantly transitivity is lost.
 *
 * In order to restore full ordering between tree levels, augment the regular
 * lock acquire functions with smp_mb__after_unlock_lock().
 *
 * As ->lock of struct rcu_node is a __private field, therefore one should use
 * these wrappers rather than directly call raw_spin_{lock,unlock}* on ->lock.
 */
#define raw_spin_lock_rcu_node(p)					\
do {									\
	raw_spin_lock(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_rcu_node(p)					\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock(&ACCESS_PRIVATE(p, lock));			\
} while (0)

#define raw_spin_lock_irq_rcu_node(p)					\
do {									\
	raw_spin_lock_irq(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irq_rcu_node(p)					\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock_irq(&ACCESS_PRIVATE(p, lock));			\
} while (0)

#define raw_spin_lock_irqsave_rcu_node(p, flags)			\
do {									\
	raw_spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irqrestore_rcu_node(p, flags)			\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags);	\
} while (0)

#define raw_spin_trylock_rcu_node(p)					\
({									\
	bool ___locked = raw_spin_trylock(&ACCESS_PRIVATE(p, lock));	\
									\
	if (___locked)							\
		smp_mb__after_unlock_lock();				\
	___locked;							\
})

#define raw_lockdep_assert_held_rcu_node(p)				\
	lockdep_assert_held(&ACCESS_PRIVATE(p, lock))

#endif /* #if defined(CONFIG_SRCU) || !defined(CONFIG_TINY_RCU) */

#ifdef CONFIG_TINY_RCU
/* Tiny RCU doesn't expedite, as its purpose in life is instead to be tiny. */
static inline bool rcu_gp_is_normal(void) { return true; }
static inline bool rcu_gp_is_expedited(void) { return false; }
static inline bool rcu_async_should_hurry(void) { return false; }
static inline void rcu_expedite_gp(void) { }
static inline void rcu_unexpedite_gp(void) { }
static inline void rcu_async_hurry(void) { }
static inline void rcu_async_relax(void) { }
static inline void rcu_request_urgent_qs_task(struct task_struct *t) { }
#else /* #ifdef CONFIG_TINY_RCU */
bool rcu_gp_is_normal(void);     /* Internal RCU use. */
bool rcu_gp_is_expedited(void);  /* Internal RCU use. */
bool rcu_async_should_hurry(void);  /* Internal RCU use. */
void rcu_expedite_gp(void);
void rcu_unexpedite_gp(void);
void rcu_async_hurry(void);
void rcu_async_relax(void);
void rcupdate_announce_bootup_oddness(void);
#ifdef CONFIG_TASKS_RCU_GENERIC
void show_rcu_tasks_gp_kthreads(void);
#else /* #ifdef CONFIG_TASKS_RCU_GENERIC */
static inline void show_rcu_tasks_gp_kthreads(void) {}
#endif /* #else #ifdef CONFIG_TASKS_RCU_GENERIC */
void rcu_request_urgent_qs_task(struct task_struct *t);
#endif /* #else #ifdef CONFIG_TINY_RCU */

#define RCU_SCHEDULER_INACTIVE	0
#define RCU_SCHEDULER_INIT	1
#define RCU_SCHEDULER_RUNNING	2

enum rcutorture_type {
	RCU_FLAVOR,
	RCU_TASKS_FLAVOR,
	RCU_TASKS_RUDE_FLAVOR,
	RCU_TASKS_TRACING_FLAVOR,
	RCU_TRIVIAL_FLAVOR,
	SRCU_FLAVOR,
	INVALID_RCU_FLAVOR
};

#if defined(CONFIG_RCU_LAZY)
unsigned long rcu_lazy_get_jiffies_till_flush(void);
void rcu_lazy_set_jiffies_till_flush(unsigned long j);
#else
static inline unsigned long rcu_lazy_get_jiffies_till_flush(void) { return 0; }
static inline void rcu_lazy_set_jiffies_till_flush(unsigned long j) { }
#endif

#if defined(CONFIG_TREE_RCU)
void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gp_seq);
void do_trace_rcu_torture_read(const char *rcutorturename,
			       struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old,
			       unsigned long c);
void rcu_gp_set_torture_wait(int duration);
#else
static inline void rcutorture_get_gp_data(enum rcutorture_type test_type,
					  int *flags, unsigned long *gp_seq)
{
	*flags = 0;
	*gp_seq = 0;
}
#ifdef CONFIG_RCU_TRACE
void do_trace_rcu_torture_read(const char *rcutorturename,
			       struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old,
			       unsigned long c);
#else
#define do_trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c) \
	do { } while (0)
#endif
static inline void rcu_gp_set_torture_wait(int duration) { }
#endif

#if IS_ENABLED(CONFIG_RCU_TORTURE_TEST) || IS_MODULE(CONFIG_RCU_TORTURE_TEST)
long rcutorture_sched_setaffinity(pid_t pid, const struct cpumask *in_mask);
#endif

#ifdef CONFIG_TINY_SRCU

static inline void srcutorture_get_gp_data(enum rcutorture_type test_type,
					   struct srcu_struct *sp, int *flags,
					   unsigned long *gp_seq)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*gp_seq = sp->srcu_idx;
}

#elif defined(CONFIG_TREE_SRCU)

void srcutorture_get_gp_data(enum rcutorture_type test_type,
			     struct srcu_struct *sp, int *flags,
			     unsigned long *gp_seq);

#endif

#ifdef CONFIG_TINY_RCU
static inline bool rcu_dynticks_zero_in_eqs(int cpu, int *vp) { return false; }
static inline unsigned long rcu_get_gp_seq(void) { return 0; }
static inline unsigned long rcu_exp_batches_completed(void) { return 0; }
static inline unsigned long
srcu_batches_completed(struct srcu_struct *sp) { return 0; }
static inline void rcu_force_quiescent_state(void) { }
static inline bool rcu_check_boost_fail(unsigned long gp_state, int *cpup) { return true; }
static inline void show_rcu_gp_kthreads(void) { }
static inline int rcu_get_gp_kthreads_prio(void) { return 0; }
static inline void rcu_fwd_progress_check(unsigned long j) { }
static inline void rcu_gp_slow_register(atomic_t *rgssp) { }
static inline void rcu_gp_slow_unregister(atomic_t *rgssp) { }
#else /* #ifdef CONFIG_TINY_RCU */
bool rcu_dynticks_zero_in_eqs(int cpu, int *vp);
unsigned long rcu_get_gp_seq(void);
unsigned long rcu_exp_batches_completed(void);
unsigned long srcu_batches_completed(struct srcu_struct *sp);
bool rcu_check_boost_fail(unsigned long gp_state, int *cpup);
void show_rcu_gp_kthreads(void);
int rcu_get_gp_kthreads_prio(void);
void rcu_fwd_progress_check(unsigned long j);
void rcu_force_quiescent_state(void);
extern struct workqueue_struct *rcu_gp_wq;
#ifdef CONFIG_RCU_EXP_KTHREAD
extern struct kthread_worker *rcu_exp_gp_kworker;
extern struct kthread_worker *rcu_exp_par_gp_kworker;
#else /* !CONFIG_RCU_EXP_KTHREAD */
extern struct workqueue_struct *rcu_par_gp_wq;
#endif /* CONFIG_RCU_EXP_KTHREAD */
void rcu_gp_slow_register(atomic_t *rgssp);
void rcu_gp_slow_unregister(atomic_t *rgssp);
#endif /* #else #ifdef CONFIG_TINY_RCU */

#ifdef CONFIG_RCU_NOCB_CPU
void rcu_bind_current_to_nocb(void);
#else
static inline void rcu_bind_current_to_nocb(void) { }
#endif

#if !defined(CONFIG_TINY_RCU) && defined(CONFIG_TASKS_RCU)
void show_rcu_tasks_classic_gp_kthread(void);
#else
static inline void show_rcu_tasks_classic_gp_kthread(void) {}
#endif
#if !defined(CONFIG_TINY_RCU) && defined(CONFIG_TASKS_RUDE_RCU)
void show_rcu_tasks_rude_gp_kthread(void);
#else
static inline void show_rcu_tasks_rude_gp_kthread(void) {}
#endif
#if !defined(CONFIG_TINY_RCU) && defined(CONFIG_TASKS_TRACE_RCU)
void show_rcu_tasks_trace_gp_kthread(void);
#else
static inline void show_rcu_tasks_trace_gp_kthread(void) {}
#endif

#ifdef CONFIG_TINY_RCU
static inline bool rcu_cpu_beenfullyonline(int cpu) { return true; }
#else
bool rcu_cpu_beenfullyonline(int cpu);
#endif

#endif /* __LINUX_RCU_H */

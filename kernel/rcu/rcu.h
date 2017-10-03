/*
 * Read-Copy Update definitions shared among RCU implementations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2011
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

#include <trace/events/rcu.h>
#ifdef CONFIG_RCU_TRACE
#define RCU_TRACE(stmt) stmt
#else /* #ifdef CONFIG_RCU_TRACE */
#define RCU_TRACE(stmt)
#endif /* #else #ifdef CONFIG_RCU_TRACE */

/* Offset to allow for unmatched rcu_irq_{enter,exit}(). */
#define DYNTICK_IRQ_NONIDLE	((INT_MAX / 2) + 1)


/*
 * Grace-period counter management.
 */

#define RCU_SEQ_CTR_SHIFT	2
#define RCU_SEQ_STATE_MASK	((1 << RCU_SEQ_CTR_SHIFT) - 1)

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

/* Adjust sequence number for end of update-side operation. */
static inline void rcu_seq_end(unsigned long *sp)
{
	smp_mb(); /* Ensure update-side operation before counter increment. */
	WARN_ON_ONCE(!rcu_seq_state(*sp));
	WRITE_ONCE(*sp, (*sp | RCU_SEQ_STATE_MASK) + 1);
}

/* Take a snapshot of the update side's sequence number. */
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
 * Given a snapshot from rcu_seq_snap(), determine whether or not a
 * full update-side operation has occurred.
 */
static inline bool rcu_seq_done(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_GE(READ_ONCE(*sp), s);
}

/*
 * debug_rcu_head_queue()/debug_rcu_head_unqueue() are used internally
 * by call_rcu() and rcu callback execution, and are therefore not part of the
 * RCU API. Leaving in rcupdate.h because they are used by all RCU flavors.
 */

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
# define STATE_RCU_HEAD_READY	0
# define STATE_RCU_HEAD_QUEUED	1

extern struct debug_obj_descr rcuhead_debug_descr;

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

void kfree(const void *);

/*
 * Reclaim the specified callback, either by invoking it (non-lazy case)
 * or freeing it directly (lazy case).  Return true if lazy, false otherwise.
 */
static inline bool __rcu_reclaim(const char *rn, struct rcu_head *head)
{
	unsigned long offset = (unsigned long)head->func;

	rcu_lock_acquire(&rcu_callback_map);
	if (__is_kfree_rcu_offset(offset)) {
		RCU_TRACE(trace_rcu_invoke_kfree_callback(rn, head, offset);)
		kfree((void *)head - offset);
		rcu_lock_release(&rcu_callback_map);
		return true;
	} else {
		RCU_TRACE(trace_rcu_invoke_callback(rn, head);)
		head->func(head);
		rcu_lock_release(&rcu_callback_map);
		return false;
	}
}

#ifdef CONFIG_RCU_STALL_COMMON

extern int rcu_cpu_stall_suppress;
int rcu_jiffies_till_stall_check(void);

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

#if defined(SRCU) || !defined(TINY_RCU)

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

/*
 * Do a full breadth-first scan of the rcu_node structures for the
 * specified rcu_state structure.
 */
#define rcu_for_each_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/*
 * Do a breadth-first scan of the non-leaf rcu_node structures for the
 * specified rcu_state structure.  Note that if there is a singleton
 * rcu_node tree with but one rcu_node structure, this loop is a no-op.
 */
#define rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < (rsp)->level[rcu_num_lvls - 1]; (rnp)++)

/*
 * Scan the leaves of the rcu_node hierarchy for the specified rcu_state
 * structure.  Note that if there is a singleton rcu_node tree with but
 * one rcu_node structure, this loop -will- visit the rcu_node structure.
 * It is still a leaf node, even if it is also the root node.
 */
#define rcu_for_each_leaf_node(rsp, rnp) \
	for ((rnp) = (rsp)->level[rcu_num_lvls - 1]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/*
 * Iterate over all possible CPUs in a leaf RCU node.
 */
#define for_each_leaf_node_possible_cpu(rnp, cpu) \
	for ((cpu) = cpumask_next(rnp->grplo - 1, cpu_possible_mask); \
	     cpu <= rnp->grphi; \
	     cpu = cpumask_next((cpu), cpu_possible_mask))

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

#define raw_spin_unlock_rcu_node(p) raw_spin_unlock(&ACCESS_PRIVATE(p, lock))

#define raw_spin_lock_irq_rcu_node(p)					\
do {									\
	raw_spin_lock_irq(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irq_rcu_node(p)					\
	raw_spin_unlock_irq(&ACCESS_PRIVATE(p, lock))

#define raw_spin_lock_irqsave_rcu_node(p, flags)			\
do {									\
	raw_spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irqrestore_rcu_node(p, flags)			\
	raw_spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags)	\

#define raw_spin_trylock_rcu_node(p)					\
({									\
	bool ___locked = raw_spin_trylock(&ACCESS_PRIVATE(p, lock));	\
									\
	if (___locked)							\
		smp_mb__after_unlock_lock();				\
	___locked;							\
})

#endif /* #if defined(SRCU) || !defined(TINY_RCU) */

#ifdef CONFIG_TINY_RCU
/* Tiny RCU doesn't expedite, as its purpose in life is instead to be tiny. */
static inline bool rcu_gp_is_normal(void) { return true; }
static inline bool rcu_gp_is_expedited(void) { return false; }
static inline void rcu_expedite_gp(void) { }
static inline void rcu_unexpedite_gp(void) { }
#else /* #ifdef CONFIG_TINY_RCU */
bool rcu_gp_is_normal(void);     /* Internal RCU use. */
bool rcu_gp_is_expedited(void);  /* Internal RCU use. */
void rcu_expedite_gp(void);
void rcu_unexpedite_gp(void);
void rcupdate_announce_bootup_oddness(void);
#endif /* #else #ifdef CONFIG_TINY_RCU */

#define RCU_SCHEDULER_INACTIVE	0
#define RCU_SCHEDULER_INIT	1
#define RCU_SCHEDULER_RUNNING	2

#ifdef CONFIG_TINY_RCU
static inline void rcu_request_urgent_qs_task(struct task_struct *t) { }
#else /* #ifdef CONFIG_TINY_RCU */
void rcu_request_urgent_qs_task(struct task_struct *t);
#endif /* #else #ifdef CONFIG_TINY_RCU */

enum rcutorture_type {
	RCU_FLAVOR,
	RCU_BH_FLAVOR,
	RCU_SCHED_FLAVOR,
	RCU_TASKS_FLAVOR,
	SRCU_FLAVOR,
	INVALID_RCU_FLAVOR
};

#if defined(CONFIG_TREE_RCU) || defined(CONFIG_PREEMPT_RCU)
void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gpnum, unsigned long *completed);
void rcutorture_record_test_transition(void);
void rcutorture_record_progress(unsigned long vernum);
void do_trace_rcu_torture_read(const char *rcutorturename,
			       struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old,
			       unsigned long c);
#else
static inline void rcutorture_get_gp_data(enum rcutorture_type test_type,
					  int *flags,
					  unsigned long *gpnum,
					  unsigned long *completed)
{
	*flags = 0;
	*gpnum = 0;
	*completed = 0;
}
static inline void rcutorture_record_test_transition(void) { }
static inline void rcutorture_record_progress(unsigned long vernum) { }
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
#endif

#ifdef CONFIG_TINY_SRCU

static inline void srcutorture_get_gp_data(enum rcutorture_type test_type,
					   struct srcu_struct *sp, int *flags,
					   unsigned long *gpnum,
					   unsigned long *completed)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*completed = sp->srcu_idx;
	*gpnum = *completed;
}

#elif defined(CONFIG_TREE_SRCU)

void srcutorture_get_gp_data(enum rcutorture_type test_type,
			     struct srcu_struct *sp, int *flags,
			     unsigned long *gpnum, unsigned long *completed);

#endif

#ifdef CONFIG_TINY_RCU
static inline unsigned long rcu_batches_started(void) { return 0; }
static inline unsigned long rcu_batches_started_bh(void) { return 0; }
static inline unsigned long rcu_batches_started_sched(void) { return 0; }
static inline unsigned long rcu_batches_completed(void) { return 0; }
static inline unsigned long rcu_batches_completed_bh(void) { return 0; }
static inline unsigned long rcu_batches_completed_sched(void) { return 0; }
static inline unsigned long rcu_exp_batches_completed(void) { return 0; }
static inline unsigned long rcu_exp_batches_completed_sched(void) { return 0; }
static inline unsigned long
srcu_batches_completed(struct srcu_struct *sp) { return 0; }
static inline void rcu_force_quiescent_state(void) { }
static inline void rcu_bh_force_quiescent_state(void) { }
static inline void rcu_sched_force_quiescent_state(void) { }
static inline void show_rcu_gp_kthreads(void) { }
#else /* #ifdef CONFIG_TINY_RCU */
extern unsigned long rcutorture_testseq;
extern unsigned long rcutorture_vernum;
unsigned long rcu_batches_started(void);
unsigned long rcu_batches_started_bh(void);
unsigned long rcu_batches_started_sched(void);
unsigned long rcu_batches_completed(void);
unsigned long rcu_batches_completed_bh(void);
unsigned long rcu_batches_completed_sched(void);
unsigned long rcu_exp_batches_completed(void);
unsigned long rcu_exp_batches_completed_sched(void);
unsigned long srcu_batches_completed(struct srcu_struct *sp);
void show_rcu_gp_kthreads(void);
void rcu_force_quiescent_state(void);
void rcu_bh_force_quiescent_state(void);
void rcu_sched_force_quiescent_state(void);
#endif /* #else #ifdef CONFIG_TINY_RCU */

#ifdef CONFIG_RCU_NOCB_CPU
bool rcu_is_nocb_cpu(int cpu);
#else
static inline bool rcu_is_nocb_cpu(int cpu) { return false; }
#endif

#endif /* __LINUX_RCU_H */

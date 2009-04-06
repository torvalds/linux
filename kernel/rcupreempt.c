/*
 * Read-Copy Update mechanism for mutual exclusion, realtime implementation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2006
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
 *		With thanks to Esben Nielsen, Bill Huey, and Ingo Molnar
 *		for pushing me away from locks and towards counters, and
 *		to Suparna Bhattacharya for pushing me completely away
 *		from atomic instructions on the read side.
 *
 *  - Added handling of Dynamic Ticks
 *      Copyright 2007 - Paul E. Mckenney <paulmck@us.ibm.com>
 *                     - Steven Rostedt <srostedt@redhat.com>
 *
 * Papers:  http://www.rdrop.com/users/paulmck/RCU
 *
 * Design Document: http://lwn.net/Articles/253651/
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <linux/rcupreempt_trace.h>
#include <asm/byteorder.h>

/*
 * PREEMPT_RCU data structures.
 */

/*
 * GP_STAGES specifies the number of times the state machine has
 * to go through the all the rcu_try_flip_states (see below)
 * in a single Grace Period.
 *
 * GP in GP_STAGES stands for Grace Period ;)
 */
#define GP_STAGES    2
struct rcu_data {
	spinlock_t	lock;		/* Protect rcu_data fields. */
	long		completed;	/* Number of last completed batch. */
	int		waitlistcount;
	struct rcu_head *nextlist;
	struct rcu_head **nexttail;
	struct rcu_head *waitlist[GP_STAGES];
	struct rcu_head **waittail[GP_STAGES];
	struct rcu_head *donelist;	/* from waitlist & waitschedlist */
	struct rcu_head **donetail;
	long rcu_flipctr[2];
	struct rcu_head *nextschedlist;
	struct rcu_head **nextschedtail;
	struct rcu_head *waitschedlist;
	struct rcu_head **waitschedtail;
	int rcu_sched_sleeping;
#ifdef CONFIG_RCU_TRACE
	struct rcupreempt_trace trace;
#endif /* #ifdef CONFIG_RCU_TRACE */
};

/*
 * States for rcu_try_flip() and friends.
 */

enum rcu_try_flip_states {

	/*
	 * Stay here if nothing is happening. Flip the counter if somthing
	 * starts happening. Denoted by "I"
	 */
	rcu_try_flip_idle_state,

	/*
	 * Wait here for all CPUs to notice that the counter has flipped. This
	 * prevents the old set of counters from ever being incremented once
	 * we leave this state, which in turn is necessary because we cannot
	 * test any individual counter for zero -- we can only check the sum.
	 * Denoted by "A".
	 */
	rcu_try_flip_waitack_state,

	/*
	 * Wait here for the sum of the old per-CPU counters to reach zero.
	 * Denoted by "Z".
	 */
	rcu_try_flip_waitzero_state,

	/*
	 * Wait here for each of the other CPUs to execute a memory barrier.
	 * This is necessary to ensure that these other CPUs really have
	 * completed executing their RCU read-side critical sections, despite
	 * their CPUs wildly reordering memory. Denoted by "M".
	 */
	rcu_try_flip_waitmb_state,
};

/*
 * States for rcu_ctrlblk.rcu_sched_sleep.
 */

enum rcu_sched_sleep_states {
	rcu_sched_not_sleeping,	/* Not sleeping, callbacks need GP.  */
	rcu_sched_sleep_prep,	/* Thinking of sleeping, rechecking. */
	rcu_sched_sleeping,	/* Sleeping, awaken if GP needed. */
};

struct rcu_ctrlblk {
	spinlock_t	fliplock;	/* Protect state-machine transitions. */
	long		completed;	/* Number of last completed batch. */
	enum rcu_try_flip_states rcu_try_flip_state; /* The current state of
							the rcu state machine */
	spinlock_t	schedlock;	/* Protect rcu_sched sleep state. */
	enum rcu_sched_sleep_states sched_sleep; /* rcu_sched state. */
	wait_queue_head_t sched_wq;	/* Place for rcu_sched to sleep. */
};

struct rcu_dyntick_sched {
	int dynticks;
	int dynticks_snap;
	int sched_qs;
	int sched_qs_snap;
	int sched_dynticks_snap;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct rcu_dyntick_sched, rcu_dyntick_sched) = {
	.dynticks = 1,
};

void rcu_qsctr_inc(int cpu)
{
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	rdssp->sched_qs++;
}

#ifdef CONFIG_NO_HZ

void rcu_enter_nohz(void)
{
	static DEFINE_RATELIMIT_STATE(rs, 10 * HZ, 1);

	smp_mb(); /* CPUs seeing ++ must see prior RCU read-side crit sects */
	__get_cpu_var(rcu_dyntick_sched).dynticks++;
	WARN_ON_RATELIMIT(__get_cpu_var(rcu_dyntick_sched).dynticks & 0x1, &rs);
}

void rcu_exit_nohz(void)
{
	static DEFINE_RATELIMIT_STATE(rs, 10 * HZ, 1);

	__get_cpu_var(rcu_dyntick_sched).dynticks++;
	smp_mb(); /* CPUs seeing ++ must see later RCU read-side crit sects */
	WARN_ON_RATELIMIT(!(__get_cpu_var(rcu_dyntick_sched).dynticks & 0x1),
				&rs);
}

#endif /* CONFIG_NO_HZ */


static DEFINE_PER_CPU(struct rcu_data, rcu_data);

static struct rcu_ctrlblk rcu_ctrlblk = {
	.fliplock = __SPIN_LOCK_UNLOCKED(rcu_ctrlblk.fliplock),
	.completed = 0,
	.rcu_try_flip_state = rcu_try_flip_idle_state,
	.schedlock = __SPIN_LOCK_UNLOCKED(rcu_ctrlblk.schedlock),
	.sched_sleep = rcu_sched_not_sleeping,
	.sched_wq = __WAIT_QUEUE_HEAD_INITIALIZER(rcu_ctrlblk.sched_wq),
};

static struct task_struct *rcu_sched_grace_period_task;

#ifdef CONFIG_RCU_TRACE
static char *rcu_try_flip_state_names[] =
	{ "idle", "waitack", "waitzero", "waitmb" };
#endif /* #ifdef CONFIG_RCU_TRACE */

static DECLARE_BITMAP(rcu_cpu_online_map, NR_CPUS) __read_mostly
	= CPU_BITS_NONE;

/*
 * Enum and per-CPU flag to determine when each CPU has seen
 * the most recent counter flip.
 */

enum rcu_flip_flag_values {
	rcu_flip_seen,		/* Steady/initial state, last flip seen. */
				/* Only GP detector can update. */
	rcu_flipped		/* Flip just completed, need confirmation. */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU_SHARED_ALIGNED(enum rcu_flip_flag_values, rcu_flip_flag)
								= rcu_flip_seen;

/*
 * Enum and per-CPU flag to determine when each CPU has executed the
 * needed memory barrier to fence in memory references from its last RCU
 * read-side critical section in the just-completed grace period.
 */

enum rcu_mb_flag_values {
	rcu_mb_done,		/* Steady/initial state, no mb()s required. */
				/* Only GP detector can update. */
	rcu_mb_needed		/* Flip just completed, need an mb(). */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU_SHARED_ALIGNED(enum rcu_mb_flag_values, rcu_mb_flag)
								= rcu_mb_done;

/*
 * RCU_DATA_ME: find the current CPU's rcu_data structure.
 * RCU_DATA_CPU: find the specified CPU's rcu_data structure.
 */
#define RCU_DATA_ME()		(&__get_cpu_var(rcu_data))
#define RCU_DATA_CPU(cpu)	(&per_cpu(rcu_data, cpu))

/*
 * Helper macro for tracing when the appropriate rcu_data is not
 * cached in a local variable, but where the CPU number is so cached.
 */
#define RCU_TRACE_CPU(f, cpu) RCU_TRACE(f, &(RCU_DATA_CPU(cpu)->trace));

/*
 * Helper macro for tracing when the appropriate rcu_data is not
 * cached in a local variable.
 */
#define RCU_TRACE_ME(f) RCU_TRACE(f, &(RCU_DATA_ME()->trace));

/*
 * Helper macro for tracing when the appropriate rcu_data is pointed
 * to by a local variable.
 */
#define RCU_TRACE_RDP(f, rdp) RCU_TRACE(f, &((rdp)->trace));

#define RCU_SCHED_BATCH_TIME (HZ / 50)

/*
 * Return the number of RCU batches processed thus far.  Useful
 * for debug and statistics.
 */
long rcu_batches_completed(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

void __rcu_read_lock(void)
{
	int idx;
	struct task_struct *t = current;
	int nesting;

	nesting = ACCESS_ONCE(t->rcu_read_lock_nesting);
	if (nesting != 0) {

		/* An earlier rcu_read_lock() covers us, just count it. */

		t->rcu_read_lock_nesting = nesting + 1;

	} else {
		unsigned long flags;

		/*
		 * We disable interrupts for the following reasons:
		 * - If we get scheduling clock interrupt here, and we
		 *   end up acking the counter flip, it's like a promise
		 *   that we will never increment the old counter again.
		 *   Thus we will break that promise if that
		 *   scheduling clock interrupt happens between the time
		 *   we pick the .completed field and the time that we
		 *   increment our counter.
		 *
		 * - We don't want to be preempted out here.
		 *
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock().
		 */

		local_irq_save(flags);

		/*
		 * Outermost nesting of rcu_read_lock(), so increment
		 * the current counter for the current CPU.  Use volatile
		 * casts to prevent the compiler from reordering.
		 */

		idx = ACCESS_ONCE(rcu_ctrlblk.completed) & 0x1;
		ACCESS_ONCE(RCU_DATA_ME()->rcu_flipctr[idx])++;

		/*
		 * Now that the per-CPU counter has been incremented, we
		 * are protected from races with rcu_read_lock() invoked
		 * from NMI handlers on this CPU.  We can therefore safely
		 * increment the nesting counter, relieving further NMIs
		 * of the need to increment the per-CPU counter.
		 */

		ACCESS_ONCE(t->rcu_read_lock_nesting) = nesting + 1;

		/*
		 * Now that we have preventing any NMIs from storing
		 * to the ->rcu_flipctr_idx, we can safely use it to
		 * remember which counter to decrement in the matching
		 * rcu_read_unlock().
		 */

		ACCESS_ONCE(t->rcu_flipctr_idx) = idx;
		local_irq_restore(flags);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

void __rcu_read_unlock(void)
{
	int idx;
	struct task_struct *t = current;
	int nesting;

	nesting = ACCESS_ONCE(t->rcu_read_lock_nesting);
	if (nesting > 1) {

		/*
		 * We are still protected by the enclosing rcu_read_lock(),
		 * so simply decrement the counter.
		 */

		t->rcu_read_lock_nesting = nesting - 1;

	} else {
		unsigned long flags;

		/*
		 * Disable local interrupts to prevent the grace-period
		 * detection state machine from seeing us half-done.
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock() and rcu_read_unlock().
		 */

		local_irq_save(flags);

		/*
		 * Outermost nesting of rcu_read_unlock(), so we must
		 * decrement the current counter for the current CPU.
		 * This must be done carefully, because NMIs can
		 * occur at any point in this code, and any rcu_read_lock()
		 * and rcu_read_unlock() pairs in the NMI handlers
		 * must interact non-destructively with this code.
		 * Lots of volatile casts, and -very- careful ordering.
		 *
		 * Changes to this code, including this one, must be
		 * inspected, validated, and tested extremely carefully!!!
		 */

		/*
		 * First, pick up the index.
		 */

		idx = ACCESS_ONCE(t->rcu_flipctr_idx);

		/*
		 * Now that we have fetched the counter index, it is
		 * safe to decrement the per-task RCU nesting counter.
		 * After this, any interrupts or NMIs will increment and
		 * decrement the per-CPU counters.
		 */
		ACCESS_ONCE(t->rcu_read_lock_nesting) = nesting - 1;

		/*
		 * It is now safe to decrement this task's nesting count.
		 * NMIs that occur after this statement will route their
		 * rcu_read_lock() calls through this "else" clause, and
		 * will thus start incrementing the per-CPU counter on
		 * their own.  They will also clobber ->rcu_flipctr_idx,
		 * but that is OK, since we have already fetched it.
		 */

		ACCESS_ONCE(RCU_DATA_ME()->rcu_flipctr[idx])--;
		local_irq_restore(flags);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

/*
 * If a global counter flip has occurred since the last time that we
 * advanced callbacks, advance them.  Hardware interrupts must be
 * disabled when calling this function.
 */
static void __rcu_advance_callbacks(struct rcu_data *rdp)
{
	int cpu;
	int i;
	int wlc = 0;

	if (rdp->completed != rcu_ctrlblk.completed) {
		if (rdp->waitlist[GP_STAGES - 1] != NULL) {
			*rdp->donetail = rdp->waitlist[GP_STAGES - 1];
			rdp->donetail = rdp->waittail[GP_STAGES - 1];
			RCU_TRACE_RDP(rcupreempt_trace_move2done, rdp);
		}
		for (i = GP_STAGES - 2; i >= 0; i--) {
			if (rdp->waitlist[i] != NULL) {
				rdp->waitlist[i + 1] = rdp->waitlist[i];
				rdp->waittail[i + 1] = rdp->waittail[i];
				wlc++;
			} else {
				rdp->waitlist[i + 1] = NULL;
				rdp->waittail[i + 1] =
					&rdp->waitlist[i + 1];
			}
		}
		if (rdp->nextlist != NULL) {
			rdp->waitlist[0] = rdp->nextlist;
			rdp->waittail[0] = rdp->nexttail;
			wlc++;
			rdp->nextlist = NULL;
			rdp->nexttail = &rdp->nextlist;
			RCU_TRACE_RDP(rcupreempt_trace_move2wait, rdp);
		} else {
			rdp->waitlist[0] = NULL;
			rdp->waittail[0] = &rdp->waitlist[0];
		}
		rdp->waitlistcount = wlc;
		rdp->completed = rcu_ctrlblk.completed;
	}

	/*
	 * Check to see if this CPU needs to report that it has seen
	 * the most recent counter flip, thereby declaring that all
	 * subsequent rcu_read_lock() invocations will respect this flip.
	 */

	cpu = raw_smp_processor_id();
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}
}

#ifdef CONFIG_NO_HZ
static DEFINE_PER_CPU(int, rcu_update_flag);

/**
 * rcu_irq_enter - Called from Hard irq handlers and NMI/SMI.
 *
 * If the CPU was idle with dynamic ticks active, this updates the
 * rcu_dyntick_sched.dynticks to let the RCU handling know that the
 * CPU is active.
 */
void rcu_irq_enter(void)
{
	int cpu = smp_processor_id();
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	if (per_cpu(rcu_update_flag, cpu))
		per_cpu(rcu_update_flag, cpu)++;

	/*
	 * Only update if we are coming from a stopped ticks mode
	 * (rcu_dyntick_sched.dynticks is even).
	 */
	if (!in_interrupt() &&
	    (rdssp->dynticks & 0x1) == 0) {
		/*
		 * The following might seem like we could have a race
		 * with NMI/SMIs. But this really isn't a problem.
		 * Here we do a read/modify/write, and the race happens
		 * when an NMI/SMI comes in after the read and before
		 * the write. But NMI/SMIs will increment this counter
		 * twice before returning, so the zero bit will not
		 * be corrupted by the NMI/SMI which is the most important
		 * part.
		 *
		 * The only thing is that we would bring back the counter
		 * to a postion that it was in during the NMI/SMI.
		 * But the zero bit would be set, so the rest of the
		 * counter would again be ignored.
		 *
		 * On return from the IRQ, the counter may have the zero
		 * bit be 0 and the counter the same as the return from
		 * the NMI/SMI. If the state machine was so unlucky to
		 * see that, it still doesn't matter, since all
		 * RCU read-side critical sections on this CPU would
		 * have already completed.
		 */
		rdssp->dynticks++;
		/*
		 * The following memory barrier ensures that any
		 * rcu_read_lock() primitives in the irq handler
		 * are seen by other CPUs to follow the above
		 * increment to rcu_dyntick_sched.dynticks. This is
		 * required in order for other CPUs to correctly
		 * determine when it is safe to advance the RCU
		 * grace-period state machine.
		 */
		smp_mb(); /* see above block comment. */
		/*
		 * Since we can't determine the dynamic tick mode from
		 * the rcu_dyntick_sched.dynticks after this routine,
		 * we use a second flag to acknowledge that we came
		 * from an idle state with ticks stopped.
		 */
		per_cpu(rcu_update_flag, cpu)++;
		/*
		 * If we take an NMI/SMI now, they will also increment
		 * the rcu_update_flag, and will not update the
		 * rcu_dyntick_sched.dynticks on exit. That is for
		 * this IRQ to do.
		 */
	}
}

/**
 * rcu_irq_exit - Called from exiting Hard irq context.
 *
 * If the CPU was idle with dynamic ticks active, update the
 * rcu_dyntick_sched.dynticks to put let the RCU handling be
 * aware that the CPU is going back to idle with no ticks.
 */
void rcu_irq_exit(void)
{
	int cpu = smp_processor_id();
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	/*
	 * rcu_update_flag is set if we interrupted the CPU
	 * when it was idle with ticks stopped.
	 * Once this occurs, we keep track of interrupt nesting
	 * because a NMI/SMI could also come in, and we still
	 * only want the IRQ that started the increment of the
	 * rcu_dyntick_sched.dynticks to be the one that modifies
	 * it on exit.
	 */
	if (per_cpu(rcu_update_flag, cpu)) {
		if (--per_cpu(rcu_update_flag, cpu))
			return;

		/* This must match the interrupt nesting */
		WARN_ON(in_interrupt());

		/*
		 * If an NMI/SMI happens now we are still
		 * protected by the rcu_dyntick_sched.dynticks being odd.
		 */

		/*
		 * The following memory barrier ensures that any
		 * rcu_read_unlock() primitives in the irq handler
		 * are seen by other CPUs to preceed the following
		 * increment to rcu_dyntick_sched.dynticks. This
		 * is required in order for other CPUs to determine
		 * when it is safe to advance the RCU grace-period
		 * state machine.
		 */
		smp_mb(); /* see above block comment. */
		rdssp->dynticks++;
		WARN_ON(rdssp->dynticks & 0x1);
	}
}

void rcu_nmi_enter(void)
{
	rcu_irq_enter();
}

void rcu_nmi_exit(void)
{
	rcu_irq_exit();
}

static void dyntick_save_progress_counter(int cpu)
{
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	rdssp->dynticks_snap = rdssp->dynticks;
}

static inline int
rcu_try_flip_waitack_needed(int cpu)
{
	long curr;
	long snap;
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	curr = rdssp->dynticks;
	snap = rdssp->dynticks_snap;
	smp_mb(); /* force ordering with cpu entering/leaving dynticks. */

	/*
	 * If the CPU remained in dynticks mode for the entire time
	 * and didn't take any interrupts, NMIs, SMIs, or whatever,
	 * then it cannot be in the middle of an rcu_read_lock(), so
	 * the next rcu_read_lock() it executes must use the new value
	 * of the counter.  So we can safely pretend that this CPU
	 * already acknowledged the counter.
	 */

	if ((curr == snap) && ((curr & 0x1) == 0))
		return 0;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq handlers, then, as above, we can safely pretend
	 * that this CPU already acknowledged the counter.
	 */

	if ((curr - snap) > 2 || (curr & 0x1) == 0)
		return 0;

	/* We need this CPU to explicitly acknowledge the counter flip. */

	return 1;
}

static inline int
rcu_try_flip_waitmb_needed(int cpu)
{
	long curr;
	long snap;
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	curr = rdssp->dynticks;
	snap = rdssp->dynticks_snap;
	smp_mb(); /* force ordering with cpu entering/leaving dynticks. */

	/*
	 * If the CPU remained in dynticks mode for the entire time
	 * and didn't take any interrupts, NMIs, SMIs, or whatever,
	 * then it cannot have executed an RCU read-side critical section
	 * during that time, so there is no need for it to execute a
	 * memory barrier.
	 */

	if ((curr == snap) && ((curr & 0x1) == 0))
		return 0;

	/*
	 * If the CPU either entered or exited an outermost interrupt,
	 * SMI, NMI, or whatever handler, then we know that it executed
	 * a memory barrier when doing so.  So we don't need another one.
	 */
	if (curr != snap)
		return 0;

	/* We need the CPU to execute a memory barrier. */

	return 1;
}

static void dyntick_save_progress_counter_sched(int cpu)
{
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	rdssp->sched_dynticks_snap = rdssp->dynticks;
}

static int rcu_qsctr_inc_needed_dyntick(int cpu)
{
	long curr;
	long snap;
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	curr = rdssp->dynticks;
	snap = rdssp->sched_dynticks_snap;
	smp_mb(); /* force ordering with cpu entering/leaving dynticks. */

	/*
	 * If the CPU remained in dynticks mode for the entire time
	 * and didn't take any interrupts, NMIs, SMIs, or whatever,
	 * then it cannot be in the middle of an rcu_read_lock(), so
	 * the next rcu_read_lock() it executes must use the new value
	 * of the counter.  Therefore, this CPU has been in a quiescent
	 * state the entire time, and we don't need to wait for it.
	 */

	if ((curr == snap) && ((curr & 0x1) == 0))
		return 0;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq handlers, then, as above, this CPU has already
	 * passed through a quiescent state.
	 */

	if ((curr - snap) > 2 || (snap & 0x1) == 0)
		return 0;

	/* We need this CPU to go through a quiescent state. */

	return 1;
}

#else /* !CONFIG_NO_HZ */

# define dyntick_save_progress_counter(cpu)		do { } while (0)
# define rcu_try_flip_waitack_needed(cpu)		(1)
# define rcu_try_flip_waitmb_needed(cpu)		(1)

# define dyntick_save_progress_counter_sched(cpu)	do { } while (0)
# define rcu_qsctr_inc_needed_dyntick(cpu)		(1)

#endif /* CONFIG_NO_HZ */

static void save_qsctr_sched(int cpu)
{
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	rdssp->sched_qs_snap = rdssp->sched_qs;
}

static inline int rcu_qsctr_inc_needed(int cpu)
{
	struct rcu_dyntick_sched *rdssp = &per_cpu(rcu_dyntick_sched, cpu);

	/*
	 * If there has been a quiescent state, no more need to wait
	 * on this CPU.
	 */

	if (rdssp->sched_qs != rdssp->sched_qs_snap) {
		smp_mb(); /* force ordering with cpu entering schedule(). */
		return 0;
	}

	/* We need this CPU to go through a quiescent state. */

	return 1;
}

/*
 * Get here when RCU is idle.  Decide whether we need to
 * move out of idle state, and return non-zero if so.
 * "Straightforward" approach for the moment, might later
 * use callback-list lengths, grace-period duration, or
 * some such to determine when to exit idle state.
 * Might also need a pre-idle test that does not acquire
 * the lock, but let's get the simple case working first...
 */

static int
rcu_try_flip_idle(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_i1);
	if (!rcu_pending(smp_processor_id())) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_ie1);
		return 0;
	}

	/*
	 * Do the flip.
	 */

	RCU_TRACE_ME(rcupreempt_trace_try_flip_g1);
	rcu_ctrlblk.completed++;  /* stands in for rcu_try_flip_g2 */

	/*
	 * Need a memory barrier so that other CPUs see the new
	 * counter value before they see the subsequent change of all
	 * the rcu_flip_flag instances to rcu_flipped.
	 */

	smp_mb();	/* see above block comment. */

	/* Now ask each CPU for acknowledgement of the flip. */

	for_each_cpu(cpu, to_cpumask(rcu_cpu_online_map)) {
		per_cpu(rcu_flip_flag, cpu) = rcu_flipped;
		dyntick_save_progress_counter(cpu);
	}

	return 1;
}

/*
 * Wait for CPUs to acknowledge the flip.
 */

static int
rcu_try_flip_waitack(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_a1);
	for_each_cpu(cpu, to_cpumask(rcu_cpu_online_map))
		if (rcu_try_flip_waitack_needed(cpu) &&
		    per_cpu(rcu_flip_flag, cpu) != rcu_flip_seen) {
			RCU_TRACE_ME(rcupreempt_trace_try_flip_ae1);
			return 0;
		}

	/*
	 * Make sure our checks above don't bleed into subsequent
	 * waiting for the sum of the counters to reach zero.
	 */

	smp_mb();	/* see above block comment. */
	RCU_TRACE_ME(rcupreempt_trace_try_flip_a2);
	return 1;
}

/*
 * Wait for collective ``last'' counter to reach zero,
 * then tell all CPUs to do an end-of-grace-period memory barrier.
 */

static int
rcu_try_flip_waitzero(void)
{
	int cpu;
	int lastidx = !(rcu_ctrlblk.completed & 0x1);
	int sum = 0;

	/* Check to see if the sum of the "last" counters is zero. */

	RCU_TRACE_ME(rcupreempt_trace_try_flip_z1);
	for_each_cpu(cpu, to_cpumask(rcu_cpu_online_map))
		sum += RCU_DATA_CPU(cpu)->rcu_flipctr[lastidx];
	if (sum != 0) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_ze1);
		return 0;
	}

	/*
	 * This ensures that the other CPUs see the call for
	 * memory barriers -after- the sum to zero has been
	 * detected here
	 */
	smp_mb();  /*  ^^^^^^^^^^^^ */

	/* Call for a memory barrier from each CPU. */
	for_each_cpu(cpu, to_cpumask(rcu_cpu_online_map)) {
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_needed;
		dyntick_save_progress_counter(cpu);
	}

	RCU_TRACE_ME(rcupreempt_trace_try_flip_z2);
	return 1;
}

/*
 * Wait for all CPUs to do their end-of-grace-period memory barrier.
 * Return 0 once all CPUs have done so.
 */

static int
rcu_try_flip_waitmb(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_m1);
	for_each_cpu(cpu, to_cpumask(rcu_cpu_online_map))
		if (rcu_try_flip_waitmb_needed(cpu) &&
		    per_cpu(rcu_mb_flag, cpu) != rcu_mb_done) {
			RCU_TRACE_ME(rcupreempt_trace_try_flip_me1);
			return 0;
		}

	smp_mb(); /* Ensure that the above checks precede any following flip. */
	RCU_TRACE_ME(rcupreempt_trace_try_flip_m2);
	return 1;
}

/*
 * Attempt a single flip of the counters.  Remember, a single flip does
 * -not- constitute a grace period.  Instead, the interval between
 * at least GP_STAGES consecutive flips is a grace period.
 *
 * If anyone is nuts enough to run this CONFIG_PREEMPT_RCU implementation
 * on a large SMP, they might want to use a hierarchical organization of
 * the per-CPU-counter pairs.
 */
static void rcu_try_flip(void)
{
	unsigned long flags;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_1);
	if (unlikely(!spin_trylock_irqsave(&rcu_ctrlblk.fliplock, flags))) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_e1);
		return;
	}

	/*
	 * Take the next transition(s) through the RCU grace-period
	 * flip-counter state machine.
	 */

	switch (rcu_ctrlblk.rcu_try_flip_state) {
	case rcu_try_flip_idle_state:
		if (rcu_try_flip_idle())
			rcu_ctrlblk.rcu_try_flip_state =
				rcu_try_flip_waitack_state;
		break;
	case rcu_try_flip_waitack_state:
		if (rcu_try_flip_waitack())
			rcu_ctrlblk.rcu_try_flip_state =
				rcu_try_flip_waitzero_state;
		break;
	case rcu_try_flip_waitzero_state:
		if (rcu_try_flip_waitzero())
			rcu_ctrlblk.rcu_try_flip_state =
				rcu_try_flip_waitmb_state;
		break;
	case rcu_try_flip_waitmb_state:
		if (rcu_try_flip_waitmb())
			rcu_ctrlblk.rcu_try_flip_state =
				rcu_try_flip_idle_state;
	}
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, flags);
}

/*
 * Check to see if this CPU needs to do a memory barrier in order to
 * ensure that any prior RCU read-side critical sections have committed
 * their counter manipulations and critical-section memory references
 * before declaring the grace period to be completed.
 */
static void rcu_check_mb(int cpu)
{
	if (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed) {
		smp_mb();  /* Ensure RCU read-side accesses are visible. */
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_done;
	}
}

void rcu_check_callbacks(int cpu, int user)
{
	unsigned long flags;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	/*
	 * If this CPU took its interrupt from user mode or from the
	 * idle loop, and this is not a nested interrupt, then
	 * this CPU has to have exited all prior preept-disable
	 * sections of code.  So increment the counter to note this.
	 *
	 * The memory barrier is needed to handle the case where
	 * writes from a preempt-disable section of code get reordered
	 * into schedule() by this CPU's write buffer.  So the memory
	 * barrier makes sure that the rcu_qsctr_inc() is seen by other
	 * CPUs to happen after any such write.
	 */

	if (user ||
	    (idle_cpu(cpu) && !in_softirq() &&
	     hardirq_count() <= (1 << HARDIRQ_SHIFT))) {
		smp_mb();	/* Guard against aggressive schedule(). */
	     	rcu_qsctr_inc(cpu);
	}

	rcu_check_mb(cpu);
	if (rcu_ctrlblk.completed == rdp->completed)
		rcu_try_flip();
	spin_lock_irqsave(&rdp->lock, flags);
	RCU_TRACE_RDP(rcupreempt_trace_check_callbacks, rdp);
	__rcu_advance_callbacks(rdp);
	if (rdp->donelist == NULL) {
		spin_unlock_irqrestore(&rdp->lock, flags);
	} else {
		spin_unlock_irqrestore(&rdp->lock, flags);
		raise_softirq(RCU_SOFTIRQ);
	}
}

/*
 * Needed by dynticks, to make sure all RCU processing has finished
 * when we go idle:
 */
void rcu_advance_callbacks(int cpu, int user)
{
	unsigned long flags;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	if (rcu_ctrlblk.completed == rdp->completed) {
		rcu_try_flip();
		if (rcu_ctrlblk.completed == rdp->completed)
			return;
	}
	spin_lock_irqsave(&rdp->lock, flags);
	RCU_TRACE_RDP(rcupreempt_trace_check_callbacks, rdp);
	__rcu_advance_callbacks(rdp);
	spin_unlock_irqrestore(&rdp->lock, flags);
}

#ifdef CONFIG_HOTPLUG_CPU
#define rcu_offline_cpu_enqueue(srclist, srctail, dstlist, dsttail) do { \
		*dsttail = srclist; \
		if (srclist != NULL) { \
			dsttail = srctail; \
			srclist = NULL; \
			srctail = &srclist;\
		} \
	} while (0)

void rcu_offline_cpu(int cpu)
{
	int i;
	struct rcu_head *list = NULL;
	unsigned long flags;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);
	struct rcu_head *schedlist = NULL;
	struct rcu_head **schedtail = &schedlist;
	struct rcu_head **tail = &list;

	/*
	 * Remove all callbacks from the newly dead CPU, retaining order.
	 * Otherwise rcu_barrier() will fail
	 */

	spin_lock_irqsave(&rdp->lock, flags);
	rcu_offline_cpu_enqueue(rdp->donelist, rdp->donetail, list, tail);
	for (i = GP_STAGES - 1; i >= 0; i--)
		rcu_offline_cpu_enqueue(rdp->waitlist[i], rdp->waittail[i],
						list, tail);
	rcu_offline_cpu_enqueue(rdp->nextlist, rdp->nexttail, list, tail);
	rcu_offline_cpu_enqueue(rdp->waitschedlist, rdp->waitschedtail,
				schedlist, schedtail);
	rcu_offline_cpu_enqueue(rdp->nextschedlist, rdp->nextschedtail,
				schedlist, schedtail);
	rdp->rcu_sched_sleeping = 0;
	spin_unlock_irqrestore(&rdp->lock, flags);
	rdp->waitlistcount = 0;

	/* Disengage the newly dead CPU from the grace-period computation. */

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, flags);
	rcu_check_mb(cpu);
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}

	RCU_DATA_ME()->rcu_flipctr[0] += RCU_DATA_CPU(cpu)->rcu_flipctr[0];
	RCU_DATA_ME()->rcu_flipctr[1] += RCU_DATA_CPU(cpu)->rcu_flipctr[1];

	RCU_DATA_CPU(cpu)->rcu_flipctr[0] = 0;
	RCU_DATA_CPU(cpu)->rcu_flipctr[1] = 0;

	cpumask_clear_cpu(cpu, to_cpumask(rcu_cpu_online_map));

	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, flags);

	/*
	 * Place the removed callbacks on the current CPU's queue.
	 * Make them all start a new grace period: simple approach,
	 * in theory could starve a given set of callbacks, but
	 * you would need to be doing some serious CPU hotplugging
	 * to make this happen.  If this becomes a problem, adding
	 * a synchronize_rcu() to the hotplug path would be a simple
	 * fix.
	 */

	local_irq_save(flags);  /* disable preempt till we know what lock. */
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	*rdp->nexttail = list;
	if (list)
		rdp->nexttail = tail;
	*rdp->nextschedtail = schedlist;
	if (schedlist)
		rdp->nextschedtail = schedtail;
	spin_unlock_irqrestore(&rdp->lock, flags);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

void rcu_offline_cpu(int cpu)
{
}

#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */

void __cpuinit rcu_online_cpu(int cpu)
{
	unsigned long flags;
	struct rcu_data *rdp;

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, flags);
	cpumask_set_cpu(cpu, to_cpumask(rcu_cpu_online_map));
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, flags);

	/*
	 * The rcu_sched grace-period processing might have bypassed
	 * this CPU, given that it was not in the rcu_cpu_online_map
	 * when the grace-period scan started.  This means that the
	 * grace-period task might sleep.  So make sure that if this
	 * should happen, the first callback posted to this CPU will
	 * wake up the grace-period task if need be.
	 */

	rdp = RCU_DATA_CPU(cpu);
	spin_lock_irqsave(&rdp->lock, flags);
	rdp->rcu_sched_sleeping = 1;
	spin_unlock_irqrestore(&rdp->lock, flags);
}

static void rcu_process_callbacks(struct softirq_action *unused)
{
	unsigned long flags;
	struct rcu_head *next, *list;
	struct rcu_data *rdp;

	local_irq_save(flags);
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	list = rdp->donelist;
	if (list == NULL) {
		spin_unlock_irqrestore(&rdp->lock, flags);
		return;
	}
	rdp->donelist = NULL;
	rdp->donetail = &rdp->donelist;
	RCU_TRACE_RDP(rcupreempt_trace_done_remove, rdp);
	spin_unlock_irqrestore(&rdp->lock, flags);
	while (list) {
		next = list->next;
		list->func(list);
		list = next;
		RCU_TRACE_ME(rcupreempt_trace_invoke);
	}
}

void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	unsigned long flags;
	struct rcu_data *rdp;

	head->func = func;
	head->next = NULL;
	local_irq_save(flags);
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	__rcu_advance_callbacks(rdp);
	*rdp->nexttail = head;
	rdp->nexttail = &head->next;
	RCU_TRACE_RDP(rcupreempt_trace_next_add, rdp);
	spin_unlock_irqrestore(&rdp->lock, flags);
}
EXPORT_SYMBOL_GPL(call_rcu);

void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	unsigned long flags;
	struct rcu_data *rdp;
	int wake_gp = 0;

	head->func = func;
	head->next = NULL;
	local_irq_save(flags);
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	*rdp->nextschedtail = head;
	rdp->nextschedtail = &head->next;
	if (rdp->rcu_sched_sleeping) {

		/* Grace-period processing might be sleeping... */

		rdp->rcu_sched_sleeping = 0;
		wake_gp = 1;
	}
	spin_unlock_irqrestore(&rdp->lock, flags);
	if (wake_gp) {

		/* Wake up grace-period processing, unless someone beat us. */

		spin_lock_irqsave(&rcu_ctrlblk.schedlock, flags);
		if (rcu_ctrlblk.sched_sleep != rcu_sched_sleeping)
			wake_gp = 0;
		rcu_ctrlblk.sched_sleep = rcu_sched_not_sleeping;
		spin_unlock_irqrestore(&rcu_ctrlblk.schedlock, flags);
		if (wake_gp)
			wake_up_interruptible(&rcu_ctrlblk.sched_wq);
	}
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/*
 * Wait until all currently running preempt_disable() code segments
 * (including hardware-irq-disable segments) complete.  Note that
 * in -rt this does -not- necessarily result in all currently executing
 * interrupt -handlers- having completed.
 */
void __synchronize_sched(void)
{
	struct rcu_synchronize rcu;

	if (num_online_cpus() == 1)
		return;  /* blocking is gp if only one CPU! */

	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	call_rcu_sched(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
}
EXPORT_SYMBOL_GPL(__synchronize_sched);

/*
 * kthread function that manages call_rcu_sched grace periods.
 */
static int rcu_sched_grace_period(void *arg)
{
	int couldsleep;		/* might sleep after current pass. */
	int couldsleepnext = 0; /* might sleep after next pass. */
	int cpu;
	unsigned long flags;
	struct rcu_data *rdp;
	int ret;

	/*
	 * Each pass through the following loop handles one
	 * rcu_sched grace period cycle.
	 */
	do {
		/* Save each CPU's current state. */

		for_each_online_cpu(cpu) {
			dyntick_save_progress_counter_sched(cpu);
			save_qsctr_sched(cpu);
		}

		/*
		 * Sleep for about an RCU grace-period's worth to
		 * allow better batching and to consume less CPU.
		 */
		schedule_timeout_interruptible(RCU_SCHED_BATCH_TIME);

		/*
		 * If there was nothing to do last time, prepare to
		 * sleep at the end of the current grace period cycle.
		 */
		couldsleep = couldsleepnext;
		couldsleepnext = 1;
		if (couldsleep) {
			spin_lock_irqsave(&rcu_ctrlblk.schedlock, flags);
			rcu_ctrlblk.sched_sleep = rcu_sched_sleep_prep;
			spin_unlock_irqrestore(&rcu_ctrlblk.schedlock, flags);
		}

		/*
		 * Wait on each CPU in turn to have either visited
		 * a quiescent state or been in dynticks-idle mode.
		 */
		for_each_online_cpu(cpu) {
			while (rcu_qsctr_inc_needed(cpu) &&
			       rcu_qsctr_inc_needed_dyntick(cpu)) {
				/* resched_cpu(cpu); @@@ */
				schedule_timeout_interruptible(1);
			}
		}

		/* Advance callbacks for each CPU.  */

		for_each_online_cpu(cpu) {

			rdp = RCU_DATA_CPU(cpu);
			spin_lock_irqsave(&rdp->lock, flags);

			/*
			 * We are running on this CPU irq-disabled, so no
			 * CPU can go offline until we re-enable irqs.
			 * The current CPU might have already gone
			 * offline (between the for_each_offline_cpu and
			 * the spin_lock_irqsave), but in that case all its
			 * callback lists will be empty, so no harm done.
			 *
			 * Advance the callbacks!  We share normal RCU's
			 * donelist, since callbacks are invoked the
			 * same way in either case.
			 */
			if (rdp->waitschedlist != NULL) {
				*rdp->donetail = rdp->waitschedlist;
				rdp->donetail = rdp->waitschedtail;

				/*
				 * Next rcu_check_callbacks() will
				 * do the required raise_softirq().
				 */
			}
			if (rdp->nextschedlist != NULL) {
				rdp->waitschedlist = rdp->nextschedlist;
				rdp->waitschedtail = rdp->nextschedtail;
				couldsleep = 0;
				couldsleepnext = 0;
			} else {
				rdp->waitschedlist = NULL;
				rdp->waitschedtail = &rdp->waitschedlist;
			}
			rdp->nextschedlist = NULL;
			rdp->nextschedtail = &rdp->nextschedlist;

			/* Mark sleep intention. */

			rdp->rcu_sched_sleeping = couldsleep;

			spin_unlock_irqrestore(&rdp->lock, flags);
		}

		/* If we saw callbacks on the last scan, go deal with them. */

		if (!couldsleep)
			continue;

		/* Attempt to block... */

		spin_lock_irqsave(&rcu_ctrlblk.schedlock, flags);
		if (rcu_ctrlblk.sched_sleep != rcu_sched_sleep_prep) {

			/*
			 * Someone posted a callback after we scanned.
			 * Go take care of it.
			 */
			spin_unlock_irqrestore(&rcu_ctrlblk.schedlock, flags);
			couldsleepnext = 0;
			continue;
		}

		/* Block until the next person posts a callback. */

		rcu_ctrlblk.sched_sleep = rcu_sched_sleeping;
		spin_unlock_irqrestore(&rcu_ctrlblk.schedlock, flags);
		ret = 0;
		__wait_event_interruptible(rcu_ctrlblk.sched_wq,
			rcu_ctrlblk.sched_sleep != rcu_sched_sleeping,
			ret);

		/*
		 * Signals would prevent us from sleeping, and we cannot
		 * do much with them in any case.  So flush them.
		 */
		if (ret)
			flush_signals(current);
		couldsleepnext = 0;

	} while (!kthread_should_stop());

	return (0);
}

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  Assumes that notifiers would take care of handling any
 * outstanding requests from the RCU core.
 *
 * This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 */
int rcu_needs_cpu(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	return (rdp->donelist != NULL ||
		!!rdp->waitlistcount ||
		rdp->nextlist != NULL ||
		rdp->nextschedlist != NULL ||
		rdp->waitschedlist != NULL);
}

int rcu_pending(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	/* The CPU has at least one callback queued somewhere. */

	if (rdp->donelist != NULL ||
	    !!rdp->waitlistcount ||
	    rdp->nextlist != NULL ||
	    rdp->nextschedlist != NULL ||
	    rdp->waitschedlist != NULL)
		return 1;

	/* The RCU core needs an acknowledgement from this CPU. */

	if ((per_cpu(rcu_flip_flag, cpu) == rcu_flipped) ||
	    (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed))
		return 1;

	/* This CPU has fallen behind the global grace-period number. */

	if (rdp->completed != rcu_ctrlblk.completed)
		return 1;

	/* Nothing needed from this CPU. */

	return 0;
}

static int __cpuinit rcu_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		rcu_online_cpu(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		rcu_offline_cpu(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata rcu_nb = {
	.notifier_call = rcu_cpu_notify,
};

void __init __rcu_init(void)
{
	int cpu;
	int i;
	struct rcu_data *rdp;

	printk(KERN_NOTICE "Preemptible RCU implementation.\n");
	for_each_possible_cpu(cpu) {
		rdp = RCU_DATA_CPU(cpu);
		spin_lock_init(&rdp->lock);
		rdp->completed = 0;
		rdp->waitlistcount = 0;
		rdp->nextlist = NULL;
		rdp->nexttail = &rdp->nextlist;
		for (i = 0; i < GP_STAGES; i++) {
			rdp->waitlist[i] = NULL;
			rdp->waittail[i] = &rdp->waitlist[i];
		}
		rdp->donelist = NULL;
		rdp->donetail = &rdp->donelist;
		rdp->rcu_flipctr[0] = 0;
		rdp->rcu_flipctr[1] = 0;
		rdp->nextschedlist = NULL;
		rdp->nextschedtail = &rdp->nextschedlist;
		rdp->waitschedlist = NULL;
		rdp->waitschedtail = &rdp->waitschedlist;
		rdp->rcu_sched_sleeping = 0;
	}
	register_cpu_notifier(&rcu_nb);

	/*
	 * We don't need protection against CPU-Hotplug here
	 * since
	 * a) If a CPU comes online while we are iterating over the
	 *    cpu_online_mask below, we would only end up making a
	 *    duplicate call to rcu_online_cpu() which sets the corresponding
	 *    CPU's mask in the rcu_cpu_online_map.
	 *
	 * b) A CPU cannot go offline at this point in time since the user
	 *    does not have access to the sysfs interface, nor do we
	 *    suspend the system.
	 */
	for_each_online_cpu(cpu)
		rcu_cpu_notify(&rcu_nb, CPU_UP_PREPARE,	(void *)(long) cpu);

	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}

/*
 * Late-boot-time RCU initialization that must wait until after scheduler
 * has been initialized.
 */
void __init rcu_init_sched(void)
{
	rcu_sched_grace_period_task = kthread_run(rcu_sched_grace_period,
						  NULL,
						  "rcu_sched_grace_period");
	WARN_ON(IS_ERR(rcu_sched_grace_period_task));
}

#ifdef CONFIG_RCU_TRACE
long *rcupreempt_flipctr(int cpu)
{
	return &RCU_DATA_CPU(cpu)->rcu_flipctr[0];
}
EXPORT_SYMBOL_GPL(rcupreempt_flipctr);

int rcupreempt_flip_flag(int cpu)
{
	return per_cpu(rcu_flip_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_flip_flag);

int rcupreempt_mb_flag(int cpu)
{
	return per_cpu(rcu_mb_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_mb_flag);

char *rcupreempt_try_flip_state_name(void)
{
	return rcu_try_flip_state_names[rcu_ctrlblk.rcu_try_flip_state];
}
EXPORT_SYMBOL_GPL(rcupreempt_try_flip_state_name);

struct rcupreempt_trace *rcupreempt_trace_cpu(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	return &rdp->trace;
}
EXPORT_SYMBOL_GPL(rcupreempt_trace_cpu);

#endif /* #ifdef RCU_TRACE */

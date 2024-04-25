/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 * Internal non-public definitions that provide either classic
 * or preemptible semantics.
 *
 * Copyright Red Hat, 2009
 * Copyright IBM Corporation, 2009
 * Copyright SUSE, 2021
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.ibm.com>
 *	   Frederic Weisbecker <frederic@kernel.org>
 */

#ifdef CONFIG_RCU_NOCB_CPU
static cpumask_var_t rcu_nocb_mask; /* CPUs to have callbacks offloaded. */
static bool __read_mostly rcu_nocb_poll;    /* Offload kthread are to poll. */
static inline int rcu_lockdep_is_held_nocb(struct rcu_data *rdp)
{
	return lockdep_is_held(&rdp->nocb_lock);
}

static inline bool rcu_current_is_nocb_kthread(struct rcu_data *rdp)
{
	/* Race on early boot between thread creation and assignment */
	if (!rdp->nocb_cb_kthread || !rdp->nocb_gp_kthread)
		return true;

	if (current == rdp->nocb_cb_kthread || current == rdp->nocb_gp_kthread)
		if (in_task())
			return true;
	return false;
}

/*
 * Offload callback processing from the boot-time-specified set of CPUs
 * specified by rcu_nocb_mask.  For the CPUs in the set, there are kthreads
 * created that pull the callbacks from the corresponding CPU, wait for
 * a grace period to elapse, and invoke the callbacks.  These kthreads
 * are organized into GP kthreads, which manage incoming callbacks, wait for
 * grace periods, and awaken CB kthreads, and the CB kthreads, which only
 * invoke callbacks.  Each GP kthread invokes its own CBs.  The no-CBs CPUs
 * do a wake_up() on their GP kthread when they insert a callback into any
 * empty list, unless the rcu_nocb_poll boot parameter has been specified,
 * in which case each kthread actively polls its CPU.  (Which isn't so great
 * for energy efficiency, but which does reduce RCU's overhead on that CPU.)
 *
 * This is intended to be used in conjunction with Frederic Weisbecker's
 * adaptive-idle work, which would seriously reduce OS jitter on CPUs
 * running CPU-bound user-mode computations.
 *
 * Offloading of callbacks can also be used as an energy-efficiency
 * measure because CPUs with no RCU callbacks queued are more aggressive
 * about entering dyntick-idle mode.
 */


/*
 * Parse the boot-time rcu_nocb_mask CPU list from the kernel parameters.
 * If the list is invalid, a warning is emitted and all CPUs are offloaded.
 */
static int __init rcu_nocb_setup(char *str)
{
	alloc_bootmem_cpumask_var(&rcu_nocb_mask);
	if (*str == '=') {
		if (cpulist_parse(++str, rcu_nocb_mask)) {
			pr_warn("rcu_nocbs= bad CPU range, all CPUs set\n");
			cpumask_setall(rcu_nocb_mask);
		}
	}
	rcu_state.nocb_is_setup = true;
	return 1;
}
__setup("rcu_nocbs", rcu_nocb_setup);

static int __init parse_rcu_nocb_poll(char *arg)
{
	rcu_nocb_poll = true;
	return 1;
}
__setup("rcu_nocb_poll", parse_rcu_nocb_poll);

/*
 * Don't bother bypassing ->cblist if the call_rcu() rate is low.
 * After all, the main point of bypassing is to avoid lock contention
 * on ->nocb_lock, which only can happen at high call_rcu() rates.
 */
static int nocb_nobypass_lim_per_jiffy = 16 * 1000 / HZ;
module_param(nocb_nobypass_lim_per_jiffy, int, 0);

/*
 * Acquire the specified rcu_data structure's ->nocb_bypass_lock.  If the
 * lock isn't immediately available, perform minimal sanity check.
 */
static void rcu_nocb_bypass_lock(struct rcu_data *rdp)
	__acquires(&rdp->nocb_bypass_lock)
{
	lockdep_assert_irqs_disabled();
	if (raw_spin_trylock(&rdp->nocb_bypass_lock))
		return;
	/*
	 * Contention expected only when local enqueue collide with
	 * remote flush from kthreads.
	 */
	WARN_ON_ONCE(smp_processor_id() != rdp->cpu);
	raw_spin_lock(&rdp->nocb_bypass_lock);
}

/*
 * Conditionally acquire the specified rcu_data structure's
 * ->nocb_bypass_lock.
 */
static bool rcu_nocb_bypass_trylock(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	return raw_spin_trylock(&rdp->nocb_bypass_lock);
}

/*
 * Release the specified rcu_data structure's ->nocb_bypass_lock.
 */
static void rcu_nocb_bypass_unlock(struct rcu_data *rdp)
	__releases(&rdp->nocb_bypass_lock)
{
	lockdep_assert_irqs_disabled();
	raw_spin_unlock(&rdp->nocb_bypass_lock);
}

/*
 * Acquire the specified rcu_data structure's ->nocb_lock, but only
 * if it corresponds to a no-CBs CPU.
 */
static void rcu_nocb_lock(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	if (!rcu_rdp_is_offloaded(rdp))
		return;
	raw_spin_lock(&rdp->nocb_lock);
}

/*
 * Release the specified rcu_data structure's ->nocb_lock, but only
 * if it corresponds to a no-CBs CPU.
 */
static void rcu_nocb_unlock(struct rcu_data *rdp)
{
	if (rcu_rdp_is_offloaded(rdp)) {
		lockdep_assert_irqs_disabled();
		raw_spin_unlock(&rdp->nocb_lock);
	}
}

/*
 * Release the specified rcu_data structure's ->nocb_lock and restore
 * interrupts, but only if it corresponds to a no-CBs CPU.
 */
static void rcu_nocb_unlock_irqrestore(struct rcu_data *rdp,
				       unsigned long flags)
{
	if (rcu_rdp_is_offloaded(rdp)) {
		lockdep_assert_irqs_disabled();
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
	} else {
		local_irq_restore(flags);
	}
}

/* Lockdep check that ->cblist may be safely accessed. */
static void rcu_lockdep_assert_cblist_protected(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	if (rcu_rdp_is_offloaded(rdp))
		lockdep_assert_held(&rdp->nocb_lock);
}

/*
 * Wake up any no-CBs CPUs' kthreads that were waiting on the just-ended
 * grace period.
 */
static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq)
{
	swake_up_all(sq);
}

static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp)
{
	return &rnp->nocb_gp_wq[rcu_seq_ctr(rnp->gp_seq) & 0x1];
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
	init_swait_queue_head(&rnp->nocb_gp_wq[0]);
	init_swait_queue_head(&rnp->nocb_gp_wq[1]);
}

static bool __wake_nocb_gp(struct rcu_data *rdp_gp,
			   struct rcu_data *rdp,
			   bool force, unsigned long flags)
	__releases(rdp_gp->nocb_gp_lock)
{
	bool needwake = false;

	if (!READ_ONCE(rdp_gp->nocb_gp_kthread)) {
		raw_spin_unlock_irqrestore(&rdp_gp->nocb_gp_lock, flags);
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
				    TPS("AlreadyAwake"));
		return false;
	}

	if (rdp_gp->nocb_defer_wakeup > RCU_NOCB_WAKE_NOT) {
		WRITE_ONCE(rdp_gp->nocb_defer_wakeup, RCU_NOCB_WAKE_NOT);
		del_timer(&rdp_gp->nocb_timer);
	}

	if (force || READ_ONCE(rdp_gp->nocb_gp_sleep)) {
		WRITE_ONCE(rdp_gp->nocb_gp_sleep, false);
		needwake = true;
	}
	raw_spin_unlock_irqrestore(&rdp_gp->nocb_gp_lock, flags);
	if (needwake) {
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("DoWake"));
		wake_up_process(rdp_gp->nocb_gp_kthread);
	}

	return needwake;
}

/*
 * Kick the GP kthread for this NOCB group.
 */
static bool wake_nocb_gp(struct rcu_data *rdp, bool force)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	raw_spin_lock_irqsave(&rdp_gp->nocb_gp_lock, flags);
	return __wake_nocb_gp(rdp_gp, rdp, force, flags);
}

#ifdef CONFIG_RCU_LAZY
/*
 * LAZY_FLUSH_JIFFIES decides the maximum amount of time that
 * can elapse before lazy callbacks are flushed. Lazy callbacks
 * could be flushed much earlier for a number of other reasons
 * however, LAZY_FLUSH_JIFFIES will ensure no lazy callbacks are
 * left unsubmitted to RCU after those many jiffies.
 */
#define LAZY_FLUSH_JIFFIES (10 * HZ)
static unsigned long jiffies_lazy_flush = LAZY_FLUSH_JIFFIES;

// To be called only from test code.
void rcu_set_jiffies_lazy_flush(unsigned long jif)
{
	jiffies_lazy_flush = jif;
}
EXPORT_SYMBOL(rcu_set_jiffies_lazy_flush);

unsigned long rcu_get_jiffies_lazy_flush(void)
{
	return jiffies_lazy_flush;
}
EXPORT_SYMBOL(rcu_get_jiffies_lazy_flush);
#endif

/*
 * Arrange to wake the GP kthread for this NOCB group at some future
 * time when it is safe to do so.
 */
static void wake_nocb_gp_defer(struct rcu_data *rdp, int waketype,
			       const char *reason)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	raw_spin_lock_irqsave(&rdp_gp->nocb_gp_lock, flags);

	/*
	 * Bypass wakeup overrides previous deferments. In case of
	 * callback storms, no need to wake up too early.
	 */
	if (waketype == RCU_NOCB_WAKE_LAZY &&
	    rdp->nocb_defer_wakeup == RCU_NOCB_WAKE_NOT) {
		mod_timer(&rdp_gp->nocb_timer, jiffies + rcu_get_jiffies_lazy_flush());
		WRITE_ONCE(rdp_gp->nocb_defer_wakeup, waketype);
	} else if (waketype == RCU_NOCB_WAKE_BYPASS) {
		mod_timer(&rdp_gp->nocb_timer, jiffies + 2);
		WRITE_ONCE(rdp_gp->nocb_defer_wakeup, waketype);
	} else {
		if (rdp_gp->nocb_defer_wakeup < RCU_NOCB_WAKE)
			mod_timer(&rdp_gp->nocb_timer, jiffies + 1);
		if (rdp_gp->nocb_defer_wakeup < waketype)
			WRITE_ONCE(rdp_gp->nocb_defer_wakeup, waketype);
	}

	raw_spin_unlock_irqrestore(&rdp_gp->nocb_gp_lock, flags);

	trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, reason);
}

/*
 * Flush the ->nocb_bypass queue into ->cblist, enqueuing rhp if non-NULL.
 * However, if there is a callback to be enqueued and if ->nocb_bypass
 * proves to be initially empty, just return false because the no-CB GP
 * kthread may need to be awakened in this case.
 *
 * Return true if there was something to be flushed and it succeeded, otherwise
 * false.
 *
 * Note that this function always returns true if rhp is NULL.
 */
static bool rcu_nocb_do_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp_in,
				     unsigned long j, bool lazy)
{
	struct rcu_cblist rcl;
	struct rcu_head *rhp = rhp_in;

	WARN_ON_ONCE(!rcu_rdp_is_offloaded(rdp));
	rcu_lockdep_assert_cblist_protected(rdp);
	lockdep_assert_held(&rdp->nocb_bypass_lock);
	if (rhp && !rcu_cblist_n_cbs(&rdp->nocb_bypass)) {
		raw_spin_unlock(&rdp->nocb_bypass_lock);
		return false;
	}
	/* Note: ->cblist.len already accounts for ->nocb_bypass contents. */
	if (rhp)
		rcu_segcblist_inc_len(&rdp->cblist); /* Must precede enqueue. */

	/*
	 * If the new CB requested was a lazy one, queue it onto the main
	 * ->cblist so that we can take advantage of the grace-period that will
	 * happen regardless. But queue it onto the bypass list first so that
	 * the lazy CB is ordered with the existing CBs in the bypass list.
	 */
	if (lazy && rhp) {
		rcu_cblist_enqueue(&rdp->nocb_bypass, rhp);
		rhp = NULL;
	}
	rcu_cblist_flush_enqueue(&rcl, &rdp->nocb_bypass, rhp);
	WRITE_ONCE(rdp->lazy_len, 0);

	rcu_segcblist_insert_pend_cbs(&rdp->cblist, &rcl);
	WRITE_ONCE(rdp->nocb_bypass_first, j);
	rcu_nocb_bypass_unlock(rdp);
	return true;
}

/*
 * Flush the ->nocb_bypass queue into ->cblist, enqueuing rhp if non-NULL.
 * However, if there is a callback to be enqueued and if ->nocb_bypass
 * proves to be initially empty, just return false because the no-CB GP
 * kthread may need to be awakened in this case.
 *
 * Note that this function always returns true if rhp is NULL.
 */
static bool rcu_nocb_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				  unsigned long j, bool lazy)
{
	if (!rcu_rdp_is_offloaded(rdp))
		return true;
	rcu_lockdep_assert_cblist_protected(rdp);
	rcu_nocb_bypass_lock(rdp);
	return rcu_nocb_do_flush_bypass(rdp, rhp, j, lazy);
}

/*
 * If the ->nocb_bypass_lock is immediately available, flush the
 * ->nocb_bypass queue into ->cblist.
 */
static void rcu_nocb_try_flush_bypass(struct rcu_data *rdp, unsigned long j)
{
	rcu_lockdep_assert_cblist_protected(rdp);
	if (!rcu_rdp_is_offloaded(rdp) ||
	    !rcu_nocb_bypass_trylock(rdp))
		return;
	WARN_ON_ONCE(!rcu_nocb_do_flush_bypass(rdp, NULL, j, false));
}

/*
 * See whether it is appropriate to use the ->nocb_bypass list in order
 * to control contention on ->nocb_lock.  A limited number of direct
 * enqueues are permitted into ->cblist per jiffy.  If ->nocb_bypass
 * is non-empty, further callbacks must be placed into ->nocb_bypass,
 * otherwise rcu_barrier() breaks.  Use rcu_nocb_flush_bypass() to switch
 * back to direct use of ->cblist.  However, ->nocb_bypass should not be
 * used if ->cblist is empty, because otherwise callbacks can be stranded
 * on ->nocb_bypass because we cannot count on the current CPU ever again
 * invoking call_rcu().  The general rule is that if ->nocb_bypass is
 * non-empty, the corresponding no-CBs grace-period kthread must not be
 * in an indefinite sleep state.
 *
 * Finally, it is not permitted to use the bypass during early boot,
 * as doing so would confuse the auto-initialization code.  Besides
 * which, there is no point in worrying about lock contention while
 * there is only one CPU in operation.
 */
static bool rcu_nocb_try_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				bool *was_alldone, unsigned long flags,
				bool lazy)
{
	unsigned long c;
	unsigned long cur_gp_seq;
	unsigned long j = jiffies;
	long ncbs = rcu_cblist_n_cbs(&rdp->nocb_bypass);
	bool bypass_is_lazy = (ncbs == READ_ONCE(rdp->lazy_len));

	lockdep_assert_irqs_disabled();

	// Pure softirq/rcuc based processing: no bypassing, no
	// locking.
	if (!rcu_rdp_is_offloaded(rdp)) {
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false;
	}

	// In the process of (de-)offloading: no bypassing, but
	// locking.
	if (!rcu_segcblist_completely_offloaded(&rdp->cblist)) {
		rcu_nocb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false; /* Not offloaded, no bypassing. */
	}

	// Don't use ->nocb_bypass during early boot.
	if (rcu_scheduler_active != RCU_SCHEDULER_RUNNING) {
		rcu_nocb_lock(rdp);
		WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->nocb_bypass));
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false;
	}

	// If we have advanced to a new jiffy, reset counts to allow
	// moving back from ->nocb_bypass to ->cblist.
	if (j == rdp->nocb_nobypass_last) {
		c = rdp->nocb_nobypass_count + 1;
	} else {
		WRITE_ONCE(rdp->nocb_nobypass_last, j);
		c = rdp->nocb_nobypass_count - nocb_nobypass_lim_per_jiffy;
		if (ULONG_CMP_LT(rdp->nocb_nobypass_count,
				 nocb_nobypass_lim_per_jiffy))
			c = 0;
		else if (c > nocb_nobypass_lim_per_jiffy)
			c = nocb_nobypass_lim_per_jiffy;
	}
	WRITE_ONCE(rdp->nocb_nobypass_count, c);

	// If there hasn't yet been all that many ->cblist enqueues
	// this jiffy, tell the caller to enqueue onto ->cblist.  But flush
	// ->nocb_bypass first.
	// Lazy CBs throttle this back and do immediate bypass queuing.
	if (rdp->nocb_nobypass_count < nocb_nobypass_lim_per_jiffy && !lazy) {
		rcu_nocb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		if (*was_alldone)
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstQ"));

		WARN_ON_ONCE(!rcu_nocb_flush_bypass(rdp, NULL, j, false));
		WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->nocb_bypass));
		return false; // Caller must enqueue the callback.
	}

	// If ->nocb_bypass has been used too long or is too full,
	// flush ->nocb_bypass to ->cblist.
	if ((ncbs && !bypass_is_lazy && j != READ_ONCE(rdp->nocb_bypass_first)) ||
	    (ncbs &&  bypass_is_lazy &&
	     (time_after(j, READ_ONCE(rdp->nocb_bypass_first) + rcu_get_jiffies_lazy_flush()))) ||
	    ncbs >= qhimark) {
		rcu_nocb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);

		if (!rcu_nocb_flush_bypass(rdp, rhp, j, lazy)) {
			if (*was_alldone)
				trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
						    TPS("FirstQ"));
			WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->nocb_bypass));
			return false; // Caller must enqueue the callback.
		}
		if (j != rdp->nocb_gp_adv_time &&
		    rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		    rcu_seq_done(&rdp->mynode->gp_seq, cur_gp_seq)) {
			rcu_advance_cbs_nowake(rdp->mynode, rdp);
			rdp->nocb_gp_adv_time = j;
		}

		// The flush succeeded and we moved CBs into the regular list.
		// Don't wait for the wake up timer as it may be too far ahead.
		// Wake up the GP thread now instead, if the cblist was empty.
		__call_rcu_nocb_wake(rdp, *was_alldone, flags);

		return true; // Callback already enqueued.
	}

	// We need to use the bypass.
	rcu_nocb_bypass_lock(rdp);
	ncbs = rcu_cblist_n_cbs(&rdp->nocb_bypass);
	rcu_segcblist_inc_len(&rdp->cblist); /* Must precede enqueue. */
	rcu_cblist_enqueue(&rdp->nocb_bypass, rhp);

	if (lazy)
		WRITE_ONCE(rdp->lazy_len, rdp->lazy_len + 1);

	if (!ncbs) {
		WRITE_ONCE(rdp->nocb_bypass_first, j);
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("FirstBQ"));
	}
	rcu_nocb_bypass_unlock(rdp);
	smp_mb(); /* Order enqueue before wake. */
	// A wake up of the grace period kthread or timer adjustment
	// needs to be done only if:
	// 1. Bypass list was fully empty before (this is the first
	//    bypass list entry), or:
	// 2. Both of these conditions are met:
	//    a. The bypass list previously had only lazy CBs, and:
	//    b. The new CB is non-lazy.
	if (!ncbs || (bypass_is_lazy && !lazy)) {
		// No-CBs GP kthread might be indefinitely asleep, if so, wake.
		rcu_nocb_lock(rdp); // Rare during call_rcu() flood.
		if (!rcu_segcblist_pend_cbs(&rdp->cblist)) {
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstBQwake"));
			__call_rcu_nocb_wake(rdp, true, flags);
		} else {
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstBQnoWake"));
			rcu_nocb_unlock(rdp);
		}
	}
	return true; // Callback already enqueued.
}

/*
 * Awaken the no-CBs grace-period kthread if needed, either due to it
 * legitimately being asleep or due to overload conditions.
 *
 * If warranted, also wake up the kthread servicing this CPUs queues.
 */
static void __call_rcu_nocb_wake(struct rcu_data *rdp, bool was_alldone,
				 unsigned long flags)
				 __releases(rdp->nocb_lock)
{
	long bypass_len;
	unsigned long cur_gp_seq;
	unsigned long j;
	long lazy_len;
	long len;
	struct task_struct *t;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	// If we are being polled or there is no kthread, just leave.
	t = READ_ONCE(rdp->nocb_gp_kthread);
	if (rcu_nocb_poll || !t) {
		rcu_nocb_unlock(rdp);
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
				    TPS("WakeNotPoll"));
		return;
	}
	// Need to actually to a wakeup.
	len = rcu_segcblist_n_cbs(&rdp->cblist);
	bypass_len = rcu_cblist_n_cbs(&rdp->nocb_bypass);
	lazy_len = READ_ONCE(rdp->lazy_len);
	if (was_alldone) {
		rdp->qlen_last_fqs_check = len;
		// Only lazy CBs in bypass list
		if (lazy_len && bypass_len == lazy_len) {
			rcu_nocb_unlock(rdp);
			wake_nocb_gp_defer(rdp, RCU_NOCB_WAKE_LAZY,
					   TPS("WakeLazy"));
		} else if (!irqs_disabled_flags(flags)) {
			/* ... if queue was empty ... */
			rcu_nocb_unlock(rdp);
			wake_nocb_gp(rdp, false);
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    TPS("WakeEmpty"));
		} else {
			rcu_nocb_unlock(rdp);
			wake_nocb_gp_defer(rdp, RCU_NOCB_WAKE,
					   TPS("WakeEmptyIsDeferred"));
		}
	} else if (len > rdp->qlen_last_fqs_check + qhimark) {
		/* ... or if many callbacks queued. */
		rdp->qlen_last_fqs_check = len;
		j = jiffies;
		if (j != rdp->nocb_gp_adv_time &&
		    rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		    rcu_seq_done(&rdp->mynode->gp_seq, cur_gp_seq)) {
			rcu_advance_cbs_nowake(rdp->mynode, rdp);
			rdp->nocb_gp_adv_time = j;
		}
		smp_mb(); /* Enqueue before timer_pending(). */
		if ((rdp->nocb_cb_sleep ||
		     !rcu_segcblist_ready_cbs(&rdp->cblist)) &&
		    !timer_pending(&rdp_gp->nocb_timer)) {
			rcu_nocb_unlock(rdp);
			wake_nocb_gp_defer(rdp, RCU_NOCB_WAKE_FORCE,
					   TPS("WakeOvfIsDeferred"));
		} else {
			rcu_nocb_unlock(rdp);
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("WakeNot"));
		}
	} else {
		rcu_nocb_unlock(rdp);
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("WakeNot"));
	}
}

static void call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *head,
			  rcu_callback_t func, unsigned long flags, bool lazy)
{
	bool was_alldone;

	if (!rcu_nocb_try_bypass(rdp, head, &was_alldone, flags, lazy)) {
		/* Not enqueued on bypass but locked, do regular enqueue */
		rcutree_enqueue(rdp, head, func);
		__call_rcu_nocb_wake(rdp, was_alldone, flags); /* unlocks */
	}
}

static int nocb_gp_toggle_rdp(struct rcu_data *rdp)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int ret;

	rcu_nocb_lock_irqsave(rdp, flags);
	if (rcu_segcblist_test_flags(cblist, SEGCBLIST_OFFLOADED) &&
	    !rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP)) {
		/*
		 * Offloading. Set our flag and notify the offload worker.
		 * We will handle this rdp until it ever gets de-offloaded.
		 */
		rcu_segcblist_set_flags(cblist, SEGCBLIST_KTHREAD_GP);
		ret = 1;
	} else if (!rcu_segcblist_test_flags(cblist, SEGCBLIST_OFFLOADED) &&
		   rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP)) {
		/*
		 * De-offloading. Clear our flag and notify the de-offload worker.
		 * We will ignore this rdp until it ever gets re-offloaded.
		 */
		rcu_segcblist_clear_flags(cblist, SEGCBLIST_KTHREAD_GP);
		ret = 0;
	} else {
		WARN_ON_ONCE(1);
		ret = -1;
	}

	rcu_nocb_unlock_irqrestore(rdp, flags);

	return ret;
}

static void nocb_gp_sleep(struct rcu_data *my_rdp, int cpu)
{
	trace_rcu_nocb_wake(rcu_state.name, cpu, TPS("Sleep"));
	swait_event_interruptible_exclusive(my_rdp->nocb_gp_wq,
					!READ_ONCE(my_rdp->nocb_gp_sleep));
	trace_rcu_nocb_wake(rcu_state.name, cpu, TPS("EndSleep"));
}

/*
 * No-CBs GP kthreads come here to wait for additional callbacks to show up
 * or for grace periods to end.
 */
static void nocb_gp_wait(struct rcu_data *my_rdp)
{
	bool bypass = false;
	int __maybe_unused cpu = my_rdp->cpu;
	unsigned long cur_gp_seq;
	unsigned long flags;
	bool gotcbs = false;
	unsigned long j = jiffies;
	bool lazy = false;
	bool needwait_gp = false; // This prevents actual uninitialized use.
	bool needwake;
	bool needwake_gp;
	struct rcu_data *rdp, *rdp_toggling = NULL;
	struct rcu_node *rnp;
	unsigned long wait_gp_seq = 0; // Suppress "use uninitialized" warning.
	bool wasempty = false;

	/*
	 * Each pass through the following loop checks for CBs and for the
	 * nearest grace period (if any) to wait for next.  The CB kthreads
	 * and the global grace-period kthread are awakened if needed.
	 */
	WARN_ON_ONCE(my_rdp->nocb_gp_rdp != my_rdp);
	/*
	 * An rcu_data structure is removed from the list after its
	 * CPU is de-offloaded and added to the list before that CPU is
	 * (re-)offloaded.  If the following loop happens to be referencing
	 * that rcu_data structure during the time that the corresponding
	 * CPU is de-offloaded and then immediately re-offloaded, this
	 * loop's rdp pointer will be carried to the end of the list by
	 * the resulting pair of list operations.  This can cause the loop
	 * to skip over some of the rcu_data structures that were supposed
	 * to have been scanned.  Fortunately a new iteration through the
	 * entire loop is forced after a given CPU's rcu_data structure
	 * is added to the list, so the skipped-over rcu_data structures
	 * won't be ignored for long.
	 */
	list_for_each_entry(rdp, &my_rdp->nocb_head_rdp, nocb_entry_rdp) {
		long bypass_ncbs;
		bool flush_bypass = false;
		long lazy_ncbs;

		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("Check"));
		rcu_nocb_lock_irqsave(rdp, flags);
		lockdep_assert_held(&rdp->nocb_lock);
		bypass_ncbs = rcu_cblist_n_cbs(&rdp->nocb_bypass);
		lazy_ncbs = READ_ONCE(rdp->lazy_len);

		if (bypass_ncbs && (lazy_ncbs == bypass_ncbs) &&
		    (time_after(j, READ_ONCE(rdp->nocb_bypass_first) + rcu_get_jiffies_lazy_flush()) ||
		     bypass_ncbs > 2 * qhimark)) {
			flush_bypass = true;
		} else if (bypass_ncbs && (lazy_ncbs != bypass_ncbs) &&
		    (time_after(j, READ_ONCE(rdp->nocb_bypass_first) + 1) ||
		     bypass_ncbs > 2 * qhimark)) {
			flush_bypass = true;
		} else if (!bypass_ncbs && rcu_segcblist_empty(&rdp->cblist)) {
			rcu_nocb_unlock_irqrestore(rdp, flags);
			continue; /* No callbacks here, try next. */
		}

		if (flush_bypass) {
			// Bypass full or old, so flush it.
			(void)rcu_nocb_try_flush_bypass(rdp, j);
			bypass_ncbs = rcu_cblist_n_cbs(&rdp->nocb_bypass);
			lazy_ncbs = READ_ONCE(rdp->lazy_len);
		}

		if (bypass_ncbs) {
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    bypass_ncbs == lazy_ncbs ? TPS("Lazy") : TPS("Bypass"));
			if (bypass_ncbs == lazy_ncbs)
				lazy = true;
			else
				bypass = true;
		}
		rnp = rdp->mynode;

		// Advance callbacks if helpful and low contention.
		needwake_gp = false;
		if (!rcu_segcblist_restempty(&rdp->cblist,
					     RCU_NEXT_READY_TAIL) ||
		    (rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		     rcu_seq_done(&rnp->gp_seq, cur_gp_seq))) {
			raw_spin_lock_rcu_node(rnp); /* irqs disabled. */
			needwake_gp = rcu_advance_cbs(rnp, rdp);
			wasempty = rcu_segcblist_restempty(&rdp->cblist,
							   RCU_NEXT_READY_TAIL);
			raw_spin_unlock_rcu_node(rnp); /* irqs disabled. */
		}
		// Need to wait on some grace period?
		WARN_ON_ONCE(wasempty &&
			     !rcu_segcblist_restempty(&rdp->cblist,
						      RCU_NEXT_READY_TAIL));
		if (rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq)) {
			if (!needwait_gp ||
			    ULONG_CMP_LT(cur_gp_seq, wait_gp_seq))
				wait_gp_seq = cur_gp_seq;
			needwait_gp = true;
			trace_rcu_nocb_wake(rcu_state.name, rdp->cpu,
					    TPS("NeedWaitGP"));
		}
		if (rcu_segcblist_ready_cbs(&rdp->cblist)) {
			needwake = rdp->nocb_cb_sleep;
			WRITE_ONCE(rdp->nocb_cb_sleep, false);
		} else {
			needwake = false;
		}
		rcu_nocb_unlock_irqrestore(rdp, flags);
		if (needwake) {
			swake_up_one(&rdp->nocb_cb_wq);
			gotcbs = true;
		}
		if (needwake_gp)
			rcu_gp_kthread_wake();
	}

	my_rdp->nocb_gp_bypass = bypass;
	my_rdp->nocb_gp_gp = needwait_gp;
	my_rdp->nocb_gp_seq = needwait_gp ? wait_gp_seq : 0;

	// At least one child with non-empty ->nocb_bypass, so set
	// timer in order to avoid stranding its callbacks.
	if (!rcu_nocb_poll) {
		// If bypass list only has lazy CBs. Add a deferred lazy wake up.
		if (lazy && !bypass) {
			wake_nocb_gp_defer(my_rdp, RCU_NOCB_WAKE_LAZY,
					TPS("WakeLazyIsDeferred"));
		// Otherwise add a deferred bypass wake up.
		} else if (bypass) {
			wake_nocb_gp_defer(my_rdp, RCU_NOCB_WAKE_BYPASS,
					TPS("WakeBypassIsDeferred"));
		}
	}

	if (rcu_nocb_poll) {
		/* Polling, so trace if first poll in the series. */
		if (gotcbs)
			trace_rcu_nocb_wake(rcu_state.name, cpu, TPS("Poll"));
		if (list_empty(&my_rdp->nocb_head_rdp)) {
			raw_spin_lock_irqsave(&my_rdp->nocb_gp_lock, flags);
			if (!my_rdp->nocb_toggling_rdp)
				WRITE_ONCE(my_rdp->nocb_gp_sleep, true);
			raw_spin_unlock_irqrestore(&my_rdp->nocb_gp_lock, flags);
			/* Wait for any offloading rdp */
			nocb_gp_sleep(my_rdp, cpu);
		} else {
			schedule_timeout_idle(1);
		}
	} else if (!needwait_gp) {
		/* Wait for callbacks to appear. */
		nocb_gp_sleep(my_rdp, cpu);
	} else {
		rnp = my_rdp->mynode;
		trace_rcu_this_gp(rnp, my_rdp, wait_gp_seq, TPS("StartWait"));
		swait_event_interruptible_exclusive(
			rnp->nocb_gp_wq[rcu_seq_ctr(wait_gp_seq) & 0x1],
			rcu_seq_done(&rnp->gp_seq, wait_gp_seq) ||
			!READ_ONCE(my_rdp->nocb_gp_sleep));
		trace_rcu_this_gp(rnp, my_rdp, wait_gp_seq, TPS("EndWait"));
	}

	if (!rcu_nocb_poll) {
		raw_spin_lock_irqsave(&my_rdp->nocb_gp_lock, flags);
		// (De-)queue an rdp to/from the group if its nocb state is changing
		rdp_toggling = my_rdp->nocb_toggling_rdp;
		if (rdp_toggling)
			my_rdp->nocb_toggling_rdp = NULL;

		if (my_rdp->nocb_defer_wakeup > RCU_NOCB_WAKE_NOT) {
			WRITE_ONCE(my_rdp->nocb_defer_wakeup, RCU_NOCB_WAKE_NOT);
			del_timer(&my_rdp->nocb_timer);
		}
		WRITE_ONCE(my_rdp->nocb_gp_sleep, true);
		raw_spin_unlock_irqrestore(&my_rdp->nocb_gp_lock, flags);
	} else {
		rdp_toggling = READ_ONCE(my_rdp->nocb_toggling_rdp);
		if (rdp_toggling) {
			/*
			 * Paranoid locking to make sure nocb_toggling_rdp is well
			 * reset *before* we (re)set SEGCBLIST_KTHREAD_GP or we could
			 * race with another round of nocb toggling for this rdp.
			 * Nocb locking should prevent from that already but we stick
			 * to paranoia, especially in rare path.
			 */
			raw_spin_lock_irqsave(&my_rdp->nocb_gp_lock, flags);
			my_rdp->nocb_toggling_rdp = NULL;
			raw_spin_unlock_irqrestore(&my_rdp->nocb_gp_lock, flags);
		}
	}

	if (rdp_toggling) {
		int ret;

		ret = nocb_gp_toggle_rdp(rdp_toggling);
		if (ret == 1)
			list_add_tail(&rdp_toggling->nocb_entry_rdp, &my_rdp->nocb_head_rdp);
		else if (ret == 0)
			list_del(&rdp_toggling->nocb_entry_rdp);

		swake_up_one(&rdp_toggling->nocb_state_wq);
	}

	my_rdp->nocb_gp_seq = -1;
	WARN_ON(signal_pending(current));
}

/*
 * No-CBs grace-period-wait kthread.  There is one of these per group
 * of CPUs, but only once at least one CPU in that group has come online
 * at least once since boot.  This kthread checks for newly posted
 * callbacks from any of the CPUs it is responsible for, waits for a
 * grace period, then awakens all of the rcu_nocb_cb_kthread() instances
 * that then have callback-invocation work to do.
 */
static int rcu_nocb_gp_kthread(void *arg)
{
	struct rcu_data *rdp = arg;

	for (;;) {
		WRITE_ONCE(rdp->nocb_gp_loops, rdp->nocb_gp_loops + 1);
		nocb_gp_wait(rdp);
		cond_resched_tasks_rcu_qs();
	}
	return 0;
}

static inline bool nocb_cb_wait_cond(struct rcu_data *rdp)
{
	return !READ_ONCE(rdp->nocb_cb_sleep) || kthread_should_park();
}

/*
 * Invoke any ready callbacks from the corresponding no-CBs CPU,
 * then, if there are no more, wait for more to appear.
 */
static void nocb_cb_wait(struct rcu_data *rdp)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long cur_gp_seq;
	unsigned long flags;
	bool needwake_gp = false;
	struct rcu_node *rnp = rdp->mynode;

	swait_event_interruptible_exclusive(rdp->nocb_cb_wq,
					    nocb_cb_wait_cond(rdp));
	if (kthread_should_park()) {
		kthread_parkme();
	} else if (READ_ONCE(rdp->nocb_cb_sleep)) {
		WARN_ON(signal_pending(current));
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("WokeEmpty"));
	}

	WARN_ON_ONCE(!rcu_rdp_is_offloaded(rdp));

	local_irq_save(flags);
	rcu_momentary_dyntick_idle();
	local_irq_restore(flags);
	/*
	 * Disable BH to provide the expected environment.  Also, when
	 * transitioning to/from NOCB mode, a self-requeuing callback might
	 * be invoked from softirq.  A short grace period could cause both
	 * instances of this callback would execute concurrently.
	 */
	local_bh_disable();
	rcu_do_batch(rdp);
	local_bh_enable();
	lockdep_assert_irqs_enabled();
	rcu_nocb_lock_irqsave(rdp, flags);
	if (rcu_segcblist_nextgp(cblist, &cur_gp_seq) &&
	    rcu_seq_done(&rnp->gp_seq, cur_gp_seq) &&
	    raw_spin_trylock_rcu_node(rnp)) { /* irqs already disabled. */
		needwake_gp = rcu_advance_cbs(rdp->mynode, rdp);
		raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled. */
	}

	if (!rcu_segcblist_ready_cbs(cblist)) {
		WRITE_ONCE(rdp->nocb_cb_sleep, true);
		trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("CBSleep"));
	} else {
		WRITE_ONCE(rdp->nocb_cb_sleep, false);
	}

	rcu_nocb_unlock_irqrestore(rdp, flags);
	if (needwake_gp)
		rcu_gp_kthread_wake();
}

/*
 * Per-rcu_data kthread, but only for no-CBs CPUs.  Repeatedly invoke
 * nocb_cb_wait() to do the dirty work.
 */
static int rcu_nocb_cb_kthread(void *arg)
{
	struct rcu_data *rdp = arg;

	// Each pass through this loop does one callback batch, and,
	// if there are no more ready callbacks, waits for them.
	for (;;) {
		nocb_cb_wait(rdp);
		cond_resched_tasks_rcu_qs();
	}
	return 0;
}

/* Is a deferred wakeup of rcu_nocb_kthread() required? */
static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp, int level)
{
	return READ_ONCE(rdp->nocb_defer_wakeup) >= level;
}

/* Do a deferred wakeup of rcu_nocb_kthread(). */
static bool do_nocb_deferred_wakeup_common(struct rcu_data *rdp_gp,
					   struct rcu_data *rdp, int level,
					   unsigned long flags)
	__releases(rdp_gp->nocb_gp_lock)
{
	int ndw;
	int ret;

	if (!rcu_nocb_need_deferred_wakeup(rdp_gp, level)) {
		raw_spin_unlock_irqrestore(&rdp_gp->nocb_gp_lock, flags);
		return false;
	}

	ndw = rdp_gp->nocb_defer_wakeup;
	ret = __wake_nocb_gp(rdp_gp, rdp, ndw == RCU_NOCB_WAKE_FORCE, flags);
	trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("DeferredWake"));

	return ret;
}

/* Do a deferred wakeup of rcu_nocb_kthread() from a timer handler. */
static void do_nocb_deferred_wakeup_timer(struct timer_list *t)
{
	unsigned long flags;
	struct rcu_data *rdp = from_timer(rdp, t, nocb_timer);

	WARN_ON_ONCE(rdp->nocb_gp_rdp != rdp);
	trace_rcu_nocb_wake(rcu_state.name, rdp->cpu, TPS("Timer"));

	raw_spin_lock_irqsave(&rdp->nocb_gp_lock, flags);
	smp_mb__after_spinlock(); /* Timer expire before wakeup. */
	do_nocb_deferred_wakeup_common(rdp, rdp, RCU_NOCB_WAKE_BYPASS, flags);
}

/*
 * Do a deferred wakeup of rcu_nocb_kthread() from fastpath.
 * This means we do an inexact common-case check.  Note that if
 * we miss, ->nocb_timer will eventually clean things up.
 */
static bool do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	if (!rdp_gp || !rcu_nocb_need_deferred_wakeup(rdp_gp, RCU_NOCB_WAKE))
		return false;

	raw_spin_lock_irqsave(&rdp_gp->nocb_gp_lock, flags);
	return do_nocb_deferred_wakeup_common(rdp_gp, rdp, RCU_NOCB_WAKE, flags);
}

void rcu_nocb_flush_deferred_wakeup(void)
{
	do_nocb_deferred_wakeup(this_cpu_ptr(&rcu_data));
}
EXPORT_SYMBOL_GPL(rcu_nocb_flush_deferred_wakeup);

static int rdp_offload_toggle(struct rcu_data *rdp,
			       bool offload, unsigned long flags)
	__releases(rdp->nocb_lock)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;
	bool wake_gp = false;

	rcu_segcblist_offload(cblist, offload);
	rcu_nocb_unlock_irqrestore(rdp, flags);

	raw_spin_lock_irqsave(&rdp_gp->nocb_gp_lock, flags);
	// Queue this rdp for add/del to/from the list to iterate on rcuog
	WRITE_ONCE(rdp_gp->nocb_toggling_rdp, rdp);
	if (rdp_gp->nocb_gp_sleep) {
		rdp_gp->nocb_gp_sleep = false;
		wake_gp = true;
	}
	raw_spin_unlock_irqrestore(&rdp_gp->nocb_gp_lock, flags);

	return wake_gp;
}

static long rcu_nocb_rdp_deoffload(void *arg)
{
	struct rcu_data *rdp = arg;
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int wake_gp;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	/*
	 * rcu_nocb_rdp_deoffload() may be called directly if
	 * rcuog/o[p] spawn failed, because at this time the rdp->cpu
	 * is not online yet.
	 */
	WARN_ON_ONCE((rdp->cpu != raw_smp_processor_id()) && cpu_online(rdp->cpu));

	pr_info("De-offloading %d\n", rdp->cpu);

	rcu_nocb_lock_irqsave(rdp, flags);
	/*
	 * Flush once and for all now. This suffices because we are
	 * running on the target CPU holding ->nocb_lock (thus having
	 * interrupts disabled), and because rdp_offload_toggle()
	 * invokes rcu_segcblist_offload(), which clears SEGCBLIST_OFFLOADED.
	 * Thus future calls to rcu_segcblist_completely_offloaded() will
	 * return false, which means that future calls to rcu_nocb_try_bypass()
	 * will refuse to put anything into the bypass.
	 */
	WARN_ON_ONCE(!rcu_nocb_flush_bypass(rdp, NULL, jiffies, false));
	/*
	 * Start with invoking rcu_core() early. This way if the current thread
	 * happens to preempt an ongoing call to rcu_core() in the middle,
	 * leaving some work dismissed because rcu_core() still thinks the rdp is
	 * completely offloaded, we are guaranteed a nearby future instance of
	 * rcu_core() to catch up.
	 */
	rcu_segcblist_set_flags(cblist, SEGCBLIST_RCU_CORE);
	invoke_rcu_core();
	wake_gp = rdp_offload_toggle(rdp, false, flags);

	mutex_lock(&rdp_gp->nocb_gp_kthread_mutex);
	if (rdp_gp->nocb_gp_kthread) {
		if (wake_gp)
			wake_up_process(rdp_gp->nocb_gp_kthread);

		swait_event_exclusive(rdp->nocb_state_wq,
				      !rcu_segcblist_test_flags(cblist,
								SEGCBLIST_KTHREAD_GP));
		if (rdp->nocb_cb_kthread)
			kthread_park(rdp->nocb_cb_kthread);
	} else {
		/*
		 * No kthread to clear the flags for us or remove the rdp from the nocb list
		 * to iterate. Do it here instead. Locking doesn't look stricly necessary
		 * but we stick to paranoia in this rare path.
		 */
		rcu_nocb_lock_irqsave(rdp, flags);
		rcu_segcblist_clear_flags(&rdp->cblist, SEGCBLIST_KTHREAD_GP);
		rcu_nocb_unlock_irqrestore(rdp, flags);

		list_del(&rdp->nocb_entry_rdp);
	}
	mutex_unlock(&rdp_gp->nocb_gp_kthread_mutex);

	/*
	 * Lock one last time to acquire latest callback updates from kthreads
	 * so we can later handle callbacks locally without locking.
	 */
	rcu_nocb_lock_irqsave(rdp, flags);
	/*
	 * Theoretically we could clear SEGCBLIST_LOCKING after the nocb
	 * lock is released but how about being paranoid for once?
	 */
	rcu_segcblist_clear_flags(cblist, SEGCBLIST_LOCKING);
	/*
	 * Without SEGCBLIST_LOCKING, we can't use
	 * rcu_nocb_unlock_irqrestore() anymore.
	 */
	raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);

	/* Sanity check */
	WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->nocb_bypass));


	return 0;
}

int rcu_nocb_cpu_deoffload(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	int ret = 0;

	cpus_read_lock();
	mutex_lock(&rcu_state.barrier_mutex);
	if (rcu_rdp_is_offloaded(rdp)) {
		if (cpu_online(cpu)) {
			ret = work_on_cpu(cpu, rcu_nocb_rdp_deoffload, rdp);
			if (!ret)
				cpumask_clear_cpu(cpu, rcu_nocb_mask);
		} else {
			pr_info("NOCB: Cannot CB-deoffload offline CPU %d\n", rdp->cpu);
			ret = -EINVAL;
		}
	}
	mutex_unlock(&rcu_state.barrier_mutex);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rcu_nocb_cpu_deoffload);

static long rcu_nocb_rdp_offload(void *arg)
{
	struct rcu_data *rdp = arg;
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int wake_gp;
	struct rcu_data *rdp_gp = rdp->nocb_gp_rdp;

	WARN_ON_ONCE(rdp->cpu != raw_smp_processor_id());
	/*
	 * For now we only support re-offload, ie: the rdp must have been
	 * offloaded on boot first.
	 */
	if (!rdp->nocb_gp_rdp)
		return -EINVAL;

	if (WARN_ON_ONCE(!rdp_gp->nocb_gp_kthread))
		return -EINVAL;

	pr_info("Offloading %d\n", rdp->cpu);

	/*
	 * Can't use rcu_nocb_lock_irqsave() before SEGCBLIST_LOCKING
	 * is set.
	 */
	raw_spin_lock_irqsave(&rdp->nocb_lock, flags);

	/*
	 * We didn't take the nocb lock while working on the
	 * rdp->cblist with SEGCBLIST_LOCKING cleared (pure softirq/rcuc mode).
	 * Every modifications that have been done previously on
	 * rdp->cblist must be visible remotely by the nocb kthreads
	 * upon wake up after reading the cblist flags.
	 *
	 * The layout against nocb_lock enforces that ordering:
	 *
	 *  __rcu_nocb_rdp_offload()   nocb_cb_wait()/nocb_gp_wait()
	 * -------------------------   ----------------------------
	 *      WRITE callbacks           rcu_nocb_lock()
	 *      rcu_nocb_lock()           READ flags
	 *      WRITE flags               READ callbacks
	 *      rcu_nocb_unlock()         rcu_nocb_unlock()
	 */
	wake_gp = rdp_offload_toggle(rdp, true, flags);
	if (wake_gp)
		wake_up_process(rdp_gp->nocb_gp_kthread);

	kthread_unpark(rdp->nocb_cb_kthread);

	swait_event_exclusive(rdp->nocb_state_wq,
			      rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP));

	/*
	 * All kthreads are ready to work, we can finally relieve rcu_core() and
	 * enable nocb bypass.
	 */
	rcu_nocb_lock_irqsave(rdp, flags);
	rcu_segcblist_clear_flags(cblist, SEGCBLIST_RCU_CORE);
	rcu_nocb_unlock_irqrestore(rdp, flags);

	return 0;
}

int rcu_nocb_cpu_offload(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	int ret = 0;

	cpus_read_lock();
	mutex_lock(&rcu_state.barrier_mutex);
	if (!rcu_rdp_is_offloaded(rdp)) {
		if (cpu_online(cpu)) {
			ret = work_on_cpu(cpu, rcu_nocb_rdp_offload, rdp);
			if (!ret)
				cpumask_set_cpu(cpu, rcu_nocb_mask);
		} else {
			pr_info("NOCB: Cannot CB-offload offline CPU %d\n", rdp->cpu);
			ret = -EINVAL;
		}
	}
	mutex_unlock(&rcu_state.barrier_mutex);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rcu_nocb_cpu_offload);

#ifdef CONFIG_RCU_LAZY
static unsigned long
lazy_rcu_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu;
	unsigned long count = 0;

	if (WARN_ON_ONCE(!cpumask_available(rcu_nocb_mask)))
		return 0;

	/*  Protect rcu_nocb_mask against concurrent (de-)offloading. */
	if (!mutex_trylock(&rcu_state.barrier_mutex))
		return 0;

	/* Snapshot count of all CPUs */
	for_each_cpu(cpu, rcu_nocb_mask) {
		struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

		count +=  READ_ONCE(rdp->lazy_len);
	}

	mutex_unlock(&rcu_state.barrier_mutex);

	return count ? count : SHRINK_EMPTY;
}

static unsigned long
lazy_rcu_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu;
	unsigned long flags;
	unsigned long count = 0;

	if (WARN_ON_ONCE(!cpumask_available(rcu_nocb_mask)))
		return 0;
	/*
	 * Protect against concurrent (de-)offloading. Otherwise nocb locking
	 * may be ignored or imbalanced.
	 */
	if (!mutex_trylock(&rcu_state.barrier_mutex)) {
		/*
		 * But really don't insist if barrier_mutex is contended since we
		 * can't guarantee that it will never engage in a dependency
		 * chain involving memory allocation. The lock is seldom contended
		 * anyway.
		 */
		return 0;
	}

	/* Snapshot count of all CPUs */
	for_each_cpu(cpu, rcu_nocb_mask) {
		struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
		int _count;

		if (WARN_ON_ONCE(!rcu_rdp_is_offloaded(rdp)))
			continue;

		if (!READ_ONCE(rdp->lazy_len))
			continue;

		rcu_nocb_lock_irqsave(rdp, flags);
		/*
		 * Recheck under the nocb lock. Since we are not holding the bypass
		 * lock we may still race with increments from the enqueuer but still
		 * we know for sure if there is at least one lazy callback.
		 */
		_count = READ_ONCE(rdp->lazy_len);
		if (!_count) {
			rcu_nocb_unlock_irqrestore(rdp, flags);
			continue;
		}
		rcu_nocb_try_flush_bypass(rdp, jiffies);
		rcu_nocb_unlock_irqrestore(rdp, flags);
		wake_nocb_gp(rdp, false);
		sc->nr_to_scan -= _count;
		count += _count;
		if (sc->nr_to_scan <= 0)
			break;
	}

	mutex_unlock(&rcu_state.barrier_mutex);

	return count ? count : SHRINK_STOP;
}
#endif // #ifdef CONFIG_RCU_LAZY

void __init rcu_init_nohz(void)
{
	int cpu;
	struct rcu_data *rdp;
	const struct cpumask *cpumask = NULL;
	struct shrinker * __maybe_unused lazy_rcu_shrinker;

#if defined(CONFIG_NO_HZ_FULL)
	if (tick_nohz_full_running && !cpumask_empty(tick_nohz_full_mask))
		cpumask = tick_nohz_full_mask;
#endif

	if (IS_ENABLED(CONFIG_RCU_NOCB_CPU_DEFAULT_ALL) &&
	    !rcu_state.nocb_is_setup && !cpumask)
		cpumask = cpu_possible_mask;

	if (cpumask) {
		if (!cpumask_available(rcu_nocb_mask)) {
			if (!zalloc_cpumask_var(&rcu_nocb_mask, GFP_KERNEL)) {
				pr_info("rcu_nocb_mask allocation failed, callback offloading disabled.\n");
				return;
			}
		}

		cpumask_or(rcu_nocb_mask, rcu_nocb_mask, cpumask);
		rcu_state.nocb_is_setup = true;
	}

	if (!rcu_state.nocb_is_setup)
		return;

#ifdef CONFIG_RCU_LAZY
	lazy_rcu_shrinker = shrinker_alloc(0, "rcu-lazy");
	if (!lazy_rcu_shrinker) {
		pr_err("Failed to allocate lazy_rcu shrinker!\n");
	} else {
		lazy_rcu_shrinker->count_objects = lazy_rcu_shrink_count;
		lazy_rcu_shrinker->scan_objects = lazy_rcu_shrink_scan;

		shrinker_register(lazy_rcu_shrinker);
	}
#endif // #ifdef CONFIG_RCU_LAZY

	if (!cpumask_subset(rcu_nocb_mask, cpu_possible_mask)) {
		pr_info("\tNote: kernel parameter 'rcu_nocbs=', 'nohz_full', or 'isolcpus=' contains nonexistent CPUs.\n");
		cpumask_and(rcu_nocb_mask, cpu_possible_mask,
			    rcu_nocb_mask);
	}
	if (cpumask_empty(rcu_nocb_mask))
		pr_info("\tOffload RCU callbacks from CPUs: (none).\n");
	else
		pr_info("\tOffload RCU callbacks from CPUs: %*pbl.\n",
			cpumask_pr_args(rcu_nocb_mask));
	if (rcu_nocb_poll)
		pr_info("\tPoll for callbacks from no-CBs CPUs.\n");

	for_each_cpu(cpu, rcu_nocb_mask) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		if (rcu_segcblist_empty(&rdp->cblist))
			rcu_segcblist_init(&rdp->cblist);
		rcu_segcblist_offload(&rdp->cblist, true);
		rcu_segcblist_set_flags(&rdp->cblist, SEGCBLIST_KTHREAD_GP);
		rcu_segcblist_clear_flags(&rdp->cblist, SEGCBLIST_RCU_CORE);
	}
	rcu_organize_nocb_kthreads();
}

/* Initialize per-rcu_data variables for no-CBs CPUs. */
static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
	init_swait_queue_head(&rdp->nocb_cb_wq);
	init_swait_queue_head(&rdp->nocb_gp_wq);
	init_swait_queue_head(&rdp->nocb_state_wq);
	raw_spin_lock_init(&rdp->nocb_lock);
	raw_spin_lock_init(&rdp->nocb_bypass_lock);
	raw_spin_lock_init(&rdp->nocb_gp_lock);
	timer_setup(&rdp->nocb_timer, do_nocb_deferred_wakeup_timer, 0);
	rcu_cblist_init(&rdp->nocb_bypass);
	WRITE_ONCE(rdp->lazy_len, 0);
	mutex_init(&rdp->nocb_gp_kthread_mutex);
}

/*
 * If the specified CPU is a no-CBs CPU that does not already have its
 * rcuo CB kthread, spawn it.  Additionally, if the rcuo GP kthread
 * for this CPU's group has not yet been created, spawn it as well.
 */
static void rcu_spawn_cpu_nocb_kthread(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct rcu_data *rdp_gp;
	struct task_struct *t;
	struct sched_param sp;

	if (!rcu_scheduler_fully_active || !rcu_state.nocb_is_setup)
		return;

	/* If there already is an rcuo kthread, then nothing to do. */
	if (rdp->nocb_cb_kthread)
		return;

	/* If we didn't spawn the GP kthread first, reorganize! */
	sp.sched_priority = kthread_prio;
	rdp_gp = rdp->nocb_gp_rdp;
	mutex_lock(&rdp_gp->nocb_gp_kthread_mutex);
	if (!rdp_gp->nocb_gp_kthread) {
		t = kthread_run(rcu_nocb_gp_kthread, rdp_gp,
				"rcuog/%d", rdp_gp->cpu);
		if (WARN_ONCE(IS_ERR(t), "%s: Could not start rcuo GP kthread, OOM is now expected behavior\n", __func__)) {
			mutex_unlock(&rdp_gp->nocb_gp_kthread_mutex);
			goto end;
		}
		WRITE_ONCE(rdp_gp->nocb_gp_kthread, t);
		if (kthread_prio)
			sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	}
	mutex_unlock(&rdp_gp->nocb_gp_kthread_mutex);

	/* Spawn the kthread for this CPU. */
	t = kthread_create(rcu_nocb_cb_kthread, rdp,
			   "rcuo%c/%d", rcu_state.abbr, cpu);
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start rcuo CB kthread, OOM is now expected behavior\n", __func__))
		goto end;

	if (rcu_rdp_is_offloaded(rdp))
		wake_up_process(t);
	else
		kthread_park(t);

	if (IS_ENABLED(CONFIG_RCU_NOCB_CPU_CB_BOOST) && kthread_prio)
		sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);

	WRITE_ONCE(rdp->nocb_cb_kthread, t);
	WRITE_ONCE(rdp->nocb_gp_kthread, rdp_gp->nocb_gp_kthread);
	return;
end:
	mutex_lock(&rcu_state.barrier_mutex);
	if (rcu_rdp_is_offloaded(rdp)) {
		rcu_nocb_rdp_deoffload(rdp);
		cpumask_clear_cpu(cpu, rcu_nocb_mask);
	}
	mutex_unlock(&rcu_state.barrier_mutex);
}

/* How many CB CPU IDs per GP kthread?  Default of -1 for sqrt(nr_cpu_ids). */
static int rcu_nocb_gp_stride = -1;
module_param(rcu_nocb_gp_stride, int, 0444);

/*
 * Initialize GP-CB relationships for all no-CBs CPU.
 */
static void __init rcu_organize_nocb_kthreads(void)
{
	int cpu;
	bool firsttime = true;
	bool gotnocbs = false;
	bool gotnocbscbs = true;
	int ls = rcu_nocb_gp_stride;
	int nl = 0;  /* Next GP kthread. */
	struct rcu_data *rdp;
	struct rcu_data *rdp_gp = NULL;  /* Suppress misguided gcc warn. */

	if (!cpumask_available(rcu_nocb_mask))
		return;
	if (ls == -1) {
		ls = nr_cpu_ids / int_sqrt(nr_cpu_ids);
		rcu_nocb_gp_stride = ls;
	}

	/*
	 * Each pass through this loop sets up one rcu_data structure.
	 * Should the corresponding CPU come online in the future, then
	 * we will spawn the needed set of rcu_nocb_kthread() kthreads.
	 */
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		if (rdp->cpu >= nl) {
			/* New GP kthread, set up for CBs & next GP. */
			gotnocbs = true;
			nl = DIV_ROUND_UP(rdp->cpu + 1, ls) * ls;
			rdp_gp = rdp;
			INIT_LIST_HEAD(&rdp->nocb_head_rdp);
			if (dump_tree) {
				if (!firsttime)
					pr_cont("%s\n", gotnocbscbs
							? "" : " (self only)");
				gotnocbscbs = false;
				firsttime = false;
				pr_alert("%s: No-CB GP kthread CPU %d:",
					 __func__, cpu);
			}
		} else {
			/* Another CB kthread, link to previous GP kthread. */
			gotnocbscbs = true;
			if (dump_tree)
				pr_cont(" %d", cpu);
		}
		rdp->nocb_gp_rdp = rdp_gp;
		if (cpumask_test_cpu(cpu, rcu_nocb_mask))
			list_add_tail(&rdp->nocb_entry_rdp, &rdp_gp->nocb_head_rdp);
	}
	if (gotnocbs && dump_tree)
		pr_cont("%s\n", gotnocbscbs ? "" : " (self only)");
}

/*
 * Bind the current task to the offloaded CPUs.  If there are no offloaded
 * CPUs, leave the task unbound.  Splat if the bind attempt fails.
 */
void rcu_bind_current_to_nocb(void)
{
	if (cpumask_available(rcu_nocb_mask) && !cpumask_empty(rcu_nocb_mask))
		WARN_ON(sched_setaffinity(current->pid, rcu_nocb_mask));
}
EXPORT_SYMBOL_GPL(rcu_bind_current_to_nocb);

// The ->on_cpu field is available only in CONFIG_SMP=y, so...
#ifdef CONFIG_SMP
static char *show_rcu_should_be_on_cpu(struct task_struct *tsp)
{
	return tsp && task_is_running(tsp) && !tsp->on_cpu ? "!" : "";
}
#else // #ifdef CONFIG_SMP
static char *show_rcu_should_be_on_cpu(struct task_struct *tsp)
{
	return "";
}
#endif // #else #ifdef CONFIG_SMP

/*
 * Dump out nocb grace-period kthread state for the specified rcu_data
 * structure.
 */
static void show_rcu_nocb_gp_state(struct rcu_data *rdp)
{
	struct rcu_node *rnp = rdp->mynode;

	pr_info("nocb GP %d %c%c%c%c%c %c[%c%c] %c%c:%ld rnp %d:%d %lu %c CPU %d%s\n",
		rdp->cpu,
		"kK"[!!rdp->nocb_gp_kthread],
		"lL"[raw_spin_is_locked(&rdp->nocb_gp_lock)],
		"dD"[!!rdp->nocb_defer_wakeup],
		"tT"[timer_pending(&rdp->nocb_timer)],
		"sS"[!!rdp->nocb_gp_sleep],
		".W"[swait_active(&rdp->nocb_gp_wq)],
		".W"[swait_active(&rnp->nocb_gp_wq[0])],
		".W"[swait_active(&rnp->nocb_gp_wq[1])],
		".B"[!!rdp->nocb_gp_bypass],
		".G"[!!rdp->nocb_gp_gp],
		(long)rdp->nocb_gp_seq,
		rnp->grplo, rnp->grphi, READ_ONCE(rdp->nocb_gp_loops),
		rdp->nocb_gp_kthread ? task_state_to_char(rdp->nocb_gp_kthread) : '.',
		rdp->nocb_gp_kthread ? (int)task_cpu(rdp->nocb_gp_kthread) : -1,
		show_rcu_should_be_on_cpu(rdp->nocb_gp_kthread));
}

/* Dump out nocb kthread state for the specified rcu_data structure. */
static void show_rcu_nocb_state(struct rcu_data *rdp)
{
	char bufw[20];
	char bufr[20];
	struct rcu_data *nocb_next_rdp;
	struct rcu_segcblist *rsclp = &rdp->cblist;
	bool waslocked;
	bool wassleep;

	if (rdp->nocb_gp_rdp == rdp)
		show_rcu_nocb_gp_state(rdp);

	nocb_next_rdp = list_next_or_null_rcu(&rdp->nocb_gp_rdp->nocb_head_rdp,
					      &rdp->nocb_entry_rdp,
					      typeof(*rdp),
					      nocb_entry_rdp);

	sprintf(bufw, "%ld", rsclp->gp_seq[RCU_WAIT_TAIL]);
	sprintf(bufr, "%ld", rsclp->gp_seq[RCU_NEXT_READY_TAIL]);
	pr_info("   CB %d^%d->%d %c%c%c%c%c F%ld L%ld C%d %c%c%s%c%s%c%c q%ld %c CPU %d%s\n",
		rdp->cpu, rdp->nocb_gp_rdp->cpu,
		nocb_next_rdp ? nocb_next_rdp->cpu : -1,
		"kK"[!!rdp->nocb_cb_kthread],
		"bB"[raw_spin_is_locked(&rdp->nocb_bypass_lock)],
		"lL"[raw_spin_is_locked(&rdp->nocb_lock)],
		"sS"[!!rdp->nocb_cb_sleep],
		".W"[swait_active(&rdp->nocb_cb_wq)],
		jiffies - rdp->nocb_bypass_first,
		jiffies - rdp->nocb_nobypass_last,
		rdp->nocb_nobypass_count,
		".D"[rcu_segcblist_ready_cbs(rsclp)],
		".W"[!rcu_segcblist_segempty(rsclp, RCU_WAIT_TAIL)],
		rcu_segcblist_segempty(rsclp, RCU_WAIT_TAIL) ? "" : bufw,
		".R"[!rcu_segcblist_segempty(rsclp, RCU_NEXT_READY_TAIL)],
		rcu_segcblist_segempty(rsclp, RCU_NEXT_READY_TAIL) ? "" : bufr,
		".N"[!rcu_segcblist_segempty(rsclp, RCU_NEXT_TAIL)],
		".B"[!!rcu_cblist_n_cbs(&rdp->nocb_bypass)],
		rcu_segcblist_n_cbs(&rdp->cblist),
		rdp->nocb_cb_kthread ? task_state_to_char(rdp->nocb_cb_kthread) : '.',
		rdp->nocb_cb_kthread ? (int)task_cpu(rdp->nocb_cb_kthread) : -1,
		show_rcu_should_be_on_cpu(rdp->nocb_cb_kthread));

	/* It is OK for GP kthreads to have GP state. */
	if (rdp->nocb_gp_rdp == rdp)
		return;

	waslocked = raw_spin_is_locked(&rdp->nocb_gp_lock);
	wassleep = swait_active(&rdp->nocb_gp_wq);
	if (!rdp->nocb_gp_sleep && !waslocked && !wassleep)
		return;  /* Nothing untoward. */

	pr_info("   nocb GP activity on CB-only CPU!!! %c%c%c %c\n",
		"lL"[waslocked],
		"dD"[!!rdp->nocb_defer_wakeup],
		"sS"[!!rdp->nocb_gp_sleep],
		".W"[wassleep]);
}

#else /* #ifdef CONFIG_RCU_NOCB_CPU */

static inline int rcu_lockdep_is_held_nocb(struct rcu_data *rdp)
{
	return 0;
}

static inline bool rcu_current_is_nocb_kthread(struct rcu_data *rdp)
{
	return false;
}

/* No ->nocb_lock to acquire.  */
static void rcu_nocb_lock(struct rcu_data *rdp)
{
}

/* No ->nocb_lock to release.  */
static void rcu_nocb_unlock(struct rcu_data *rdp)
{
}

/* No ->nocb_lock to release.  */
static void rcu_nocb_unlock_irqrestore(struct rcu_data *rdp,
				       unsigned long flags)
{
	local_irq_restore(flags);
}

/* Lockdep check that ->cblist may be safely accessed. */
static void rcu_lockdep_assert_cblist_protected(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
}

static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq)
{
}

static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp)
{
	return NULL;
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
}

static bool wake_nocb_gp(struct rcu_data *rdp, bool force)
{
	return false;
}

static bool rcu_nocb_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				  unsigned long j, bool lazy)
{
	return true;
}

static void call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *head,
			  rcu_callback_t func, unsigned long flags, bool lazy)
{
	WARN_ON_ONCE(1);  /* Should be dead code! */
}

static void __call_rcu_nocb_wake(struct rcu_data *rdp, bool was_empty,
				 unsigned long flags)
{
	WARN_ON_ONCE(1);  /* Should be dead code! */
}

static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
}

static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp, int level)
{
	return false;
}

static bool do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
	return false;
}

static void rcu_spawn_cpu_nocb_kthread(int cpu)
{
}

static void show_rcu_nocb_state(struct rcu_data *rdp)
{
}

#endif /* #else #ifdef CONFIG_RCU_NOCB_CPU */

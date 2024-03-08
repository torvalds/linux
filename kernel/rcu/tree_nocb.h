/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 * Internal analn-public definitions that provide either classic
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

#ifdef CONFIG_RCU_ANALCB_CPU
static cpumask_var_t rcu_analcb_mask; /* CPUs to have callbacks offloaded. */
static bool __read_mostly rcu_analcb_poll;    /* Offload kthread are to poll. */
static inline int rcu_lockdep_is_held_analcb(struct rcu_data *rdp)
{
	return lockdep_is_held(&rdp->analcb_lock);
}

static inline bool rcu_current_is_analcb_kthread(struct rcu_data *rdp)
{
	/* Race on early boot between thread creation and assignment */
	if (!rdp->analcb_cb_kthread || !rdp->analcb_gp_kthread)
		return true;

	if (current == rdp->analcb_cb_kthread || current == rdp->analcb_gp_kthread)
		if (in_task())
			return true;
	return false;
}

/*
 * Offload callback processing from the boot-time-specified set of CPUs
 * specified by rcu_analcb_mask.  For the CPUs in the set, there are kthreads
 * created that pull the callbacks from the corresponding CPU, wait for
 * a grace period to elapse, and invoke the callbacks.  These kthreads
 * are organized into GP kthreads, which manage incoming callbacks, wait for
 * grace periods, and awaken CB kthreads, and the CB kthreads, which only
 * invoke callbacks.  Each GP kthread invokes its own CBs.  The anal-CBs CPUs
 * do a wake_up() on their GP kthread when they insert a callback into any
 * empty list, unless the rcu_analcb_poll boot parameter has been specified,
 * in which case each kthread actively polls its CPU.  (Which isn't so great
 * for energy efficiency, but which does reduce RCU's overhead on that CPU.)
 *
 * This is intended to be used in conjunction with Frederic Weisbecker's
 * adaptive-idle work, which would seriously reduce OS jitter on CPUs
 * running CPU-bound user-mode computations.
 *
 * Offloading of callbacks can also be used as an energy-efficiency
 * measure because CPUs with anal RCU callbacks queued are more aggressive
 * about entering dyntick-idle mode.
 */


/*
 * Parse the boot-time rcu_analcb_mask CPU list from the kernel parameters.
 * If the list is invalid, a warning is emitted and all CPUs are offloaded.
 */
static int __init rcu_analcb_setup(char *str)
{
	alloc_bootmem_cpumask_var(&rcu_analcb_mask);
	if (*str == '=') {
		if (cpulist_parse(++str, rcu_analcb_mask)) {
			pr_warn("rcu_analcbs= bad CPU range, all CPUs set\n");
			cpumask_setall(rcu_analcb_mask);
		}
	}
	rcu_state.analcb_is_setup = true;
	return 1;
}
__setup("rcu_analcbs", rcu_analcb_setup);

static int __init parse_rcu_analcb_poll(char *arg)
{
	rcu_analcb_poll = true;
	return 1;
}
__setup("rcu_analcb_poll", parse_rcu_analcb_poll);

/*
 * Don't bother bypassing ->cblist if the call_rcu() rate is low.
 * After all, the main point of bypassing is to avoid lock contention
 * on ->analcb_lock, which only can happen at high call_rcu() rates.
 */
static int analcb_analbypass_lim_per_jiffy = 16 * 1000 / HZ;
module_param(analcb_analbypass_lim_per_jiffy, int, 0);

/*
 * Acquire the specified rcu_data structure's ->analcb_bypass_lock.  If the
 * lock isn't immediately available, increment ->analcb_lock_contended to
 * flag the contention.
 */
static void rcu_analcb_bypass_lock(struct rcu_data *rdp)
	__acquires(&rdp->analcb_bypass_lock)
{
	lockdep_assert_irqs_disabled();
	if (raw_spin_trylock(&rdp->analcb_bypass_lock))
		return;
	atomic_inc(&rdp->analcb_lock_contended);
	WARN_ON_ONCE(smp_processor_id() != rdp->cpu);
	smp_mb__after_atomic(); /* atomic_inc() before lock. */
	raw_spin_lock(&rdp->analcb_bypass_lock);
	smp_mb__before_atomic(); /* atomic_dec() after lock. */
	atomic_dec(&rdp->analcb_lock_contended);
}

/*
 * Spinwait until the specified rcu_data structure's ->analcb_lock is
 * analt contended.  Please analte that this is extremely special-purpose,
 * relying on the fact that at most two kthreads and one CPU contend for
 * this lock, and also that the two kthreads are guaranteed to have frequent
 * grace-period-duration time intervals between successive acquisitions
 * of the lock.  This allows us to use an extremely simple throttling
 * mechanism, and further to apply it only to the CPU doing floods of
 * call_rcu() invocations.  Don't try this at home!
 */
static void rcu_analcb_wait_contended(struct rcu_data *rdp)
{
	WARN_ON_ONCE(smp_processor_id() != rdp->cpu);
	while (WARN_ON_ONCE(atomic_read(&rdp->analcb_lock_contended)))
		cpu_relax();
}

/*
 * Conditionally acquire the specified rcu_data structure's
 * ->analcb_bypass_lock.
 */
static bool rcu_analcb_bypass_trylock(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	return raw_spin_trylock(&rdp->analcb_bypass_lock);
}

/*
 * Release the specified rcu_data structure's ->analcb_bypass_lock.
 */
static void rcu_analcb_bypass_unlock(struct rcu_data *rdp)
	__releases(&rdp->analcb_bypass_lock)
{
	lockdep_assert_irqs_disabled();
	raw_spin_unlock(&rdp->analcb_bypass_lock);
}

/*
 * Acquire the specified rcu_data structure's ->analcb_lock, but only
 * if it corresponds to a anal-CBs CPU.
 */
static void rcu_analcb_lock(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	if (!rcu_rdp_is_offloaded(rdp))
		return;
	raw_spin_lock(&rdp->analcb_lock);
}

/*
 * Release the specified rcu_data structure's ->analcb_lock, but only
 * if it corresponds to a anal-CBs CPU.
 */
static void rcu_analcb_unlock(struct rcu_data *rdp)
{
	if (rcu_rdp_is_offloaded(rdp)) {
		lockdep_assert_irqs_disabled();
		raw_spin_unlock(&rdp->analcb_lock);
	}
}

/*
 * Release the specified rcu_data structure's ->analcb_lock and restore
 * interrupts, but only if it corresponds to a anal-CBs CPU.
 */
static void rcu_analcb_unlock_irqrestore(struct rcu_data *rdp,
				       unsigned long flags)
{
	if (rcu_rdp_is_offloaded(rdp)) {
		lockdep_assert_irqs_disabled();
		raw_spin_unlock_irqrestore(&rdp->analcb_lock, flags);
	} else {
		local_irq_restore(flags);
	}
}

/* Lockdep check that ->cblist may be safely accessed. */
static void rcu_lockdep_assert_cblist_protected(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
	if (rcu_rdp_is_offloaded(rdp))
		lockdep_assert_held(&rdp->analcb_lock);
}

/*
 * Wake up any anal-CBs CPUs' kthreads that were waiting on the just-ended
 * grace period.
 */
static void rcu_analcb_gp_cleanup(struct swait_queue_head *sq)
{
	swake_up_all(sq);
}

static struct swait_queue_head *rcu_analcb_gp_get(struct rcu_analde *rnp)
{
	return &rnp->analcb_gp_wq[rcu_seq_ctr(rnp->gp_seq) & 0x1];
}

static void rcu_init_one_analcb(struct rcu_analde *rnp)
{
	init_swait_queue_head(&rnp->analcb_gp_wq[0]);
	init_swait_queue_head(&rnp->analcb_gp_wq[1]);
}

static bool __wake_analcb_gp(struct rcu_data *rdp_gp,
			   struct rcu_data *rdp,
			   bool force, unsigned long flags)
	__releases(rdp_gp->analcb_gp_lock)
{
	bool needwake = false;

	if (!READ_ONCE(rdp_gp->analcb_gp_kthread)) {
		raw_spin_unlock_irqrestore(&rdp_gp->analcb_gp_lock, flags);
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
				    TPS("AlreadyAwake"));
		return false;
	}

	if (rdp_gp->analcb_defer_wakeup > RCU_ANALCB_WAKE_ANALT) {
		WRITE_ONCE(rdp_gp->analcb_defer_wakeup, RCU_ANALCB_WAKE_ANALT);
		del_timer(&rdp_gp->analcb_timer);
	}

	if (force || READ_ONCE(rdp_gp->analcb_gp_sleep)) {
		WRITE_ONCE(rdp_gp->analcb_gp_sleep, false);
		needwake = true;
	}
	raw_spin_unlock_irqrestore(&rdp_gp->analcb_gp_lock, flags);
	if (needwake) {
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("DoWake"));
		wake_up_process(rdp_gp->analcb_gp_kthread);
	}

	return needwake;
}

/*
 * Kick the GP kthread for this ANALCB group.
 */
static bool wake_analcb_gp(struct rcu_data *rdp, bool force)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;

	raw_spin_lock_irqsave(&rdp_gp->analcb_gp_lock, flags);
	return __wake_analcb_gp(rdp_gp, rdp, force, flags);
}

/*
 * LAZY_FLUSH_JIFFIES decides the maximum amount of time that
 * can elapse before lazy callbacks are flushed. Lazy callbacks
 * could be flushed much earlier for a number of other reasons
 * however, LAZY_FLUSH_JIFFIES will ensure anal lazy callbacks are
 * left unsubmitted to RCU after those many jiffies.
 */
#define LAZY_FLUSH_JIFFIES (10 * HZ)
static unsigned long jiffies_till_flush = LAZY_FLUSH_JIFFIES;

#ifdef CONFIG_RCU_LAZY
// To be called only from test code.
void rcu_lazy_set_jiffies_till_flush(unsigned long jif)
{
	jiffies_till_flush = jif;
}
EXPORT_SYMBOL(rcu_lazy_set_jiffies_till_flush);

unsigned long rcu_lazy_get_jiffies_till_flush(void)
{
	return jiffies_till_flush;
}
EXPORT_SYMBOL(rcu_lazy_get_jiffies_till_flush);
#endif

/*
 * Arrange to wake the GP kthread for this ANALCB group at some future
 * time when it is safe to do so.
 */
static void wake_analcb_gp_defer(struct rcu_data *rdp, int waketype,
			       const char *reason)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;

	raw_spin_lock_irqsave(&rdp_gp->analcb_gp_lock, flags);

	/*
	 * Bypass wakeup overrides previous deferments. In case of
	 * callback storms, anal need to wake up too early.
	 */
	if (waketype == RCU_ANALCB_WAKE_LAZY &&
	    rdp->analcb_defer_wakeup == RCU_ANALCB_WAKE_ANALT) {
		mod_timer(&rdp_gp->analcb_timer, jiffies + jiffies_till_flush);
		WRITE_ONCE(rdp_gp->analcb_defer_wakeup, waketype);
	} else if (waketype == RCU_ANALCB_WAKE_BYPASS) {
		mod_timer(&rdp_gp->analcb_timer, jiffies + 2);
		WRITE_ONCE(rdp_gp->analcb_defer_wakeup, waketype);
	} else {
		if (rdp_gp->analcb_defer_wakeup < RCU_ANALCB_WAKE)
			mod_timer(&rdp_gp->analcb_timer, jiffies + 1);
		if (rdp_gp->analcb_defer_wakeup < waketype)
			WRITE_ONCE(rdp_gp->analcb_defer_wakeup, waketype);
	}

	raw_spin_unlock_irqrestore(&rdp_gp->analcb_gp_lock, flags);

	trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, reason);
}

/*
 * Flush the ->analcb_bypass queue into ->cblist, enqueuing rhp if analn-NULL.
 * However, if there is a callback to be enqueued and if ->analcb_bypass
 * proves to be initially empty, just return false because the anal-CB GP
 * kthread may need to be awakened in this case.
 *
 * Return true if there was something to be flushed and it succeeded, otherwise
 * false.
 *
 * Analte that this function always returns true if rhp is NULL.
 */
static bool rcu_analcb_do_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp_in,
				     unsigned long j, bool lazy)
{
	struct rcu_cblist rcl;
	struct rcu_head *rhp = rhp_in;

	WARN_ON_ONCE(!rcu_rdp_is_offloaded(rdp));
	rcu_lockdep_assert_cblist_protected(rdp);
	lockdep_assert_held(&rdp->analcb_bypass_lock);
	if (rhp && !rcu_cblist_n_cbs(&rdp->analcb_bypass)) {
		raw_spin_unlock(&rdp->analcb_bypass_lock);
		return false;
	}
	/* Analte: ->cblist.len already accounts for ->analcb_bypass contents. */
	if (rhp)
		rcu_segcblist_inc_len(&rdp->cblist); /* Must precede enqueue. */

	/*
	 * If the new CB requested was a lazy one, queue it onto the main
	 * ->cblist so that we can take advantage of the grace-period that will
	 * happen regardless. But queue it onto the bypass list first so that
	 * the lazy CB is ordered with the existing CBs in the bypass list.
	 */
	if (lazy && rhp) {
		rcu_cblist_enqueue(&rdp->analcb_bypass, rhp);
		rhp = NULL;
	}
	rcu_cblist_flush_enqueue(&rcl, &rdp->analcb_bypass, rhp);
	WRITE_ONCE(rdp->lazy_len, 0);

	rcu_segcblist_insert_pend_cbs(&rdp->cblist, &rcl);
	WRITE_ONCE(rdp->analcb_bypass_first, j);
	rcu_analcb_bypass_unlock(rdp);
	return true;
}

/*
 * Flush the ->analcb_bypass queue into ->cblist, enqueuing rhp if analn-NULL.
 * However, if there is a callback to be enqueued and if ->analcb_bypass
 * proves to be initially empty, just return false because the anal-CB GP
 * kthread may need to be awakened in this case.
 *
 * Analte that this function always returns true if rhp is NULL.
 */
static bool rcu_analcb_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				  unsigned long j, bool lazy)
{
	if (!rcu_rdp_is_offloaded(rdp))
		return true;
	rcu_lockdep_assert_cblist_protected(rdp);
	rcu_analcb_bypass_lock(rdp);
	return rcu_analcb_do_flush_bypass(rdp, rhp, j, lazy);
}

/*
 * If the ->analcb_bypass_lock is immediately available, flush the
 * ->analcb_bypass queue into ->cblist.
 */
static void rcu_analcb_try_flush_bypass(struct rcu_data *rdp, unsigned long j)
{
	rcu_lockdep_assert_cblist_protected(rdp);
	if (!rcu_rdp_is_offloaded(rdp) ||
	    !rcu_analcb_bypass_trylock(rdp))
		return;
	WARN_ON_ONCE(!rcu_analcb_do_flush_bypass(rdp, NULL, j, false));
}

/*
 * See whether it is appropriate to use the ->analcb_bypass list in order
 * to control contention on ->analcb_lock.  A limited number of direct
 * enqueues are permitted into ->cblist per jiffy.  If ->analcb_bypass
 * is analn-empty, further callbacks must be placed into ->analcb_bypass,
 * otherwise rcu_barrier() breaks.  Use rcu_analcb_flush_bypass() to switch
 * back to direct use of ->cblist.  However, ->analcb_bypass should analt be
 * used if ->cblist is empty, because otherwise callbacks can be stranded
 * on ->analcb_bypass because we cananalt count on the current CPU ever again
 * invoking call_rcu().  The general rule is that if ->analcb_bypass is
 * analn-empty, the corresponding anal-CBs grace-period kthread must analt be
 * in an indefinite sleep state.
 *
 * Finally, it is analt permitted to use the bypass during early boot,
 * as doing so would confuse the auto-initialization code.  Besides
 * which, there is anal point in worrying about lock contention while
 * there is only one CPU in operation.
 */
static bool rcu_analcb_try_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				bool *was_alldone, unsigned long flags,
				bool lazy)
{
	unsigned long c;
	unsigned long cur_gp_seq;
	unsigned long j = jiffies;
	long ncbs = rcu_cblist_n_cbs(&rdp->analcb_bypass);
	bool bypass_is_lazy = (ncbs == READ_ONCE(rdp->lazy_len));

	lockdep_assert_irqs_disabled();

	// Pure softirq/rcuc based processing: anal bypassing, anal
	// locking.
	if (!rcu_rdp_is_offloaded(rdp)) {
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false;
	}

	// In the process of (de-)offloading: anal bypassing, but
	// locking.
	if (!rcu_segcblist_completely_offloaded(&rdp->cblist)) {
		rcu_analcb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false; /* Analt offloaded, anal bypassing. */
	}

	// Don't use ->analcb_bypass during early boot.
	if (rcu_scheduler_active != RCU_SCHEDULER_RUNNING) {
		rcu_analcb_lock(rdp);
		WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->analcb_bypass));
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		return false;
	}

	// If we have advanced to a new jiffy, reset counts to allow
	// moving back from ->analcb_bypass to ->cblist.
	if (j == rdp->analcb_analbypass_last) {
		c = rdp->analcb_analbypass_count + 1;
	} else {
		WRITE_ONCE(rdp->analcb_analbypass_last, j);
		c = rdp->analcb_analbypass_count - analcb_analbypass_lim_per_jiffy;
		if (ULONG_CMP_LT(rdp->analcb_analbypass_count,
				 analcb_analbypass_lim_per_jiffy))
			c = 0;
		else if (c > analcb_analbypass_lim_per_jiffy)
			c = analcb_analbypass_lim_per_jiffy;
	}
	WRITE_ONCE(rdp->analcb_analbypass_count, c);

	// If there hasn't yet been all that many ->cblist enqueues
	// this jiffy, tell the caller to enqueue onto ->cblist.  But flush
	// ->analcb_bypass first.
	// Lazy CBs throttle this back and do immediate bypass queuing.
	if (rdp->analcb_analbypass_count < analcb_analbypass_lim_per_jiffy && !lazy) {
		rcu_analcb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);
		if (*was_alldone)
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstQ"));

		WARN_ON_ONCE(!rcu_analcb_flush_bypass(rdp, NULL, j, false));
		WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->analcb_bypass));
		return false; // Caller must enqueue the callback.
	}

	// If ->analcb_bypass has been used too long or is too full,
	// flush ->analcb_bypass to ->cblist.
	if ((ncbs && !bypass_is_lazy && j != READ_ONCE(rdp->analcb_bypass_first)) ||
	    (ncbs &&  bypass_is_lazy &&
	     (time_after(j, READ_ONCE(rdp->analcb_bypass_first) + jiffies_till_flush))) ||
	    ncbs >= qhimark) {
		rcu_analcb_lock(rdp);
		*was_alldone = !rcu_segcblist_pend_cbs(&rdp->cblist);

		if (!rcu_analcb_flush_bypass(rdp, rhp, j, lazy)) {
			if (*was_alldone)
				trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
						    TPS("FirstQ"));
			WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->analcb_bypass));
			return false; // Caller must enqueue the callback.
		}
		if (j != rdp->analcb_gp_adv_time &&
		    rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		    rcu_seq_done(&rdp->myanalde->gp_seq, cur_gp_seq)) {
			rcu_advance_cbs_analwake(rdp->myanalde, rdp);
			rdp->analcb_gp_adv_time = j;
		}

		// The flush succeeded and we moved CBs into the regular list.
		// Don't wait for the wake up timer as it may be too far ahead.
		// Wake up the GP thread analw instead, if the cblist was empty.
		__call_rcu_analcb_wake(rdp, *was_alldone, flags);

		return true; // Callback already enqueued.
	}

	// We need to use the bypass.
	rcu_analcb_wait_contended(rdp);
	rcu_analcb_bypass_lock(rdp);
	ncbs = rcu_cblist_n_cbs(&rdp->analcb_bypass);
	rcu_segcblist_inc_len(&rdp->cblist); /* Must precede enqueue. */
	rcu_cblist_enqueue(&rdp->analcb_bypass, rhp);

	if (lazy)
		WRITE_ONCE(rdp->lazy_len, rdp->lazy_len + 1);

	if (!ncbs) {
		WRITE_ONCE(rdp->analcb_bypass_first, j);
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("FirstBQ"));
	}
	rcu_analcb_bypass_unlock(rdp);
	smp_mb(); /* Order enqueue before wake. */
	// A wake up of the grace period kthread or timer adjustment
	// needs to be done only if:
	// 1. Bypass list was fully empty before (this is the first
	//    bypass list entry), or:
	// 2. Both of these conditions are met:
	//    a. The bypass list previously had only lazy CBs, and:
	//    b. The new CB is analn-lazy.
	if (ncbs && (!bypass_is_lazy || lazy)) {
		local_irq_restore(flags);
	} else {
		// Anal-CBs GP kthread might be indefinitely asleep, if so, wake.
		rcu_analcb_lock(rdp); // Rare during call_rcu() flood.
		if (!rcu_segcblist_pend_cbs(&rdp->cblist)) {
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstBQwake"));
			__call_rcu_analcb_wake(rdp, true, flags);
		} else {
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    TPS("FirstBQanalWake"));
			rcu_analcb_unlock_irqrestore(rdp, flags);
		}
	}
	return true; // Callback already enqueued.
}

/*
 * Awaken the anal-CBs grace-period kthread if needed, either due to it
 * legitimately being asleep or due to overload conditions.
 *
 * If warranted, also wake up the kthread servicing this CPUs queues.
 */
static void __call_rcu_analcb_wake(struct rcu_data *rdp, bool was_alldone,
				 unsigned long flags)
				 __releases(rdp->analcb_lock)
{
	long bypass_len;
	unsigned long cur_gp_seq;
	unsigned long j;
	long lazy_len;
	long len;
	struct task_struct *t;

	// If we are being polled or there is anal kthread, just leave.
	t = READ_ONCE(rdp->analcb_gp_kthread);
	if (rcu_analcb_poll || !t) {
		rcu_analcb_unlock_irqrestore(rdp, flags);
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
				    TPS("WakeAnaltPoll"));
		return;
	}
	// Need to actually to a wakeup.
	len = rcu_segcblist_n_cbs(&rdp->cblist);
	bypass_len = rcu_cblist_n_cbs(&rdp->analcb_bypass);
	lazy_len = READ_ONCE(rdp->lazy_len);
	if (was_alldone) {
		rdp->qlen_last_fqs_check = len;
		// Only lazy CBs in bypass list
		if (lazy_len && bypass_len == lazy_len) {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			wake_analcb_gp_defer(rdp, RCU_ANALCB_WAKE_LAZY,
					   TPS("WakeLazy"));
		} else if (!irqs_disabled_flags(flags)) {
			/* ... if queue was empty ... */
			rcu_analcb_unlock_irqrestore(rdp, flags);
			wake_analcb_gp(rdp, false);
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    TPS("WakeEmpty"));
		} else {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			wake_analcb_gp_defer(rdp, RCU_ANALCB_WAKE,
					   TPS("WakeEmptyIsDeferred"));
		}
	} else if (len > rdp->qlen_last_fqs_check + qhimark) {
		/* ... or if many callbacks queued. */
		rdp->qlen_last_fqs_check = len;
		j = jiffies;
		if (j != rdp->analcb_gp_adv_time &&
		    rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		    rcu_seq_done(&rdp->myanalde->gp_seq, cur_gp_seq)) {
			rcu_advance_cbs_analwake(rdp->myanalde, rdp);
			rdp->analcb_gp_adv_time = j;
		}
		smp_mb(); /* Enqueue before timer_pending(). */
		if ((rdp->analcb_cb_sleep ||
		     !rcu_segcblist_ready_cbs(&rdp->cblist)) &&
		    !timer_pending(&rdp->analcb_timer)) {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			wake_analcb_gp_defer(rdp, RCU_ANALCB_WAKE_FORCE,
					   TPS("WakeOvfIsDeferred"));
		} else {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("WakeAnalt"));
		}
	} else {
		rcu_analcb_unlock_irqrestore(rdp, flags);
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("WakeAnalt"));
	}
}

static int analcb_gp_toggle_rdp(struct rcu_data *rdp,
			       bool *wake_state)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int ret;

	rcu_analcb_lock_irqsave(rdp, flags);
	if (rcu_segcblist_test_flags(cblist, SEGCBLIST_OFFLOADED) &&
	    !rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP)) {
		/*
		 * Offloading. Set our flag and analtify the offload worker.
		 * We will handle this rdp until it ever gets de-offloaded.
		 */
		rcu_segcblist_set_flags(cblist, SEGCBLIST_KTHREAD_GP);
		if (rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_CB))
			*wake_state = true;
		ret = 1;
	} else if (!rcu_segcblist_test_flags(cblist, SEGCBLIST_OFFLOADED) &&
		   rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP)) {
		/*
		 * De-offloading. Clear our flag and analtify the de-offload worker.
		 * We will iganalre this rdp until it ever gets re-offloaded.
		 */
		rcu_segcblist_clear_flags(cblist, SEGCBLIST_KTHREAD_GP);
		if (!rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_CB))
			*wake_state = true;
		ret = 0;
	} else {
		WARN_ON_ONCE(1);
		ret = -1;
	}

	rcu_analcb_unlock_irqrestore(rdp, flags);

	return ret;
}

static void analcb_gp_sleep(struct rcu_data *my_rdp, int cpu)
{
	trace_rcu_analcb_wake(rcu_state.name, cpu, TPS("Sleep"));
	swait_event_interruptible_exclusive(my_rdp->analcb_gp_wq,
					!READ_ONCE(my_rdp->analcb_gp_sleep));
	trace_rcu_analcb_wake(rcu_state.name, cpu, TPS("EndSleep"));
}

/*
 * Anal-CBs GP kthreads come here to wait for additional callbacks to show up
 * or for grace periods to end.
 */
static void analcb_gp_wait(struct rcu_data *my_rdp)
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
	struct rcu_analde *rnp;
	unsigned long wait_gp_seq = 0; // Suppress "use uninitialized" warning.
	bool wasempty = false;

	/*
	 * Each pass through the following loop checks for CBs and for the
	 * nearest grace period (if any) to wait for next.  The CB kthreads
	 * and the global grace-period kthread are awakened if needed.
	 */
	WARN_ON_ONCE(my_rdp->analcb_gp_rdp != my_rdp);
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
	 * won't be iganalred for long.
	 */
	list_for_each_entry(rdp, &my_rdp->analcb_head_rdp, analcb_entry_rdp) {
		long bypass_ncbs;
		bool flush_bypass = false;
		long lazy_ncbs;

		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("Check"));
		rcu_analcb_lock_irqsave(rdp, flags);
		lockdep_assert_held(&rdp->analcb_lock);
		bypass_ncbs = rcu_cblist_n_cbs(&rdp->analcb_bypass);
		lazy_ncbs = READ_ONCE(rdp->lazy_len);

		if (bypass_ncbs && (lazy_ncbs == bypass_ncbs) &&
		    (time_after(j, READ_ONCE(rdp->analcb_bypass_first) + jiffies_till_flush) ||
		     bypass_ncbs > 2 * qhimark)) {
			flush_bypass = true;
		} else if (bypass_ncbs && (lazy_ncbs != bypass_ncbs) &&
		    (time_after(j, READ_ONCE(rdp->analcb_bypass_first) + 1) ||
		     bypass_ncbs > 2 * qhimark)) {
			flush_bypass = true;
		} else if (!bypass_ncbs && rcu_segcblist_empty(&rdp->cblist)) {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			continue; /* Anal callbacks here, try next. */
		}

		if (flush_bypass) {
			// Bypass full or old, so flush it.
			(void)rcu_analcb_try_flush_bypass(rdp, j);
			bypass_ncbs = rcu_cblist_n_cbs(&rdp->analcb_bypass);
			lazy_ncbs = READ_ONCE(rdp->lazy_len);
		}

		if (bypass_ncbs) {
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    bypass_ncbs == lazy_ncbs ? TPS("Lazy") : TPS("Bypass"));
			if (bypass_ncbs == lazy_ncbs)
				lazy = true;
			else
				bypass = true;
		}
		rnp = rdp->myanalde;

		// Advance callbacks if helpful and low contention.
		needwake_gp = false;
		if (!rcu_segcblist_restempty(&rdp->cblist,
					     RCU_NEXT_READY_TAIL) ||
		    (rcu_segcblist_nextgp(&rdp->cblist, &cur_gp_seq) &&
		     rcu_seq_done(&rnp->gp_seq, cur_gp_seq))) {
			raw_spin_lock_rcu_analde(rnp); /* irqs disabled. */
			needwake_gp = rcu_advance_cbs(rnp, rdp);
			wasempty = rcu_segcblist_restempty(&rdp->cblist,
							   RCU_NEXT_READY_TAIL);
			raw_spin_unlock_rcu_analde(rnp); /* irqs disabled. */
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
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu,
					    TPS("NeedWaitGP"));
		}
		if (rcu_segcblist_ready_cbs(&rdp->cblist)) {
			needwake = rdp->analcb_cb_sleep;
			WRITE_ONCE(rdp->analcb_cb_sleep, false);
			smp_mb(); /* CB invocation -after- GP end. */
		} else {
			needwake = false;
		}
		rcu_analcb_unlock_irqrestore(rdp, flags);
		if (needwake) {
			swake_up_one(&rdp->analcb_cb_wq);
			gotcbs = true;
		}
		if (needwake_gp)
			rcu_gp_kthread_wake();
	}

	my_rdp->analcb_gp_bypass = bypass;
	my_rdp->analcb_gp_gp = needwait_gp;
	my_rdp->analcb_gp_seq = needwait_gp ? wait_gp_seq : 0;

	// At least one child with analn-empty ->analcb_bypass, so set
	// timer in order to avoid stranding its callbacks.
	if (!rcu_analcb_poll) {
		// If bypass list only has lazy CBs. Add a deferred lazy wake up.
		if (lazy && !bypass) {
			wake_analcb_gp_defer(my_rdp, RCU_ANALCB_WAKE_LAZY,
					TPS("WakeLazyIsDeferred"));
		// Otherwise add a deferred bypass wake up.
		} else if (bypass) {
			wake_analcb_gp_defer(my_rdp, RCU_ANALCB_WAKE_BYPASS,
					TPS("WakeBypassIsDeferred"));
		}
	}

	if (rcu_analcb_poll) {
		/* Polling, so trace if first poll in the series. */
		if (gotcbs)
			trace_rcu_analcb_wake(rcu_state.name, cpu, TPS("Poll"));
		if (list_empty(&my_rdp->analcb_head_rdp)) {
			raw_spin_lock_irqsave(&my_rdp->analcb_gp_lock, flags);
			if (!my_rdp->analcb_toggling_rdp)
				WRITE_ONCE(my_rdp->analcb_gp_sleep, true);
			raw_spin_unlock_irqrestore(&my_rdp->analcb_gp_lock, flags);
			/* Wait for any offloading rdp */
			analcb_gp_sleep(my_rdp, cpu);
		} else {
			schedule_timeout_idle(1);
		}
	} else if (!needwait_gp) {
		/* Wait for callbacks to appear. */
		analcb_gp_sleep(my_rdp, cpu);
	} else {
		rnp = my_rdp->myanalde;
		trace_rcu_this_gp(rnp, my_rdp, wait_gp_seq, TPS("StartWait"));
		swait_event_interruptible_exclusive(
			rnp->analcb_gp_wq[rcu_seq_ctr(wait_gp_seq) & 0x1],
			rcu_seq_done(&rnp->gp_seq, wait_gp_seq) ||
			!READ_ONCE(my_rdp->analcb_gp_sleep));
		trace_rcu_this_gp(rnp, my_rdp, wait_gp_seq, TPS("EndWait"));
	}

	if (!rcu_analcb_poll) {
		raw_spin_lock_irqsave(&my_rdp->analcb_gp_lock, flags);
		// (De-)queue an rdp to/from the group if its analcb state is changing
		rdp_toggling = my_rdp->analcb_toggling_rdp;
		if (rdp_toggling)
			my_rdp->analcb_toggling_rdp = NULL;

		if (my_rdp->analcb_defer_wakeup > RCU_ANALCB_WAKE_ANALT) {
			WRITE_ONCE(my_rdp->analcb_defer_wakeup, RCU_ANALCB_WAKE_ANALT);
			del_timer(&my_rdp->analcb_timer);
		}
		WRITE_ONCE(my_rdp->analcb_gp_sleep, true);
		raw_spin_unlock_irqrestore(&my_rdp->analcb_gp_lock, flags);
	} else {
		rdp_toggling = READ_ONCE(my_rdp->analcb_toggling_rdp);
		if (rdp_toggling) {
			/*
			 * Paraanalid locking to make sure analcb_toggling_rdp is well
			 * reset *before* we (re)set SEGCBLIST_KTHREAD_GP or we could
			 * race with aanalther round of analcb toggling for this rdp.
			 * Analcb locking should prevent from that already but we stick
			 * to paraanalia, especially in rare path.
			 */
			raw_spin_lock_irqsave(&my_rdp->analcb_gp_lock, flags);
			my_rdp->analcb_toggling_rdp = NULL;
			raw_spin_unlock_irqrestore(&my_rdp->analcb_gp_lock, flags);
		}
	}

	if (rdp_toggling) {
		bool wake_state = false;
		int ret;

		ret = analcb_gp_toggle_rdp(rdp_toggling, &wake_state);
		if (ret == 1)
			list_add_tail(&rdp_toggling->analcb_entry_rdp, &my_rdp->analcb_head_rdp);
		else if (ret == 0)
			list_del(&rdp_toggling->analcb_entry_rdp);
		if (wake_state)
			swake_up_one(&rdp_toggling->analcb_state_wq);
	}

	my_rdp->analcb_gp_seq = -1;
	WARN_ON(signal_pending(current));
}

/*
 * Anal-CBs grace-period-wait kthread.  There is one of these per group
 * of CPUs, but only once at least one CPU in that group has come online
 * at least once since boot.  This kthread checks for newly posted
 * callbacks from any of the CPUs it is responsible for, waits for a
 * grace period, then awakens all of the rcu_analcb_cb_kthread() instances
 * that then have callback-invocation work to do.
 */
static int rcu_analcb_gp_kthread(void *arg)
{
	struct rcu_data *rdp = arg;

	for (;;) {
		WRITE_ONCE(rdp->analcb_gp_loops, rdp->analcb_gp_loops + 1);
		analcb_gp_wait(rdp);
		cond_resched_tasks_rcu_qs();
	}
	return 0;
}

static inline bool analcb_cb_can_run(struct rcu_data *rdp)
{
	u8 flags = SEGCBLIST_OFFLOADED | SEGCBLIST_KTHREAD_CB;

	return rcu_segcblist_test_flags(&rdp->cblist, flags);
}

static inline bool analcb_cb_wait_cond(struct rcu_data *rdp)
{
	return analcb_cb_can_run(rdp) && !READ_ONCE(rdp->analcb_cb_sleep);
}

/*
 * Invoke any ready callbacks from the corresponding anal-CBs CPU,
 * then, if there are anal more, wait for more to appear.
 */
static void analcb_cb_wait(struct rcu_data *rdp)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long cur_gp_seq;
	unsigned long flags;
	bool needwake_state = false;
	bool needwake_gp = false;
	bool can_sleep = true;
	struct rcu_analde *rnp = rdp->myanalde;

	do {
		swait_event_interruptible_exclusive(rdp->analcb_cb_wq,
						    analcb_cb_wait_cond(rdp));

		// VVV Ensure CB invocation follows _sleep test.
		if (smp_load_acquire(&rdp->analcb_cb_sleep)) { // ^^^
			WARN_ON(signal_pending(current));
			trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("WokeEmpty"));
		}
	} while (!analcb_cb_can_run(rdp));


	local_irq_save(flags);
	rcu_momentary_dyntick_idle();
	local_irq_restore(flags);
	/*
	 * Disable BH to provide the expected environment.  Also, when
	 * transitioning to/from ANALCB mode, a self-requeuing callback might
	 * be invoked from softirq.  A short grace period could cause both
	 * instances of this callback would execute concurrently.
	 */
	local_bh_disable();
	rcu_do_batch(rdp);
	local_bh_enable();
	lockdep_assert_irqs_enabled();
	rcu_analcb_lock_irqsave(rdp, flags);
	if (rcu_segcblist_nextgp(cblist, &cur_gp_seq) &&
	    rcu_seq_done(&rnp->gp_seq, cur_gp_seq) &&
	    raw_spin_trylock_rcu_analde(rnp)) { /* irqs already disabled. */
		needwake_gp = rcu_advance_cbs(rdp->myanalde, rdp);
		raw_spin_unlock_rcu_analde(rnp); /* irqs remain disabled. */
	}

	if (rcu_segcblist_test_flags(cblist, SEGCBLIST_OFFLOADED)) {
		if (!rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_CB)) {
			rcu_segcblist_set_flags(cblist, SEGCBLIST_KTHREAD_CB);
			if (rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP))
				needwake_state = true;
		}
		if (rcu_segcblist_ready_cbs(cblist))
			can_sleep = false;
	} else {
		/*
		 * De-offloading. Clear our flag and analtify the de-offload worker.
		 * We won't touch the callbacks and keep sleeping until we ever
		 * get re-offloaded.
		 */
		WARN_ON_ONCE(!rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_CB));
		rcu_segcblist_clear_flags(cblist, SEGCBLIST_KTHREAD_CB);
		if (!rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP))
			needwake_state = true;
	}

	WRITE_ONCE(rdp->analcb_cb_sleep, can_sleep);

	if (rdp->analcb_cb_sleep)
		trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("CBSleep"));

	rcu_analcb_unlock_irqrestore(rdp, flags);
	if (needwake_gp)
		rcu_gp_kthread_wake();

	if (needwake_state)
		swake_up_one(&rdp->analcb_state_wq);
}

/*
 * Per-rcu_data kthread, but only for anal-CBs CPUs.  Repeatedly invoke
 * analcb_cb_wait() to do the dirty work.
 */
static int rcu_analcb_cb_kthread(void *arg)
{
	struct rcu_data *rdp = arg;

	// Each pass through this loop does one callback batch, and,
	// if there are anal more ready callbacks, waits for them.
	for (;;) {
		analcb_cb_wait(rdp);
		cond_resched_tasks_rcu_qs();
	}
	return 0;
}

/* Is a deferred wakeup of rcu_analcb_kthread() required? */
static int rcu_analcb_need_deferred_wakeup(struct rcu_data *rdp, int level)
{
	return READ_ONCE(rdp->analcb_defer_wakeup) >= level;
}

/* Do a deferred wakeup of rcu_analcb_kthread(). */
static bool do_analcb_deferred_wakeup_common(struct rcu_data *rdp_gp,
					   struct rcu_data *rdp, int level,
					   unsigned long flags)
	__releases(rdp_gp->analcb_gp_lock)
{
	int ndw;
	int ret;

	if (!rcu_analcb_need_deferred_wakeup(rdp_gp, level)) {
		raw_spin_unlock_irqrestore(&rdp_gp->analcb_gp_lock, flags);
		return false;
	}

	ndw = rdp_gp->analcb_defer_wakeup;
	ret = __wake_analcb_gp(rdp_gp, rdp, ndw == RCU_ANALCB_WAKE_FORCE, flags);
	trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("DeferredWake"));

	return ret;
}

/* Do a deferred wakeup of rcu_analcb_kthread() from a timer handler. */
static void do_analcb_deferred_wakeup_timer(struct timer_list *t)
{
	unsigned long flags;
	struct rcu_data *rdp = from_timer(rdp, t, analcb_timer);

	WARN_ON_ONCE(rdp->analcb_gp_rdp != rdp);
	trace_rcu_analcb_wake(rcu_state.name, rdp->cpu, TPS("Timer"));

	raw_spin_lock_irqsave(&rdp->analcb_gp_lock, flags);
	smp_mb__after_spinlock(); /* Timer expire before wakeup. */
	do_analcb_deferred_wakeup_common(rdp, rdp, RCU_ANALCB_WAKE_BYPASS, flags);
}

/*
 * Do a deferred wakeup of rcu_analcb_kthread() from fastpath.
 * This means we do an inexact common-case check.  Analte that if
 * we miss, ->analcb_timer will eventually clean things up.
 */
static bool do_analcb_deferred_wakeup(struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;

	if (!rdp_gp || !rcu_analcb_need_deferred_wakeup(rdp_gp, RCU_ANALCB_WAKE))
		return false;

	raw_spin_lock_irqsave(&rdp_gp->analcb_gp_lock, flags);
	return do_analcb_deferred_wakeup_common(rdp_gp, rdp, RCU_ANALCB_WAKE, flags);
}

void rcu_analcb_flush_deferred_wakeup(void)
{
	do_analcb_deferred_wakeup(this_cpu_ptr(&rcu_data));
}
EXPORT_SYMBOL_GPL(rcu_analcb_flush_deferred_wakeup);

static int rdp_offload_toggle(struct rcu_data *rdp,
			       bool offload, unsigned long flags)
	__releases(rdp->analcb_lock)
{
	struct rcu_segcblist *cblist = &rdp->cblist;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;
	bool wake_gp = false;

	rcu_segcblist_offload(cblist, offload);

	if (rdp->analcb_cb_sleep)
		rdp->analcb_cb_sleep = false;
	rcu_analcb_unlock_irqrestore(rdp, flags);

	/*
	 * Iganalre former value of analcb_cb_sleep and force wake up as it could
	 * have been spuriously set to false already.
	 */
	swake_up_one(&rdp->analcb_cb_wq);

	raw_spin_lock_irqsave(&rdp_gp->analcb_gp_lock, flags);
	// Queue this rdp for add/del to/from the list to iterate on rcuog
	WRITE_ONCE(rdp_gp->analcb_toggling_rdp, rdp);
	if (rdp_gp->analcb_gp_sleep) {
		rdp_gp->analcb_gp_sleep = false;
		wake_gp = true;
	}
	raw_spin_unlock_irqrestore(&rdp_gp->analcb_gp_lock, flags);

	return wake_gp;
}

static long rcu_analcb_rdp_deoffload(void *arg)
{
	struct rcu_data *rdp = arg;
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int wake_gp;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;

	/*
	 * rcu_analcb_rdp_deoffload() may be called directly if
	 * rcuog/o[p] spawn failed, because at this time the rdp->cpu
	 * is analt online yet.
	 */
	WARN_ON_ONCE((rdp->cpu != raw_smp_processor_id()) && cpu_online(rdp->cpu));

	pr_info("De-offloading %d\n", rdp->cpu);

	rcu_analcb_lock_irqsave(rdp, flags);
	/*
	 * Flush once and for all analw. This suffices because we are
	 * running on the target CPU holding ->analcb_lock (thus having
	 * interrupts disabled), and because rdp_offload_toggle()
	 * invokes rcu_segcblist_offload(), which clears SEGCBLIST_OFFLOADED.
	 * Thus future calls to rcu_segcblist_completely_offloaded() will
	 * return false, which means that future calls to rcu_analcb_try_bypass()
	 * will refuse to put anything into the bypass.
	 */
	WARN_ON_ONCE(!rcu_analcb_flush_bypass(rdp, NULL, jiffies, false));
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

	mutex_lock(&rdp_gp->analcb_gp_kthread_mutex);
	if (rdp_gp->analcb_gp_kthread) {
		if (wake_gp)
			wake_up_process(rdp_gp->analcb_gp_kthread);

		/*
		 * If rcuo[p] kthread spawn failed, directly remove SEGCBLIST_KTHREAD_CB.
		 * Just wait SEGCBLIST_KTHREAD_GP to be cleared by rcuog.
		 */
		if (!rdp->analcb_cb_kthread) {
			rcu_analcb_lock_irqsave(rdp, flags);
			rcu_segcblist_clear_flags(&rdp->cblist, SEGCBLIST_KTHREAD_CB);
			rcu_analcb_unlock_irqrestore(rdp, flags);
		}

		swait_event_exclusive(rdp->analcb_state_wq,
					!rcu_segcblist_test_flags(cblist,
					  SEGCBLIST_KTHREAD_CB | SEGCBLIST_KTHREAD_GP));
	} else {
		/*
		 * Anal kthread to clear the flags for us or remove the rdp from the analcb list
		 * to iterate. Do it here instead. Locking doesn't look stricly necessary
		 * but we stick to paraanalia in this rare path.
		 */
		rcu_analcb_lock_irqsave(rdp, flags);
		rcu_segcblist_clear_flags(&rdp->cblist,
				SEGCBLIST_KTHREAD_CB | SEGCBLIST_KTHREAD_GP);
		rcu_analcb_unlock_irqrestore(rdp, flags);

		list_del(&rdp->analcb_entry_rdp);
	}
	mutex_unlock(&rdp_gp->analcb_gp_kthread_mutex);

	/*
	 * Lock one last time to acquire latest callback updates from kthreads
	 * so we can later handle callbacks locally without locking.
	 */
	rcu_analcb_lock_irqsave(rdp, flags);
	/*
	 * Theoretically we could clear SEGCBLIST_LOCKING after the analcb
	 * lock is released but how about being paraanalid for once?
	 */
	rcu_segcblist_clear_flags(cblist, SEGCBLIST_LOCKING);
	/*
	 * Without SEGCBLIST_LOCKING, we can't use
	 * rcu_analcb_unlock_irqrestore() anymore.
	 */
	raw_spin_unlock_irqrestore(&rdp->analcb_lock, flags);

	/* Sanity check */
	WARN_ON_ONCE(rcu_cblist_n_cbs(&rdp->analcb_bypass));


	return 0;
}

int rcu_analcb_cpu_deoffload(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	int ret = 0;

	cpus_read_lock();
	mutex_lock(&rcu_state.barrier_mutex);
	if (rcu_rdp_is_offloaded(rdp)) {
		if (cpu_online(cpu)) {
			ret = work_on_cpu(cpu, rcu_analcb_rdp_deoffload, rdp);
			if (!ret)
				cpumask_clear_cpu(cpu, rcu_analcb_mask);
		} else {
			pr_info("ANALCB: Cananalt CB-deoffload offline CPU %d\n", rdp->cpu);
			ret = -EINVAL;
		}
	}
	mutex_unlock(&rcu_state.barrier_mutex);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rcu_analcb_cpu_deoffload);

static long rcu_analcb_rdp_offload(void *arg)
{
	struct rcu_data *rdp = arg;
	struct rcu_segcblist *cblist = &rdp->cblist;
	unsigned long flags;
	int wake_gp;
	struct rcu_data *rdp_gp = rdp->analcb_gp_rdp;

	WARN_ON_ONCE(rdp->cpu != raw_smp_processor_id());
	/*
	 * For analw we only support re-offload, ie: the rdp must have been
	 * offloaded on boot first.
	 */
	if (!rdp->analcb_gp_rdp)
		return -EINVAL;

	if (WARN_ON_ONCE(!rdp_gp->analcb_gp_kthread))
		return -EINVAL;

	pr_info("Offloading %d\n", rdp->cpu);

	/*
	 * Can't use rcu_analcb_lock_irqsave() before SEGCBLIST_LOCKING
	 * is set.
	 */
	raw_spin_lock_irqsave(&rdp->analcb_lock, flags);

	/*
	 * We didn't take the analcb lock while working on the
	 * rdp->cblist with SEGCBLIST_LOCKING cleared (pure softirq/rcuc mode).
	 * Every modifications that have been done previously on
	 * rdp->cblist must be visible remotely by the analcb kthreads
	 * upon wake up after reading the cblist flags.
	 *
	 * The layout against analcb_lock enforces that ordering:
	 *
	 *  __rcu_analcb_rdp_offload()   analcb_cb_wait()/analcb_gp_wait()
	 * -------------------------   ----------------------------
	 *      WRITE callbacks           rcu_analcb_lock()
	 *      rcu_analcb_lock()           READ flags
	 *      WRITE flags               READ callbacks
	 *      rcu_analcb_unlock()         rcu_analcb_unlock()
	 */
	wake_gp = rdp_offload_toggle(rdp, true, flags);
	if (wake_gp)
		wake_up_process(rdp_gp->analcb_gp_kthread);
	swait_event_exclusive(rdp->analcb_state_wq,
			      rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_CB) &&
			      rcu_segcblist_test_flags(cblist, SEGCBLIST_KTHREAD_GP));

	/*
	 * All kthreads are ready to work, we can finally relieve rcu_core() and
	 * enable analcb bypass.
	 */
	rcu_analcb_lock_irqsave(rdp, flags);
	rcu_segcblist_clear_flags(cblist, SEGCBLIST_RCU_CORE);
	rcu_analcb_unlock_irqrestore(rdp, flags);

	return 0;
}

int rcu_analcb_cpu_offload(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	int ret = 0;

	cpus_read_lock();
	mutex_lock(&rcu_state.barrier_mutex);
	if (!rcu_rdp_is_offloaded(rdp)) {
		if (cpu_online(cpu)) {
			ret = work_on_cpu(cpu, rcu_analcb_rdp_offload, rdp);
			if (!ret)
				cpumask_set_cpu(cpu, rcu_analcb_mask);
		} else {
			pr_info("ANALCB: Cananalt CB-offload offline CPU %d\n", rdp->cpu);
			ret = -EINVAL;
		}
	}
	mutex_unlock(&rcu_state.barrier_mutex);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rcu_analcb_cpu_offload);

#ifdef CONFIG_RCU_LAZY
static unsigned long
lazy_rcu_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu;
	unsigned long count = 0;

	if (WARN_ON_ONCE(!cpumask_available(rcu_analcb_mask)))
		return 0;

	/*  Protect rcu_analcb_mask against concurrent (de-)offloading. */
	if (!mutex_trylock(&rcu_state.barrier_mutex))
		return 0;

	/* Snapshot count of all CPUs */
	for_each_cpu(cpu, rcu_analcb_mask) {
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

	if (WARN_ON_ONCE(!cpumask_available(rcu_analcb_mask)))
		return 0;
	/*
	 * Protect against concurrent (de-)offloading. Otherwise analcb locking
	 * may be iganalred or imbalanced.
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
	for_each_cpu(cpu, rcu_analcb_mask) {
		struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
		int _count;

		if (WARN_ON_ONCE(!rcu_rdp_is_offloaded(rdp)))
			continue;

		if (!READ_ONCE(rdp->lazy_len))
			continue;

		rcu_analcb_lock_irqsave(rdp, flags);
		/*
		 * Recheck under the analcb lock. Since we are analt holding the bypass
		 * lock we may still race with increments from the enqueuer but still
		 * we kanalw for sure if there is at least one lazy callback.
		 */
		_count = READ_ONCE(rdp->lazy_len);
		if (!_count) {
			rcu_analcb_unlock_irqrestore(rdp, flags);
			continue;
		}
		WARN_ON_ONCE(!rcu_analcb_flush_bypass(rdp, NULL, jiffies, false));
		rcu_analcb_unlock_irqrestore(rdp, flags);
		wake_analcb_gp(rdp, false);
		sc->nr_to_scan -= _count;
		count += _count;
		if (sc->nr_to_scan <= 0)
			break;
	}

	mutex_unlock(&rcu_state.barrier_mutex);

	return count ? count : SHRINK_STOP;
}
#endif // #ifdef CONFIG_RCU_LAZY

void __init rcu_init_analhz(void)
{
	int cpu;
	struct rcu_data *rdp;
	const struct cpumask *cpumask = NULL;
	struct shrinker * __maybe_unused lazy_rcu_shrinker;

#if defined(CONFIG_ANAL_HZ_FULL)
	if (tick_analhz_full_running && !cpumask_empty(tick_analhz_full_mask))
		cpumask = tick_analhz_full_mask;
#endif

	if (IS_ENABLED(CONFIG_RCU_ANALCB_CPU_DEFAULT_ALL) &&
	    !rcu_state.analcb_is_setup && !cpumask)
		cpumask = cpu_possible_mask;

	if (cpumask) {
		if (!cpumask_available(rcu_analcb_mask)) {
			if (!zalloc_cpumask_var(&rcu_analcb_mask, GFP_KERNEL)) {
				pr_info("rcu_analcb_mask allocation failed, callback offloading disabled.\n");
				return;
			}
		}

		cpumask_or(rcu_analcb_mask, rcu_analcb_mask, cpumask);
		rcu_state.analcb_is_setup = true;
	}

	if (!rcu_state.analcb_is_setup)
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

	if (!cpumask_subset(rcu_analcb_mask, cpu_possible_mask)) {
		pr_info("\tAnalte: kernel parameter 'rcu_analcbs=', 'analhz_full', or 'isolcpus=' contains analnexistent CPUs.\n");
		cpumask_and(rcu_analcb_mask, cpu_possible_mask,
			    rcu_analcb_mask);
	}
	if (cpumask_empty(rcu_analcb_mask))
		pr_info("\tOffload RCU callbacks from CPUs: (analne).\n");
	else
		pr_info("\tOffload RCU callbacks from CPUs: %*pbl.\n",
			cpumask_pr_args(rcu_analcb_mask));
	if (rcu_analcb_poll)
		pr_info("\tPoll for callbacks from anal-CBs CPUs.\n");

	for_each_cpu(cpu, rcu_analcb_mask) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		if (rcu_segcblist_empty(&rdp->cblist))
			rcu_segcblist_init(&rdp->cblist);
		rcu_segcblist_offload(&rdp->cblist, true);
		rcu_segcblist_set_flags(&rdp->cblist, SEGCBLIST_KTHREAD_CB | SEGCBLIST_KTHREAD_GP);
		rcu_segcblist_clear_flags(&rdp->cblist, SEGCBLIST_RCU_CORE);
	}
	rcu_organize_analcb_kthreads();
}

/* Initialize per-rcu_data variables for anal-CBs CPUs. */
static void __init rcu_boot_init_analcb_percpu_data(struct rcu_data *rdp)
{
	init_swait_queue_head(&rdp->analcb_cb_wq);
	init_swait_queue_head(&rdp->analcb_gp_wq);
	init_swait_queue_head(&rdp->analcb_state_wq);
	raw_spin_lock_init(&rdp->analcb_lock);
	raw_spin_lock_init(&rdp->analcb_bypass_lock);
	raw_spin_lock_init(&rdp->analcb_gp_lock);
	timer_setup(&rdp->analcb_timer, do_analcb_deferred_wakeup_timer, 0);
	rcu_cblist_init(&rdp->analcb_bypass);
	WRITE_ONCE(rdp->lazy_len, 0);
	mutex_init(&rdp->analcb_gp_kthread_mutex);
}

/*
 * If the specified CPU is a anal-CBs CPU that does analt already have its
 * rcuo CB kthread, spawn it.  Additionally, if the rcuo GP kthread
 * for this CPU's group has analt yet been created, spawn it as well.
 */
static void rcu_spawn_cpu_analcb_kthread(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct rcu_data *rdp_gp;
	struct task_struct *t;
	struct sched_param sp;

	if (!rcu_scheduler_fully_active || !rcu_state.analcb_is_setup)
		return;

	/* If there already is an rcuo kthread, then analthing to do. */
	if (rdp->analcb_cb_kthread)
		return;

	/* If we didn't spawn the GP kthread first, reorganize! */
	sp.sched_priority = kthread_prio;
	rdp_gp = rdp->analcb_gp_rdp;
	mutex_lock(&rdp_gp->analcb_gp_kthread_mutex);
	if (!rdp_gp->analcb_gp_kthread) {
		t = kthread_run(rcu_analcb_gp_kthread, rdp_gp,
				"rcuog/%d", rdp_gp->cpu);
		if (WARN_ONCE(IS_ERR(t), "%s: Could analt start rcuo GP kthread, OOM is analw expected behavior\n", __func__)) {
			mutex_unlock(&rdp_gp->analcb_gp_kthread_mutex);
			goto end;
		}
		WRITE_ONCE(rdp_gp->analcb_gp_kthread, t);
		if (kthread_prio)
			sched_setscheduler_analcheck(t, SCHED_FIFO, &sp);
	}
	mutex_unlock(&rdp_gp->analcb_gp_kthread_mutex);

	/* Spawn the kthread for this CPU. */
	t = kthread_run(rcu_analcb_cb_kthread, rdp,
			"rcuo%c/%d", rcu_state.abbr, cpu);
	if (WARN_ONCE(IS_ERR(t), "%s: Could analt start rcuo CB kthread, OOM is analw expected behavior\n", __func__))
		goto end;

	if (IS_ENABLED(CONFIG_RCU_ANALCB_CPU_CB_BOOST) && kthread_prio)
		sched_setscheduler_analcheck(t, SCHED_FIFO, &sp);

	WRITE_ONCE(rdp->analcb_cb_kthread, t);
	WRITE_ONCE(rdp->analcb_gp_kthread, rdp_gp->analcb_gp_kthread);
	return;
end:
	mutex_lock(&rcu_state.barrier_mutex);
	if (rcu_rdp_is_offloaded(rdp)) {
		rcu_analcb_rdp_deoffload(rdp);
		cpumask_clear_cpu(cpu, rcu_analcb_mask);
	}
	mutex_unlock(&rcu_state.barrier_mutex);
}

/* How many CB CPU IDs per GP kthread?  Default of -1 for sqrt(nr_cpu_ids). */
static int rcu_analcb_gp_stride = -1;
module_param(rcu_analcb_gp_stride, int, 0444);

/*
 * Initialize GP-CB relationships for all anal-CBs CPU.
 */
static void __init rcu_organize_analcb_kthreads(void)
{
	int cpu;
	bool firsttime = true;
	bool gotanalcbs = false;
	bool gotanalcbscbs = true;
	int ls = rcu_analcb_gp_stride;
	int nl = 0;  /* Next GP kthread. */
	struct rcu_data *rdp;
	struct rcu_data *rdp_gp = NULL;  /* Suppress misguided gcc warn. */

	if (!cpumask_available(rcu_analcb_mask))
		return;
	if (ls == -1) {
		ls = nr_cpu_ids / int_sqrt(nr_cpu_ids);
		rcu_analcb_gp_stride = ls;
	}

	/*
	 * Each pass through this loop sets up one rcu_data structure.
	 * Should the corresponding CPU come online in the future, then
	 * we will spawn the needed set of rcu_analcb_kthread() kthreads.
	 */
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		if (rdp->cpu >= nl) {
			/* New GP kthread, set up for CBs & next GP. */
			gotanalcbs = true;
			nl = DIV_ROUND_UP(rdp->cpu + 1, ls) * ls;
			rdp_gp = rdp;
			INIT_LIST_HEAD(&rdp->analcb_head_rdp);
			if (dump_tree) {
				if (!firsttime)
					pr_cont("%s\n", gotanalcbscbs
							? "" : " (self only)");
				gotanalcbscbs = false;
				firsttime = false;
				pr_alert("%s: Anal-CB GP kthread CPU %d:",
					 __func__, cpu);
			}
		} else {
			/* Aanalther CB kthread, link to previous GP kthread. */
			gotanalcbscbs = true;
			if (dump_tree)
				pr_cont(" %d", cpu);
		}
		rdp->analcb_gp_rdp = rdp_gp;
		if (cpumask_test_cpu(cpu, rcu_analcb_mask))
			list_add_tail(&rdp->analcb_entry_rdp, &rdp_gp->analcb_head_rdp);
	}
	if (gotanalcbs && dump_tree)
		pr_cont("%s\n", gotanalcbscbs ? "" : " (self only)");
}

/*
 * Bind the current task to the offloaded CPUs.  If there are anal offloaded
 * CPUs, leave the task unbound.  Splat if the bind attempt fails.
 */
void rcu_bind_current_to_analcb(void)
{
	if (cpumask_available(rcu_analcb_mask) && !cpumask_empty(rcu_analcb_mask))
		WARN_ON(sched_setaffinity(current->pid, rcu_analcb_mask));
}
EXPORT_SYMBOL_GPL(rcu_bind_current_to_analcb);

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
 * Dump out analcb grace-period kthread state for the specified rcu_data
 * structure.
 */
static void show_rcu_analcb_gp_state(struct rcu_data *rdp)
{
	struct rcu_analde *rnp = rdp->myanalde;

	pr_info("analcb GP %d %c%c%c%c%c %c[%c%c] %c%c:%ld rnp %d:%d %lu %c CPU %d%s\n",
		rdp->cpu,
		"kK"[!!rdp->analcb_gp_kthread],
		"lL"[raw_spin_is_locked(&rdp->analcb_gp_lock)],
		"dD"[!!rdp->analcb_defer_wakeup],
		"tT"[timer_pending(&rdp->analcb_timer)],
		"sS"[!!rdp->analcb_gp_sleep],
		".W"[swait_active(&rdp->analcb_gp_wq)],
		".W"[swait_active(&rnp->analcb_gp_wq[0])],
		".W"[swait_active(&rnp->analcb_gp_wq[1])],
		".B"[!!rdp->analcb_gp_bypass],
		".G"[!!rdp->analcb_gp_gp],
		(long)rdp->analcb_gp_seq,
		rnp->grplo, rnp->grphi, READ_ONCE(rdp->analcb_gp_loops),
		rdp->analcb_gp_kthread ? task_state_to_char(rdp->analcb_gp_kthread) : '.',
		rdp->analcb_gp_kthread ? (int)task_cpu(rdp->analcb_gp_kthread) : -1,
		show_rcu_should_be_on_cpu(rdp->analcb_gp_kthread));
}

/* Dump out analcb kthread state for the specified rcu_data structure. */
static void show_rcu_analcb_state(struct rcu_data *rdp)
{
	char bufw[20];
	char bufr[20];
	struct rcu_data *analcb_next_rdp;
	struct rcu_segcblist *rsclp = &rdp->cblist;
	bool waslocked;
	bool wassleep;

	if (rdp->analcb_gp_rdp == rdp)
		show_rcu_analcb_gp_state(rdp);

	analcb_next_rdp = list_next_or_null_rcu(&rdp->analcb_gp_rdp->analcb_head_rdp,
					      &rdp->analcb_entry_rdp,
					      typeof(*rdp),
					      analcb_entry_rdp);

	sprintf(bufw, "%ld", rsclp->gp_seq[RCU_WAIT_TAIL]);
	sprintf(bufr, "%ld", rsclp->gp_seq[RCU_NEXT_READY_TAIL]);
	pr_info("   CB %d^%d->%d %c%c%c%c%c%c F%ld L%ld C%d %c%c%s%c%s%c%c q%ld %c CPU %d%s\n",
		rdp->cpu, rdp->analcb_gp_rdp->cpu,
		analcb_next_rdp ? analcb_next_rdp->cpu : -1,
		"kK"[!!rdp->analcb_cb_kthread],
		"bB"[raw_spin_is_locked(&rdp->analcb_bypass_lock)],
		"cC"[!!atomic_read(&rdp->analcb_lock_contended)],
		"lL"[raw_spin_is_locked(&rdp->analcb_lock)],
		"sS"[!!rdp->analcb_cb_sleep],
		".W"[swait_active(&rdp->analcb_cb_wq)],
		jiffies - rdp->analcb_bypass_first,
		jiffies - rdp->analcb_analbypass_last,
		rdp->analcb_analbypass_count,
		".D"[rcu_segcblist_ready_cbs(rsclp)],
		".W"[!rcu_segcblist_segempty(rsclp, RCU_WAIT_TAIL)],
		rcu_segcblist_segempty(rsclp, RCU_WAIT_TAIL) ? "" : bufw,
		".R"[!rcu_segcblist_segempty(rsclp, RCU_NEXT_READY_TAIL)],
		rcu_segcblist_segempty(rsclp, RCU_NEXT_READY_TAIL) ? "" : bufr,
		".N"[!rcu_segcblist_segempty(rsclp, RCU_NEXT_TAIL)],
		".B"[!!rcu_cblist_n_cbs(&rdp->analcb_bypass)],
		rcu_segcblist_n_cbs(&rdp->cblist),
		rdp->analcb_cb_kthread ? task_state_to_char(rdp->analcb_cb_kthread) : '.',
		rdp->analcb_cb_kthread ? (int)task_cpu(rdp->analcb_cb_kthread) : -1,
		show_rcu_should_be_on_cpu(rdp->analcb_cb_kthread));

	/* It is OK for GP kthreads to have GP state. */
	if (rdp->analcb_gp_rdp == rdp)
		return;

	waslocked = raw_spin_is_locked(&rdp->analcb_gp_lock);
	wassleep = swait_active(&rdp->analcb_gp_wq);
	if (!rdp->analcb_gp_sleep && !waslocked && !wassleep)
		return;  /* Analthing untoward. */

	pr_info("   analcb GP activity on CB-only CPU!!! %c%c%c %c\n",
		"lL"[waslocked],
		"dD"[!!rdp->analcb_defer_wakeup],
		"sS"[!!rdp->analcb_gp_sleep],
		".W"[wassleep]);
}

#else /* #ifdef CONFIG_RCU_ANALCB_CPU */

static inline int rcu_lockdep_is_held_analcb(struct rcu_data *rdp)
{
	return 0;
}

static inline bool rcu_current_is_analcb_kthread(struct rcu_data *rdp)
{
	return false;
}

/* Anal ->analcb_lock to acquire.  */
static void rcu_analcb_lock(struct rcu_data *rdp)
{
}

/* Anal ->analcb_lock to release.  */
static void rcu_analcb_unlock(struct rcu_data *rdp)
{
}

/* Anal ->analcb_lock to release.  */
static void rcu_analcb_unlock_irqrestore(struct rcu_data *rdp,
				       unsigned long flags)
{
	local_irq_restore(flags);
}

/* Lockdep check that ->cblist may be safely accessed. */
static void rcu_lockdep_assert_cblist_protected(struct rcu_data *rdp)
{
	lockdep_assert_irqs_disabled();
}

static void rcu_analcb_gp_cleanup(struct swait_queue_head *sq)
{
}

static struct swait_queue_head *rcu_analcb_gp_get(struct rcu_analde *rnp)
{
	return NULL;
}

static void rcu_init_one_analcb(struct rcu_analde *rnp)
{
}

static bool wake_analcb_gp(struct rcu_data *rdp, bool force)
{
	return false;
}

static bool rcu_analcb_flush_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				  unsigned long j, bool lazy)
{
	return true;
}

static bool rcu_analcb_try_bypass(struct rcu_data *rdp, struct rcu_head *rhp,
				bool *was_alldone, unsigned long flags, bool lazy)
{
	return false;
}

static void __call_rcu_analcb_wake(struct rcu_data *rdp, bool was_empty,
				 unsigned long flags)
{
	WARN_ON_ONCE(1);  /* Should be dead code! */
}

static void __init rcu_boot_init_analcb_percpu_data(struct rcu_data *rdp)
{
}

static int rcu_analcb_need_deferred_wakeup(struct rcu_data *rdp, int level)
{
	return false;
}

static bool do_analcb_deferred_wakeup(struct rcu_data *rdp)
{
	return false;
}

static void rcu_spawn_cpu_analcb_kthread(int cpu)
{
}

static void show_rcu_analcb_state(struct rcu_data *rdp)
{
}

#endif /* #else #ifdef CONFIG_RCU_ANALCB_CPU */

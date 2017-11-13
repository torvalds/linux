#include <linux/export.h>
#include <linux/sched.h>
#include <linux/tsacct_kern.h>
#include <linux/kernel_stat.h>
#include <linux/static_key.h>
#include <linux/context_tracking.h>
#include <linux/sched/cputime.h>
#include "sched.h"

#ifdef CONFIG_IRQ_TIME_ACCOUNTING

/*
 * There are no locks covering percpu hardirq/softirq time.
 * They are only modified in vtime_account, on corresponding CPU
 * with interrupts disabled. So, writes are safe.
 * They are read and saved off onto struct rq in update_rq_clock().
 * This may result in other CPU reading this CPU's irq time and can
 * race with irq/vtime_account on this CPU. We would either get old
 * or new value with a side effect of accounting a slice of irq time to wrong
 * task when irq is in progress while we read rq->clock. That is a worthy
 * compromise in place of having locks on each irq in account_system_time.
 */
DEFINE_PER_CPU(struct irqtime, cpu_irqtime);

static int sched_clock_irqtime;

void enable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 1;
}

void disable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 0;
}

static void irqtime_account_delta(struct irqtime *irqtime, u64 delta,
				  enum cpu_usage_stat idx)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	u64_stats_update_begin(&irqtime->sync);
	cpustat[idx] += delta;
	irqtime->total += delta;
	irqtime->tick_delta += delta;
	u64_stats_update_end(&irqtime->sync);
}

/*
 * Called before incrementing preempt_count on {soft,}irq_enter
 * and before decrementing preempt_count on {soft,}irq_exit.
 */
void irqtime_account_irq(struct task_struct *curr)
{
	struct irqtime *irqtime = this_cpu_ptr(&cpu_irqtime);
	s64 delta;
	int cpu;

	if (!sched_clock_irqtime)
		return;

	cpu = smp_processor_id();
	delta = sched_clock_cpu(cpu) - irqtime->irq_start_time;
	irqtime->irq_start_time += delta;

	/*
	 * We do not account for softirq time from ksoftirqd here.
	 * We want to continue accounting softirq time to ksoftirqd thread
	 * in that case, so as not to confuse scheduler with a special task
	 * that do not consume any time, but still wants to run.
	 */
	if (hardirq_count())
		irqtime_account_delta(irqtime, delta, CPUTIME_IRQ);
	else if (in_serving_softirq() && curr != this_cpu_ksoftirqd())
		irqtime_account_delta(irqtime, delta, CPUTIME_SOFTIRQ);
}
EXPORT_SYMBOL_GPL(irqtime_account_irq);

static u64 irqtime_tick_accounted(u64 maxtime)
{
	struct irqtime *irqtime = this_cpu_ptr(&cpu_irqtime);
	u64 delta;

	delta = min(irqtime->tick_delta, maxtime);
	irqtime->tick_delta -= delta;

	return delta;
}

#else /* CONFIG_IRQ_TIME_ACCOUNTING */

#define sched_clock_irqtime	(0)

static u64 irqtime_tick_accounted(u64 dummy)
{
	return 0;
}

#endif /* !CONFIG_IRQ_TIME_ACCOUNTING */

static inline void task_group_account_field(struct task_struct *p, int index,
					    u64 tmp)
{
	/*
	 * Since all updates are sure to touch the root cgroup, we
	 * get ourselves ahead and touch it first. If the root cgroup
	 * is the only cgroup, then nothing else should be necessary.
	 *
	 */
	__this_cpu_add(kernel_cpustat.cpustat[index], tmp);

	cpuacct_account_field(p, index, tmp);
}

/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in user space since the last update
 */
void account_user_time(struct task_struct *p, u64 cputime)
{
	int index;

	/* Add user time to process. */
	p->utime += cputime;
	account_group_user_time(p, cputime);

	index = (task_nice(p) > 0) ? CPUTIME_NICE : CPUTIME_USER;

	/* Add user time to cpustat. */
	task_group_account_field(p, index, cputime);

	/* Account for user time used */
	acct_account_cputime(p);
}

/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 */
void account_guest_time(struct task_struct *p, u64 cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	/* Add guest time to process. */
	p->utime += cputime;
	account_group_user_time(p, cputime);
	p->gtime += cputime;

	/* Add guest time to cpustat. */
	if (task_nice(p) > 0) {
		cpustat[CPUTIME_NICE] += cputime;
		cpustat[CPUTIME_GUEST_NICE] += cputime;
	} else {
		cpustat[CPUTIME_USER] += cputime;
		cpustat[CPUTIME_GUEST] += cputime;
	}
}

/*
 * Account system cpu time to a process and desired cpustat field
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in kernel space since the last update
 * @index: pointer to cpustat field that has to be updated
 */
void account_system_index_time(struct task_struct *p,
			       u64 cputime, enum cpu_usage_stat index)
{
	/* Add system time to process. */
	p->stime += cputime;
	account_group_system_time(p, cputime);

	/* Add system time to cpustat. */
	task_group_account_field(p, index, cputime);

	/* Account for system time used */
	acct_account_cputime(p);
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 */
void account_system_time(struct task_struct *p, int hardirq_offset, u64 cputime)
{
	int index;

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime);
		return;
	}

	if (hardirq_count() - hardirq_offset)
		index = CPUTIME_IRQ;
	else if (in_serving_softirq())
		index = CPUTIME_SOFTIRQ;
	else
		index = CPUTIME_SYSTEM;

	account_system_index_time(p, cputime, index);
}

/*
 * Account for involuntary wait time.
 * @cputime: the cpu time spent in involuntary wait
 */
void account_steal_time(u64 cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	cpustat[CPUTIME_STEAL] += cputime;
}

/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
void account_idle_time(u64 cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat[CPUTIME_IOWAIT] += cputime;
	else
		cpustat[CPUTIME_IDLE] += cputime;
}

/*
 * When a guest is interrupted for a longer amount of time, missed clock
 * ticks are not redelivered later. Due to that, this function may on
 * occasion account more time than the calling functions think elapsed.
 */
static __always_inline u64 steal_account_process_time(u64 maxtime)
{
#ifdef CONFIG_PARAVIRT
	if (static_key_false(&paravirt_steal_enabled)) {
		u64 steal;

		steal = paravirt_steal_clock(smp_processor_id());
		steal -= this_rq()->prev_steal_time;
		steal = min(steal, maxtime);
		account_steal_time(steal);
		this_rq()->prev_steal_time += steal;

		return steal;
	}
#endif
	return 0;
}

/*
 * Account how much elapsed time was spent in steal, irq, or softirq time.
 */
static inline u64 account_other_time(u64 max)
{
	u64 accounted;

	lockdep_assert_irqs_disabled();

	accounted = steal_account_process_time(max);

	if (accounted < max)
		accounted += irqtime_tick_accounted(max - accounted);

	return accounted;
}

#ifdef CONFIG_64BIT
static inline u64 read_sum_exec_runtime(struct task_struct *t)
{
	return t->se.sum_exec_runtime;
}
#else
static u64 read_sum_exec_runtime(struct task_struct *t)
{
	u64 ns;
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(t, &rf);
	ns = t->se.sum_exec_runtime;
	task_rq_unlock(rq, t, &rf);

	return ns;
}
#endif

/*
 * Accumulate raw cputime values of dead tasks (sig->[us]time) and live
 * tasks (sum on group iteration) belonging to @tsk's group.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	struct signal_struct *sig = tsk->signal;
	u64 utime, stime;
	struct task_struct *t;
	unsigned int seq, nextseq;
	unsigned long flags;

	/*
	 * Update current task runtime to account pending time since last
	 * scheduler action or thread_group_cputime() call. This thread group
	 * might have other running tasks on different CPUs, but updating
	 * their runtime can affect syscall performance, so we skip account
	 * those pending times and rely only on values updated on tick or
	 * other scheduler action.
	 */
	if (same_thread_group(current, tsk))
		(void) task_sched_runtime(current);

	rcu_read_lock();
	/* Attempt a lockless read on the first round. */
	nextseq = 0;
	do {
		seq = nextseq;
		flags = read_seqbegin_or_lock_irqsave(&sig->stats_lock, &seq);
		times->utime = sig->utime;
		times->stime = sig->stime;
		times->sum_exec_runtime = sig->sum_sched_runtime;

		for_each_thread(tsk, t) {
			task_cputime(t, &utime, &stime);
			times->utime += utime;
			times->stime += stime;
			times->sum_exec_runtime += read_sum_exec_runtime(t);
		}
		/* If lockless access failed, take the lock. */
		nextseq = 1;
	} while (need_seqretry(&sig->stats_lock, seq));
	done_seqretry_irqrestore(&sig->stats_lock, seq, flags);
	rcu_read_unlock();
}

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
/*
 * Account a tick to a process and cpustat
 * @p: the process that the cpu time gets accounted to
 * @user_tick: is the tick from userspace
 * @rq: the pointer to rq
 *
 * Tick demultiplexing follows the order
 * - pending hardirq update
 * - pending softirq update
 * - user_time
 * - idle_time
 * - system time
 *   - check for guest_time
 *   - else account as system_time
 *
 * Check for hardirq is done both for system and user time as there is
 * no timer going off while we are on hardirq and hence we may never get an
 * opportunity to update it solely in system time.
 * p->stime and friends are only updated on system time and not on irq
 * softirq as those do not count in task exec_runtime any more.
 */
static void irqtime_account_process_tick(struct task_struct *p, int user_tick,
					 struct rq *rq, int ticks)
{
	u64 other, cputime = TICK_NSEC * ticks;

	/*
	 * When returning from idle, many ticks can get accounted at
	 * once, including some ticks of steal, irq, and softirq time.
	 * Subtract those ticks from the amount of time accounted to
	 * idle, or potentially user or system time. Due to rounding,
	 * other time can exceed ticks occasionally.
	 */
	other = account_other_time(ULONG_MAX);
	if (other >= cputime)
		return;

	cputime -= other;

	if (this_cpu_ksoftirqd() == p) {
		/*
		 * ksoftirqd time do not get accounted in cpu_softirq_time.
		 * So, we have to handle it separately here.
		 * Also, p->stime needs to be updated for ksoftirqd.
		 */
		account_system_index_time(p, cputime, CPUTIME_SOFTIRQ);
	} else if (user_tick) {
		account_user_time(p, cputime);
	} else if (p == rq->idle) {
		account_idle_time(cputime);
	} else if (p->flags & PF_VCPU) { /* System time or guest time */
		account_guest_time(p, cputime);
	} else {
		account_system_index_time(p, cputime, CPUTIME_SYSTEM);
	}
}

static void irqtime_account_idle_ticks(int ticks)
{
	struct rq *rq = this_rq();

	irqtime_account_process_tick(current, 0, rq, ticks);
}
#else /* CONFIG_IRQ_TIME_ACCOUNTING */
static inline void irqtime_account_idle_ticks(int ticks) {}
static inline void irqtime_account_process_tick(struct task_struct *p, int user_tick,
						struct rq *rq, int nr_ticks) {}
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING

#ifndef __ARCH_HAS_VTIME_TASK_SWITCH
void vtime_common_task_switch(struct task_struct *prev)
{
	if (is_idle_task(prev))
		vtime_account_idle(prev);
	else
		vtime_account_system(prev);

	vtime_flush(prev);
	arch_vtime_task_switch(prev);
}
#endif

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */


#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
/*
 * Archs that account the whole time spent in the idle task
 * (outside irq) as idle time can rely on this and just implement
 * vtime_account_system() and vtime_account_idle(). Archs that
 * have other meaning of the idle time (s390 only includes the
 * time spent by the CPU when it's in low power mode) must override
 * vtime_account().
 */
#ifndef __ARCH_HAS_VTIME_ACCOUNT
void vtime_account_irq_enter(struct task_struct *tsk)
{
	if (!in_interrupt() && is_idle_task(tsk))
		vtime_account_idle(tsk);
	else
		vtime_account_system(tsk);
}
EXPORT_SYMBOL_GPL(vtime_account_irq_enter);
#endif /* __ARCH_HAS_VTIME_ACCOUNT */

void task_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	*ut = p->utime;
	*st = p->stime;
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);

	*ut = cputime.utime;
	*st = cputime.stime;
}
#else /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
/*
 * Account a single tick of cpu time.
 * @p: the process that the cpu time gets accounted to
 * @user_tick: indicates if the tick is a user or a system tick
 */
void account_process_tick(struct task_struct *p, int user_tick)
{
	u64 cputime, steal;
	struct rq *rq = this_rq();

	if (vtime_accounting_cpu_enabled())
		return;

	if (sched_clock_irqtime) {
		irqtime_account_process_tick(p, user_tick, rq, 1);
		return;
	}

	cputime = TICK_NSEC;
	steal = steal_account_process_time(ULONG_MAX);

	if (steal >= cputime)
		return;

	cputime -= steal;

	if (user_tick)
		account_user_time(p, cputime);
	else if ((p != rq->idle) || (irq_count() != HARDIRQ_OFFSET))
		account_system_time(p, HARDIRQ_OFFSET, cputime);
	else
		account_idle_time(cputime);
}

/*
 * Account multiple ticks of idle time.
 * @ticks: number of stolen ticks
 */
void account_idle_ticks(unsigned long ticks)
{
	u64 cputime, steal;

	if (sched_clock_irqtime) {
		irqtime_account_idle_ticks(ticks);
		return;
	}

	cputime = ticks * TICK_NSEC;
	steal = steal_account_process_time(ULONG_MAX);

	if (steal >= cputime)
		return;

	cputime -= steal;
	account_idle_time(cputime);
}

/*
 * Perform (stime * rtime) / total, but avoid multiplication overflow by
 * loosing precision when the numbers are big.
 */
static u64 scale_stime(u64 stime, u64 rtime, u64 total)
{
	u64 scaled;

	for (;;) {
		/* Make sure "rtime" is the bigger of stime/rtime */
		if (stime > rtime)
			swap(rtime, stime);

		/* Make sure 'total' fits in 32 bits */
		if (total >> 32)
			goto drop_precision;

		/* Does rtime (and thus stime) fit in 32 bits? */
		if (!(rtime >> 32))
			break;

		/* Can we just balance rtime/stime rather than dropping bits? */
		if (stime >> 31)
			goto drop_precision;

		/* We can grow stime and shrink rtime and try to make them both fit */
		stime <<= 1;
		rtime >>= 1;
		continue;

drop_precision:
		/* We drop from rtime, it has more bits than stime */
		rtime >>= 1;
		total >>= 1;
	}

	/*
	 * Make sure gcc understands that this is a 32x32->64 multiply,
	 * followed by a 64/32->64 divide.
	 */
	scaled = div_u64((u64) (u32) stime * (u64) (u32) rtime, (u32)total);
	return scaled;
}

/*
 * Adjust tick based cputime random precision against scheduler runtime
 * accounting.
 *
 * Tick based cputime accounting depend on random scheduling timeslices of a
 * task to be interrupted or not by the timer.  Depending on these
 * circumstances, the number of these interrupts may be over or
 * under-optimistic, matching the real user and system cputime with a variable
 * precision.
 *
 * Fix this by scaling these tick based values against the total runtime
 * accounted by the CFS scheduler.
 *
 * This code provides the following guarantees:
 *
 *   stime + utime == rtime
 *   stime_i+1 >= stime_i, utime_i+1 >= utime_i
 *
 * Assuming that rtime_i+1 >= rtime_i.
 */
static void cputime_adjust(struct task_cputime *curr,
			   struct prev_cputime *prev,
			   u64 *ut, u64 *st)
{
	u64 rtime, stime, utime;
	unsigned long flags;

	/* Serialize concurrent callers such that we can honour our guarantees */
	raw_spin_lock_irqsave(&prev->lock, flags);
	rtime = curr->sum_exec_runtime;

	/*
	 * This is possible under two circumstances:
	 *  - rtime isn't monotonic after all (a bug);
	 *  - we got reordered by the lock.
	 *
	 * In both cases this acts as a filter such that the rest of the code
	 * can assume it is monotonic regardless of anything else.
	 */
	if (prev->stime + prev->utime >= rtime)
		goto out;

	stime = curr->stime;
	utime = curr->utime;

	/*
	 * If either stime or utime are 0, assume all runtime is userspace.
	 * Once a task gets some ticks, the monotonicy code at 'update:'
	 * will ensure things converge to the observed ratio.
	 */
	if (stime == 0) {
		utime = rtime;
		goto update;
	}

	if (utime == 0) {
		stime = rtime;
		goto update;
	}

	stime = scale_stime(stime, rtime, stime + utime);

update:
	/*
	 * Make sure stime doesn't go backwards; this preserves monotonicity
	 * for utime because rtime is monotonic.
	 *
	 *  utime_i+1 = rtime_i+1 - stime_i
	 *            = rtime_i+1 - (rtime_i - utime_i)
	 *            = (rtime_i+1 - rtime_i) + utime_i
	 *            >= utime_i
	 */
	if (stime < prev->stime)
		stime = prev->stime;
	utime = rtime - stime;

	/*
	 * Make sure utime doesn't go backwards; this still preserves
	 * monotonicity for stime, analogous argument to above.
	 */
	if (utime < prev->utime) {
		utime = prev->utime;
		stime = rtime - utime;
	}

	prev->stime = stime;
	prev->utime = utime;
out:
	*ut = prev->utime;
	*st = prev->stime;
	raw_spin_unlock_irqrestore(&prev->lock, flags);
}

void task_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	struct task_cputime cputime = {
		.sum_exec_runtime = p->se.sum_exec_runtime,
	};

	task_cputime(p, &cputime.utime, &cputime.stime);
	cputime_adjust(&cputime, &p->prev_cputime, ut, st);
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);
	cputime_adjust(&cputime, &p->signal->prev_cputime, ut, st);
}
#endif /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
static u64 vtime_delta(struct vtime *vtime)
{
	unsigned long long clock;

	clock = sched_clock();
	if (clock < vtime->starttime)
		return 0;

	return clock - vtime->starttime;
}

static u64 get_vtime_delta(struct vtime *vtime)
{
	u64 delta = vtime_delta(vtime);
	u64 other;

	/*
	 * Unlike tick based timing, vtime based timing never has lost
	 * ticks, and no need for steal time accounting to make up for
	 * lost ticks. Vtime accounts a rounded version of actual
	 * elapsed time. Limit account_other_time to prevent rounding
	 * errors from causing elapsed vtime to go negative.
	 */
	other = account_other_time(delta);
	WARN_ON_ONCE(vtime->state == VTIME_INACTIVE);
	vtime->starttime += delta;

	return delta - other;
}

static void __vtime_account_system(struct task_struct *tsk,
				   struct vtime *vtime)
{
	vtime->stime += get_vtime_delta(vtime);
	if (vtime->stime >= TICK_NSEC) {
		account_system_time(tsk, irq_count(), vtime->stime);
		vtime->stime = 0;
	}
}

static void vtime_account_guest(struct task_struct *tsk,
				struct vtime *vtime)
{
	vtime->gtime += get_vtime_delta(vtime);
	if (vtime->gtime >= TICK_NSEC) {
		account_guest_time(tsk, vtime->gtime);
		vtime->gtime = 0;
	}
}

void vtime_account_system(struct task_struct *tsk)
{
	struct vtime *vtime = &tsk->vtime;

	if (!vtime_delta(vtime))
		return;

	write_seqcount_begin(&vtime->seqcount);
	/* We might have scheduled out from guest path */
	if (current->flags & PF_VCPU)
		vtime_account_guest(tsk, vtime);
	else
		__vtime_account_system(tsk, vtime);
	write_seqcount_end(&vtime->seqcount);
}

void vtime_user_enter(struct task_struct *tsk)
{
	struct vtime *vtime = &tsk->vtime;

	write_seqcount_begin(&vtime->seqcount);
	__vtime_account_system(tsk, vtime);
	vtime->state = VTIME_USER;
	write_seqcount_end(&vtime->seqcount);
}

void vtime_user_exit(struct task_struct *tsk)
{
	struct vtime *vtime = &tsk->vtime;

	write_seqcount_begin(&vtime->seqcount);
	vtime->utime += get_vtime_delta(vtime);
	if (vtime->utime >= TICK_NSEC) {
		account_user_time(tsk, vtime->utime);
		vtime->utime = 0;
	}
	vtime->state = VTIME_SYS;
	write_seqcount_end(&vtime->seqcount);
}

void vtime_guest_enter(struct task_struct *tsk)
{
	struct vtime *vtime = &tsk->vtime;
	/*
	 * The flags must be updated under the lock with
	 * the vtime_starttime flush and update.
	 * That enforces a right ordering and update sequence
	 * synchronization against the reader (task_gtime())
	 * that can thus safely catch up with a tickless delta.
	 */
	write_seqcount_begin(&vtime->seqcount);
	__vtime_account_system(tsk, vtime);
	current->flags |= PF_VCPU;
	write_seqcount_end(&vtime->seqcount);
}
EXPORT_SYMBOL_GPL(vtime_guest_enter);

void vtime_guest_exit(struct task_struct *tsk)
{
	struct vtime *vtime = &tsk->vtime;

	write_seqcount_begin(&vtime->seqcount);
	vtime_account_guest(tsk, vtime);
	current->flags &= ~PF_VCPU;
	write_seqcount_end(&vtime->seqcount);
}
EXPORT_SYMBOL_GPL(vtime_guest_exit);

void vtime_account_idle(struct task_struct *tsk)
{
	account_idle_time(get_vtime_delta(&tsk->vtime));
}

void arch_vtime_task_switch(struct task_struct *prev)
{
	struct vtime *vtime = &prev->vtime;

	write_seqcount_begin(&vtime->seqcount);
	vtime->state = VTIME_INACTIVE;
	write_seqcount_end(&vtime->seqcount);

	vtime = &current->vtime;

	write_seqcount_begin(&vtime->seqcount);
	vtime->state = VTIME_SYS;
	vtime->starttime = sched_clock();
	write_seqcount_end(&vtime->seqcount);
}

void vtime_init_idle(struct task_struct *t, int cpu)
{
	struct vtime *vtime = &t->vtime;
	unsigned long flags;

	local_irq_save(flags);
	write_seqcount_begin(&vtime->seqcount);
	vtime->state = VTIME_SYS;
	vtime->starttime = sched_clock();
	write_seqcount_end(&vtime->seqcount);
	local_irq_restore(flags);
}

u64 task_gtime(struct task_struct *t)
{
	struct vtime *vtime = &t->vtime;
	unsigned int seq;
	u64 gtime;

	if (!vtime_accounting_enabled())
		return t->gtime;

	do {
		seq = read_seqcount_begin(&vtime->seqcount);

		gtime = t->gtime;
		if (vtime->state == VTIME_SYS && t->flags & PF_VCPU)
			gtime += vtime->gtime + vtime_delta(vtime);

	} while (read_seqcount_retry(&vtime->seqcount, seq));

	return gtime;
}

/*
 * Fetch cputime raw values from fields of task_struct and
 * add up the pending nohz execution time since the last
 * cputime snapshot.
 */
void task_cputime(struct task_struct *t, u64 *utime, u64 *stime)
{
	struct vtime *vtime = &t->vtime;
	unsigned int seq;
	u64 delta;

	if (!vtime_accounting_enabled()) {
		*utime = t->utime;
		*stime = t->stime;
		return;
	}

	do {
		seq = read_seqcount_begin(&vtime->seqcount);

		*utime = t->utime;
		*stime = t->stime;

		/* Task is sleeping, nothing to add */
		if (vtime->state == VTIME_INACTIVE || is_idle_task(t))
			continue;

		delta = vtime_delta(vtime);

		/*
		 * Task runs either in user or kernel space, add pending nohz time to
		 * the right place.
		 */
		if (vtime->state == VTIME_USER || t->flags & PF_VCPU)
			*utime += vtime->utime + delta;
		else if (vtime->state == VTIME_SYS)
			*stime += vtime->stime + delta;
	} while (read_seqcount_retry(&vtime->seqcount, seq));
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_GEN */

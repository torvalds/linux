/*
 * Performance counter core code
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright  ©  2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 *  For licensing details see kernel-base/COPYING
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sysfs.h>
#include <linux/dcache.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/vmstat.h>
#include <linux/hardirq.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/kernel_stat.h>
#include <linux/perf_counter.h>

#include <asm/irq_regs.h>

/*
 * Each CPU has a list of per CPU counters:
 */
DEFINE_PER_CPU(struct perf_cpu_context, perf_cpu_context);

int perf_max_counters __read_mostly = 1;
static int perf_reserved_percpu __read_mostly;
static int perf_overcommit __read_mostly = 1;

static atomic_t nr_counters __read_mostly;
static atomic_t nr_mmap_counters __read_mostly;
static atomic_t nr_comm_counters __read_mostly;

/*
 * perf counter paranoia level:
 *  0 - not paranoid
 *  1 - disallow cpu counters to unpriv
 *  2 - disallow kernel profiling to unpriv
 */
int sysctl_perf_counter_paranoid __read_mostly;

static inline bool perf_paranoid_cpu(void)
{
	return sysctl_perf_counter_paranoid > 0;
}

static inline bool perf_paranoid_kernel(void)
{
	return sysctl_perf_counter_paranoid > 1;
}

int sysctl_perf_counter_mlock __read_mostly = 512; /* 'free' kb per user */

/*
 * max perf counter sample rate
 */
int sysctl_perf_counter_sample_rate __read_mostly = 100000;

static atomic64_t perf_counter_id;

/*
 * Lock for (sysadmin-configurable) counter reservations:
 */
static DEFINE_SPINLOCK(perf_resource_lock);

/*
 * Architecture provided APIs - weak aliases:
 */
extern __weak const struct pmu *hw_perf_counter_init(struct perf_counter *counter)
{
	return NULL;
}

void __weak hw_perf_disable(void)		{ barrier(); }
void __weak hw_perf_enable(void)		{ barrier(); }

void __weak hw_perf_counter_setup(int cpu)	{ barrier(); }

int __weak
hw_perf_group_sched_in(struct perf_counter *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx, int cpu)
{
	return 0;
}

void __weak perf_counter_print_debug(void)	{ }

static DEFINE_PER_CPU(int, disable_count);

void __perf_disable(void)
{
	__get_cpu_var(disable_count)++;
}

bool __perf_enable(void)
{
	return !--__get_cpu_var(disable_count);
}

void perf_disable(void)
{
	__perf_disable();
	hw_perf_disable();
}

void perf_enable(void)
{
	if (__perf_enable())
		hw_perf_enable();
}

static void get_ctx(struct perf_counter_context *ctx)
{
	WARN_ON(!atomic_inc_not_zero(&ctx->refcount));
}

static void free_ctx(struct rcu_head *head)
{
	struct perf_counter_context *ctx;

	ctx = container_of(head, struct perf_counter_context, rcu_head);
	kfree(ctx);
}

static void put_ctx(struct perf_counter_context *ctx)
{
	if (atomic_dec_and_test(&ctx->refcount)) {
		if (ctx->parent_ctx)
			put_ctx(ctx->parent_ctx);
		if (ctx->task)
			put_task_struct(ctx->task);
		call_rcu(&ctx->rcu_head, free_ctx);
	}
}

/*
 * Get the perf_counter_context for a task and lock it.
 * This has to cope with with the fact that until it is locked,
 * the context could get moved to another task.
 */
static struct perf_counter_context *
perf_lock_task_context(struct task_struct *task, unsigned long *flags)
{
	struct perf_counter_context *ctx;

	rcu_read_lock();
 retry:
	ctx = rcu_dereference(task->perf_counter_ctxp);
	if (ctx) {
		/*
		 * If this context is a clone of another, it might
		 * get swapped for another underneath us by
		 * perf_counter_task_sched_out, though the
		 * rcu_read_lock() protects us from any context
		 * getting freed.  Lock the context and check if it
		 * got swapped before we could get the lock, and retry
		 * if so.  If we locked the right context, then it
		 * can't get swapped on us any more.
		 */
		spin_lock_irqsave(&ctx->lock, *flags);
		if (ctx != rcu_dereference(task->perf_counter_ctxp)) {
			spin_unlock_irqrestore(&ctx->lock, *flags);
			goto retry;
		}

		if (!atomic_inc_not_zero(&ctx->refcount)) {
			spin_unlock_irqrestore(&ctx->lock, *flags);
			ctx = NULL;
		}
	}
	rcu_read_unlock();
	return ctx;
}

/*
 * Get the context for a task and increment its pin_count so it
 * can't get swapped to another task.  This also increments its
 * reference count so that the context can't get freed.
 */
static struct perf_counter_context *perf_pin_task_context(struct task_struct *task)
{
	struct perf_counter_context *ctx;
	unsigned long flags;

	ctx = perf_lock_task_context(task, &flags);
	if (ctx) {
		++ctx->pin_count;
		spin_unlock_irqrestore(&ctx->lock, flags);
	}
	return ctx;
}

static void perf_unpin_context(struct perf_counter_context *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	--ctx->pin_count;
	spin_unlock_irqrestore(&ctx->lock, flags);
	put_ctx(ctx);
}

/*
 * Add a counter from the lists for its context.
 * Must be called with ctx->mutex and ctx->lock held.
 */
static void
list_add_counter(struct perf_counter *counter, struct perf_counter_context *ctx)
{
	struct perf_counter *group_leader = counter->group_leader;

	/*
	 * Depending on whether it is a standalone or sibling counter,
	 * add it straight to the context's counter list, or to the group
	 * leader's sibling list:
	 */
	if (group_leader == counter)
		list_add_tail(&counter->list_entry, &ctx->counter_list);
	else {
		list_add_tail(&counter->list_entry, &group_leader->sibling_list);
		group_leader->nr_siblings++;
	}

	list_add_rcu(&counter->event_entry, &ctx->event_list);
	ctx->nr_counters++;
	if (counter->attr.inherit_stat)
		ctx->nr_stat++;
}

/*
 * Remove a counter from the lists for its context.
 * Must be called with ctx->mutex and ctx->lock held.
 */
static void
list_del_counter(struct perf_counter *counter, struct perf_counter_context *ctx)
{
	struct perf_counter *sibling, *tmp;

	if (list_empty(&counter->list_entry))
		return;
	ctx->nr_counters--;
	if (counter->attr.inherit_stat)
		ctx->nr_stat--;

	list_del_init(&counter->list_entry);
	list_del_rcu(&counter->event_entry);

	if (counter->group_leader != counter)
		counter->group_leader->nr_siblings--;

	/*
	 * If this was a group counter with sibling counters then
	 * upgrade the siblings to singleton counters by adding them
	 * to the context list directly:
	 */
	list_for_each_entry_safe(sibling, tmp,
				 &counter->sibling_list, list_entry) {

		list_move_tail(&sibling->list_entry, &ctx->counter_list);
		sibling->group_leader = sibling;
	}
}

static void
counter_sched_out(struct perf_counter *counter,
		  struct perf_cpu_context *cpuctx,
		  struct perf_counter_context *ctx)
{
	if (counter->state != PERF_COUNTER_STATE_ACTIVE)
		return;

	counter->state = PERF_COUNTER_STATE_INACTIVE;
	counter->tstamp_stopped = ctx->time;
	counter->pmu->disable(counter);
	counter->oncpu = -1;

	if (!is_software_counter(counter))
		cpuctx->active_oncpu--;
	ctx->nr_active--;
	if (counter->attr.exclusive || !cpuctx->active_oncpu)
		cpuctx->exclusive = 0;
}

static void
group_sched_out(struct perf_counter *group_counter,
		struct perf_cpu_context *cpuctx,
		struct perf_counter_context *ctx)
{
	struct perf_counter *counter;

	if (group_counter->state != PERF_COUNTER_STATE_ACTIVE)
		return;

	counter_sched_out(group_counter, cpuctx, ctx);

	/*
	 * Schedule out siblings (if any):
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry)
		counter_sched_out(counter, cpuctx, ctx);

	if (group_counter->attr.exclusive)
		cpuctx->exclusive = 0;
}

/*
 * Cross CPU call to remove a performance counter
 *
 * We disable the counter on the hardware level first. After that we
 * remove it from the context list.
 */
static void __perf_counter_remove_from_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock(&ctx->lock);
	/*
	 * Protect the list operation against NMI by disabling the
	 * counters on a global level.
	 */
	perf_disable();

	counter_sched_out(counter, cpuctx, ctx);

	list_del_counter(counter, ctx);

	if (!ctx->task) {
		/*
		 * Allow more per task counters with respect to the
		 * reservation:
		 */
		cpuctx->max_pertask =
			min(perf_max_counters - ctx->nr_counters,
			    perf_max_counters - perf_reserved_percpu);
	}

	perf_enable();
	spin_unlock(&ctx->lock);
}


/*
 * Remove the counter from a task's (or a CPU's) list of counters.
 *
 * Must be called with ctx->mutex held.
 *
 * CPU counters are removed with a smp call. For task counters we only
 * call when the task is on a CPU.
 *
 * If counter->ctx is a cloned context, callers must make sure that
 * every task struct that counter->ctx->task could possibly point to
 * remains valid.  This is OK when called from perf_release since
 * that only calls us on the top-level context, which can't be a clone.
 * When called from perf_counter_exit_task, it's OK because the
 * context has been detached from its task.
 */
static void perf_counter_remove_from_context(struct perf_counter *counter)
{
	struct perf_counter_context *ctx = counter->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Per cpu counters are removed via an smp call and
		 * the removal is always sucessful.
		 */
		smp_call_function_single(counter->cpu,
					 __perf_counter_remove_from_context,
					 counter, 1);
		return;
	}

retry:
	task_oncpu_function_call(task, __perf_counter_remove_from_context,
				 counter);

	spin_lock_irq(&ctx->lock);
	/*
	 * If the context is active we need to retry the smp call.
	 */
	if (ctx->nr_active && !list_empty(&counter->list_entry)) {
		spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * The lock prevents that this context is scheduled in so we
	 * can remove the counter safely, if the call above did not
	 * succeed.
	 */
	if (!list_empty(&counter->list_entry)) {
		list_del_counter(counter, ctx);
	}
	spin_unlock_irq(&ctx->lock);
}

static inline u64 perf_clock(void)
{
	return cpu_clock(smp_processor_id());
}

/*
 * Update the record of the current time in a context.
 */
static void update_context_time(struct perf_counter_context *ctx)
{
	u64 now = perf_clock();

	ctx->time += now - ctx->timestamp;
	ctx->timestamp = now;
}

/*
 * Update the total_time_enabled and total_time_running fields for a counter.
 */
static void update_counter_times(struct perf_counter *counter)
{
	struct perf_counter_context *ctx = counter->ctx;
	u64 run_end;

	if (counter->state < PERF_COUNTER_STATE_INACTIVE)
		return;

	counter->total_time_enabled = ctx->time - counter->tstamp_enabled;

	if (counter->state == PERF_COUNTER_STATE_INACTIVE)
		run_end = counter->tstamp_stopped;
	else
		run_end = ctx->time;

	counter->total_time_running = run_end - counter->tstamp_running;
}

/*
 * Update total_time_enabled and total_time_running for all counters in a group.
 */
static void update_group_times(struct perf_counter *leader)
{
	struct perf_counter *counter;

	update_counter_times(leader);
	list_for_each_entry(counter, &leader->sibling_list, list_entry)
		update_counter_times(counter);
}

/*
 * Cross CPU call to disable a performance counter
 */
static void __perf_counter_disable(void *info)
{
	struct perf_counter *counter = info;
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter_context *ctx = counter->ctx;

	/*
	 * If this is a per-task counter, need to check whether this
	 * counter's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock(&ctx->lock);

	/*
	 * If the counter is on, turn it off.
	 * If it is in error state, leave it in error state.
	 */
	if (counter->state >= PERF_COUNTER_STATE_INACTIVE) {
		update_context_time(ctx);
		update_counter_times(counter);
		if (counter == counter->group_leader)
			group_sched_out(counter, cpuctx, ctx);
		else
			counter_sched_out(counter, cpuctx, ctx);
		counter->state = PERF_COUNTER_STATE_OFF;
	}

	spin_unlock(&ctx->lock);
}

/*
 * Disable a counter.
 *
 * If counter->ctx is a cloned context, callers must make sure that
 * every task struct that counter->ctx->task could possibly point to
 * remains valid.  This condition is satisifed when called through
 * perf_counter_for_each_child or perf_counter_for_each because they
 * hold the top-level counter's child_mutex, so any descendant that
 * goes to exit will block in sync_child_counter.
 * When called from perf_pending_counter it's OK because counter->ctx
 * is the current context on this CPU and preemption is disabled,
 * hence we can't get into perf_counter_task_sched_out for this context.
 */
static void perf_counter_disable(struct perf_counter *counter)
{
	struct perf_counter_context *ctx = counter->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Disable the counter on the cpu that it's on
		 */
		smp_call_function_single(counter->cpu, __perf_counter_disable,
					 counter, 1);
		return;
	}

 retry:
	task_oncpu_function_call(task, __perf_counter_disable, counter);

	spin_lock_irq(&ctx->lock);
	/*
	 * If the counter is still active, we need to retry the cross-call.
	 */
	if (counter->state == PERF_COUNTER_STATE_ACTIVE) {
		spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * Since we have the lock this context can't be scheduled
	 * in, so we can change the state safely.
	 */
	if (counter->state == PERF_COUNTER_STATE_INACTIVE) {
		update_counter_times(counter);
		counter->state = PERF_COUNTER_STATE_OFF;
	}

	spin_unlock_irq(&ctx->lock);
}

static int
counter_sched_in(struct perf_counter *counter,
		 struct perf_cpu_context *cpuctx,
		 struct perf_counter_context *ctx,
		 int cpu)
{
	if (counter->state <= PERF_COUNTER_STATE_OFF)
		return 0;

	counter->state = PERF_COUNTER_STATE_ACTIVE;
	counter->oncpu = cpu;	/* TODO: put 'cpu' into cpuctx->cpu */
	/*
	 * The new state must be visible before we turn it on in the hardware:
	 */
	smp_wmb();

	if (counter->pmu->enable(counter)) {
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		counter->oncpu = -1;
		return -EAGAIN;
	}

	counter->tstamp_running += ctx->time - counter->tstamp_stopped;

	if (!is_software_counter(counter))
		cpuctx->active_oncpu++;
	ctx->nr_active++;

	if (counter->attr.exclusive)
		cpuctx->exclusive = 1;

	return 0;
}

static int
group_sched_in(struct perf_counter *group_counter,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx,
	       int cpu)
{
	struct perf_counter *counter, *partial_group;
	int ret;

	if (group_counter->state == PERF_COUNTER_STATE_OFF)
		return 0;

	ret = hw_perf_group_sched_in(group_counter, cpuctx, ctx, cpu);
	if (ret)
		return ret < 0 ? ret : 0;

	if (counter_sched_in(group_counter, cpuctx, ctx, cpu))
		return -EAGAIN;

	/*
	 * Schedule in siblings as one group (if any):
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry) {
		if (counter_sched_in(counter, cpuctx, ctx, cpu)) {
			partial_group = counter;
			goto group_error;
		}
	}

	return 0;

group_error:
	/*
	 * Groups can be scheduled in as one unit only, so undo any
	 * partial group before returning:
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry) {
		if (counter == partial_group)
			break;
		counter_sched_out(counter, cpuctx, ctx);
	}
	counter_sched_out(group_counter, cpuctx, ctx);

	return -EAGAIN;
}

/*
 * Return 1 for a group consisting entirely of software counters,
 * 0 if the group contains any hardware counters.
 */
static int is_software_only_group(struct perf_counter *leader)
{
	struct perf_counter *counter;

	if (!is_software_counter(leader))
		return 0;

	list_for_each_entry(counter, &leader->sibling_list, list_entry)
		if (!is_software_counter(counter))
			return 0;

	return 1;
}

/*
 * Work out whether we can put this counter group on the CPU now.
 */
static int group_can_go_on(struct perf_counter *counter,
			   struct perf_cpu_context *cpuctx,
			   int can_add_hw)
{
	/*
	 * Groups consisting entirely of software counters can always go on.
	 */
	if (is_software_only_group(counter))
		return 1;
	/*
	 * If an exclusive group is already on, no other hardware
	 * counters can go on.
	 */
	if (cpuctx->exclusive)
		return 0;
	/*
	 * If this group is exclusive and there are already
	 * counters on the CPU, it can't go on.
	 */
	if (counter->attr.exclusive && cpuctx->active_oncpu)
		return 0;
	/*
	 * Otherwise, try to add it if all previous groups were able
	 * to go on.
	 */
	return can_add_hw;
}

static void add_counter_to_ctx(struct perf_counter *counter,
			       struct perf_counter_context *ctx)
{
	list_add_counter(counter, ctx);
	counter->tstamp_enabled = ctx->time;
	counter->tstamp_running = ctx->time;
	counter->tstamp_stopped = ctx->time;
}

/*
 * Cross CPU call to install and enable a performance counter
 *
 * Must be called with ctx->mutex held
 */
static void __perf_install_in_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_counter *leader = counter->group_leader;
	int cpu = smp_processor_id();
	int err;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 * Or possibly this is the right context but it isn't
	 * on this cpu because it had no counters.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx) {
		if (cpuctx->task_ctx || ctx->task != current)
			return;
		cpuctx->task_ctx = ctx;
	}

	spin_lock(&ctx->lock);
	ctx->is_active = 1;
	update_context_time(ctx);

	/*
	 * Protect the list operation against NMI by disabling the
	 * counters on a global level. NOP for non NMI based counters.
	 */
	perf_disable();

	add_counter_to_ctx(counter, ctx);

	/*
	 * Don't put the counter on if it is disabled or if
	 * it is in a group and the group isn't on.
	 */
	if (counter->state != PERF_COUNTER_STATE_INACTIVE ||
	    (leader != counter && leader->state != PERF_COUNTER_STATE_ACTIVE))
		goto unlock;

	/*
	 * An exclusive counter can't go on if there are already active
	 * hardware counters, and no hardware counter can go on if there
	 * is already an exclusive counter on.
	 */
	if (!group_can_go_on(counter, cpuctx, 1))
		err = -EEXIST;
	else
		err = counter_sched_in(counter, cpuctx, ctx, cpu);

	if (err) {
		/*
		 * This counter couldn't go on.  If it is in a group
		 * then we have to pull the whole group off.
		 * If the counter group is pinned then put it in error state.
		 */
		if (leader != counter)
			group_sched_out(leader, cpuctx, ctx);
		if (leader->attr.pinned) {
			update_group_times(leader);
			leader->state = PERF_COUNTER_STATE_ERROR;
		}
	}

	if (!err && !ctx->task && cpuctx->max_pertask)
		cpuctx->max_pertask--;

 unlock:
	perf_enable();

	spin_unlock(&ctx->lock);
}

/*
 * Attach a performance counter to a context
 *
 * First we add the counter to the list with the hardware enable bit
 * in counter->hw_config cleared.
 *
 * If the counter is attached to a task which is on a CPU we use a smp
 * call to enable it in the task context. The task might have been
 * scheduled away, but we check this in the smp call again.
 *
 * Must be called with ctx->mutex held.
 */
static void
perf_install_in_context(struct perf_counter_context *ctx,
			struct perf_counter *counter,
			int cpu)
{
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Per cpu counters are installed via an smp call and
		 * the install is always sucessful.
		 */
		smp_call_function_single(cpu, __perf_install_in_context,
					 counter, 1);
		return;
	}

retry:
	task_oncpu_function_call(task, __perf_install_in_context,
				 counter);

	spin_lock_irq(&ctx->lock);
	/*
	 * we need to retry the smp call.
	 */
	if (ctx->is_active && list_empty(&counter->list_entry)) {
		spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * The lock prevents that this context is scheduled in so we
	 * can add the counter safely, if it the call above did not
	 * succeed.
	 */
	if (list_empty(&counter->list_entry))
		add_counter_to_ctx(counter, ctx);
	spin_unlock_irq(&ctx->lock);
}

/*
 * Cross CPU call to enable a performance counter
 */
static void __perf_counter_enable(void *info)
{
	struct perf_counter *counter = info;
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_counter *leader = counter->group_leader;
	int err;

	/*
	 * If this is a per-task counter, need to check whether this
	 * counter's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx) {
		if (cpuctx->task_ctx || ctx->task != current)
			return;
		cpuctx->task_ctx = ctx;
	}

	spin_lock(&ctx->lock);
	ctx->is_active = 1;
	update_context_time(ctx);

	if (counter->state >= PERF_COUNTER_STATE_INACTIVE)
		goto unlock;
	counter->state = PERF_COUNTER_STATE_INACTIVE;
	counter->tstamp_enabled = ctx->time - counter->total_time_enabled;

	/*
	 * If the counter is in a group and isn't the group leader,
	 * then don't put it on unless the group is on.
	 */
	if (leader != counter && leader->state != PERF_COUNTER_STATE_ACTIVE)
		goto unlock;

	if (!group_can_go_on(counter, cpuctx, 1)) {
		err = -EEXIST;
	} else {
		perf_disable();
		if (counter == leader)
			err = group_sched_in(counter, cpuctx, ctx,
					     smp_processor_id());
		else
			err = counter_sched_in(counter, cpuctx, ctx,
					       smp_processor_id());
		perf_enable();
	}

	if (err) {
		/*
		 * If this counter can't go on and it's part of a
		 * group, then the whole group has to come off.
		 */
		if (leader != counter)
			group_sched_out(leader, cpuctx, ctx);
		if (leader->attr.pinned) {
			update_group_times(leader);
			leader->state = PERF_COUNTER_STATE_ERROR;
		}
	}

 unlock:
	spin_unlock(&ctx->lock);
}

/*
 * Enable a counter.
 *
 * If counter->ctx is a cloned context, callers must make sure that
 * every task struct that counter->ctx->task could possibly point to
 * remains valid.  This condition is satisfied when called through
 * perf_counter_for_each_child or perf_counter_for_each as described
 * for perf_counter_disable.
 */
static void perf_counter_enable(struct perf_counter *counter)
{
	struct perf_counter_context *ctx = counter->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Enable the counter on the cpu that it's on
		 */
		smp_call_function_single(counter->cpu, __perf_counter_enable,
					 counter, 1);
		return;
	}

	spin_lock_irq(&ctx->lock);
	if (counter->state >= PERF_COUNTER_STATE_INACTIVE)
		goto out;

	/*
	 * If the counter is in error state, clear that first.
	 * That way, if we see the counter in error state below, we
	 * know that it has gone back into error state, as distinct
	 * from the task having been scheduled away before the
	 * cross-call arrived.
	 */
	if (counter->state == PERF_COUNTER_STATE_ERROR)
		counter->state = PERF_COUNTER_STATE_OFF;

 retry:
	spin_unlock_irq(&ctx->lock);
	task_oncpu_function_call(task, __perf_counter_enable, counter);

	spin_lock_irq(&ctx->lock);

	/*
	 * If the context is active and the counter is still off,
	 * we need to retry the cross-call.
	 */
	if (ctx->is_active && counter->state == PERF_COUNTER_STATE_OFF)
		goto retry;

	/*
	 * Since we have the lock this context can't be scheduled
	 * in, so we can change the state safely.
	 */
	if (counter->state == PERF_COUNTER_STATE_OFF) {
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		counter->tstamp_enabled =
			ctx->time - counter->total_time_enabled;
	}
 out:
	spin_unlock_irq(&ctx->lock);
}

static int perf_counter_refresh(struct perf_counter *counter, int refresh)
{
	/*
	 * not supported on inherited counters
	 */
	if (counter->attr.inherit)
		return -EINVAL;

	atomic_add(refresh, &counter->event_limit);
	perf_counter_enable(counter);

	return 0;
}

void __perf_counter_sched_out(struct perf_counter_context *ctx,
			      struct perf_cpu_context *cpuctx)
{
	struct perf_counter *counter;

	spin_lock(&ctx->lock);
	ctx->is_active = 0;
	if (likely(!ctx->nr_counters))
		goto out;
	update_context_time(ctx);

	perf_disable();
	if (ctx->nr_active) {
		list_for_each_entry(counter, &ctx->counter_list, list_entry) {
			if (counter != counter->group_leader)
				counter_sched_out(counter, cpuctx, ctx);
			else
				group_sched_out(counter, cpuctx, ctx);
		}
	}
	perf_enable();
 out:
	spin_unlock(&ctx->lock);
}

/*
 * Test whether two contexts are equivalent, i.e. whether they
 * have both been cloned from the same version of the same context
 * and they both have the same number of enabled counters.
 * If the number of enabled counters is the same, then the set
 * of enabled counters should be the same, because these are both
 * inherited contexts, therefore we can't access individual counters
 * in them directly with an fd; we can only enable/disable all
 * counters via prctl, or enable/disable all counters in a family
 * via ioctl, which will have the same effect on both contexts.
 */
static int context_equiv(struct perf_counter_context *ctx1,
			 struct perf_counter_context *ctx2)
{
	return ctx1->parent_ctx && ctx1->parent_ctx == ctx2->parent_ctx
		&& ctx1->parent_gen == ctx2->parent_gen
		&& !ctx1->pin_count && !ctx2->pin_count;
}

static void __perf_counter_read(void *counter);

static void __perf_counter_sync_stat(struct perf_counter *counter,
				     struct perf_counter *next_counter)
{
	u64 value;

	if (!counter->attr.inherit_stat)
		return;

	/*
	 * Update the counter value, we cannot use perf_counter_read()
	 * because we're in the middle of a context switch and have IRQs
	 * disabled, which upsets smp_call_function_single(), however
	 * we know the counter must be on the current CPU, therefore we
	 * don't need to use it.
	 */
	switch (counter->state) {
	case PERF_COUNTER_STATE_ACTIVE:
		__perf_counter_read(counter);
		break;

	case PERF_COUNTER_STATE_INACTIVE:
		update_counter_times(counter);
		break;

	default:
		break;
	}

	/*
	 * In order to keep per-task stats reliable we need to flip the counter
	 * values when we flip the contexts.
	 */
	value = atomic64_read(&next_counter->count);
	value = atomic64_xchg(&counter->count, value);
	atomic64_set(&next_counter->count, value);

	swap(counter->total_time_enabled, next_counter->total_time_enabled);
	swap(counter->total_time_running, next_counter->total_time_running);

	/*
	 * Since we swizzled the values, update the user visible data too.
	 */
	perf_counter_update_userpage(counter);
	perf_counter_update_userpage(next_counter);
}

#define list_next_entry(pos, member) \
	list_entry(pos->member.next, typeof(*pos), member)

static void perf_counter_sync_stat(struct perf_counter_context *ctx,
				   struct perf_counter_context *next_ctx)
{
	struct perf_counter *counter, *next_counter;

	if (!ctx->nr_stat)
		return;

	counter = list_first_entry(&ctx->event_list,
				   struct perf_counter, event_entry);

	next_counter = list_first_entry(&next_ctx->event_list,
					struct perf_counter, event_entry);

	while (&counter->event_entry != &ctx->event_list &&
	       &next_counter->event_entry != &next_ctx->event_list) {

		__perf_counter_sync_stat(counter, next_counter);

		counter = list_next_entry(counter, event_entry);
		next_counter = list_next_entry(counter, event_entry);
	}
}

/*
 * Called from scheduler to remove the counters of the current task,
 * with interrupts disabled.
 *
 * We stop each counter and update the counter value in counter->count.
 *
 * This does not protect us against NMI, but disable()
 * sets the disabled bit in the control field of counter _before_
 * accessing the counter control register. If a NMI hits, then it will
 * not restart the counter.
 */
void perf_counter_task_sched_out(struct task_struct *task,
				 struct task_struct *next, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = task->perf_counter_ctxp;
	struct perf_counter_context *next_ctx;
	struct perf_counter_context *parent;
	struct pt_regs *regs;
	int do_switch = 1;

	regs = task_pt_regs(task);
	perf_swcounter_event(PERF_COUNT_SW_CONTEXT_SWITCHES, 1, 1, regs, 0);

	if (likely(!ctx || !cpuctx->task_ctx))
		return;

	update_context_time(ctx);

	rcu_read_lock();
	parent = rcu_dereference(ctx->parent_ctx);
	next_ctx = next->perf_counter_ctxp;
	if (parent && next_ctx &&
	    rcu_dereference(next_ctx->parent_ctx) == parent) {
		/*
		 * Looks like the two contexts are clones, so we might be
		 * able to optimize the context switch.  We lock both
		 * contexts and check that they are clones under the
		 * lock (including re-checking that neither has been
		 * uncloned in the meantime).  It doesn't matter which
		 * order we take the locks because no other cpu could
		 * be trying to lock both of these tasks.
		 */
		spin_lock(&ctx->lock);
		spin_lock_nested(&next_ctx->lock, SINGLE_DEPTH_NESTING);
		if (context_equiv(ctx, next_ctx)) {
			/*
			 * XXX do we need a memory barrier of sorts
			 * wrt to rcu_dereference() of perf_counter_ctxp
			 */
			task->perf_counter_ctxp = next_ctx;
			next->perf_counter_ctxp = ctx;
			ctx->task = next;
			next_ctx->task = task;
			do_switch = 0;

			perf_counter_sync_stat(ctx, next_ctx);
		}
		spin_unlock(&next_ctx->lock);
		spin_unlock(&ctx->lock);
	}
	rcu_read_unlock();

	if (do_switch) {
		__perf_counter_sched_out(ctx, cpuctx);
		cpuctx->task_ctx = NULL;
	}
}

/*
 * Called with IRQs disabled
 */
static void __perf_counter_task_sched_out(struct perf_counter_context *ctx)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);

	if (!cpuctx->task_ctx)
		return;

	if (WARN_ON_ONCE(ctx != cpuctx->task_ctx))
		return;

	__perf_counter_sched_out(ctx, cpuctx);
	cpuctx->task_ctx = NULL;
}

/*
 * Called with IRQs disabled
 */
static void perf_counter_cpu_sched_out(struct perf_cpu_context *cpuctx)
{
	__perf_counter_sched_out(&cpuctx->ctx, cpuctx);
}

static void
__perf_counter_sched_in(struct perf_counter_context *ctx,
			struct perf_cpu_context *cpuctx, int cpu)
{
	struct perf_counter *counter;
	int can_add_hw = 1;

	spin_lock(&ctx->lock);
	ctx->is_active = 1;
	if (likely(!ctx->nr_counters))
		goto out;

	ctx->timestamp = perf_clock();

	perf_disable();

	/*
	 * First go through the list and put on any pinned groups
	 * in order to give them the best chance of going on.
	 */
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state <= PERF_COUNTER_STATE_OFF ||
		    !counter->attr.pinned)
			continue;
		if (counter->cpu != -1 && counter->cpu != cpu)
			continue;

		if (counter != counter->group_leader)
			counter_sched_in(counter, cpuctx, ctx, cpu);
		else {
			if (group_can_go_on(counter, cpuctx, 1))
				group_sched_in(counter, cpuctx, ctx, cpu);
		}

		/*
		 * If this pinned group hasn't been scheduled,
		 * put it in error state.
		 */
		if (counter->state == PERF_COUNTER_STATE_INACTIVE) {
			update_group_times(counter);
			counter->state = PERF_COUNTER_STATE_ERROR;
		}
	}

	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		/*
		 * Ignore counters in OFF or ERROR state, and
		 * ignore pinned counters since we did them already.
		 */
		if (counter->state <= PERF_COUNTER_STATE_OFF ||
		    counter->attr.pinned)
			continue;

		/*
		 * Listen to the 'cpu' scheduling filter constraint
		 * of counters:
		 */
		if (counter->cpu != -1 && counter->cpu != cpu)
			continue;

		if (counter != counter->group_leader) {
			if (counter_sched_in(counter, cpuctx, ctx, cpu))
				can_add_hw = 0;
		} else {
			if (group_can_go_on(counter, cpuctx, can_add_hw)) {
				if (group_sched_in(counter, cpuctx, ctx, cpu))
					can_add_hw = 0;
			}
		}
	}
	perf_enable();
 out:
	spin_unlock(&ctx->lock);
}

/*
 * Called from scheduler to add the counters of the current task
 * with interrupts disabled.
 *
 * We restore the counter value and then enable it.
 *
 * This does not protect us against NMI, but enable()
 * sets the enabled bit in the control field of counter _before_
 * accessing the counter control register. If a NMI hits, then it will
 * keep the counter running.
 */
void perf_counter_task_sched_in(struct task_struct *task, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = task->perf_counter_ctxp;

	if (likely(!ctx))
		return;
	if (cpuctx->task_ctx == ctx)
		return;
	__perf_counter_sched_in(ctx, cpuctx, cpu);
	cpuctx->task_ctx = ctx;
}

static void perf_counter_cpu_sched_in(struct perf_cpu_context *cpuctx, int cpu)
{
	struct perf_counter_context *ctx = &cpuctx->ctx;

	__perf_counter_sched_in(ctx, cpuctx, cpu);
}

#define MAX_INTERRUPTS (~0ULL)

static void perf_log_throttle(struct perf_counter *counter, int enable);
static void perf_log_period(struct perf_counter *counter, u64 period);

static void perf_adjust_period(struct perf_counter *counter, u64 events)
{
	struct hw_perf_counter *hwc = &counter->hw;
	u64 period, sample_period;
	s64 delta;

	events *= hwc->sample_period;
	period = div64_u64(events, counter->attr.sample_freq);

	delta = (s64)(period - hwc->sample_period);
	delta = (delta + 7) / 8; /* low pass filter */

	sample_period = hwc->sample_period + delta;

	if (!sample_period)
		sample_period = 1;

	perf_log_period(counter, sample_period);

	hwc->sample_period = sample_period;
}

static void perf_ctx_adjust_freq(struct perf_counter_context *ctx)
{
	struct perf_counter *counter;
	struct hw_perf_counter *hwc;
	u64 interrupts, freq;

	spin_lock(&ctx->lock);
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state != PERF_COUNTER_STATE_ACTIVE)
			continue;

		hwc = &counter->hw;

		interrupts = hwc->interrupts;
		hwc->interrupts = 0;

		/*
		 * unthrottle counters on the tick
		 */
		if (interrupts == MAX_INTERRUPTS) {
			perf_log_throttle(counter, 1);
			counter->pmu->unthrottle(counter);
			interrupts = 2*sysctl_perf_counter_sample_rate/HZ;
		}

		if (!counter->attr.freq || !counter->attr.sample_freq)
			continue;

		/*
		 * if the specified freq < HZ then we need to skip ticks
		 */
		if (counter->attr.sample_freq < HZ) {
			freq = counter->attr.sample_freq;

			hwc->freq_count += freq;
			hwc->freq_interrupts += interrupts;

			if (hwc->freq_count < HZ)
				continue;

			interrupts = hwc->freq_interrupts;
			hwc->freq_interrupts = 0;
			hwc->freq_count -= HZ;
		} else
			freq = HZ;

		perf_adjust_period(counter, freq * interrupts);

		/*
		 * In order to avoid being stalled by an (accidental) huge
		 * sample period, force reset the sample period if we didn't
		 * get any events in this freq period.
		 */
		if (!interrupts) {
			perf_disable();
			counter->pmu->disable(counter);
			atomic64_set(&hwc->period_left, 0);
			counter->pmu->enable(counter);
			perf_enable();
		}
	}
	spin_unlock(&ctx->lock);
}

/*
 * Round-robin a context's counters:
 */
static void rotate_ctx(struct perf_counter_context *ctx)
{
	struct perf_counter *counter;

	if (!ctx->nr_counters)
		return;

	spin_lock(&ctx->lock);
	/*
	 * Rotate the first entry last (works just fine for group counters too):
	 */
	perf_disable();
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		list_move_tail(&counter->list_entry, &ctx->counter_list);
		break;
	}
	perf_enable();

	spin_unlock(&ctx->lock);
}

void perf_counter_task_tick(struct task_struct *curr, int cpu)
{
	struct perf_cpu_context *cpuctx;
	struct perf_counter_context *ctx;

	if (!atomic_read(&nr_counters))
		return;

	cpuctx = &per_cpu(perf_cpu_context, cpu);
	ctx = curr->perf_counter_ctxp;

	perf_ctx_adjust_freq(&cpuctx->ctx);
	if (ctx)
		perf_ctx_adjust_freq(ctx);

	perf_counter_cpu_sched_out(cpuctx);
	if (ctx)
		__perf_counter_task_sched_out(ctx);

	rotate_ctx(&cpuctx->ctx);
	if (ctx)
		rotate_ctx(ctx);

	perf_counter_cpu_sched_in(cpuctx, cpu);
	if (ctx)
		perf_counter_task_sched_in(curr, cpu);
}

/*
 * Enable all of a task's counters that have been marked enable-on-exec.
 * This expects task == current.
 */
static void perf_counter_enable_on_exec(struct task_struct *task)
{
	struct perf_counter_context *ctx;
	struct perf_counter *counter;
	unsigned long flags;
	int enabled = 0;

	local_irq_save(flags);
	ctx = task->perf_counter_ctxp;
	if (!ctx || !ctx->nr_counters)
		goto out;

	__perf_counter_task_sched_out(ctx);

	spin_lock(&ctx->lock);

	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (!counter->attr.enable_on_exec)
			continue;
		counter->attr.enable_on_exec = 0;
		if (counter->state >= PERF_COUNTER_STATE_INACTIVE)
			continue;
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		counter->tstamp_enabled =
			ctx->time - counter->total_time_enabled;
		enabled = 1;
	}

	/*
	 * Unclone this context if we enabled any counter.
	 */
	if (enabled && ctx->parent_ctx) {
		put_ctx(ctx->parent_ctx);
		ctx->parent_ctx = NULL;
	}

	spin_unlock(&ctx->lock);

	perf_counter_task_sched_in(task, smp_processor_id());
 out:
	local_irq_restore(flags);
}

/*
 * Cross CPU call to read the hardware counter
 */
static void __perf_counter_read(void *info)
{
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	unsigned long flags;

	local_irq_save(flags);
	if (ctx->is_active)
		update_context_time(ctx);
	counter->pmu->read(counter);
	update_counter_times(counter);
	local_irq_restore(flags);
}

static u64 perf_counter_read(struct perf_counter *counter)
{
	/*
	 * If counter is enabled and currently active on a CPU, update the
	 * value in the counter structure:
	 */
	if (counter->state == PERF_COUNTER_STATE_ACTIVE) {
		smp_call_function_single(counter->oncpu,
					 __perf_counter_read, counter, 1);
	} else if (counter->state == PERF_COUNTER_STATE_INACTIVE) {
		update_counter_times(counter);
	}

	return atomic64_read(&counter->count);
}

/*
 * Initialize the perf_counter context in a task_struct:
 */
static void
__perf_counter_init_context(struct perf_counter_context *ctx,
			    struct task_struct *task)
{
	memset(ctx, 0, sizeof(*ctx));
	spin_lock_init(&ctx->lock);
	mutex_init(&ctx->mutex);
	INIT_LIST_HEAD(&ctx->counter_list);
	INIT_LIST_HEAD(&ctx->event_list);
	atomic_set(&ctx->refcount, 1);
	ctx->task = task;
}

static struct perf_counter_context *find_get_context(pid_t pid, int cpu)
{
	struct perf_counter_context *parent_ctx;
	struct perf_counter_context *ctx;
	struct perf_cpu_context *cpuctx;
	struct task_struct *task;
	unsigned long flags;
	int err;

	/*
	 * If cpu is not a wildcard then this is a percpu counter:
	 */
	if (cpu != -1) {
		/* Must be root to operate on a CPU counter: */
		if (perf_paranoid_cpu() && !capable(CAP_SYS_ADMIN))
			return ERR_PTR(-EACCES);

		if (cpu < 0 || cpu > num_possible_cpus())
			return ERR_PTR(-EINVAL);

		/*
		 * We could be clever and allow to attach a counter to an
		 * offline CPU and activate it when the CPU comes up, but
		 * that's for later.
		 */
		if (!cpu_isset(cpu, cpu_online_map))
			return ERR_PTR(-ENODEV);

		cpuctx = &per_cpu(perf_cpu_context, cpu);
		ctx = &cpuctx->ctx;
		get_ctx(ctx);

		return ctx;
	}

	rcu_read_lock();
	if (!pid)
		task = current;
	else
		task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task)
		return ERR_PTR(-ESRCH);

	/*
	 * Can't attach counters to a dying task.
	 */
	err = -ESRCH;
	if (task->flags & PF_EXITING)
		goto errout;

	/* Reuse ptrace permission checks for now. */
	err = -EACCES;
	if (!ptrace_may_access(task, PTRACE_MODE_READ))
		goto errout;

 retry:
	ctx = perf_lock_task_context(task, &flags);
	if (ctx) {
		parent_ctx = ctx->parent_ctx;
		if (parent_ctx) {
			put_ctx(parent_ctx);
			ctx->parent_ctx = NULL;		/* no longer a clone */
		}
		spin_unlock_irqrestore(&ctx->lock, flags);
	}

	if (!ctx) {
		ctx = kmalloc(sizeof(struct perf_counter_context), GFP_KERNEL);
		err = -ENOMEM;
		if (!ctx)
			goto errout;
		__perf_counter_init_context(ctx, task);
		get_ctx(ctx);
		if (cmpxchg(&task->perf_counter_ctxp, NULL, ctx)) {
			/*
			 * We raced with some other task; use
			 * the context they set.
			 */
			kfree(ctx);
			goto retry;
		}
		get_task_struct(task);
	}

	put_task_struct(task);
	return ctx;

 errout:
	put_task_struct(task);
	return ERR_PTR(err);
}

static void free_counter_rcu(struct rcu_head *head)
{
	struct perf_counter *counter;

	counter = container_of(head, struct perf_counter, rcu_head);
	if (counter->ns)
		put_pid_ns(counter->ns);
	kfree(counter);
}

static void perf_pending_sync(struct perf_counter *counter);

static void free_counter(struct perf_counter *counter)
{
	perf_pending_sync(counter);

	if (!counter->parent) {
		atomic_dec(&nr_counters);
		if (counter->attr.mmap)
			atomic_dec(&nr_mmap_counters);
		if (counter->attr.comm)
			atomic_dec(&nr_comm_counters);
	}

	if (counter->destroy)
		counter->destroy(counter);

	put_ctx(counter->ctx);
	call_rcu(&counter->rcu_head, free_counter_rcu);
}

/*
 * Called when the last reference to the file is gone.
 */
static int perf_release(struct inode *inode, struct file *file)
{
	struct perf_counter *counter = file->private_data;
	struct perf_counter_context *ctx = counter->ctx;

	file->private_data = NULL;

	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	perf_counter_remove_from_context(counter);
	mutex_unlock(&ctx->mutex);

	mutex_lock(&counter->owner->perf_counter_mutex);
	list_del_init(&counter->owner_entry);
	mutex_unlock(&counter->owner->perf_counter_mutex);
	put_task_struct(counter->owner);

	free_counter(counter);

	return 0;
}

/*
 * Read the performance counter - simple non blocking version for now
 */
static ssize_t
perf_read_hw(struct perf_counter *counter, char __user *buf, size_t count)
{
	u64 values[4];
	int n;

	/*
	 * Return end-of-file for a read on a counter that is in
	 * error state (i.e. because it was pinned but it couldn't be
	 * scheduled on to the CPU at some point).
	 */
	if (counter->state == PERF_COUNTER_STATE_ERROR)
		return 0;

	WARN_ON_ONCE(counter->ctx->parent_ctx);
	mutex_lock(&counter->child_mutex);
	values[0] = perf_counter_read(counter);
	n = 1;
	if (counter->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = counter->total_time_enabled +
			atomic64_read(&counter->child_total_time_enabled);
	if (counter->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = counter->total_time_running +
			atomic64_read(&counter->child_total_time_running);
	if (counter->attr.read_format & PERF_FORMAT_ID)
		values[n++] = counter->id;
	mutex_unlock(&counter->child_mutex);

	if (count < n * sizeof(u64))
		return -EINVAL;
	count = n * sizeof(u64);

	if (copy_to_user(buf, values, count))
		return -EFAULT;

	return count;
}

static ssize_t
perf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct perf_counter *counter = file->private_data;

	return perf_read_hw(counter, buf, count);
}

static unsigned int perf_poll(struct file *file, poll_table *wait)
{
	struct perf_counter *counter = file->private_data;
	struct perf_mmap_data *data;
	unsigned int events = POLL_HUP;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (data)
		events = atomic_xchg(&data->poll, 0);
	rcu_read_unlock();

	poll_wait(file, &counter->waitq, wait);

	return events;
}

static void perf_counter_reset(struct perf_counter *counter)
{
	(void)perf_counter_read(counter);
	atomic64_set(&counter->count, 0);
	perf_counter_update_userpage(counter);
}

/*
 * Holding the top-level counter's child_mutex means that any
 * descendant process that has inherited this counter will block
 * in sync_child_counter if it goes to exit, thus satisfying the
 * task existence requirements of perf_counter_enable/disable.
 */
static void perf_counter_for_each_child(struct perf_counter *counter,
					void (*func)(struct perf_counter *))
{
	struct perf_counter *child;

	WARN_ON_ONCE(counter->ctx->parent_ctx);
	mutex_lock(&counter->child_mutex);
	func(counter);
	list_for_each_entry(child, &counter->child_list, child_list)
		func(child);
	mutex_unlock(&counter->child_mutex);
}

static void perf_counter_for_each(struct perf_counter *counter,
				  void (*func)(struct perf_counter *))
{
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_counter *sibling;

	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	counter = counter->group_leader;

	perf_counter_for_each_child(counter, func);
	func(counter);
	list_for_each_entry(sibling, &counter->sibling_list, list_entry)
		perf_counter_for_each_child(counter, func);
	mutex_unlock(&ctx->mutex);
}

static int perf_counter_period(struct perf_counter *counter, u64 __user *arg)
{
	struct perf_counter_context *ctx = counter->ctx;
	unsigned long size;
	int ret = 0;
	u64 value;

	if (!counter->attr.sample_period)
		return -EINVAL;

	size = copy_from_user(&value, arg, sizeof(value));
	if (size != sizeof(value))
		return -EFAULT;

	if (!value)
		return -EINVAL;

	spin_lock_irq(&ctx->lock);
	if (counter->attr.freq) {
		if (value > sysctl_perf_counter_sample_rate) {
			ret = -EINVAL;
			goto unlock;
		}

		counter->attr.sample_freq = value;
	} else {
		perf_log_period(counter, value);

		counter->attr.sample_period = value;
		counter->hw.sample_period = value;
	}
unlock:
	spin_unlock_irq(&ctx->lock);

	return ret;
}

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct perf_counter *counter = file->private_data;
	void (*func)(struct perf_counter *);
	u32 flags = arg;

	switch (cmd) {
	case PERF_COUNTER_IOC_ENABLE:
		func = perf_counter_enable;
		break;
	case PERF_COUNTER_IOC_DISABLE:
		func = perf_counter_disable;
		break;
	case PERF_COUNTER_IOC_RESET:
		func = perf_counter_reset;
		break;

	case PERF_COUNTER_IOC_REFRESH:
		return perf_counter_refresh(counter, arg);

	case PERF_COUNTER_IOC_PERIOD:
		return perf_counter_period(counter, (u64 __user *)arg);

	default:
		return -ENOTTY;
	}

	if (flags & PERF_IOC_FLAG_GROUP)
		perf_counter_for_each(counter, func);
	else
		perf_counter_for_each_child(counter, func);

	return 0;
}

int perf_counter_task_enable(void)
{
	struct perf_counter *counter;

	mutex_lock(&current->perf_counter_mutex);
	list_for_each_entry(counter, &current->perf_counter_list, owner_entry)
		perf_counter_for_each_child(counter, perf_counter_enable);
	mutex_unlock(&current->perf_counter_mutex);

	return 0;
}

int perf_counter_task_disable(void)
{
	struct perf_counter *counter;

	mutex_lock(&current->perf_counter_mutex);
	list_for_each_entry(counter, &current->perf_counter_list, owner_entry)
		perf_counter_for_each_child(counter, perf_counter_disable);
	mutex_unlock(&current->perf_counter_mutex);

	return 0;
}

static int perf_counter_index(struct perf_counter *counter)
{
	if (counter->state != PERF_COUNTER_STATE_ACTIVE)
		return 0;

	return counter->hw.idx + 1 - PERF_COUNTER_INDEX_OFFSET;
}

/*
 * Callers need to ensure there can be no nesting of this function, otherwise
 * the seqlock logic goes bad. We can not serialize this because the arch
 * code calls this from NMI context.
 */
void perf_counter_update_userpage(struct perf_counter *counter)
{
	struct perf_counter_mmap_page *userpg;
	struct perf_mmap_data *data;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (!data)
		goto unlock;

	userpg = data->user_page;

	/*
	 * Disable preemption so as to not let the corresponding user-space
	 * spin too long if we get preempted.
	 */
	preempt_disable();
	++userpg->lock;
	barrier();
	userpg->index = perf_counter_index(counter);
	userpg->offset = atomic64_read(&counter->count);
	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		userpg->offset -= atomic64_read(&counter->hw.prev_count);

	userpg->time_enabled = counter->total_time_enabled +
			atomic64_read(&counter->child_total_time_enabled);

	userpg->time_running = counter->total_time_running +
			atomic64_read(&counter->child_total_time_running);

	barrier();
	++userpg->lock;
	preempt_enable();
unlock:
	rcu_read_unlock();
}

static int perf_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct perf_counter *counter = vma->vm_file->private_data;
	struct perf_mmap_data *data;
	int ret = VM_FAULT_SIGBUS;

	if (vmf->flags & FAULT_FLAG_MKWRITE) {
		if (vmf->pgoff == 0)
			ret = 0;
		return ret;
	}

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (!data)
		goto unlock;

	if (vmf->pgoff == 0) {
		vmf->page = virt_to_page(data->user_page);
	} else {
		int nr = vmf->pgoff - 1;

		if ((unsigned)nr > data->nr_pages)
			goto unlock;

		if (vmf->flags & FAULT_FLAG_WRITE)
			goto unlock;

		vmf->page = virt_to_page(data->data_pages[nr]);
	}

	get_page(vmf->page);
	vmf->page->mapping = vma->vm_file->f_mapping;
	vmf->page->index   = vmf->pgoff;

	ret = 0;
unlock:
	rcu_read_unlock();

	return ret;
}

static int perf_mmap_data_alloc(struct perf_counter *counter, int nr_pages)
{
	struct perf_mmap_data *data;
	unsigned long size;
	int i;

	WARN_ON(atomic_read(&counter->mmap_count));

	size = sizeof(struct perf_mmap_data);
	size += nr_pages * sizeof(void *);

	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		goto fail;

	data->user_page = (void *)get_zeroed_page(GFP_KERNEL);
	if (!data->user_page)
		goto fail_user_page;

	for (i = 0; i < nr_pages; i++) {
		data->data_pages[i] = (void *)get_zeroed_page(GFP_KERNEL);
		if (!data->data_pages[i])
			goto fail_data_pages;
	}

	data->nr_pages = nr_pages;
	atomic_set(&data->lock, -1);

	rcu_assign_pointer(counter->data, data);

	return 0;

fail_data_pages:
	for (i--; i >= 0; i--)
		free_page((unsigned long)data->data_pages[i]);

	free_page((unsigned long)data->user_page);

fail_user_page:
	kfree(data);

fail:
	return -ENOMEM;
}

static void perf_mmap_free_page(unsigned long addr)
{
	struct page *page = virt_to_page((void *)addr);

	page->mapping = NULL;
	__free_page(page);
}

static void __perf_mmap_data_free(struct rcu_head *rcu_head)
{
	struct perf_mmap_data *data;
	int i;

	data = container_of(rcu_head, struct perf_mmap_data, rcu_head);

	perf_mmap_free_page((unsigned long)data->user_page);
	for (i = 0; i < data->nr_pages; i++)
		perf_mmap_free_page((unsigned long)data->data_pages[i]);

	kfree(data);
}

static void perf_mmap_data_free(struct perf_counter *counter)
{
	struct perf_mmap_data *data = counter->data;

	WARN_ON(atomic_read(&counter->mmap_count));

	rcu_assign_pointer(counter->data, NULL);
	call_rcu(&data->rcu_head, __perf_mmap_data_free);
}

static void perf_mmap_open(struct vm_area_struct *vma)
{
	struct perf_counter *counter = vma->vm_file->private_data;

	atomic_inc(&counter->mmap_count);
}

static void perf_mmap_close(struct vm_area_struct *vma)
{
	struct perf_counter *counter = vma->vm_file->private_data;

	WARN_ON_ONCE(counter->ctx->parent_ctx);
	if (atomic_dec_and_mutex_lock(&counter->mmap_count, &counter->mmap_mutex)) {
		struct user_struct *user = current_user();

		atomic_long_sub(counter->data->nr_pages + 1, &user->locked_vm);
		vma->vm_mm->locked_vm -= counter->data->nr_locked;
		perf_mmap_data_free(counter);
		mutex_unlock(&counter->mmap_mutex);
	}
}

static struct vm_operations_struct perf_mmap_vmops = {
	.open		= perf_mmap_open,
	.close		= perf_mmap_close,
	.fault		= perf_mmap_fault,
	.page_mkwrite	= perf_mmap_fault,
};

static int perf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct perf_counter *counter = file->private_data;
	unsigned long user_locked, user_lock_limit;
	struct user_struct *user = current_user();
	unsigned long locked, lock_limit;
	unsigned long vma_size;
	unsigned long nr_pages;
	long user_extra, extra;
	int ret = 0;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	vma_size = vma->vm_end - vma->vm_start;
	nr_pages = (vma_size / PAGE_SIZE) - 1;

	/*
	 * If we have data pages ensure they're a power-of-two number, so we
	 * can do bitmasks instead of modulo.
	 */
	if (nr_pages != 0 && !is_power_of_2(nr_pages))
		return -EINVAL;

	if (vma_size != PAGE_SIZE * (1 + nr_pages))
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	WARN_ON_ONCE(counter->ctx->parent_ctx);
	mutex_lock(&counter->mmap_mutex);
	if (atomic_inc_not_zero(&counter->mmap_count)) {
		if (nr_pages != counter->data->nr_pages)
			ret = -EINVAL;
		goto unlock;
	}

	user_extra = nr_pages + 1;
	user_lock_limit = sysctl_perf_counter_mlock >> (PAGE_SHIFT - 10);

	/*
	 * Increase the limit linearly with more CPUs:
	 */
	user_lock_limit *= num_online_cpus();

	user_locked = atomic_long_read(&user->locked_vm) + user_extra;

	extra = 0;
	if (user_locked > user_lock_limit)
		extra = user_locked - user_lock_limit;

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;
	locked = vma->vm_mm->locked_vm + extra;

	if ((locked > lock_limit) && !capable(CAP_IPC_LOCK)) {
		ret = -EPERM;
		goto unlock;
	}

	WARN_ON(counter->data);
	ret = perf_mmap_data_alloc(counter, nr_pages);
	if (ret)
		goto unlock;

	atomic_set(&counter->mmap_count, 1);
	atomic_long_add(user_extra, &user->locked_vm);
	vma->vm_mm->locked_vm += extra;
	counter->data->nr_locked = extra;
	if (vma->vm_flags & VM_WRITE)
		counter->data->writable = 1;

unlock:
	mutex_unlock(&counter->mmap_mutex);

	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &perf_mmap_vmops;

	return ret;
}

static int perf_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct perf_counter *counter = filp->private_data;
	int retval;

	mutex_lock(&inode->i_mutex);
	retval = fasync_helper(fd, filp, on, &counter->fasync);
	mutex_unlock(&inode->i_mutex);

	if (retval < 0)
		return retval;

	return 0;
}

static const struct file_operations perf_fops = {
	.release		= perf_release,
	.read			= perf_read,
	.poll			= perf_poll,
	.unlocked_ioctl		= perf_ioctl,
	.compat_ioctl		= perf_ioctl,
	.mmap			= perf_mmap,
	.fasync			= perf_fasync,
};

/*
 * Perf counter wakeup
 *
 * If there's data, ensure we set the poll() state and publish everything
 * to user-space before waking everybody up.
 */

void perf_counter_wakeup(struct perf_counter *counter)
{
	wake_up_all(&counter->waitq);

	if (counter->pending_kill) {
		kill_fasync(&counter->fasync, SIGIO, counter->pending_kill);
		counter->pending_kill = 0;
	}
}

/*
 * Pending wakeups
 *
 * Handle the case where we need to wakeup up from NMI (or rq->lock) context.
 *
 * The NMI bit means we cannot possibly take locks. Therefore, maintain a
 * single linked list and use cmpxchg() to add entries lockless.
 */

static void perf_pending_counter(struct perf_pending_entry *entry)
{
	struct perf_counter *counter = container_of(entry,
			struct perf_counter, pending);

	if (counter->pending_disable) {
		counter->pending_disable = 0;
		perf_counter_disable(counter);
	}

	if (counter->pending_wakeup) {
		counter->pending_wakeup = 0;
		perf_counter_wakeup(counter);
	}
}

#define PENDING_TAIL ((struct perf_pending_entry *)-1UL)

static DEFINE_PER_CPU(struct perf_pending_entry *, perf_pending_head) = {
	PENDING_TAIL,
};

static void perf_pending_queue(struct perf_pending_entry *entry,
			       void (*func)(struct perf_pending_entry *))
{
	struct perf_pending_entry **head;

	if (cmpxchg(&entry->next, NULL, PENDING_TAIL) != NULL)
		return;

	entry->func = func;

	head = &get_cpu_var(perf_pending_head);

	do {
		entry->next = *head;
	} while (cmpxchg(head, entry->next, entry) != entry->next);

	set_perf_counter_pending();

	put_cpu_var(perf_pending_head);
}

static int __perf_pending_run(void)
{
	struct perf_pending_entry *list;
	int nr = 0;

	list = xchg(&__get_cpu_var(perf_pending_head), PENDING_TAIL);
	while (list != PENDING_TAIL) {
		void (*func)(struct perf_pending_entry *);
		struct perf_pending_entry *entry = list;

		list = list->next;

		func = entry->func;
		entry->next = NULL;
		/*
		 * Ensure we observe the unqueue before we issue the wakeup,
		 * so that we won't be waiting forever.
		 * -- see perf_not_pending().
		 */
		smp_wmb();

		func(entry);
		nr++;
	}

	return nr;
}

static inline int perf_not_pending(struct perf_counter *counter)
{
	/*
	 * If we flush on whatever cpu we run, there is a chance we don't
	 * need to wait.
	 */
	get_cpu();
	__perf_pending_run();
	put_cpu();

	/*
	 * Ensure we see the proper queue state before going to sleep
	 * so that we do not miss the wakeup. -- see perf_pending_handle()
	 */
	smp_rmb();
	return counter->pending.next == NULL;
}

static void perf_pending_sync(struct perf_counter *counter)
{
	wait_event(counter->waitq, perf_not_pending(counter));
}

void perf_counter_do_pending(void)
{
	__perf_pending_run();
}

/*
 * Callchain support -- arch specific
 */

__weak struct perf_callchain_entry *perf_callchain(struct pt_regs *regs)
{
	return NULL;
}

/*
 * Output
 */

struct perf_output_handle {
	struct perf_counter	*counter;
	struct perf_mmap_data	*data;
	unsigned long		head;
	unsigned long		offset;
	int			nmi;
	int			sample;
	int			locked;
	unsigned long		flags;
};

static bool perf_output_space(struct perf_mmap_data *data,
			      unsigned int offset, unsigned int head)
{
	unsigned long tail;
	unsigned long mask;

	if (!data->writable)
		return true;

	mask = (data->nr_pages << PAGE_SHIFT) - 1;
	/*
	 * Userspace could choose to issue a mb() before updating the tail
	 * pointer. So that all reads will be completed before the write is
	 * issued.
	 */
	tail = ACCESS_ONCE(data->user_page->data_tail);
	smp_rmb();

	offset = (offset - tail) & mask;
	head   = (head   - tail) & mask;

	if ((int)(head - offset) < 0)
		return false;

	return true;
}

static void perf_output_wakeup(struct perf_output_handle *handle)
{
	atomic_set(&handle->data->poll, POLL_IN);

	if (handle->nmi) {
		handle->counter->pending_wakeup = 1;
		perf_pending_queue(&handle->counter->pending,
				   perf_pending_counter);
	} else
		perf_counter_wakeup(handle->counter);
}

/*
 * Curious locking construct.
 *
 * We need to ensure a later event doesn't publish a head when a former
 * event isn't done writing. However since we need to deal with NMIs we
 * cannot fully serialize things.
 *
 * What we do is serialize between CPUs so we only have to deal with NMI
 * nesting on a single CPU.
 *
 * We only publish the head (and generate a wakeup) when the outer-most
 * event completes.
 */
static void perf_output_lock(struct perf_output_handle *handle)
{
	struct perf_mmap_data *data = handle->data;
	int cpu;

	handle->locked = 0;

	local_irq_save(handle->flags);
	cpu = smp_processor_id();

	if (in_nmi() && atomic_read(&data->lock) == cpu)
		return;

	while (atomic_cmpxchg(&data->lock, -1, cpu) != -1)
		cpu_relax();

	handle->locked = 1;
}

static void perf_output_unlock(struct perf_output_handle *handle)
{
	struct perf_mmap_data *data = handle->data;
	unsigned long head;
	int cpu;

	data->done_head = data->head;

	if (!handle->locked)
		goto out;

again:
	/*
	 * The xchg implies a full barrier that ensures all writes are done
	 * before we publish the new head, matched by a rmb() in userspace when
	 * reading this position.
	 */
	while ((head = atomic_long_xchg(&data->done_head, 0)))
		data->user_page->data_head = head;

	/*
	 * NMI can happen here, which means we can miss a done_head update.
	 */

	cpu = atomic_xchg(&data->lock, -1);
	WARN_ON_ONCE(cpu != smp_processor_id());

	/*
	 * Therefore we have to validate we did not indeed do so.
	 */
	if (unlikely(atomic_long_read(&data->done_head))) {
		/*
		 * Since we had it locked, we can lock it again.
		 */
		while (atomic_cmpxchg(&data->lock, -1, cpu) != -1)
			cpu_relax();

		goto again;
	}

	if (atomic_xchg(&data->wakeup, 0))
		perf_output_wakeup(handle);
out:
	local_irq_restore(handle->flags);
}

static void perf_output_copy(struct perf_output_handle *handle,
			     const void *buf, unsigned int len)
{
	unsigned int pages_mask;
	unsigned int offset;
	unsigned int size;
	void **pages;

	offset		= handle->offset;
	pages_mask	= handle->data->nr_pages - 1;
	pages		= handle->data->data_pages;

	do {
		unsigned int page_offset;
		int nr;

		nr	    = (offset >> PAGE_SHIFT) & pages_mask;
		page_offset = offset & (PAGE_SIZE - 1);
		size	    = min_t(unsigned int, PAGE_SIZE - page_offset, len);

		memcpy(pages[nr] + page_offset, buf, size);

		len	    -= size;
		buf	    += size;
		offset	    += size;
	} while (len);

	handle->offset = offset;

	/*
	 * Check we didn't copy past our reservation window, taking the
	 * possible unsigned int wrap into account.
	 */
	WARN_ON_ONCE(((long)(handle->head - handle->offset)) < 0);
}

#define perf_output_put(handle, x) \
	perf_output_copy((handle), &(x), sizeof(x))

static int perf_output_begin(struct perf_output_handle *handle,
			     struct perf_counter *counter, unsigned int size,
			     int nmi, int sample)
{
	struct perf_mmap_data *data;
	unsigned int offset, head;
	int have_lost;
	struct {
		struct perf_event_header header;
		u64			 id;
		u64			 lost;
	} lost_event;

	/*
	 * For inherited counters we send all the output towards the parent.
	 */
	if (counter->parent)
		counter = counter->parent;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (!data)
		goto out;

	handle->data	= data;
	handle->counter	= counter;
	handle->nmi	= nmi;
	handle->sample	= sample;

	if (!data->nr_pages)
		goto fail;

	have_lost = atomic_read(&data->lost);
	if (have_lost)
		size += sizeof(lost_event);

	perf_output_lock(handle);

	do {
		offset = head = atomic_long_read(&data->head);
		head += size;
		if (unlikely(!perf_output_space(data, offset, head)))
			goto fail;
	} while (atomic_long_cmpxchg(&data->head, offset, head) != offset);

	handle->offset	= offset;
	handle->head	= head;

	if ((offset >> PAGE_SHIFT) != (head >> PAGE_SHIFT))
		atomic_set(&data->wakeup, 1);

	if (have_lost) {
		lost_event.header.type = PERF_EVENT_LOST;
		lost_event.header.misc = 0;
		lost_event.header.size = sizeof(lost_event);
		lost_event.id          = counter->id;
		lost_event.lost        = atomic_xchg(&data->lost, 0);

		perf_output_put(handle, lost_event);
	}

	return 0;

fail:
	atomic_inc(&data->lost);
	perf_output_unlock(handle);
out:
	rcu_read_unlock();

	return -ENOSPC;
}

static void perf_output_end(struct perf_output_handle *handle)
{
	struct perf_counter *counter = handle->counter;
	struct perf_mmap_data *data = handle->data;

	int wakeup_events = counter->attr.wakeup_events;

	if (handle->sample && wakeup_events) {
		int events = atomic_inc_return(&data->events);
		if (events >= wakeup_events) {
			atomic_sub(wakeup_events, &data->events);
			atomic_set(&data->wakeup, 1);
		}
	}

	perf_output_unlock(handle);
	rcu_read_unlock();
}

static u32 perf_counter_pid(struct perf_counter *counter, struct task_struct *p)
{
	/*
	 * only top level counters have the pid namespace they were created in
	 */
	if (counter->parent)
		counter = counter->parent;

	return task_tgid_nr_ns(p, counter->ns);
}

static u32 perf_counter_tid(struct perf_counter *counter, struct task_struct *p)
{
	/*
	 * only top level counters have the pid namespace they were created in
	 */
	if (counter->parent)
		counter = counter->parent;

	return task_pid_nr_ns(p, counter->ns);
}

static void perf_counter_output(struct perf_counter *counter, int nmi,
				struct perf_sample_data *data)
{
	int ret;
	u64 sample_type = counter->attr.sample_type;
	struct perf_output_handle handle;
	struct perf_event_header header;
	u64 ip;
	struct {
		u32 pid, tid;
	} tid_entry;
	struct {
		u64 id;
		u64 counter;
	} group_entry;
	struct perf_callchain_entry *callchain = NULL;
	int callchain_size = 0;
	u64 time;
	struct {
		u32 cpu, reserved;
	} cpu_entry;

	header.type = PERF_EVENT_SAMPLE;
	header.size = sizeof(header);

	header.misc = 0;
	header.misc |= perf_misc_flags(data->regs);

	if (sample_type & PERF_SAMPLE_IP) {
		ip = perf_instruction_pointer(data->regs);
		header.size += sizeof(ip);
	}

	if (sample_type & PERF_SAMPLE_TID) {
		/* namespace issues */
		tid_entry.pid = perf_counter_pid(counter, current);
		tid_entry.tid = perf_counter_tid(counter, current);

		header.size += sizeof(tid_entry);
	}

	if (sample_type & PERF_SAMPLE_TIME) {
		/*
		 * Maybe do better on x86 and provide cpu_clock_nmi()
		 */
		time = sched_clock();

		header.size += sizeof(u64);
	}

	if (sample_type & PERF_SAMPLE_ADDR)
		header.size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_ID)
		header.size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_CPU) {
		header.size += sizeof(cpu_entry);

		cpu_entry.cpu = raw_smp_processor_id();
	}

	if (sample_type & PERF_SAMPLE_PERIOD)
		header.size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_GROUP) {
		header.size += sizeof(u64) +
			counter->nr_siblings * sizeof(group_entry);
	}

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
		callchain = perf_callchain(data->regs);

		if (callchain) {
			callchain_size = (1 + callchain->nr) * sizeof(u64);
			header.size += callchain_size;
		} else
			header.size += sizeof(u64);
	}

	ret = perf_output_begin(&handle, counter, header.size, nmi, 1);
	if (ret)
		return;

	perf_output_put(&handle, header);

	if (sample_type & PERF_SAMPLE_IP)
		perf_output_put(&handle, ip);

	if (sample_type & PERF_SAMPLE_TID)
		perf_output_put(&handle, tid_entry);

	if (sample_type & PERF_SAMPLE_TIME)
		perf_output_put(&handle, time);

	if (sample_type & PERF_SAMPLE_ADDR)
		perf_output_put(&handle, data->addr);

	if (sample_type & PERF_SAMPLE_ID)
		perf_output_put(&handle, counter->id);

	if (sample_type & PERF_SAMPLE_CPU)
		perf_output_put(&handle, cpu_entry);

	if (sample_type & PERF_SAMPLE_PERIOD)
		perf_output_put(&handle, data->period);

	/*
	 * XXX PERF_SAMPLE_GROUP vs inherited counters seems difficult.
	 */
	if (sample_type & PERF_SAMPLE_GROUP) {
		struct perf_counter *leader, *sub;
		u64 nr = counter->nr_siblings;

		perf_output_put(&handle, nr);

		leader = counter->group_leader;
		list_for_each_entry(sub, &leader->sibling_list, list_entry) {
			if (sub != counter)
				sub->pmu->read(sub);

			group_entry.id = sub->id;
			group_entry.counter = atomic64_read(&sub->count);

			perf_output_put(&handle, group_entry);
		}
	}

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
		if (callchain)
			perf_output_copy(&handle, callchain, callchain_size);
		else {
			u64 nr = 0;
			perf_output_put(&handle, nr);
		}
	}

	perf_output_end(&handle);
}

/*
 * read event
 */

struct perf_read_event {
	struct perf_event_header	header;

	u32				pid;
	u32				tid;
	u64				value;
	u64				format[3];
};

static void
perf_counter_read_event(struct perf_counter *counter,
			struct task_struct *task)
{
	struct perf_output_handle handle;
	struct perf_read_event event = {
		.header = {
			.type = PERF_EVENT_READ,
			.misc = 0,
			.size = sizeof(event) - sizeof(event.format),
		},
		.pid = perf_counter_pid(counter, task),
		.tid = perf_counter_tid(counter, task),
		.value = atomic64_read(&counter->count),
	};
	int ret, i = 0;

	if (counter->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
		event.header.size += sizeof(u64);
		event.format[i++] = counter->total_time_enabled;
	}

	if (counter->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
		event.header.size += sizeof(u64);
		event.format[i++] = counter->total_time_running;
	}

	if (counter->attr.read_format & PERF_FORMAT_ID) {
		u64 id;

		event.header.size += sizeof(u64);
		if (counter->parent)
			id = counter->parent->id;
		else
			id = counter->id;

		event.format[i++] = id;
	}

	ret = perf_output_begin(&handle, counter, event.header.size, 0, 0);
	if (ret)
		return;

	perf_output_copy(&handle, &event, event.header.size);
	perf_output_end(&handle);
}

/*
 * fork tracking
 */

struct perf_fork_event {
	struct task_struct	*task;

	struct {
		struct perf_event_header	header;

		u32				pid;
		u32				ppid;
	} event;
};

static void perf_counter_fork_output(struct perf_counter *counter,
				     struct perf_fork_event *fork_event)
{
	struct perf_output_handle handle;
	int size = fork_event->event.header.size;
	struct task_struct *task = fork_event->task;
	int ret = perf_output_begin(&handle, counter, size, 0, 0);

	if (ret)
		return;

	fork_event->event.pid = perf_counter_pid(counter, task);
	fork_event->event.ppid = perf_counter_pid(counter, task->real_parent);

	perf_output_put(&handle, fork_event->event);
	perf_output_end(&handle);
}

static int perf_counter_fork_match(struct perf_counter *counter)
{
	if (counter->attr.comm || counter->attr.mmap)
		return 1;

	return 0;
}

static void perf_counter_fork_ctx(struct perf_counter_context *ctx,
				  struct perf_fork_event *fork_event)
{
	struct perf_counter *counter;

	if (system_state != SYSTEM_RUNNING || list_empty(&ctx->event_list))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(counter, &ctx->event_list, event_entry) {
		if (perf_counter_fork_match(counter))
			perf_counter_fork_output(counter, fork_event);
	}
	rcu_read_unlock();
}

static void perf_counter_fork_event(struct perf_fork_event *fork_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_counter_context *ctx;

	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_counter_fork_ctx(&cpuctx->ctx, fork_event);
	put_cpu_var(perf_cpu_context);

	rcu_read_lock();
	/*
	 * doesn't really matter which of the child contexts the
	 * events ends up in.
	 */
	ctx = rcu_dereference(current->perf_counter_ctxp);
	if (ctx)
		perf_counter_fork_ctx(ctx, fork_event);
	rcu_read_unlock();
}

void perf_counter_fork(struct task_struct *task)
{
	struct perf_fork_event fork_event;

	if (!atomic_read(&nr_comm_counters) &&
	    !atomic_read(&nr_mmap_counters))
		return;

	fork_event = (struct perf_fork_event){
		.task	= task,
		.event  = {
			.header = {
				.type = PERF_EVENT_FORK,
				.size = sizeof(fork_event.event),
			},
		},
	};

	perf_counter_fork_event(&fork_event);
}

/*
 * comm tracking
 */

struct perf_comm_event {
	struct task_struct	*task;
	char			*comm;
	int			comm_size;

	struct {
		struct perf_event_header	header;

		u32				pid;
		u32				tid;
	} event;
};

static void perf_counter_comm_output(struct perf_counter *counter,
				     struct perf_comm_event *comm_event)
{
	struct perf_output_handle handle;
	int size = comm_event->event.header.size;
	int ret = perf_output_begin(&handle, counter, size, 0, 0);

	if (ret)
		return;

	comm_event->event.pid = perf_counter_pid(counter, comm_event->task);
	comm_event->event.tid = perf_counter_tid(counter, comm_event->task);

	perf_output_put(&handle, comm_event->event);
	perf_output_copy(&handle, comm_event->comm,
				   comm_event->comm_size);
	perf_output_end(&handle);
}

static int perf_counter_comm_match(struct perf_counter *counter)
{
	if (counter->attr.comm)
		return 1;

	return 0;
}

static void perf_counter_comm_ctx(struct perf_counter_context *ctx,
				  struct perf_comm_event *comm_event)
{
	struct perf_counter *counter;

	if (system_state != SYSTEM_RUNNING || list_empty(&ctx->event_list))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(counter, &ctx->event_list, event_entry) {
		if (perf_counter_comm_match(counter))
			perf_counter_comm_output(counter, comm_event);
	}
	rcu_read_unlock();
}

static void perf_counter_comm_event(struct perf_comm_event *comm_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_counter_context *ctx;
	unsigned int size;
	char *comm = comm_event->task->comm;

	size = ALIGN(strlen(comm)+1, sizeof(u64));

	comm_event->comm = comm;
	comm_event->comm_size = size;

	comm_event->event.header.size = sizeof(comm_event->event) + size;

	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_counter_comm_ctx(&cpuctx->ctx, comm_event);
	put_cpu_var(perf_cpu_context);

	rcu_read_lock();
	/*
	 * doesn't really matter which of the child contexts the
	 * events ends up in.
	 */
	ctx = rcu_dereference(current->perf_counter_ctxp);
	if (ctx)
		perf_counter_comm_ctx(ctx, comm_event);
	rcu_read_unlock();
}

void perf_counter_comm(struct task_struct *task)
{
	struct perf_comm_event comm_event;

	if (task->perf_counter_ctxp)
		perf_counter_enable_on_exec(task);

	if (!atomic_read(&nr_comm_counters))
		return;

	comm_event = (struct perf_comm_event){
		.task	= task,
		.event  = {
			.header = { .type = PERF_EVENT_COMM, },
		},
	};

	perf_counter_comm_event(&comm_event);
}

/*
 * mmap tracking
 */

struct perf_mmap_event {
	struct vm_area_struct	*vma;

	const char		*file_name;
	int			file_size;

	struct {
		struct perf_event_header	header;

		u32				pid;
		u32				tid;
		u64				start;
		u64				len;
		u64				pgoff;
	} event;
};

static void perf_counter_mmap_output(struct perf_counter *counter,
				     struct perf_mmap_event *mmap_event)
{
	struct perf_output_handle handle;
	int size = mmap_event->event.header.size;
	int ret = perf_output_begin(&handle, counter, size, 0, 0);

	if (ret)
		return;

	mmap_event->event.pid = perf_counter_pid(counter, current);
	mmap_event->event.tid = perf_counter_tid(counter, current);

	perf_output_put(&handle, mmap_event->event);
	perf_output_copy(&handle, mmap_event->file_name,
				   mmap_event->file_size);
	perf_output_end(&handle);
}

static int perf_counter_mmap_match(struct perf_counter *counter,
				   struct perf_mmap_event *mmap_event)
{
	if (counter->attr.mmap)
		return 1;

	return 0;
}

static void perf_counter_mmap_ctx(struct perf_counter_context *ctx,
				  struct perf_mmap_event *mmap_event)
{
	struct perf_counter *counter;

	if (system_state != SYSTEM_RUNNING || list_empty(&ctx->event_list))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(counter, &ctx->event_list, event_entry) {
		if (perf_counter_mmap_match(counter, mmap_event))
			perf_counter_mmap_output(counter, mmap_event);
	}
	rcu_read_unlock();
}

static void perf_counter_mmap_event(struct perf_mmap_event *mmap_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_counter_context *ctx;
	struct vm_area_struct *vma = mmap_event->vma;
	struct file *file = vma->vm_file;
	unsigned int size;
	char tmp[16];
	char *buf = NULL;
	const char *name;

	if (file) {
		buf = kzalloc(PATH_MAX, GFP_KERNEL);
		if (!buf) {
			name = strncpy(tmp, "//enomem", sizeof(tmp));
			goto got_name;
		}
		name = d_path(&file->f_path, buf, PATH_MAX);
		if (IS_ERR(name)) {
			name = strncpy(tmp, "//toolong", sizeof(tmp));
			goto got_name;
		}
	} else {
		name = arch_vma_name(mmap_event->vma);
		if (name)
			goto got_name;

		if (!vma->vm_mm) {
			name = strncpy(tmp, "[vdso]", sizeof(tmp));
			goto got_name;
		}

		name = strncpy(tmp, "//anon", sizeof(tmp));
		goto got_name;
	}

got_name:
	size = ALIGN(strlen(name)+1, sizeof(u64));

	mmap_event->file_name = name;
	mmap_event->file_size = size;

	mmap_event->event.header.size = sizeof(mmap_event->event) + size;

	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_counter_mmap_ctx(&cpuctx->ctx, mmap_event);
	put_cpu_var(perf_cpu_context);

	rcu_read_lock();
	/*
	 * doesn't really matter which of the child contexts the
	 * events ends up in.
	 */
	ctx = rcu_dereference(current->perf_counter_ctxp);
	if (ctx)
		perf_counter_mmap_ctx(ctx, mmap_event);
	rcu_read_unlock();

	kfree(buf);
}

void __perf_counter_mmap(struct vm_area_struct *vma)
{
	struct perf_mmap_event mmap_event;

	if (!atomic_read(&nr_mmap_counters))
		return;

	mmap_event = (struct perf_mmap_event){
		.vma	= vma,
		.event  = {
			.header = { .type = PERF_EVENT_MMAP, },
			.start  = vma->vm_start,
			.len    = vma->vm_end - vma->vm_start,
			.pgoff  = vma->vm_pgoff,
		},
	};

	perf_counter_mmap_event(&mmap_event);
}

/*
 * Log sample_period changes so that analyzing tools can re-normalize the
 * event flow.
 */

struct freq_event {
	struct perf_event_header	header;
	u64				time;
	u64				id;
	u64				period;
};

static void perf_log_period(struct perf_counter *counter, u64 period)
{
	struct perf_output_handle handle;
	struct freq_event event;
	int ret;

	if (counter->hw.sample_period == period)
		return;

	if (counter->attr.sample_type & PERF_SAMPLE_PERIOD)
		return;

	event = (struct freq_event) {
		.header = {
			.type = PERF_EVENT_PERIOD,
			.misc = 0,
			.size = sizeof(event),
		},
		.time = sched_clock(),
		.id = counter->id,
		.period = period,
	};

	ret = perf_output_begin(&handle, counter, sizeof(event), 1, 0);
	if (ret)
		return;

	perf_output_put(&handle, event);
	perf_output_end(&handle);
}

/*
 * IRQ throttle logging
 */

static void perf_log_throttle(struct perf_counter *counter, int enable)
{
	struct perf_output_handle handle;
	int ret;

	struct {
		struct perf_event_header	header;
		u64				time;
		u64				id;
	} throttle_event = {
		.header = {
			.type = PERF_EVENT_THROTTLE + 1,
			.misc = 0,
			.size = sizeof(throttle_event),
		},
		.time	= sched_clock(),
		.id	= counter->id,
	};

	ret = perf_output_begin(&handle, counter, sizeof(throttle_event), 1, 0);
	if (ret)
		return;

	perf_output_put(&handle, throttle_event);
	perf_output_end(&handle);
}

/*
 * Generic counter overflow handling, sampling.
 */

int perf_counter_overflow(struct perf_counter *counter, int nmi,
			  struct perf_sample_data *data)
{
	int events = atomic_read(&counter->event_limit);
	int throttle = counter->pmu->unthrottle != NULL;
	struct hw_perf_counter *hwc = &counter->hw;
	int ret = 0;

	if (!throttle) {
		hwc->interrupts++;
	} else {
		if (hwc->interrupts != MAX_INTERRUPTS) {
			hwc->interrupts++;
			if (HZ * hwc->interrupts >
					(u64)sysctl_perf_counter_sample_rate) {
				hwc->interrupts = MAX_INTERRUPTS;
				perf_log_throttle(counter, 0);
				ret = 1;
			}
		} else {
			/*
			 * Keep re-disabling counters even though on the previous
			 * pass we disabled it - just in case we raced with a
			 * sched-in and the counter got enabled again:
			 */
			ret = 1;
		}
	}

	if (counter->attr.freq) {
		u64 now = sched_clock();
		s64 delta = now - hwc->freq_stamp;

		hwc->freq_stamp = now;

		if (delta > 0 && delta < TICK_NSEC)
			perf_adjust_period(counter, NSEC_PER_SEC / (int)delta);
	}

	/*
	 * XXX event_limit might not quite work as expected on inherited
	 * counters
	 */

	counter->pending_kill = POLL_IN;
	if (events && atomic_dec_and_test(&counter->event_limit)) {
		ret = 1;
		counter->pending_kill = POLL_HUP;
		if (nmi) {
			counter->pending_disable = 1;
			perf_pending_queue(&counter->pending,
					   perf_pending_counter);
		} else
			perf_counter_disable(counter);
	}

	perf_counter_output(counter, nmi, data);
	return ret;
}

/*
 * Generic software counter infrastructure
 */

static void perf_swcounter_update(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	u64 prev, now;
	s64 delta;

again:
	prev = atomic64_read(&hwc->prev_count);
	now = atomic64_read(&hwc->count);
	if (atomic64_cmpxchg(&hwc->prev_count, prev, now) != prev)
		goto again;

	delta = now - prev;

	atomic64_add(delta, &counter->count);
	atomic64_sub(delta, &hwc->period_left);
}

static void perf_swcounter_set_period(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	s64 left = atomic64_read(&hwc->period_left);
	s64 period = hwc->sample_period;

	if (unlikely(left <= -period)) {
		left = period;
		atomic64_set(&hwc->period_left, left);
		hwc->last_period = period;
	}

	if (unlikely(left <= 0)) {
		left += period;
		atomic64_add(period, &hwc->period_left);
		hwc->last_period = period;
	}

	atomic64_set(&hwc->prev_count, -left);
	atomic64_set(&hwc->count, -left);
}

static enum hrtimer_restart perf_swcounter_hrtimer(struct hrtimer *hrtimer)
{
	enum hrtimer_restart ret = HRTIMER_RESTART;
	struct perf_sample_data data;
	struct perf_counter *counter;
	u64 period;

	counter	= container_of(hrtimer, struct perf_counter, hw.hrtimer);
	counter->pmu->read(counter);

	data.addr = 0;
	data.regs = get_irq_regs();
	/*
	 * In case we exclude kernel IPs or are somehow not in interrupt
	 * context, provide the next best thing, the user IP.
	 */
	if ((counter->attr.exclude_kernel || !data.regs) &&
			!counter->attr.exclude_user)
		data.regs = task_pt_regs(current);

	if (data.regs) {
		if (perf_counter_overflow(counter, 0, &data))
			ret = HRTIMER_NORESTART;
	}

	period = max_t(u64, 10000, counter->hw.sample_period);
	hrtimer_forward_now(hrtimer, ns_to_ktime(period));

	return ret;
}

static void perf_swcounter_overflow(struct perf_counter *counter,
				    int nmi, struct perf_sample_data *data)
{
	data->period = counter->hw.last_period;

	perf_swcounter_update(counter);
	perf_swcounter_set_period(counter);
	if (perf_counter_overflow(counter, nmi, data))
		/* soft-disable the counter */
		;
}

static int perf_swcounter_is_counting(struct perf_counter *counter)
{
	struct perf_counter_context *ctx;
	unsigned long flags;
	int count;

	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		return 1;

	if (counter->state != PERF_COUNTER_STATE_INACTIVE)
		return 0;

	/*
	 * If the counter is inactive, it could be just because
	 * its task is scheduled out, or because it's in a group
	 * which could not go on the PMU.  We want to count in
	 * the first case but not the second.  If the context is
	 * currently active then an inactive software counter must
	 * be the second case.  If it's not currently active then
	 * we need to know whether the counter was active when the
	 * context was last active, which we can determine by
	 * comparing counter->tstamp_stopped with ctx->time.
	 *
	 * We are within an RCU read-side critical section,
	 * which protects the existence of *ctx.
	 */
	ctx = counter->ctx;
	spin_lock_irqsave(&ctx->lock, flags);
	count = 1;
	/* Re-check state now we have the lock */
	if (counter->state < PERF_COUNTER_STATE_INACTIVE ||
	    counter->ctx->is_active ||
	    counter->tstamp_stopped < ctx->time)
		count = 0;
	spin_unlock_irqrestore(&ctx->lock, flags);
	return count;
}

static int perf_swcounter_match(struct perf_counter *counter,
				enum perf_type_id type,
				u32 event, struct pt_regs *regs)
{
	if (!perf_swcounter_is_counting(counter))
		return 0;

	if (counter->attr.type != type)
		return 0;
	if (counter->attr.config != event)
		return 0;

	if (regs) {
		if (counter->attr.exclude_user && user_mode(regs))
			return 0;

		if (counter->attr.exclude_kernel && !user_mode(regs))
			return 0;
	}

	return 1;
}

static void perf_swcounter_add(struct perf_counter *counter, u64 nr,
			       int nmi, struct perf_sample_data *data)
{
	int neg = atomic64_add_negative(nr, &counter->hw.count);

	if (counter->hw.sample_period && !neg && data->regs)
		perf_swcounter_overflow(counter, nmi, data);
}

static void perf_swcounter_ctx_event(struct perf_counter_context *ctx,
				     enum perf_type_id type,
				     u32 event, u64 nr, int nmi,
				     struct perf_sample_data *data)
{
	struct perf_counter *counter;

	if (system_state != SYSTEM_RUNNING || list_empty(&ctx->event_list))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(counter, &ctx->event_list, event_entry) {
		if (perf_swcounter_match(counter, type, event, data->regs))
			perf_swcounter_add(counter, nr, nmi, data);
	}
	rcu_read_unlock();
}

static int *perf_swcounter_recursion_context(struct perf_cpu_context *cpuctx)
{
	if (in_nmi())
		return &cpuctx->recursion[3];

	if (in_irq())
		return &cpuctx->recursion[2];

	if (in_softirq())
		return &cpuctx->recursion[1];

	return &cpuctx->recursion[0];
}

static void do_perf_swcounter_event(enum perf_type_id type, u32 event,
				    u64 nr, int nmi,
				    struct perf_sample_data *data)
{
	struct perf_cpu_context *cpuctx = &get_cpu_var(perf_cpu_context);
	int *recursion = perf_swcounter_recursion_context(cpuctx);
	struct perf_counter_context *ctx;

	if (*recursion)
		goto out;

	(*recursion)++;
	barrier();

	perf_swcounter_ctx_event(&cpuctx->ctx, type, event,
				 nr, nmi, data);
	rcu_read_lock();
	/*
	 * doesn't really matter which of the child contexts the
	 * events ends up in.
	 */
	ctx = rcu_dereference(current->perf_counter_ctxp);
	if (ctx)
		perf_swcounter_ctx_event(ctx, type, event, nr, nmi, data);
	rcu_read_unlock();

	barrier();
	(*recursion)--;

out:
	put_cpu_var(perf_cpu_context);
}

void __perf_swcounter_event(u32 event, u64 nr, int nmi,
			    struct pt_regs *regs, u64 addr)
{
	struct perf_sample_data data = {
		.regs = regs,
		.addr = addr,
	};

	do_perf_swcounter_event(PERF_TYPE_SOFTWARE, event, nr, nmi, &data);
}

static void perf_swcounter_read(struct perf_counter *counter)
{
	perf_swcounter_update(counter);
}

static int perf_swcounter_enable(struct perf_counter *counter)
{
	perf_swcounter_set_period(counter);
	return 0;
}

static void perf_swcounter_disable(struct perf_counter *counter)
{
	perf_swcounter_update(counter);
}

static const struct pmu perf_ops_generic = {
	.enable		= perf_swcounter_enable,
	.disable	= perf_swcounter_disable,
	.read		= perf_swcounter_read,
};

/*
 * Software counter: cpu wall time clock
 */

static void cpu_clock_perf_counter_update(struct perf_counter *counter)
{
	int cpu = raw_smp_processor_id();
	s64 prev;
	u64 now;

	now = cpu_clock(cpu);
	prev = atomic64_read(&counter->hw.prev_count);
	atomic64_set(&counter->hw.prev_count, now);
	atomic64_add(now - prev, &counter->count);
}

static int cpu_clock_perf_counter_enable(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	int cpu = raw_smp_processor_id();

	atomic64_set(&hwc->prev_count, cpu_clock(cpu));
	hrtimer_init(&hwc->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hwc->hrtimer.function = perf_swcounter_hrtimer;
	if (hwc->sample_period) {
		u64 period = max_t(u64, 10000, hwc->sample_period);
		__hrtimer_start_range_ns(&hwc->hrtimer,
				ns_to_ktime(period), 0,
				HRTIMER_MODE_REL, 0);
	}

	return 0;
}

static void cpu_clock_perf_counter_disable(struct perf_counter *counter)
{
	if (counter->hw.sample_period)
		hrtimer_cancel(&counter->hw.hrtimer);
	cpu_clock_perf_counter_update(counter);
}

static void cpu_clock_perf_counter_read(struct perf_counter *counter)
{
	cpu_clock_perf_counter_update(counter);
}

static const struct pmu perf_ops_cpu_clock = {
	.enable		= cpu_clock_perf_counter_enable,
	.disable	= cpu_clock_perf_counter_disable,
	.read		= cpu_clock_perf_counter_read,
};

/*
 * Software counter: task time clock
 */

static void task_clock_perf_counter_update(struct perf_counter *counter, u64 now)
{
	u64 prev;
	s64 delta;

	prev = atomic64_xchg(&counter->hw.prev_count, now);
	delta = now - prev;
	atomic64_add(delta, &counter->count);
}

static int task_clock_perf_counter_enable(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	u64 now;

	now = counter->ctx->time;

	atomic64_set(&hwc->prev_count, now);
	hrtimer_init(&hwc->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hwc->hrtimer.function = perf_swcounter_hrtimer;
	if (hwc->sample_period) {
		u64 period = max_t(u64, 10000, hwc->sample_period);
		__hrtimer_start_range_ns(&hwc->hrtimer,
				ns_to_ktime(period), 0,
				HRTIMER_MODE_REL, 0);
	}

	return 0;
}

static void task_clock_perf_counter_disable(struct perf_counter *counter)
{
	if (counter->hw.sample_period)
		hrtimer_cancel(&counter->hw.hrtimer);
	task_clock_perf_counter_update(counter, counter->ctx->time);

}

static void task_clock_perf_counter_read(struct perf_counter *counter)
{
	u64 time;

	if (!in_nmi()) {
		update_context_time(counter->ctx);
		time = counter->ctx->time;
	} else {
		u64 now = perf_clock();
		u64 delta = now - counter->ctx->timestamp;
		time = counter->ctx->time + delta;
	}

	task_clock_perf_counter_update(counter, time);
}

static const struct pmu perf_ops_task_clock = {
	.enable		= task_clock_perf_counter_enable,
	.disable	= task_clock_perf_counter_disable,
	.read		= task_clock_perf_counter_read,
};

#ifdef CONFIG_EVENT_PROFILE
void perf_tpcounter_event(int event_id)
{
	struct perf_sample_data data = {
		.regs = get_irq_regs();
		.addr = 0,
	};

	if (!data.regs)
		data.regs = task_pt_regs(current);

	do_perf_swcounter_event(PERF_TYPE_TRACEPOINT, event_id, 1, 1, &data);
}
EXPORT_SYMBOL_GPL(perf_tpcounter_event);

extern int ftrace_profile_enable(int);
extern void ftrace_profile_disable(int);

static void tp_perf_counter_destroy(struct perf_counter *counter)
{
	ftrace_profile_disable(perf_event_id(&counter->attr));
}

static const struct pmu *tp_perf_counter_init(struct perf_counter *counter)
{
	int event_id = perf_event_id(&counter->attr);
	int ret;

	ret = ftrace_profile_enable(event_id);
	if (ret)
		return NULL;

	counter->destroy = tp_perf_counter_destroy;

	return &perf_ops_generic;
}
#else
static const struct pmu *tp_perf_counter_init(struct perf_counter *counter)
{
	return NULL;
}
#endif

atomic_t perf_swcounter_enabled[PERF_COUNT_SW_MAX];

static void sw_perf_counter_destroy(struct perf_counter *counter)
{
	u64 event = counter->attr.config;

	WARN_ON(counter->parent);

	atomic_dec(&perf_swcounter_enabled[event]);
}

static const struct pmu *sw_perf_counter_init(struct perf_counter *counter)
{
	const struct pmu *pmu = NULL;
	u64 event = counter->attr.config;

	/*
	 * Software counters (currently) can't in general distinguish
	 * between user, kernel and hypervisor events.
	 * However, context switches and cpu migrations are considered
	 * to be kernel events, and page faults are never hypervisor
	 * events.
	 */
	switch (event) {
	case PERF_COUNT_SW_CPU_CLOCK:
		pmu = &perf_ops_cpu_clock;

		break;
	case PERF_COUNT_SW_TASK_CLOCK:
		/*
		 * If the user instantiates this as a per-cpu counter,
		 * use the cpu_clock counter instead.
		 */
		if (counter->ctx->task)
			pmu = &perf_ops_task_clock;
		else
			pmu = &perf_ops_cpu_clock;

		break;
	case PERF_COUNT_SW_PAGE_FAULTS:
	case PERF_COUNT_SW_PAGE_FAULTS_MIN:
	case PERF_COUNT_SW_PAGE_FAULTS_MAJ:
	case PERF_COUNT_SW_CONTEXT_SWITCHES:
	case PERF_COUNT_SW_CPU_MIGRATIONS:
		if (!counter->parent) {
			atomic_inc(&perf_swcounter_enabled[event]);
			counter->destroy = sw_perf_counter_destroy;
		}
		pmu = &perf_ops_generic;
		break;
	}

	return pmu;
}

/*
 * Allocate and initialize a counter structure
 */
static struct perf_counter *
perf_counter_alloc(struct perf_counter_attr *attr,
		   int cpu,
		   struct perf_counter_context *ctx,
		   struct perf_counter *group_leader,
		   struct perf_counter *parent_counter,
		   gfp_t gfpflags)
{
	const struct pmu *pmu;
	struct perf_counter *counter;
	struct hw_perf_counter *hwc;
	long err;

	counter = kzalloc(sizeof(*counter), gfpflags);
	if (!counter)
		return ERR_PTR(-ENOMEM);

	/*
	 * Single counters are their own group leaders, with an
	 * empty sibling list:
	 */
	if (!group_leader)
		group_leader = counter;

	mutex_init(&counter->child_mutex);
	INIT_LIST_HEAD(&counter->child_list);

	INIT_LIST_HEAD(&counter->list_entry);
	INIT_LIST_HEAD(&counter->event_entry);
	INIT_LIST_HEAD(&counter->sibling_list);
	init_waitqueue_head(&counter->waitq);

	mutex_init(&counter->mmap_mutex);

	counter->cpu		= cpu;
	counter->attr		= *attr;
	counter->group_leader	= group_leader;
	counter->pmu		= NULL;
	counter->ctx		= ctx;
	counter->oncpu		= -1;

	counter->parent		= parent_counter;

	counter->ns		= get_pid_ns(current->nsproxy->pid_ns);
	counter->id		= atomic64_inc_return(&perf_counter_id);

	counter->state		= PERF_COUNTER_STATE_INACTIVE;

	if (attr->disabled)
		counter->state = PERF_COUNTER_STATE_OFF;

	pmu = NULL;

	hwc = &counter->hw;
	hwc->sample_period = attr->sample_period;
	if (attr->freq && attr->sample_freq)
		hwc->sample_period = 1;

	atomic64_set(&hwc->period_left, hwc->sample_period);

	/*
	 * we currently do not support PERF_SAMPLE_GROUP on inherited counters
	 */
	if (attr->inherit && (attr->sample_type & PERF_SAMPLE_GROUP))
		goto done;

	switch (attr->type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		pmu = hw_perf_counter_init(counter);
		break;

	case PERF_TYPE_SOFTWARE:
		pmu = sw_perf_counter_init(counter);
		break;

	case PERF_TYPE_TRACEPOINT:
		pmu = tp_perf_counter_init(counter);
		break;

	default:
		break;
	}
done:
	err = 0;
	if (!pmu)
		err = -EINVAL;
	else if (IS_ERR(pmu))
		err = PTR_ERR(pmu);

	if (err) {
		if (counter->ns)
			put_pid_ns(counter->ns);
		kfree(counter);
		return ERR_PTR(err);
	}

	counter->pmu = pmu;

	if (!counter->parent) {
		atomic_inc(&nr_counters);
		if (counter->attr.mmap)
			atomic_inc(&nr_mmap_counters);
		if (counter->attr.comm)
			atomic_inc(&nr_comm_counters);
	}

	return counter;
}

static int perf_copy_attr(struct perf_counter_attr __user *uattr,
			  struct perf_counter_attr *attr)
{
	int ret;
	u32 size;

	if (!access_ok(VERIFY_WRITE, uattr, PERF_ATTR_SIZE_VER0))
		return -EFAULT;

	/*
	 * zero the full structure, so that a short copy will be nice.
	 */
	memset(attr, 0, sizeof(*attr));

	ret = get_user(size, &uattr->size);
	if (ret)
		return ret;

	if (size > PAGE_SIZE)	/* silly large */
		goto err_size;

	if (!size)		/* abi compat */
		size = PERF_ATTR_SIZE_VER0;

	if (size < PERF_ATTR_SIZE_VER0)
		goto err_size;

	/*
	 * If we're handed a bigger struct than we know of,
	 * ensure all the unknown bits are 0.
	 */
	if (size > sizeof(*attr)) {
		unsigned long val;
		unsigned long __user *addr;
		unsigned long __user *end;

		addr = PTR_ALIGN((void __user *)uattr + sizeof(*attr),
				sizeof(unsigned long));
		end  = PTR_ALIGN((void __user *)uattr + size,
				sizeof(unsigned long));

		for (; addr < end; addr += sizeof(unsigned long)) {
			ret = get_user(val, addr);
			if (ret)
				return ret;
			if (val)
				goto err_size;
		}
	}

	ret = copy_from_user(attr, uattr, size);
	if (ret)
		return -EFAULT;

	/*
	 * If the type exists, the corresponding creation will verify
	 * the attr->config.
	 */
	if (attr->type >= PERF_TYPE_MAX)
		return -EINVAL;

	if (attr->__reserved_1 || attr->__reserved_2 || attr->__reserved_3)
		return -EINVAL;

	if (attr->sample_type & ~(PERF_SAMPLE_MAX-1))
		return -EINVAL;

	if (attr->read_format & ~(PERF_FORMAT_MAX-1))
		return -EINVAL;

out:
	return ret;

err_size:
	put_user(sizeof(*attr), &uattr->size);
	ret = -E2BIG;
	goto out;
}

/**
 * sys_perf_counter_open - open a performance counter, associate it to a task/cpu
 *
 * @attr_uptr:	event type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader counter fd
 */
SYSCALL_DEFINE5(perf_counter_open,
		struct perf_counter_attr __user *, attr_uptr,
		pid_t, pid, int, cpu, int, group_fd, unsigned long, flags)
{
	struct perf_counter *counter, *group_leader;
	struct perf_counter_attr attr;
	struct perf_counter_context *ctx;
	struct file *counter_file = NULL;
	struct file *group_file = NULL;
	int fput_needed = 0;
	int fput_needed2 = 0;
	int ret;

	/* for future expandability... */
	if (flags)
		return -EINVAL;

	ret = perf_copy_attr(attr_uptr, &attr);
	if (ret)
		return ret;

	if (!attr.exclude_kernel) {
		if (perf_paranoid_kernel() && !capable(CAP_SYS_ADMIN))
			return -EACCES;
	}

	if (attr.freq) {
		if (attr.sample_freq > sysctl_perf_counter_sample_rate)
			return -EINVAL;
	}

	/*
	 * Get the target context (task or percpu):
	 */
	ctx = find_get_context(pid, cpu);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/*
	 * Look up the group leader (we will attach this counter to it):
	 */
	group_leader = NULL;
	if (group_fd != -1) {
		ret = -EINVAL;
		group_file = fget_light(group_fd, &fput_needed);
		if (!group_file)
			goto err_put_context;
		if (group_file->f_op != &perf_fops)
			goto err_put_context;

		group_leader = group_file->private_data;
		/*
		 * Do not allow a recursive hierarchy (this new sibling
		 * becoming part of another group-sibling):
		 */
		if (group_leader->group_leader != group_leader)
			goto err_put_context;
		/*
		 * Do not allow to attach to a group in a different
		 * task or CPU context:
		 */
		if (group_leader->ctx != ctx)
			goto err_put_context;
		/*
		 * Only a group leader can be exclusive or pinned
		 */
		if (attr.exclusive || attr.pinned)
			goto err_put_context;
	}

	counter = perf_counter_alloc(&attr, cpu, ctx, group_leader,
				     NULL, GFP_KERNEL);
	ret = PTR_ERR(counter);
	if (IS_ERR(counter))
		goto err_put_context;

	ret = anon_inode_getfd("[perf_counter]", &perf_fops, counter, 0);
	if (ret < 0)
		goto err_free_put_context;

	counter_file = fget_light(ret, &fput_needed2);
	if (!counter_file)
		goto err_free_put_context;

	counter->filp = counter_file;
	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	perf_install_in_context(ctx, counter, cpu);
	++ctx->generation;
	mutex_unlock(&ctx->mutex);

	counter->owner = current;
	get_task_struct(current);
	mutex_lock(&current->perf_counter_mutex);
	list_add_tail(&counter->owner_entry, &current->perf_counter_list);
	mutex_unlock(&current->perf_counter_mutex);

	fput_light(counter_file, fput_needed2);

out_fput:
	fput_light(group_file, fput_needed);

	return ret;

err_free_put_context:
	kfree(counter);

err_put_context:
	put_ctx(ctx);

	goto out_fput;
}

/*
 * inherit a counter from parent task to child task:
 */
static struct perf_counter *
inherit_counter(struct perf_counter *parent_counter,
	      struct task_struct *parent,
	      struct perf_counter_context *parent_ctx,
	      struct task_struct *child,
	      struct perf_counter *group_leader,
	      struct perf_counter_context *child_ctx)
{
	struct perf_counter *child_counter;

	/*
	 * Instead of creating recursive hierarchies of counters,
	 * we link inherited counters back to the original parent,
	 * which has a filp for sure, which we use as the reference
	 * count:
	 */
	if (parent_counter->parent)
		parent_counter = parent_counter->parent;

	child_counter = perf_counter_alloc(&parent_counter->attr,
					   parent_counter->cpu, child_ctx,
					   group_leader, parent_counter,
					   GFP_KERNEL);
	if (IS_ERR(child_counter))
		return child_counter;
	get_ctx(child_ctx);

	/*
	 * Make the child state follow the state of the parent counter,
	 * not its attr.disabled bit.  We hold the parent's mutex,
	 * so we won't race with perf_counter_{en, dis}able_family.
	 */
	if (parent_counter->state >= PERF_COUNTER_STATE_INACTIVE)
		child_counter->state = PERF_COUNTER_STATE_INACTIVE;
	else
		child_counter->state = PERF_COUNTER_STATE_OFF;

	if (parent_counter->attr.freq)
		child_counter->hw.sample_period = parent_counter->hw.sample_period;

	/*
	 * Link it up in the child's context:
	 */
	add_counter_to_ctx(child_counter, child_ctx);

	/*
	 * Get a reference to the parent filp - we will fput it
	 * when the child counter exits. This is safe to do because
	 * we are in the parent and we know that the filp still
	 * exists and has a nonzero count:
	 */
	atomic_long_inc(&parent_counter->filp->f_count);

	/*
	 * Link this into the parent counter's child list
	 */
	WARN_ON_ONCE(parent_counter->ctx->parent_ctx);
	mutex_lock(&parent_counter->child_mutex);
	list_add_tail(&child_counter->child_list, &parent_counter->child_list);
	mutex_unlock(&parent_counter->child_mutex);

	return child_counter;
}

static int inherit_group(struct perf_counter *parent_counter,
	      struct task_struct *parent,
	      struct perf_counter_context *parent_ctx,
	      struct task_struct *child,
	      struct perf_counter_context *child_ctx)
{
	struct perf_counter *leader;
	struct perf_counter *sub;
	struct perf_counter *child_ctr;

	leader = inherit_counter(parent_counter, parent, parent_ctx,
				 child, NULL, child_ctx);
	if (IS_ERR(leader))
		return PTR_ERR(leader);
	list_for_each_entry(sub, &parent_counter->sibling_list, list_entry) {
		child_ctr = inherit_counter(sub, parent, parent_ctx,
					    child, leader, child_ctx);
		if (IS_ERR(child_ctr))
			return PTR_ERR(child_ctr);
	}
	return 0;
}

static void sync_child_counter(struct perf_counter *child_counter,
			       struct task_struct *child)
{
	struct perf_counter *parent_counter = child_counter->parent;
	u64 child_val;

	if (child_counter->attr.inherit_stat)
		perf_counter_read_event(child_counter, child);

	child_val = atomic64_read(&child_counter->count);

	/*
	 * Add back the child's count to the parent's count:
	 */
	atomic64_add(child_val, &parent_counter->count);
	atomic64_add(child_counter->total_time_enabled,
		     &parent_counter->child_total_time_enabled);
	atomic64_add(child_counter->total_time_running,
		     &parent_counter->child_total_time_running);

	/*
	 * Remove this counter from the parent's list
	 */
	WARN_ON_ONCE(parent_counter->ctx->parent_ctx);
	mutex_lock(&parent_counter->child_mutex);
	list_del_init(&child_counter->child_list);
	mutex_unlock(&parent_counter->child_mutex);

	/*
	 * Release the parent counter, if this was the last
	 * reference to it.
	 */
	fput(parent_counter->filp);
}

static void
__perf_counter_exit_task(struct perf_counter *child_counter,
			 struct perf_counter_context *child_ctx,
			 struct task_struct *child)
{
	struct perf_counter *parent_counter;

	update_counter_times(child_counter);
	perf_counter_remove_from_context(child_counter);

	parent_counter = child_counter->parent;
	/*
	 * It can happen that parent exits first, and has counters
	 * that are still around due to the child reference. These
	 * counters need to be zapped - but otherwise linger.
	 */
	if (parent_counter) {
		sync_child_counter(child_counter, child);
		free_counter(child_counter);
	}
}

/*
 * When a child task exits, feed back counter values to parent counters.
 */
void perf_counter_exit_task(struct task_struct *child)
{
	struct perf_counter *child_counter, *tmp;
	struct perf_counter_context *child_ctx;
	unsigned long flags;

	if (likely(!child->perf_counter_ctxp))
		return;

	local_irq_save(flags);
	/*
	 * We can't reschedule here because interrupts are disabled,
	 * and either child is current or it is a task that can't be
	 * scheduled, so we are now safe from rescheduling changing
	 * our context.
	 */
	child_ctx = child->perf_counter_ctxp;
	__perf_counter_task_sched_out(child_ctx);

	/*
	 * Take the context lock here so that if find_get_context is
	 * reading child->perf_counter_ctxp, we wait until it has
	 * incremented the context's refcount before we do put_ctx below.
	 */
	spin_lock(&child_ctx->lock);
	child->perf_counter_ctxp = NULL;
	if (child_ctx->parent_ctx) {
		/*
		 * This context is a clone; unclone it so it can't get
		 * swapped to another process while we're removing all
		 * the counters from it.
		 */
		put_ctx(child_ctx->parent_ctx);
		child_ctx->parent_ctx = NULL;
	}
	spin_unlock(&child_ctx->lock);
	local_irq_restore(flags);

	/*
	 * We can recurse on the same lock type through:
	 *
	 *   __perf_counter_exit_task()
	 *     sync_child_counter()
	 *       fput(parent_counter->filp)
	 *         perf_release()
	 *           mutex_lock(&ctx->mutex)
	 *
	 * But since its the parent context it won't be the same instance.
	 */
	mutex_lock_nested(&child_ctx->mutex, SINGLE_DEPTH_NESTING);

again:
	list_for_each_entry_safe(child_counter, tmp, &child_ctx->counter_list,
				 list_entry)
		__perf_counter_exit_task(child_counter, child_ctx, child);

	/*
	 * If the last counter was a group counter, it will have appended all
	 * its siblings to the list, but we obtained 'tmp' before that which
	 * will still point to the list head terminating the iteration.
	 */
	if (!list_empty(&child_ctx->counter_list))
		goto again;

	mutex_unlock(&child_ctx->mutex);

	put_ctx(child_ctx);
}

/*
 * free an unexposed, unused context as created by inheritance by
 * init_task below, used by fork() in case of fail.
 */
void perf_counter_free_task(struct task_struct *task)
{
	struct perf_counter_context *ctx = task->perf_counter_ctxp;
	struct perf_counter *counter, *tmp;

	if (!ctx)
		return;

	mutex_lock(&ctx->mutex);
again:
	list_for_each_entry_safe(counter, tmp, &ctx->counter_list, list_entry) {
		struct perf_counter *parent = counter->parent;

		if (WARN_ON_ONCE(!parent))
			continue;

		mutex_lock(&parent->child_mutex);
		list_del_init(&counter->child_list);
		mutex_unlock(&parent->child_mutex);

		fput(parent->filp);

		list_del_counter(counter, ctx);
		free_counter(counter);
	}

	if (!list_empty(&ctx->counter_list))
		goto again;

	mutex_unlock(&ctx->mutex);

	put_ctx(ctx);
}

/*
 * Initialize the perf_counter context in task_struct
 */
int perf_counter_init_task(struct task_struct *child)
{
	struct perf_counter_context *child_ctx, *parent_ctx;
	struct perf_counter_context *cloned_ctx;
	struct perf_counter *counter;
	struct task_struct *parent = current;
	int inherited_all = 1;
	int ret = 0;

	child->perf_counter_ctxp = NULL;

	mutex_init(&child->perf_counter_mutex);
	INIT_LIST_HEAD(&child->perf_counter_list);

	if (likely(!parent->perf_counter_ctxp))
		return 0;

	/*
	 * This is executed from the parent task context, so inherit
	 * counters that have been marked for cloning.
	 * First allocate and initialize a context for the child.
	 */

	child_ctx = kmalloc(sizeof(struct perf_counter_context), GFP_KERNEL);
	if (!child_ctx)
		return -ENOMEM;

	__perf_counter_init_context(child_ctx, child);
	child->perf_counter_ctxp = child_ctx;
	get_task_struct(child);

	/*
	 * If the parent's context is a clone, pin it so it won't get
	 * swapped under us.
	 */
	parent_ctx = perf_pin_task_context(parent);

	/*
	 * No need to check if parent_ctx != NULL here; since we saw
	 * it non-NULL earlier, the only reason for it to become NULL
	 * is if we exit, and since we're currently in the middle of
	 * a fork we can't be exiting at the same time.
	 */

	/*
	 * Lock the parent list. No need to lock the child - not PID
	 * hashed yet and not running, so nobody can access it.
	 */
	mutex_lock(&parent_ctx->mutex);

	/*
	 * We dont have to disable NMIs - we are only looking at
	 * the list, not manipulating it:
	 */
	list_for_each_entry_rcu(counter, &parent_ctx->event_list, event_entry) {
		if (counter != counter->group_leader)
			continue;

		if (!counter->attr.inherit) {
			inherited_all = 0;
			continue;
		}

		ret = inherit_group(counter, parent, parent_ctx,
					     child, child_ctx);
		if (ret) {
			inherited_all = 0;
			break;
		}
	}

	if (inherited_all) {
		/*
		 * Mark the child context as a clone of the parent
		 * context, or of whatever the parent is a clone of.
		 * Note that if the parent is a clone, it could get
		 * uncloned at any point, but that doesn't matter
		 * because the list of counters and the generation
		 * count can't have changed since we took the mutex.
		 */
		cloned_ctx = rcu_dereference(parent_ctx->parent_ctx);
		if (cloned_ctx) {
			child_ctx->parent_ctx = cloned_ctx;
			child_ctx->parent_gen = parent_ctx->parent_gen;
		} else {
			child_ctx->parent_ctx = parent_ctx;
			child_ctx->parent_gen = parent_ctx->generation;
		}
		get_ctx(child_ctx->parent_ctx);
	}

	mutex_unlock(&parent_ctx->mutex);

	perf_unpin_context(parent_ctx);

	return ret;
}

static void __cpuinit perf_counter_init_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx;

	cpuctx = &per_cpu(perf_cpu_context, cpu);
	__perf_counter_init_context(&cpuctx->ctx, NULL);

	spin_lock(&perf_resource_lock);
	cpuctx->max_pertask = perf_max_counters - perf_reserved_percpu;
	spin_unlock(&perf_resource_lock);

	hw_perf_counter_setup(cpu);
}

#ifdef CONFIG_HOTPLUG_CPU
static void __perf_counter_exit_cpu(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter_context *ctx = &cpuctx->ctx;
	struct perf_counter *counter, *tmp;

	list_for_each_entry_safe(counter, tmp, &ctx->counter_list, list_entry)
		__perf_counter_remove_from_context(counter);
}
static void perf_counter_exit_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = &cpuctx->ctx;

	mutex_lock(&ctx->mutex);
	smp_call_function_single(cpu, __perf_counter_exit_cpu, NULL, 1);
	mutex_unlock(&ctx->mutex);
}
#else
static inline void perf_counter_exit_cpu(int cpu) { }
#endif

static int __cpuinit
perf_cpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action) {

	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		perf_counter_init_cpu(cpu);
		break;

	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		perf_counter_exit_cpu(cpu);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

/*
 * This has to have a higher priority than migration_notifier in sched.c.
 */
static struct notifier_block __cpuinitdata perf_cpu_nb = {
	.notifier_call		= perf_cpu_notify,
	.priority		= 20,
};

void __init perf_counter_init(void)
{
	perf_cpu_notify(&perf_cpu_nb, (unsigned long)CPU_UP_PREPARE,
			(void *)(long)smp_processor_id());
	register_cpu_notifier(&perf_cpu_nb);
}

static ssize_t perf_show_reserve_percpu(struct sysdev_class *class, char *buf)
{
	return sprintf(buf, "%d\n", perf_reserved_percpu);
}

static ssize_t
perf_set_reserve_percpu(struct sysdev_class *class,
			const char *buf,
			size_t count)
{
	struct perf_cpu_context *cpuctx;
	unsigned long val;
	int err, cpu, mpt;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > perf_max_counters)
		return -EINVAL;

	spin_lock(&perf_resource_lock);
	perf_reserved_percpu = val;
	for_each_online_cpu(cpu) {
		cpuctx = &per_cpu(perf_cpu_context, cpu);
		spin_lock_irq(&cpuctx->ctx.lock);
		mpt = min(perf_max_counters - cpuctx->ctx.nr_counters,
			  perf_max_counters - perf_reserved_percpu);
		cpuctx->max_pertask = mpt;
		spin_unlock_irq(&cpuctx->ctx.lock);
	}
	spin_unlock(&perf_resource_lock);

	return count;
}

static ssize_t perf_show_overcommit(struct sysdev_class *class, char *buf)
{
	return sprintf(buf, "%d\n", perf_overcommit);
}

static ssize_t
perf_set_overcommit(struct sysdev_class *class, const char *buf, size_t count)
{
	unsigned long val;
	int err;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > 1)
		return -EINVAL;

	spin_lock(&perf_resource_lock);
	perf_overcommit = val;
	spin_unlock(&perf_resource_lock);

	return count;
}

static SYSDEV_CLASS_ATTR(
				reserve_percpu,
				0644,
				perf_show_reserve_percpu,
				perf_set_reserve_percpu
			);

static SYSDEV_CLASS_ATTR(
				overcommit,
				0644,
				perf_show_overcommit,
				perf_set_overcommit
			);

static struct attribute *perfclass_attrs[] = {
	&attr_reserve_percpu.attr,
	&attr_overcommit.attr,
	NULL
};

static struct attribute_group perfclass_attr_group = {
	.attrs			= perfclass_attrs,
	.name			= "perf_counters",
};

static int __init perf_counter_sysfs_init(void)
{
	return sysfs_create_group(&cpu_sysdev_class.kset.kobj,
				  &perfclass_attr_group);
}
device_initcall(perf_counter_sysfs_init);

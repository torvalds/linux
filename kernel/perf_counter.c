/*
 * Performance counter core code
 *
 *  Copyright(C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2008 Red Hat, Inc., Ingo Molnar
 *
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
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/vmstat.h>
#include <linux/hardirq.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/kernel_stat.h>
#include <linux/perf_counter.h>
#include <linux/dcache.h>

#include <asm/irq_regs.h>

/*
 * Each CPU has a list of per CPU counters:
 */
DEFINE_PER_CPU(struct perf_cpu_context, perf_cpu_context);

int perf_max_counters __read_mostly = 1;
static int perf_reserved_percpu __read_mostly;
static int perf_overcommit __read_mostly = 1;

/*
 * Mutex for (sysadmin-configurable) counter reservations:
 */
static DEFINE_MUTEX(perf_resource_mutex);

/*
 * Architecture provided APIs - weak aliases:
 */
extern __weak const struct hw_perf_counter_ops *
hw_perf_counter_init(struct perf_counter *counter)
{
	return NULL;
}

u64 __weak hw_perf_save_disable(void)		{ return 0; }
void __weak hw_perf_restore(u64 ctrl)		{ barrier(); }
void __weak hw_perf_counter_setup(int cpu)	{ barrier(); }
int __weak hw_perf_group_sched_in(struct perf_counter *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx, int cpu)
{
	return 0;
}

void __weak perf_counter_print_debug(void)	{ }

static void
list_add_counter(struct perf_counter *counter, struct perf_counter_context *ctx)
{
	struct perf_counter *group_leader = counter->group_leader;

	/*
	 * Depending on whether it is a standalone or sibling counter,
	 * add it straight to the context's counter list, or to the group
	 * leader's sibling list:
	 */
	if (counter->group_leader == counter)
		list_add_tail(&counter->list_entry, &ctx->counter_list);
	else {
		list_add_tail(&counter->list_entry, &group_leader->sibling_list);
		group_leader->nr_siblings++;
	}

	list_add_rcu(&counter->event_entry, &ctx->event_list);
}

static void
list_del_counter(struct perf_counter *counter, struct perf_counter_context *ctx)
{
	struct perf_counter *sibling, *tmp;

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
	counter->hw_ops->disable(counter);
	counter->oncpu = -1;

	if (!is_software_counter(counter))
		cpuctx->active_oncpu--;
	ctx->nr_active--;
	if (counter->hw_event.exclusive || !cpuctx->active_oncpu)
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

	if (group_counter->hw_event.exclusive)
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
	unsigned long flags;
	u64 perf_flags;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock_irqsave(&ctx->lock, flags);

	counter_sched_out(counter, cpuctx, ctx);

	counter->task = NULL;
	ctx->nr_counters--;

	/*
	 * Protect the list operation against NMI by disabling the
	 * counters on a global level. NOP for non NMI based counters.
	 */
	perf_flags = hw_perf_save_disable();
	list_del_counter(counter, ctx);
	hw_perf_restore(perf_flags);

	if (!ctx->task) {
		/*
		 * Allow more per task counters with respect to the
		 * reservation:
		 */
		cpuctx->max_pertask =
			min(perf_max_counters - ctx->nr_counters,
			    perf_max_counters - perf_reserved_percpu);
	}

	spin_unlock_irqrestore(&ctx->lock, flags);
}


/*
 * Remove the counter from a task's (or a CPU's) list of counters.
 *
 * Must be called with counter->mutex and ctx->mutex held.
 *
 * CPU counters are removed with a smp call. For task counters we only
 * call when the task is on a CPU.
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
		ctx->nr_counters--;
		list_del_counter(counter, ctx);
		counter->task = NULL;
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
	unsigned long flags;

	/*
	 * If this is a per-task counter, need to check whether this
	 * counter's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock_irqsave(&ctx->lock, flags);

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

	spin_unlock_irqrestore(&ctx->lock, flags);
}

/*
 * Disable a counter.
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

/*
 * Disable a counter and all its children.
 */
static void perf_counter_disable_family(struct perf_counter *counter)
{
	struct perf_counter *child;

	perf_counter_disable(counter);

	/*
	 * Lock the mutex to protect the list of children
	 */
	mutex_lock(&counter->mutex);
	list_for_each_entry(child, &counter->child_list, child_list)
		perf_counter_disable(child);
	mutex_unlock(&counter->mutex);
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

	if (counter->hw_ops->enable(counter)) {
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		counter->oncpu = -1;
		return -EAGAIN;
	}

	counter->tstamp_running += ctx->time - counter->tstamp_stopped;

	if (!is_software_counter(counter))
		cpuctx->active_oncpu++;
	ctx->nr_active++;

	if (counter->hw_event.exclusive)
		cpuctx->exclusive = 1;

	return 0;
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
	if (counter->hw_event.exclusive && cpuctx->active_oncpu)
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
	ctx->nr_counters++;
	counter->prev_state = PERF_COUNTER_STATE_OFF;
	counter->tstamp_enabled = ctx->time;
	counter->tstamp_running = ctx->time;
	counter->tstamp_stopped = ctx->time;
}

/*
 * Cross CPU call to install and enable a performance counter
 */
static void __perf_install_in_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_counter *leader = counter->group_leader;
	int cpu = smp_processor_id();
	unsigned long flags;
	u64 perf_flags;
	int err;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock_irqsave(&ctx->lock, flags);
	update_context_time(ctx);

	/*
	 * Protect the list operation against NMI by disabling the
	 * counters on a global level. NOP for non NMI based counters.
	 */
	perf_flags = hw_perf_save_disable();

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
		if (leader->hw_event.pinned) {
			update_group_times(leader);
			leader->state = PERF_COUNTER_STATE_ERROR;
		}
	}

	if (!err && !ctx->task && cpuctx->max_pertask)
		cpuctx->max_pertask--;

 unlock:
	hw_perf_restore(perf_flags);

	spin_unlock_irqrestore(&ctx->lock, flags);
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

	counter->task = task;
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
	unsigned long flags;
	int err;

	/*
	 * If this is a per-task counter, need to check whether this
	 * counter's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	spin_lock_irqsave(&ctx->lock, flags);
	update_context_time(ctx);

	counter->prev_state = counter->state;
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

	if (!group_can_go_on(counter, cpuctx, 1))
		err = -EEXIST;
	else
		err = counter_sched_in(counter, cpuctx, ctx,
				       smp_processor_id());

	if (err) {
		/*
		 * If this counter can't go on and it's part of a
		 * group, then the whole group has to come off.
		 */
		if (leader != counter)
			group_sched_out(leader, cpuctx, ctx);
		if (leader->hw_event.pinned) {
			update_group_times(leader);
			leader->state = PERF_COUNTER_STATE_ERROR;
		}
	}

 unlock:
	spin_unlock_irqrestore(&ctx->lock, flags);
}

/*
 * Enable a counter.
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

static void perf_counter_refresh(struct perf_counter *counter, int refresh)
{
	atomic_add(refresh, &counter->event_limit);
	perf_counter_enable(counter);
}

/*
 * Enable a counter and all its children.
 */
static void perf_counter_enable_family(struct perf_counter *counter)
{
	struct perf_counter *child;

	perf_counter_enable(counter);

	/*
	 * Lock the mutex to protect the list of children
	 */
	mutex_lock(&counter->mutex);
	list_for_each_entry(child, &counter->child_list, child_list)
		perf_counter_enable(child);
	mutex_unlock(&counter->mutex);
}

void __perf_counter_sched_out(struct perf_counter_context *ctx,
			      struct perf_cpu_context *cpuctx)
{
	struct perf_counter *counter;
	u64 flags;

	spin_lock(&ctx->lock);
	ctx->is_active = 0;
	if (likely(!ctx->nr_counters))
		goto out;
	update_context_time(ctx);

	flags = hw_perf_save_disable();
	if (ctx->nr_active) {
		list_for_each_entry(counter, &ctx->counter_list, list_entry)
			group_sched_out(counter, cpuctx, ctx);
	}
	hw_perf_restore(flags);
 out:
	spin_unlock(&ctx->lock);
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
void perf_counter_task_sched_out(struct task_struct *task, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = &task->perf_counter_ctx;
	struct pt_regs *regs;

	if (likely(!cpuctx->task_ctx))
		return;

	update_context_time(ctx);

	regs = task_pt_regs(task);
	perf_swcounter_event(PERF_COUNT_CONTEXT_SWITCHES, 1, 1, regs, 0);
	__perf_counter_sched_out(ctx, cpuctx);

	cpuctx->task_ctx = NULL;
}

static void perf_counter_cpu_sched_out(struct perf_cpu_context *cpuctx)
{
	__perf_counter_sched_out(&cpuctx->ctx, cpuctx);
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

	group_counter->prev_state = group_counter->state;
	if (counter_sched_in(group_counter, cpuctx, ctx, cpu))
		return -EAGAIN;

	/*
	 * Schedule in siblings as one group (if any):
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry) {
		counter->prev_state = counter->state;
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

static void
__perf_counter_sched_in(struct perf_counter_context *ctx,
			struct perf_cpu_context *cpuctx, int cpu)
{
	struct perf_counter *counter;
	u64 flags;
	int can_add_hw = 1;

	spin_lock(&ctx->lock);
	ctx->is_active = 1;
	if (likely(!ctx->nr_counters))
		goto out;

	ctx->timestamp = perf_clock();

	flags = hw_perf_save_disable();

	/*
	 * First go through the list and put on any pinned groups
	 * in order to give them the best chance of going on.
	 */
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state <= PERF_COUNTER_STATE_OFF ||
		    !counter->hw_event.pinned)
			continue;
		if (counter->cpu != -1 && counter->cpu != cpu)
			continue;

		if (group_can_go_on(counter, cpuctx, 1))
			group_sched_in(counter, cpuctx, ctx, cpu);

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
		    counter->hw_event.pinned)
			continue;

		/*
		 * Listen to the 'cpu' scheduling filter constraint
		 * of counters:
		 */
		if (counter->cpu != -1 && counter->cpu != cpu)
			continue;

		if (group_can_go_on(counter, cpuctx, can_add_hw)) {
			if (group_sched_in(counter, cpuctx, ctx, cpu))
				can_add_hw = 0;
		}
	}
	hw_perf_restore(flags);
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
	struct perf_counter_context *ctx = &task->perf_counter_ctx;

	__perf_counter_sched_in(ctx, cpuctx, cpu);
	cpuctx->task_ctx = ctx;
}

static void perf_counter_cpu_sched_in(struct perf_cpu_context *cpuctx, int cpu)
{
	struct perf_counter_context *ctx = &cpuctx->ctx;

	__perf_counter_sched_in(ctx, cpuctx, cpu);
}

int perf_counter_task_disable(void)
{
	struct task_struct *curr = current;
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	struct perf_counter *counter;
	unsigned long flags;
	u64 perf_flags;
	int cpu;

	if (likely(!ctx->nr_counters))
		return 0;

	local_irq_save(flags);
	cpu = smp_processor_id();

	perf_counter_task_sched_out(curr, cpu);

	spin_lock(&ctx->lock);

	/*
	 * Disable all the counters:
	 */
	perf_flags = hw_perf_save_disable();

	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state != PERF_COUNTER_STATE_ERROR) {
			update_group_times(counter);
			counter->state = PERF_COUNTER_STATE_OFF;
		}
	}

	hw_perf_restore(perf_flags);

	spin_unlock_irqrestore(&ctx->lock, flags);

	return 0;
}

int perf_counter_task_enable(void)
{
	struct task_struct *curr = current;
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	struct perf_counter *counter;
	unsigned long flags;
	u64 perf_flags;
	int cpu;

	if (likely(!ctx->nr_counters))
		return 0;

	local_irq_save(flags);
	cpu = smp_processor_id();

	perf_counter_task_sched_out(curr, cpu);

	spin_lock(&ctx->lock);

	/*
	 * Disable all the counters:
	 */
	perf_flags = hw_perf_save_disable();

	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state > PERF_COUNTER_STATE_OFF)
			continue;
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		counter->tstamp_enabled =
			ctx->time - counter->total_time_enabled;
		counter->hw_event.disabled = 0;
	}
	hw_perf_restore(perf_flags);

	spin_unlock(&ctx->lock);

	perf_counter_task_sched_in(curr, cpu);

	local_irq_restore(flags);

	return 0;
}

/*
 * Round-robin a context's counters:
 */
static void rotate_ctx(struct perf_counter_context *ctx)
{
	struct perf_counter *counter;
	u64 perf_flags;

	if (!ctx->nr_counters)
		return;

	spin_lock(&ctx->lock);
	/*
	 * Rotate the first entry last (works just fine for group counters too):
	 */
	perf_flags = hw_perf_save_disable();
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		list_move_tail(&counter->list_entry, &ctx->counter_list);
		break;
	}
	hw_perf_restore(perf_flags);

	spin_unlock(&ctx->lock);
}

void perf_counter_task_tick(struct task_struct *curr, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	const int rotate_percpu = 0;

	if (rotate_percpu)
		perf_counter_cpu_sched_out(cpuctx);
	perf_counter_task_sched_out(curr, cpu);

	if (rotate_percpu)
		rotate_ctx(&cpuctx->ctx);
	rotate_ctx(ctx);

	if (rotate_percpu)
		perf_counter_cpu_sched_in(cpuctx, cpu);
	perf_counter_task_sched_in(curr, cpu);
}

/*
 * Cross CPU call to read the hardware counter
 */
static void __read(void *info)
{
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	unsigned long flags;

	local_irq_save(flags);
	if (ctx->is_active)
		update_context_time(ctx);
	counter->hw_ops->read(counter);
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
					 __read, counter, 1);
	} else if (counter->state == PERF_COUNTER_STATE_INACTIVE) {
		update_counter_times(counter);
	}

	return atomic64_read(&counter->count);
}

static void put_context(struct perf_counter_context *ctx)
{
	if (ctx->task)
		put_task_struct(ctx->task);
}

static struct perf_counter_context *find_get_context(pid_t pid, int cpu)
{
	struct perf_cpu_context *cpuctx;
	struct perf_counter_context *ctx;
	struct task_struct *task;

	/*
	 * If cpu is not a wildcard then this is a percpu counter:
	 */
	if (cpu != -1) {
		/* Must be root to operate on a CPU counter: */
		if (!capable(CAP_SYS_ADMIN))
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

	ctx = &task->perf_counter_ctx;
	ctx->task = task;

	/* Reuse ptrace permission checks for now. */
	if (!ptrace_may_access(task, PTRACE_MODE_READ)) {
		put_context(ctx);
		return ERR_PTR(-EACCES);
	}

	return ctx;
}

static void free_counter_rcu(struct rcu_head *head)
{
	struct perf_counter *counter;

	counter = container_of(head, struct perf_counter, rcu_head);
	kfree(counter);
}

static void perf_pending_sync(struct perf_counter *counter);

static void free_counter(struct perf_counter *counter)
{
	perf_pending_sync(counter);

	if (counter->destroy)
		counter->destroy(counter);

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

	mutex_lock(&ctx->mutex);
	mutex_lock(&counter->mutex);

	perf_counter_remove_from_context(counter);

	mutex_unlock(&counter->mutex);
	mutex_unlock(&ctx->mutex);

	free_counter(counter);
	put_context(ctx);

	return 0;
}

/*
 * Read the performance counter - simple non blocking version for now
 */
static ssize_t
perf_read_hw(struct perf_counter *counter, char __user *buf, size_t count)
{
	u64 values[3];
	int n;

	/*
	 * Return end-of-file for a read on a counter that is in
	 * error state (i.e. because it was pinned but it couldn't be
	 * scheduled on to the CPU at some point).
	 */
	if (counter->state == PERF_COUNTER_STATE_ERROR)
		return 0;

	mutex_lock(&counter->mutex);
	values[0] = perf_counter_read(counter);
	n = 1;
	if (counter->hw_event.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = counter->total_time_enabled +
			atomic64_read(&counter->child_total_time_enabled);
	if (counter->hw_event.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = counter->total_time_running +
			atomic64_read(&counter->child_total_time_running);
	mutex_unlock(&counter->mutex);

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
	unsigned int events;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (data)
		events = atomic_xchg(&data->wakeup, 0);
	else
		events = POLL_HUP;
	rcu_read_unlock();

	poll_wait(file, &counter->waitq, wait);

	return events;
}

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct perf_counter *counter = file->private_data;
	int err = 0;

	switch (cmd) {
	case PERF_COUNTER_IOC_ENABLE:
		perf_counter_enable_family(counter);
		break;
	case PERF_COUNTER_IOC_DISABLE:
		perf_counter_disable_family(counter);
		break;
	case PERF_COUNTER_IOC_REFRESH:
		perf_counter_refresh(counter, arg);
		break;
	default:
		err = -ENOTTY;
	}
	return err;
}

/*
 * Callers need to ensure there can be no nesting of this function, otherwise
 * the seqlock logic goes bad. We can not serialize this because the arch
 * code calls this from NMI context.
 */
void perf_counter_update_userpage(struct perf_counter *counter)
{
	struct perf_mmap_data *data;
	struct perf_counter_mmap_page *userpg;

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
	userpg->index = counter->hw.idx;
	userpg->offset = atomic64_read(&counter->count);
	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		userpg->offset -= atomic64_read(&counter->hw.prev_count);

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

		vmf->page = virt_to_page(data->data_pages[nr]);
	}
	get_page(vmf->page);
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

static void __perf_mmap_data_free(struct rcu_head *rcu_head)
{
	struct perf_mmap_data *data = container_of(rcu_head,
			struct perf_mmap_data, rcu_head);
	int i;

	free_page((unsigned long)data->user_page);
	for (i = 0; i < data->nr_pages; i++)
		free_page((unsigned long)data->data_pages[i]);
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

	if (atomic_dec_and_mutex_lock(&counter->mmap_count,
				      &counter->mmap_mutex)) {
		vma->vm_mm->locked_vm -= counter->data->nr_pages + 1;
		perf_mmap_data_free(counter);
		mutex_unlock(&counter->mmap_mutex);
	}
}

static struct vm_operations_struct perf_mmap_vmops = {
	.open  = perf_mmap_open,
	.close = perf_mmap_close,
	.fault = perf_mmap_fault,
};

static int perf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct perf_counter *counter = file->private_data;
	unsigned long vma_size;
	unsigned long nr_pages;
	unsigned long locked, lock_limit;
	int ret = 0;

	if (!(vma->vm_flags & VM_SHARED) || (vma->vm_flags & VM_WRITE))
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

	mutex_lock(&counter->mmap_mutex);
	if (atomic_inc_not_zero(&counter->mmap_count)) {
		if (nr_pages != counter->data->nr_pages)
			ret = -EINVAL;
		goto unlock;
	}

	locked = vma->vm_mm->locked_vm;
	locked += nr_pages + 1;

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	if ((locked > lock_limit) && !capable(CAP_IPC_LOCK)) {
		ret = -EPERM;
		goto unlock;
	}

	WARN_ON(counter->data);
	ret = perf_mmap_data_alloc(counter, nr_pages);
	if (ret)
		goto unlock;

	atomic_set(&counter->mmap_count, 1);
	vma->vm_mm->locked_vm += nr_pages + 1;
unlock:
	mutex_unlock(&counter->mmap_mutex);

	vma->vm_flags &= ~VM_MAYWRITE;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &perf_mmap_vmops;

	return ret;
}

static int perf_fasync(int fd, struct file *filp, int on)
{
	struct perf_counter *counter = filp->private_data;
	struct inode *inode = filp->f_path.dentry->d_inode;
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
	struct perf_mmap_data *data;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (data) {
		atomic_set(&data->wakeup, POLL_IN);
		/*
		 * Ensure all data writes are issued before updating the
		 * user-space data head information. The matching rmb()
		 * will be in userspace after reading this value.
		 */
		smp_wmb();
		data->user_page->data_head = atomic_read(&data->head);
	}
	rcu_read_unlock();

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
	unsigned int		offset;
	unsigned int		head;
	int			wakeup;
	int			nmi;
	int			overflow;
};

static inline void __perf_output_wakeup(struct perf_output_handle *handle)
{
	if (handle->nmi) {
		handle->counter->pending_wakeup = 1;
		perf_pending_queue(&handle->counter->pending,
				   perf_pending_counter);
	} else
		perf_counter_wakeup(handle->counter);
}

static int perf_output_begin(struct perf_output_handle *handle,
			     struct perf_counter *counter, unsigned int size,
			     int nmi, int overflow)
{
	struct perf_mmap_data *data;
	unsigned int offset, head;

	rcu_read_lock();
	data = rcu_dereference(counter->data);
	if (!data)
		goto out;

	handle->counter	 = counter;
	handle->nmi	 = nmi;
	handle->overflow = overflow;

	if (!data->nr_pages)
		goto fail;

	do {
		offset = head = atomic_read(&data->head);
		head += size;
	} while (atomic_cmpxchg(&data->head, offset, head) != offset);

	handle->data	= data;
	handle->offset	= offset;
	handle->head	= head;
	handle->wakeup	= (offset >> PAGE_SHIFT) != (head >> PAGE_SHIFT);

	return 0;

fail:
	__perf_output_wakeup(handle);
out:
	rcu_read_unlock();

	return -ENOSPC;
}

static void perf_output_copy(struct perf_output_handle *handle,
			     void *buf, unsigned int len)
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

	WARN_ON_ONCE(handle->offset > handle->head);
}

#define perf_output_put(handle, x) \
	perf_output_copy((handle), &(x), sizeof(x))

static void perf_output_end(struct perf_output_handle *handle)
{
	int wakeup_events = handle->counter->hw_event.wakeup_events;

	if (handle->overflow && wakeup_events) {
		int events = atomic_inc_return(&handle->data->events);
		if (events >= wakeup_events) {
			atomic_sub(wakeup_events, &handle->data->events);
			__perf_output_wakeup(handle);
		}
	} else if (handle->wakeup)
		__perf_output_wakeup(handle);
	rcu_read_unlock();
}

static void perf_counter_output(struct perf_counter *counter,
				int nmi, struct pt_regs *regs, u64 addr)
{
	int ret;
	u64 record_type = counter->hw_event.record_type;
	struct perf_output_handle handle;
	struct perf_event_header header;
	u64 ip;
	struct {
		u32 pid, tid;
	} tid_entry;
	struct {
		u64 event;
		u64 counter;
	} group_entry;
	struct perf_callchain_entry *callchain = NULL;
	int callchain_size = 0;
	u64 time;

	header.type = 0;
	header.size = sizeof(header);

	header.misc = PERF_EVENT_MISC_OVERFLOW;
	header.misc |= user_mode(regs) ?
		PERF_EVENT_MISC_USER : PERF_EVENT_MISC_KERNEL;

	if (record_type & PERF_RECORD_IP) {
		ip = instruction_pointer(regs);
		header.type |= PERF_RECORD_IP;
		header.size += sizeof(ip);
	}

	if (record_type & PERF_RECORD_TID) {
		/* namespace issues */
		tid_entry.pid = current->group_leader->pid;
		tid_entry.tid = current->pid;

		header.type |= PERF_RECORD_TID;
		header.size += sizeof(tid_entry);
	}

	if (record_type & PERF_RECORD_TIME) {
		/*
		 * Maybe do better on x86 and provide cpu_clock_nmi()
		 */
		time = sched_clock();

		header.type |= PERF_RECORD_TIME;
		header.size += sizeof(u64);
	}

	if (record_type & PERF_RECORD_ADDR) {
		header.type |= PERF_RECORD_ADDR;
		header.size += sizeof(u64);
	}

	if (record_type & PERF_RECORD_GROUP) {
		header.type |= PERF_RECORD_GROUP;
		header.size += sizeof(u64) +
			counter->nr_siblings * sizeof(group_entry);
	}

	if (record_type & PERF_RECORD_CALLCHAIN) {
		callchain = perf_callchain(regs);

		if (callchain) {
			callchain_size = (1 + callchain->nr) * sizeof(u64);

			header.type |= PERF_RECORD_CALLCHAIN;
			header.size += callchain_size;
		}
	}

	ret = perf_output_begin(&handle, counter, header.size, nmi, 1);
	if (ret)
		return;

	perf_output_put(&handle, header);

	if (record_type & PERF_RECORD_IP)
		perf_output_put(&handle, ip);

	if (record_type & PERF_RECORD_TID)
		perf_output_put(&handle, tid_entry);

	if (record_type & PERF_RECORD_TIME)
		perf_output_put(&handle, time);

	if (record_type & PERF_RECORD_ADDR)
		perf_output_put(&handle, addr);

	if (record_type & PERF_RECORD_GROUP) {
		struct perf_counter *leader, *sub;
		u64 nr = counter->nr_siblings;

		perf_output_put(&handle, nr);

		leader = counter->group_leader;
		list_for_each_entry(sub, &leader->sibling_list, list_entry) {
			if (sub != counter)
				sub->hw_ops->read(sub);

			group_entry.event = sub->hw_event.config;
			group_entry.counter = atomic64_read(&sub->count);

			perf_output_put(&handle, group_entry);
		}
	}

	if (callchain)
		perf_output_copy(&handle, callchain, callchain_size);

	perf_output_end(&handle);
}

/*
 * comm tracking
 */

struct perf_comm_event {
	struct task_struct 	*task;
	char 			*comm;
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

	perf_output_put(&handle, comm_event->event);
	perf_output_copy(&handle, comm_event->comm,
				   comm_event->comm_size);
	perf_output_end(&handle);
}

static int perf_counter_comm_match(struct perf_counter *counter,
				   struct perf_comm_event *comm_event)
{
	if (counter->hw_event.comm &&
	    comm_event->event.header.type == PERF_EVENT_COMM)
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
		if (perf_counter_comm_match(counter, comm_event))
			perf_counter_comm_output(counter, comm_event);
	}
	rcu_read_unlock();
}

static void perf_counter_comm_event(struct perf_comm_event *comm_event)
{
	struct perf_cpu_context *cpuctx;
	unsigned int size;
	char *comm = comm_event->task->comm;

	size = ALIGN(strlen(comm)+1, sizeof(u64));

	comm_event->comm = comm;
	comm_event->comm_size = size;

	comm_event->event.header.size = sizeof(comm_event->event) + size;

	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_counter_comm_ctx(&cpuctx->ctx, comm_event);
	put_cpu_var(perf_cpu_context);

	perf_counter_comm_ctx(&current->perf_counter_ctx, comm_event);
}

void perf_counter_comm(struct task_struct *task)
{
	struct perf_comm_event comm_event = {
		.task	= task,
		.event  = {
			.header = { .type = PERF_EVENT_COMM, },
			.pid	= task->group_leader->pid,
			.tid	= task->pid,
		},
	};

	perf_counter_comm_event(&comm_event);
}

/*
 * mmap tracking
 */

struct perf_mmap_event {
	struct file	*file;
	char		*file_name;
	int		file_size;

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

	perf_output_put(&handle, mmap_event->event);
	perf_output_copy(&handle, mmap_event->file_name,
				   mmap_event->file_size);
	perf_output_end(&handle);
}

static int perf_counter_mmap_match(struct perf_counter *counter,
				   struct perf_mmap_event *mmap_event)
{
	if (counter->hw_event.mmap &&
	    mmap_event->event.header.type == PERF_EVENT_MMAP)
		return 1;

	if (counter->hw_event.munmap &&
	    mmap_event->event.header.type == PERF_EVENT_MUNMAP)
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
	struct file *file = mmap_event->file;
	unsigned int size;
	char tmp[16];
	char *buf = NULL;
	char *name;

	if (file) {
		buf = kzalloc(PATH_MAX, GFP_KERNEL);
		if (!buf) {
			name = strncpy(tmp, "//enomem", sizeof(tmp));
			goto got_name;
		}
		name = dentry_path(file->f_dentry, buf, PATH_MAX);
		if (IS_ERR(name)) {
			name = strncpy(tmp, "//toolong", sizeof(tmp));
			goto got_name;
		}
	} else {
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

	perf_counter_mmap_ctx(&current->perf_counter_ctx, mmap_event);

	kfree(buf);
}

void perf_counter_mmap(unsigned long addr, unsigned long len,
		       unsigned long pgoff, struct file *file)
{
	struct perf_mmap_event mmap_event = {
		.file   = file,
		.event  = {
			.header = { .type = PERF_EVENT_MMAP, },
			.pid	= current->group_leader->pid,
			.tid	= current->pid,
			.start  = addr,
			.len    = len,
			.pgoff  = pgoff,
		},
	};

	perf_counter_mmap_event(&mmap_event);
}

void perf_counter_munmap(unsigned long addr, unsigned long len,
			 unsigned long pgoff, struct file *file)
{
	struct perf_mmap_event mmap_event = {
		.file   = file,
		.event  = {
			.header = { .type = PERF_EVENT_MUNMAP, },
			.pid	= current->group_leader->pid,
			.tid	= current->pid,
			.start  = addr,
			.len    = len,
			.pgoff  = pgoff,
		},
	};

	perf_counter_mmap_event(&mmap_event);
}

/*
 * Generic counter overflow handling.
 */

int perf_counter_overflow(struct perf_counter *counter,
			  int nmi, struct pt_regs *regs, u64 addr)
{
	int events = atomic_read(&counter->event_limit);
	int ret = 0;

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

	perf_counter_output(counter, nmi, regs, addr);
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
	s64 period = hwc->irq_period;

	if (unlikely(left <= -period)) {
		left = period;
		atomic64_set(&hwc->period_left, left);
	}

	if (unlikely(left <= 0)) {
		left += period;
		atomic64_add(period, &hwc->period_left);
	}

	atomic64_set(&hwc->prev_count, -left);
	atomic64_set(&hwc->count, -left);
}

static enum hrtimer_restart perf_swcounter_hrtimer(struct hrtimer *hrtimer)
{
	enum hrtimer_restart ret = HRTIMER_RESTART;
	struct perf_counter *counter;
	struct pt_regs *regs;

	counter	= container_of(hrtimer, struct perf_counter, hw.hrtimer);
	counter->hw_ops->read(counter);

	regs = get_irq_regs();
	/*
	 * In case we exclude kernel IPs or are somehow not in interrupt
	 * context, provide the next best thing, the user IP.
	 */
	if ((counter->hw_event.exclude_kernel || !regs) &&
			!counter->hw_event.exclude_user)
		regs = task_pt_regs(current);

	if (regs) {
		if (perf_counter_overflow(counter, 0, regs, 0))
			ret = HRTIMER_NORESTART;
	}

	hrtimer_forward_now(hrtimer, ns_to_ktime(counter->hw.irq_period));

	return ret;
}

static void perf_swcounter_overflow(struct perf_counter *counter,
				    int nmi, struct pt_regs *regs, u64 addr)
{
	perf_swcounter_update(counter);
	perf_swcounter_set_period(counter);
	if (perf_counter_overflow(counter, nmi, regs, addr))
		/* soft-disable the counter */
		;

}

static int perf_swcounter_match(struct perf_counter *counter,
				enum perf_event_types type,
				u32 event, struct pt_regs *regs)
{
	if (counter->state != PERF_COUNTER_STATE_ACTIVE)
		return 0;

	if (perf_event_raw(&counter->hw_event))
		return 0;

	if (perf_event_type(&counter->hw_event) != type)
		return 0;

	if (perf_event_id(&counter->hw_event) != event)
		return 0;

	if (counter->hw_event.exclude_user && user_mode(regs))
		return 0;

	if (counter->hw_event.exclude_kernel && !user_mode(regs))
		return 0;

	return 1;
}

static void perf_swcounter_add(struct perf_counter *counter, u64 nr,
			       int nmi, struct pt_regs *regs, u64 addr)
{
	int neg = atomic64_add_negative(nr, &counter->hw.count);
	if (counter->hw.irq_period && !neg)
		perf_swcounter_overflow(counter, nmi, regs, addr);
}

static void perf_swcounter_ctx_event(struct perf_counter_context *ctx,
				     enum perf_event_types type, u32 event,
				     u64 nr, int nmi, struct pt_regs *regs,
				     u64 addr)
{
	struct perf_counter *counter;

	if (system_state != SYSTEM_RUNNING || list_empty(&ctx->event_list))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(counter, &ctx->event_list, event_entry) {
		if (perf_swcounter_match(counter, type, event, regs))
			perf_swcounter_add(counter, nr, nmi, regs, addr);
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

static void __perf_swcounter_event(enum perf_event_types type, u32 event,
				   u64 nr, int nmi, struct pt_regs *regs,
				   u64 addr)
{
	struct perf_cpu_context *cpuctx = &get_cpu_var(perf_cpu_context);
	int *recursion = perf_swcounter_recursion_context(cpuctx);

	if (*recursion)
		goto out;

	(*recursion)++;
	barrier();

	perf_swcounter_ctx_event(&cpuctx->ctx, type, event,
				 nr, nmi, regs, addr);
	if (cpuctx->task_ctx) {
		perf_swcounter_ctx_event(cpuctx->task_ctx, type, event,
					 nr, nmi, regs, addr);
	}

	barrier();
	(*recursion)--;

out:
	put_cpu_var(perf_cpu_context);
}

void
perf_swcounter_event(u32 event, u64 nr, int nmi, struct pt_regs *regs, u64 addr)
{
	__perf_swcounter_event(PERF_TYPE_SOFTWARE, event, nr, nmi, regs, addr);
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

static const struct hw_perf_counter_ops perf_ops_generic = {
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
	if (hwc->irq_period) {
		__hrtimer_start_range_ns(&hwc->hrtimer,
				ns_to_ktime(hwc->irq_period), 0,
				HRTIMER_MODE_REL, 0);
	}

	return 0;
}

static void cpu_clock_perf_counter_disable(struct perf_counter *counter)
{
	hrtimer_cancel(&counter->hw.hrtimer);
	cpu_clock_perf_counter_update(counter);
}

static void cpu_clock_perf_counter_read(struct perf_counter *counter)
{
	cpu_clock_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_cpu_clock = {
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
	if (hwc->irq_period) {
		__hrtimer_start_range_ns(&hwc->hrtimer,
				ns_to_ktime(hwc->irq_period), 0,
				HRTIMER_MODE_REL, 0);
	}

	return 0;
}

static void task_clock_perf_counter_disable(struct perf_counter *counter)
{
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

static const struct hw_perf_counter_ops perf_ops_task_clock = {
	.enable		= task_clock_perf_counter_enable,
	.disable	= task_clock_perf_counter_disable,
	.read		= task_clock_perf_counter_read,
};

/*
 * Software counter: cpu migrations
 */

static inline u64 get_cpu_migrations(struct perf_counter *counter)
{
	struct task_struct *curr = counter->ctx->task;

	if (curr)
		return curr->se.nr_migrations;
	return cpu_nr_migrations(smp_processor_id());
}

static void cpu_migrations_perf_counter_update(struct perf_counter *counter)
{
	u64 prev, now;
	s64 delta;

	prev = atomic64_read(&counter->hw.prev_count);
	now = get_cpu_migrations(counter);

	atomic64_set(&counter->hw.prev_count, now);

	delta = now - prev;

	atomic64_add(delta, &counter->count);
}

static void cpu_migrations_perf_counter_read(struct perf_counter *counter)
{
	cpu_migrations_perf_counter_update(counter);
}

static int cpu_migrations_perf_counter_enable(struct perf_counter *counter)
{
	if (counter->prev_state <= PERF_COUNTER_STATE_OFF)
		atomic64_set(&counter->hw.prev_count,
			     get_cpu_migrations(counter));
	return 0;
}

static void cpu_migrations_perf_counter_disable(struct perf_counter *counter)
{
	cpu_migrations_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_cpu_migrations = {
	.enable		= cpu_migrations_perf_counter_enable,
	.disable	= cpu_migrations_perf_counter_disable,
	.read		= cpu_migrations_perf_counter_read,
};

#ifdef CONFIG_EVENT_PROFILE
void perf_tpcounter_event(int event_id)
{
	struct pt_regs *regs = get_irq_regs();

	if (!regs)
		regs = task_pt_regs(current);

	__perf_swcounter_event(PERF_TYPE_TRACEPOINT, event_id, 1, 1, regs, 0);
}

extern int ftrace_profile_enable(int);
extern void ftrace_profile_disable(int);

static void tp_perf_counter_destroy(struct perf_counter *counter)
{
	ftrace_profile_disable(perf_event_id(&counter->hw_event));
}

static const struct hw_perf_counter_ops *
tp_perf_counter_init(struct perf_counter *counter)
{
	int event_id = perf_event_id(&counter->hw_event);
	int ret;

	ret = ftrace_profile_enable(event_id);
	if (ret)
		return NULL;

	counter->destroy = tp_perf_counter_destroy;
	counter->hw.irq_period = counter->hw_event.irq_period;

	return &perf_ops_generic;
}
#else
static const struct hw_perf_counter_ops *
tp_perf_counter_init(struct perf_counter *counter)
{
	return NULL;
}
#endif

static const struct hw_perf_counter_ops *
sw_perf_counter_init(struct perf_counter *counter)
{
	struct perf_counter_hw_event *hw_event = &counter->hw_event;
	const struct hw_perf_counter_ops *hw_ops = NULL;
	struct hw_perf_counter *hwc = &counter->hw;

	/*
	 * Software counters (currently) can't in general distinguish
	 * between user, kernel and hypervisor events.
	 * However, context switches and cpu migrations are considered
	 * to be kernel events, and page faults are never hypervisor
	 * events.
	 */
	switch (perf_event_id(&counter->hw_event)) {
	case PERF_COUNT_CPU_CLOCK:
		hw_ops = &perf_ops_cpu_clock;

		if (hw_event->irq_period && hw_event->irq_period < 10000)
			hw_event->irq_period = 10000;
		break;
	case PERF_COUNT_TASK_CLOCK:
		/*
		 * If the user instantiates this as a per-cpu counter,
		 * use the cpu_clock counter instead.
		 */
		if (counter->ctx->task)
			hw_ops = &perf_ops_task_clock;
		else
			hw_ops = &perf_ops_cpu_clock;

		if (hw_event->irq_period && hw_event->irq_period < 10000)
			hw_event->irq_period = 10000;
		break;
	case PERF_COUNT_PAGE_FAULTS:
	case PERF_COUNT_PAGE_FAULTS_MIN:
	case PERF_COUNT_PAGE_FAULTS_MAJ:
	case PERF_COUNT_CONTEXT_SWITCHES:
		hw_ops = &perf_ops_generic;
		break;
	case PERF_COUNT_CPU_MIGRATIONS:
		if (!counter->hw_event.exclude_kernel)
			hw_ops = &perf_ops_cpu_migrations;
		break;
	}

	if (hw_ops)
		hwc->irq_period = hw_event->irq_period;

	return hw_ops;
}

/*
 * Allocate and initialize a counter structure
 */
static struct perf_counter *
perf_counter_alloc(struct perf_counter_hw_event *hw_event,
		   int cpu,
		   struct perf_counter_context *ctx,
		   struct perf_counter *group_leader,
		   gfp_t gfpflags)
{
	const struct hw_perf_counter_ops *hw_ops;
	struct perf_counter *counter;
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

	mutex_init(&counter->mutex);
	INIT_LIST_HEAD(&counter->list_entry);
	INIT_LIST_HEAD(&counter->event_entry);
	INIT_LIST_HEAD(&counter->sibling_list);
	init_waitqueue_head(&counter->waitq);

	mutex_init(&counter->mmap_mutex);

	INIT_LIST_HEAD(&counter->child_list);

	counter->cpu			= cpu;
	counter->hw_event		= *hw_event;
	counter->group_leader		= group_leader;
	counter->hw_ops			= NULL;
	counter->ctx			= ctx;

	counter->state = PERF_COUNTER_STATE_INACTIVE;
	if (hw_event->disabled)
		counter->state = PERF_COUNTER_STATE_OFF;

	hw_ops = NULL;

	if (perf_event_raw(hw_event)) {
		hw_ops = hw_perf_counter_init(counter);
		goto done;
	}

	switch (perf_event_type(hw_event)) {
	case PERF_TYPE_HARDWARE:
		hw_ops = hw_perf_counter_init(counter);
		break;

	case PERF_TYPE_SOFTWARE:
		hw_ops = sw_perf_counter_init(counter);
		break;

	case PERF_TYPE_TRACEPOINT:
		hw_ops = tp_perf_counter_init(counter);
		break;
	}
done:
	err = 0;
	if (!hw_ops)
		err = -EINVAL;
	else if (IS_ERR(hw_ops))
		err = PTR_ERR(hw_ops);

	if (err) {
		kfree(counter);
		return ERR_PTR(err);
	}

	counter->hw_ops = hw_ops;

	return counter;
}

/**
 * sys_perf_counter_open - open a performance counter, associate it to a task/cpu
 *
 * @hw_event_uptr:	event type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader counter fd
 */
SYSCALL_DEFINE5(perf_counter_open,
		const struct perf_counter_hw_event __user *, hw_event_uptr,
		pid_t, pid, int, cpu, int, group_fd, unsigned long, flags)
{
	struct perf_counter *counter, *group_leader;
	struct perf_counter_hw_event hw_event;
	struct perf_counter_context *ctx;
	struct file *counter_file = NULL;
	struct file *group_file = NULL;
	int fput_needed = 0;
	int fput_needed2 = 0;
	int ret;

	/* for future expandability... */
	if (flags)
		return -EINVAL;

	if (copy_from_user(&hw_event, hw_event_uptr, sizeof(hw_event)) != 0)
		return -EFAULT;

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
		if (hw_event.exclusive || hw_event.pinned)
			goto err_put_context;
	}

	counter = perf_counter_alloc(&hw_event, cpu, ctx, group_leader,
				     GFP_KERNEL);
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
	mutex_lock(&ctx->mutex);
	perf_install_in_context(ctx, counter, cpu);
	mutex_unlock(&ctx->mutex);

	fput_light(counter_file, fput_needed2);

out_fput:
	fput_light(group_file, fput_needed);

	return ret;

err_free_put_context:
	kfree(counter);

err_put_context:
	put_context(ctx);

	goto out_fput;
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
	ctx->task = task;
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

	child_counter = perf_counter_alloc(&parent_counter->hw_event,
					   parent_counter->cpu, child_ctx,
					   group_leader, GFP_KERNEL);
	if (IS_ERR(child_counter))
		return child_counter;

	/*
	 * Link it up in the child's context:
	 */
	child_counter->task = child;
	add_counter_to_ctx(child_counter, child_ctx);

	child_counter->parent = parent_counter;
	/*
	 * inherit into child's child as well:
	 */
	child_counter->hw_event.inherit = 1;

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
	mutex_lock(&parent_counter->mutex);
	list_add_tail(&child_counter->child_list, &parent_counter->child_list);

	/*
	 * Make the child state follow the state of the parent counter,
	 * not its hw_event.disabled bit.  We hold the parent's mutex,
	 * so we won't race with perf_counter_{en,dis}able_family.
	 */
	if (parent_counter->state >= PERF_COUNTER_STATE_INACTIVE)
		child_counter->state = PERF_COUNTER_STATE_INACTIVE;
	else
		child_counter->state = PERF_COUNTER_STATE_OFF;

	mutex_unlock(&parent_counter->mutex);

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
			       struct perf_counter *parent_counter)
{
	u64 parent_val, child_val;

	parent_val = atomic64_read(&parent_counter->count);
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
	mutex_lock(&parent_counter->mutex);
	list_del_init(&child_counter->child_list);
	mutex_unlock(&parent_counter->mutex);

	/*
	 * Release the parent counter, if this was the last
	 * reference to it.
	 */
	fput(parent_counter->filp);
}

static void
__perf_counter_exit_task(struct task_struct *child,
			 struct perf_counter *child_counter,
			 struct perf_counter_context *child_ctx)
{
	struct perf_counter *parent_counter;
	struct perf_counter *sub, *tmp;

	/*
	 * If we do not self-reap then we have to wait for the
	 * child task to unschedule (it will happen for sure),
	 * so that its counter is at its final count. (This
	 * condition triggers rarely - child tasks usually get
	 * off their CPU before the parent has a chance to
	 * get this far into the reaping action)
	 */
	if (child != current) {
		wait_task_inactive(child, 0);
		list_del_init(&child_counter->list_entry);
		update_counter_times(child_counter);
	} else {
		struct perf_cpu_context *cpuctx;
		unsigned long flags;
		u64 perf_flags;

		/*
		 * Disable and unlink this counter.
		 *
		 * Be careful about zapping the list - IRQ/NMI context
		 * could still be processing it:
		 */
		local_irq_save(flags);
		perf_flags = hw_perf_save_disable();

		cpuctx = &__get_cpu_var(perf_cpu_context);

		group_sched_out(child_counter, cpuctx, child_ctx);
		update_counter_times(child_counter);

		list_del_init(&child_counter->list_entry);

		child_ctx->nr_counters--;

		hw_perf_restore(perf_flags);
		local_irq_restore(flags);
	}

	parent_counter = child_counter->parent;
	/*
	 * It can happen that parent exits first, and has counters
	 * that are still around due to the child reference. These
	 * counters need to be zapped - but otherwise linger.
	 */
	if (parent_counter) {
		sync_child_counter(child_counter, parent_counter);
		list_for_each_entry_safe(sub, tmp, &child_counter->sibling_list,
					 list_entry) {
			if (sub->parent) {
				sync_child_counter(sub, sub->parent);
				free_counter(sub);
			}
		}
		free_counter(child_counter);
	}
}

/*
 * When a child task exits, feed back counter values to parent counters.
 *
 * Note: we may be running in child context, but the PID is not hashed
 * anymore so new counters will not be added.
 */
void perf_counter_exit_task(struct task_struct *child)
{
	struct perf_counter *child_counter, *tmp;
	struct perf_counter_context *child_ctx;

	child_ctx = &child->perf_counter_ctx;

	if (likely(!child_ctx->nr_counters))
		return;

	list_for_each_entry_safe(child_counter, tmp, &child_ctx->counter_list,
				 list_entry)
		__perf_counter_exit_task(child, child_counter, child_ctx);
}

/*
 * Initialize the perf_counter context in task_struct
 */
void perf_counter_init_task(struct task_struct *child)
{
	struct perf_counter_context *child_ctx, *parent_ctx;
	struct perf_counter *counter;
	struct task_struct *parent = current;

	child_ctx  =  &child->perf_counter_ctx;
	parent_ctx = &parent->perf_counter_ctx;

	__perf_counter_init_context(child_ctx, child);

	/*
	 * This is executed from the parent task context, so inherit
	 * counters that have been marked for cloning:
	 */

	if (likely(!parent_ctx->nr_counters))
		return;

	/*
	 * Lock the parent list. No need to lock the child - not PID
	 * hashed yet and not running, so nobody can access it.
	 */
	mutex_lock(&parent_ctx->mutex);

	/*
	 * We dont have to disable NMIs - we are only looking at
	 * the list, not manipulating it:
	 */
	list_for_each_entry(counter, &parent_ctx->counter_list, list_entry) {
		if (!counter->hw_event.inherit)
			continue;

		if (inherit_group(counter, parent,
				  parent_ctx, child, child_ctx))
			break;
	}

	mutex_unlock(&parent_ctx->mutex);
}

static void __cpuinit perf_counter_init_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx;

	cpuctx = &per_cpu(perf_cpu_context, cpu);
	__perf_counter_init_context(&cpuctx->ctx, NULL);

	mutex_lock(&perf_resource_mutex);
	cpuctx->max_pertask = perf_max_counters - perf_reserved_percpu;
	mutex_unlock(&perf_resource_mutex);

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

static struct notifier_block __cpuinitdata perf_cpu_nb = {
	.notifier_call		= perf_cpu_notify,
};

static int __init perf_counter_init(void)
{
	perf_cpu_notify(&perf_cpu_nb, (unsigned long)CPU_UP_PREPARE,
			(void *)(long)smp_processor_id());
	register_cpu_notifier(&perf_cpu_nb);

	return 0;
}
early_initcall(perf_counter_init);

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

	mutex_lock(&perf_resource_mutex);
	perf_reserved_percpu = val;
	for_each_online_cpu(cpu) {
		cpuctx = &per_cpu(perf_cpu_context, cpu);
		spin_lock_irq(&cpuctx->ctx.lock);
		mpt = min(perf_max_counters - cpuctx->ctx.nr_counters,
			  perf_max_counters - perf_reserved_percpu);
		cpuctx->max_pertask = mpt;
		spin_unlock_irq(&cpuctx->ctx.lock);
	}
	mutex_unlock(&perf_resource_mutex);

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

	mutex_lock(&perf_resource_mutex);
	perf_overcommit = val;
	mutex_unlock(&perf_resource_mutex);

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

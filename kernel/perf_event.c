/*
 * Performance events core code:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright  ©  2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * For licensing details see kernel-base/COPYING
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/sysfs.h>
#include <linux/dcache.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/vmstat.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/kernel_stat.h>
#include <linux/perf_event.h>
#include <linux/ftrace_event.h>
#include <linux/hw_breakpoint.h>

#include <asm/irq_regs.h>

/*
 * Each CPU has a list of per CPU events:
 */
static DEFINE_PER_CPU(struct perf_cpu_context, perf_cpu_context);

int perf_max_events __read_mostly = 1;
static int perf_reserved_percpu __read_mostly;
static int perf_overcommit __read_mostly = 1;

static atomic_t nr_events __read_mostly;
static atomic_t nr_mmap_events __read_mostly;
static atomic_t nr_comm_events __read_mostly;
static atomic_t nr_task_events __read_mostly;

/*
 * perf event paranoia level:
 *  -1 - not paranoid at all
 *   0 - disallow raw tracepoint access for unpriv
 *   1 - disallow cpu events for unpriv
 *   2 - disallow kernel profiling for unpriv
 */
int sysctl_perf_event_paranoid __read_mostly = 1;

int sysctl_perf_event_mlock __read_mostly = 512; /* 'free' kb per user */

/*
 * max perf event sample rate
 */
int sysctl_perf_event_sample_rate __read_mostly = 100000;

static atomic64_t perf_event_id;

/*
 * Lock for (sysadmin-configurable) event reservations:
 */
static DEFINE_SPINLOCK(perf_resource_lock);

/*
 * Architecture provided APIs - weak aliases:
 */
extern __weak const struct pmu *hw_perf_event_init(struct perf_event *event)
{
	return NULL;
}

void __weak hw_perf_disable(void)		{ barrier(); }
void __weak hw_perf_enable(void)		{ barrier(); }

void __weak perf_event_print_debug(void)	{ }

static DEFINE_PER_CPU(int, perf_disable_count);

void perf_disable(void)
{
	if (!__get_cpu_var(perf_disable_count)++)
		hw_perf_disable();
}

void perf_enable(void)
{
	if (!--__get_cpu_var(perf_disable_count))
		hw_perf_enable();
}

static void get_ctx(struct perf_event_context *ctx)
{
	WARN_ON(!atomic_inc_not_zero(&ctx->refcount));
}

static void free_ctx(struct rcu_head *head)
{
	struct perf_event_context *ctx;

	ctx = container_of(head, struct perf_event_context, rcu_head);
	kfree(ctx);
}

static void put_ctx(struct perf_event_context *ctx)
{
	if (atomic_dec_and_test(&ctx->refcount)) {
		if (ctx->parent_ctx)
			put_ctx(ctx->parent_ctx);
		if (ctx->task)
			put_task_struct(ctx->task);
		call_rcu(&ctx->rcu_head, free_ctx);
	}
}

static void unclone_ctx(struct perf_event_context *ctx)
{
	if (ctx->parent_ctx) {
		put_ctx(ctx->parent_ctx);
		ctx->parent_ctx = NULL;
	}
}

/*
 * If we inherit events we want to return the parent event id
 * to userspace.
 */
static u64 primary_event_id(struct perf_event *event)
{
	u64 id = event->id;

	if (event->parent)
		id = event->parent->id;

	return id;
}

/*
 * Get the perf_event_context for a task and lock it.
 * This has to cope with with the fact that until it is locked,
 * the context could get moved to another task.
 */
static struct perf_event_context *
perf_lock_task_context(struct task_struct *task, unsigned long *flags)
{
	struct perf_event_context *ctx;

	rcu_read_lock();
 retry:
	ctx = rcu_dereference(task->perf_event_ctxp);
	if (ctx) {
		/*
		 * If this context is a clone of another, it might
		 * get swapped for another underneath us by
		 * perf_event_task_sched_out, though the
		 * rcu_read_lock() protects us from any context
		 * getting freed.  Lock the context and check if it
		 * got swapped before we could get the lock, and retry
		 * if so.  If we locked the right context, then it
		 * can't get swapped on us any more.
		 */
		raw_spin_lock_irqsave(&ctx->lock, *flags);
		if (ctx != rcu_dereference(task->perf_event_ctxp)) {
			raw_spin_unlock_irqrestore(&ctx->lock, *flags);
			goto retry;
		}

		if (!atomic_inc_not_zero(&ctx->refcount)) {
			raw_spin_unlock_irqrestore(&ctx->lock, *flags);
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
static struct perf_event_context *perf_pin_task_context(struct task_struct *task)
{
	struct perf_event_context *ctx;
	unsigned long flags;

	ctx = perf_lock_task_context(task, &flags);
	if (ctx) {
		++ctx->pin_count;
		raw_spin_unlock_irqrestore(&ctx->lock, flags);
	}
	return ctx;
}

static void perf_unpin_context(struct perf_event_context *ctx)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ctx->lock, flags);
	--ctx->pin_count;
	raw_spin_unlock_irqrestore(&ctx->lock, flags);
	put_ctx(ctx);
}

static inline u64 perf_clock(void)
{
	return local_clock();
}

/*
 * Update the record of the current time in a context.
 */
static void update_context_time(struct perf_event_context *ctx)
{
	u64 now = perf_clock();

	ctx->time += now - ctx->timestamp;
	ctx->timestamp = now;
}

/*
 * Update the total_time_enabled and total_time_running fields for a event.
 */
static void update_event_times(struct perf_event *event)
{
	struct perf_event_context *ctx = event->ctx;
	u64 run_end;

	if (event->state < PERF_EVENT_STATE_INACTIVE ||
	    event->group_leader->state < PERF_EVENT_STATE_INACTIVE)
		return;

	if (ctx->is_active)
		run_end = ctx->time;
	else
		run_end = event->tstamp_stopped;

	event->total_time_enabled = run_end - event->tstamp_enabled;

	if (event->state == PERF_EVENT_STATE_INACTIVE)
		run_end = event->tstamp_stopped;
	else
		run_end = ctx->time;

	event->total_time_running = run_end - event->tstamp_running;
}

/*
 * Update total_time_enabled and total_time_running for all events in a group.
 */
static void update_group_times(struct perf_event *leader)
{
	struct perf_event *event;

	update_event_times(leader);
	list_for_each_entry(event, &leader->sibling_list, group_entry)
		update_event_times(event);
}

static struct list_head *
ctx_group_list(struct perf_event *event, struct perf_event_context *ctx)
{
	if (event->attr.pinned)
		return &ctx->pinned_groups;
	else
		return &ctx->flexible_groups;
}

/*
 * Add a event from the lists for its context.
 * Must be called with ctx->mutex and ctx->lock held.
 */
static void
list_add_event(struct perf_event *event, struct perf_event_context *ctx)
{
	WARN_ON_ONCE(event->attach_state & PERF_ATTACH_CONTEXT);
	event->attach_state |= PERF_ATTACH_CONTEXT;

	/*
	 * If we're a stand alone event or group leader, we go to the context
	 * list, group events are kept attached to the group so that
	 * perf_group_detach can, at all times, locate all siblings.
	 */
	if (event->group_leader == event) {
		struct list_head *list;

		if (is_software_event(event))
			event->group_flags |= PERF_GROUP_SOFTWARE;

		list = ctx_group_list(event, ctx);
		list_add_tail(&event->group_entry, list);
	}

	list_add_rcu(&event->event_entry, &ctx->event_list);
	ctx->nr_events++;
	if (event->attr.inherit_stat)
		ctx->nr_stat++;
}

static void perf_group_attach(struct perf_event *event)
{
	struct perf_event *group_leader = event->group_leader;

	WARN_ON_ONCE(event->attach_state & PERF_ATTACH_GROUP);
	event->attach_state |= PERF_ATTACH_GROUP;

	if (group_leader == event)
		return;

	if (group_leader->group_flags & PERF_GROUP_SOFTWARE &&
			!is_software_event(event))
		group_leader->group_flags &= ~PERF_GROUP_SOFTWARE;

	list_add_tail(&event->group_entry, &group_leader->sibling_list);
	group_leader->nr_siblings++;
}

/*
 * Remove a event from the lists for its context.
 * Must be called with ctx->mutex and ctx->lock held.
 */
static void
list_del_event(struct perf_event *event, struct perf_event_context *ctx)
{
	/*
	 * We can have double detach due to exit/hot-unplug + close.
	 */
	if (!(event->attach_state & PERF_ATTACH_CONTEXT))
		return;

	event->attach_state &= ~PERF_ATTACH_CONTEXT;

	ctx->nr_events--;
	if (event->attr.inherit_stat)
		ctx->nr_stat--;

	list_del_rcu(&event->event_entry);

	if (event->group_leader == event)
		list_del_init(&event->group_entry);

	update_group_times(event);

	/*
	 * If event was in error state, then keep it
	 * that way, otherwise bogus counts will be
	 * returned on read(). The only way to get out
	 * of error state is by explicit re-enabling
	 * of the event
	 */
	if (event->state > PERF_EVENT_STATE_OFF)
		event->state = PERF_EVENT_STATE_OFF;
}

static void perf_group_detach(struct perf_event *event)
{
	struct perf_event *sibling, *tmp;
	struct list_head *list = NULL;

	/*
	 * We can have double detach due to exit/hot-unplug + close.
	 */
	if (!(event->attach_state & PERF_ATTACH_GROUP))
		return;

	event->attach_state &= ~PERF_ATTACH_GROUP;

	/*
	 * If this is a sibling, remove it from its group.
	 */
	if (event->group_leader != event) {
		list_del_init(&event->group_entry);
		event->group_leader->nr_siblings--;
		return;
	}

	if (!list_empty(&event->group_entry))
		list = &event->group_entry;

	/*
	 * If this was a group event with sibling events then
	 * upgrade the siblings to singleton events by adding them
	 * to whatever list we are on.
	 */
	list_for_each_entry_safe(sibling, tmp, &event->sibling_list, group_entry) {
		if (list)
			list_move_tail(&sibling->group_entry, list);
		sibling->group_leader = sibling;

		/* Inherit group flags from the previous leader */
		sibling->group_flags = event->group_flags;
	}
}

static inline int
event_filter_match(struct perf_event *event)
{
	return event->cpu == -1 || event->cpu == smp_processor_id();
}

static void
event_sched_out(struct perf_event *event,
		  struct perf_cpu_context *cpuctx,
		  struct perf_event_context *ctx)
{
	u64 delta;
	/*
	 * An event which could not be activated because of
	 * filter mismatch still needs to have its timings
	 * maintained, otherwise bogus information is return
	 * via read() for time_enabled, time_running:
	 */
	if (event->state == PERF_EVENT_STATE_INACTIVE
	    && !event_filter_match(event)) {
		delta = ctx->time - event->tstamp_stopped;
		event->tstamp_running += delta;
		event->tstamp_stopped = ctx->time;
	}

	if (event->state != PERF_EVENT_STATE_ACTIVE)
		return;

	event->state = PERF_EVENT_STATE_INACTIVE;
	if (event->pending_disable) {
		event->pending_disable = 0;
		event->state = PERF_EVENT_STATE_OFF;
	}
	event->tstamp_stopped = ctx->time;
	event->pmu->disable(event);
	event->oncpu = -1;

	if (!is_software_event(event))
		cpuctx->active_oncpu--;
	ctx->nr_active--;
	if (event->attr.exclusive || !cpuctx->active_oncpu)
		cpuctx->exclusive = 0;
}

static void
group_sched_out(struct perf_event *group_event,
		struct perf_cpu_context *cpuctx,
		struct perf_event_context *ctx)
{
	struct perf_event *event;
	int state = group_event->state;

	event_sched_out(group_event, cpuctx, ctx);

	/*
	 * Schedule out siblings (if any):
	 */
	list_for_each_entry(event, &group_event->sibling_list, group_entry)
		event_sched_out(event, cpuctx, ctx);

	if (state == PERF_EVENT_STATE_ACTIVE && group_event->attr.exclusive)
		cpuctx->exclusive = 0;
}

/*
 * Cross CPU call to remove a performance event
 *
 * We disable the event on the hardware level first. After that we
 * remove it from the context list.
 */
static void __perf_event_remove_from_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event *event = info;
	struct perf_event_context *ctx = event->ctx;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	raw_spin_lock(&ctx->lock);
	/*
	 * Protect the list operation against NMI by disabling the
	 * events on a global level.
	 */
	perf_disable();

	event_sched_out(event, cpuctx, ctx);

	list_del_event(event, ctx);

	if (!ctx->task) {
		/*
		 * Allow more per task events with respect to the
		 * reservation:
		 */
		cpuctx->max_pertask =
			min(perf_max_events - ctx->nr_events,
			    perf_max_events - perf_reserved_percpu);
	}

	perf_enable();
	raw_spin_unlock(&ctx->lock);
}


/*
 * Remove the event from a task's (or a CPU's) list of events.
 *
 * Must be called with ctx->mutex held.
 *
 * CPU events are removed with a smp call. For task events we only
 * call when the task is on a CPU.
 *
 * If event->ctx is a cloned context, callers must make sure that
 * every task struct that event->ctx->task could possibly point to
 * remains valid.  This is OK when called from perf_release since
 * that only calls us on the top-level context, which can't be a clone.
 * When called from perf_event_exit_task, it's OK because the
 * context has been detached from its task.
 */
static void perf_event_remove_from_context(struct perf_event *event)
{
	struct perf_event_context *ctx = event->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Per cpu events are removed via an smp call and
		 * the removal is always successful.
		 */
		smp_call_function_single(event->cpu,
					 __perf_event_remove_from_context,
					 event, 1);
		return;
	}

retry:
	task_oncpu_function_call(task, __perf_event_remove_from_context,
				 event);

	raw_spin_lock_irq(&ctx->lock);
	/*
	 * If the context is active we need to retry the smp call.
	 */
	if (ctx->nr_active && !list_empty(&event->group_entry)) {
		raw_spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * The lock prevents that this context is scheduled in so we
	 * can remove the event safely, if the call above did not
	 * succeed.
	 */
	if (!list_empty(&event->group_entry))
		list_del_event(event, ctx);
	raw_spin_unlock_irq(&ctx->lock);
}

/*
 * Cross CPU call to disable a performance event
 */
static void __perf_event_disable(void *info)
{
	struct perf_event *event = info;
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = event->ctx;

	/*
	 * If this is a per-task event, need to check whether this
	 * event's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	raw_spin_lock(&ctx->lock);

	/*
	 * If the event is on, turn it off.
	 * If it is in error state, leave it in error state.
	 */
	if (event->state >= PERF_EVENT_STATE_INACTIVE) {
		update_context_time(ctx);
		update_group_times(event);
		if (event == event->group_leader)
			group_sched_out(event, cpuctx, ctx);
		else
			event_sched_out(event, cpuctx, ctx);
		event->state = PERF_EVENT_STATE_OFF;
	}

	raw_spin_unlock(&ctx->lock);
}

/*
 * Disable a event.
 *
 * If event->ctx is a cloned context, callers must make sure that
 * every task struct that event->ctx->task could possibly point to
 * remains valid.  This condition is satisifed when called through
 * perf_event_for_each_child or perf_event_for_each because they
 * hold the top-level event's child_mutex, so any descendant that
 * goes to exit will block in sync_child_event.
 * When called from perf_pending_event it's OK because event->ctx
 * is the current context on this CPU and preemption is disabled,
 * hence we can't get into perf_event_task_sched_out for this context.
 */
void perf_event_disable(struct perf_event *event)
{
	struct perf_event_context *ctx = event->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Disable the event on the cpu that it's on
		 */
		smp_call_function_single(event->cpu, __perf_event_disable,
					 event, 1);
		return;
	}

 retry:
	task_oncpu_function_call(task, __perf_event_disable, event);

	raw_spin_lock_irq(&ctx->lock);
	/*
	 * If the event is still active, we need to retry the cross-call.
	 */
	if (event->state == PERF_EVENT_STATE_ACTIVE) {
		raw_spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * Since we have the lock this context can't be scheduled
	 * in, so we can change the state safely.
	 */
	if (event->state == PERF_EVENT_STATE_INACTIVE) {
		update_group_times(event);
		event->state = PERF_EVENT_STATE_OFF;
	}

	raw_spin_unlock_irq(&ctx->lock);
}

static int
event_sched_in(struct perf_event *event,
		 struct perf_cpu_context *cpuctx,
		 struct perf_event_context *ctx)
{
	if (event->state <= PERF_EVENT_STATE_OFF)
		return 0;

	event->state = PERF_EVENT_STATE_ACTIVE;
	event->oncpu = smp_processor_id();
	/*
	 * The new state must be visible before we turn it on in the hardware:
	 */
	smp_wmb();

	if (event->pmu->enable(event)) {
		event->state = PERF_EVENT_STATE_INACTIVE;
		event->oncpu = -1;
		return -EAGAIN;
	}

	event->tstamp_running += ctx->time - event->tstamp_stopped;

	if (!is_software_event(event))
		cpuctx->active_oncpu++;
	ctx->nr_active++;

	if (event->attr.exclusive)
		cpuctx->exclusive = 1;

	return 0;
}

static int
group_sched_in(struct perf_event *group_event,
	       struct perf_cpu_context *cpuctx,
	       struct perf_event_context *ctx)
{
	struct perf_event *event, *partial_group = NULL;
	const struct pmu *pmu = group_event->pmu;
	bool txn = false;

	if (group_event->state == PERF_EVENT_STATE_OFF)
		return 0;

	/* Check if group transaction availabe */
	if (pmu->start_txn)
		txn = true;

	if (txn)
		pmu->start_txn(pmu);

	if (event_sched_in(group_event, cpuctx, ctx)) {
		if (txn)
			pmu->cancel_txn(pmu);
		return -EAGAIN;
	}

	/*
	 * Schedule in siblings as one group (if any):
	 */
	list_for_each_entry(event, &group_event->sibling_list, group_entry) {
		if (event_sched_in(event, cpuctx, ctx)) {
			partial_group = event;
			goto group_error;
		}
	}

	if (!txn || !pmu->commit_txn(pmu))
		return 0;

group_error:
	/*
	 * Groups can be scheduled in as one unit only, so undo any
	 * partial group before returning:
	 */
	list_for_each_entry(event, &group_event->sibling_list, group_entry) {
		if (event == partial_group)
			break;
		event_sched_out(event, cpuctx, ctx);
	}
	event_sched_out(group_event, cpuctx, ctx);

	if (txn)
		pmu->cancel_txn(pmu);

	return -EAGAIN;
}

/*
 * Work out whether we can put this event group on the CPU now.
 */
static int group_can_go_on(struct perf_event *event,
			   struct perf_cpu_context *cpuctx,
			   int can_add_hw)
{
	/*
	 * Groups consisting entirely of software events can always go on.
	 */
	if (event->group_flags & PERF_GROUP_SOFTWARE)
		return 1;
	/*
	 * If an exclusive group is already on, no other hardware
	 * events can go on.
	 */
	if (cpuctx->exclusive)
		return 0;
	/*
	 * If this group is exclusive and there are already
	 * events on the CPU, it can't go on.
	 */
	if (event->attr.exclusive && cpuctx->active_oncpu)
		return 0;
	/*
	 * Otherwise, try to add it if all previous groups were able
	 * to go on.
	 */
	return can_add_hw;
}

static void add_event_to_ctx(struct perf_event *event,
			       struct perf_event_context *ctx)
{
	list_add_event(event, ctx);
	perf_group_attach(event);
	event->tstamp_enabled = ctx->time;
	event->tstamp_running = ctx->time;
	event->tstamp_stopped = ctx->time;
}

/*
 * Cross CPU call to install and enable a performance event
 *
 * Must be called with ctx->mutex held
 */
static void __perf_install_in_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event *event = info;
	struct perf_event_context *ctx = event->ctx;
	struct perf_event *leader = event->group_leader;
	int err;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 * Or possibly this is the right context but it isn't
	 * on this cpu because it had no events.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx) {
		if (cpuctx->task_ctx || ctx->task != current)
			return;
		cpuctx->task_ctx = ctx;
	}

	raw_spin_lock(&ctx->lock);
	ctx->is_active = 1;
	update_context_time(ctx);

	/*
	 * Protect the list operation against NMI by disabling the
	 * events on a global level. NOP for non NMI based events.
	 */
	perf_disable();

	add_event_to_ctx(event, ctx);

	if (event->cpu != -1 && event->cpu != smp_processor_id())
		goto unlock;

	/*
	 * Don't put the event on if it is disabled or if
	 * it is in a group and the group isn't on.
	 */
	if (event->state != PERF_EVENT_STATE_INACTIVE ||
	    (leader != event && leader->state != PERF_EVENT_STATE_ACTIVE))
		goto unlock;

	/*
	 * An exclusive event can't go on if there are already active
	 * hardware events, and no hardware event can go on if there
	 * is already an exclusive event on.
	 */
	if (!group_can_go_on(event, cpuctx, 1))
		err = -EEXIST;
	else
		err = event_sched_in(event, cpuctx, ctx);

	if (err) {
		/*
		 * This event couldn't go on.  If it is in a group
		 * then we have to pull the whole group off.
		 * If the event group is pinned then put it in error state.
		 */
		if (leader != event)
			group_sched_out(leader, cpuctx, ctx);
		if (leader->attr.pinned) {
			update_group_times(leader);
			leader->state = PERF_EVENT_STATE_ERROR;
		}
	}

	if (!err && !ctx->task && cpuctx->max_pertask)
		cpuctx->max_pertask--;

 unlock:
	perf_enable();

	raw_spin_unlock(&ctx->lock);
}

/*
 * Attach a performance event to a context
 *
 * First we add the event to the list with the hardware enable bit
 * in event->hw_config cleared.
 *
 * If the event is attached to a task which is on a CPU we use a smp
 * call to enable it in the task context. The task might have been
 * scheduled away, but we check this in the smp call again.
 *
 * Must be called with ctx->mutex held.
 */
static void
perf_install_in_context(struct perf_event_context *ctx,
			struct perf_event *event,
			int cpu)
{
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Per cpu events are installed via an smp call and
		 * the install is always successful.
		 */
		smp_call_function_single(cpu, __perf_install_in_context,
					 event, 1);
		return;
	}

retry:
	task_oncpu_function_call(task, __perf_install_in_context,
				 event);

	raw_spin_lock_irq(&ctx->lock);
	/*
	 * we need to retry the smp call.
	 */
	if (ctx->is_active && list_empty(&event->group_entry)) {
		raw_spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * The lock prevents that this context is scheduled in so we
	 * can add the event safely, if it the call above did not
	 * succeed.
	 */
	if (list_empty(&event->group_entry))
		add_event_to_ctx(event, ctx);
	raw_spin_unlock_irq(&ctx->lock);
}

/*
 * Put a event into inactive state and update time fields.
 * Enabling the leader of a group effectively enables all
 * the group members that aren't explicitly disabled, so we
 * have to update their ->tstamp_enabled also.
 * Note: this works for group members as well as group leaders
 * since the non-leader members' sibling_lists will be empty.
 */
static void __perf_event_mark_enabled(struct perf_event *event,
					struct perf_event_context *ctx)
{
	struct perf_event *sub;

	event->state = PERF_EVENT_STATE_INACTIVE;
	event->tstamp_enabled = ctx->time - event->total_time_enabled;
	list_for_each_entry(sub, &event->sibling_list, group_entry)
		if (sub->state >= PERF_EVENT_STATE_INACTIVE)
			sub->tstamp_enabled =
				ctx->time - sub->total_time_enabled;
}

/*
 * Cross CPU call to enable a performance event
 */
static void __perf_event_enable(void *info)
{
	struct perf_event *event = info;
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = event->ctx;
	struct perf_event *leader = event->group_leader;
	int err;

	/*
	 * If this is a per-task event, need to check whether this
	 * event's task is the current task on this cpu.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx) {
		if (cpuctx->task_ctx || ctx->task != current)
			return;
		cpuctx->task_ctx = ctx;
	}

	raw_spin_lock(&ctx->lock);
	ctx->is_active = 1;
	update_context_time(ctx);

	if (event->state >= PERF_EVENT_STATE_INACTIVE)
		goto unlock;
	__perf_event_mark_enabled(event, ctx);

	if (event->cpu != -1 && event->cpu != smp_processor_id())
		goto unlock;

	/*
	 * If the event is in a group and isn't the group leader,
	 * then don't put it on unless the group is on.
	 */
	if (leader != event && leader->state != PERF_EVENT_STATE_ACTIVE)
		goto unlock;

	if (!group_can_go_on(event, cpuctx, 1)) {
		err = -EEXIST;
	} else {
		perf_disable();
		if (event == leader)
			err = group_sched_in(event, cpuctx, ctx);
		else
			err = event_sched_in(event, cpuctx, ctx);
		perf_enable();
	}

	if (err) {
		/*
		 * If this event can't go on and it's part of a
		 * group, then the whole group has to come off.
		 */
		if (leader != event)
			group_sched_out(leader, cpuctx, ctx);
		if (leader->attr.pinned) {
			update_group_times(leader);
			leader->state = PERF_EVENT_STATE_ERROR;
		}
	}

 unlock:
	raw_spin_unlock(&ctx->lock);
}

/*
 * Enable a event.
 *
 * If event->ctx is a cloned context, callers must make sure that
 * every task struct that event->ctx->task could possibly point to
 * remains valid.  This condition is satisfied when called through
 * perf_event_for_each_child or perf_event_for_each as described
 * for perf_event_disable.
 */
void perf_event_enable(struct perf_event *event)
{
	struct perf_event_context *ctx = event->ctx;
	struct task_struct *task = ctx->task;

	if (!task) {
		/*
		 * Enable the event on the cpu that it's on
		 */
		smp_call_function_single(event->cpu, __perf_event_enable,
					 event, 1);
		return;
	}

	raw_spin_lock_irq(&ctx->lock);
	if (event->state >= PERF_EVENT_STATE_INACTIVE)
		goto out;

	/*
	 * If the event is in error state, clear that first.
	 * That way, if we see the event in error state below, we
	 * know that it has gone back into error state, as distinct
	 * from the task having been scheduled away before the
	 * cross-call arrived.
	 */
	if (event->state == PERF_EVENT_STATE_ERROR)
		event->state = PERF_EVENT_STATE_OFF;

 retry:
	raw_spin_unlock_irq(&ctx->lock);
	task_oncpu_function_call(task, __perf_event_enable, event);

	raw_spin_lock_irq(&ctx->lock);

	/*
	 * If the context is active and the event is still off,
	 * we need to retry the cross-call.
	 */
	if (ctx->is_active && event->state == PERF_EVENT_STATE_OFF)
		goto retry;

	/*
	 * Since we have the lock this context can't be scheduled
	 * in, so we can change the state safely.
	 */
	if (event->state == PERF_EVENT_STATE_OFF)
		__perf_event_mark_enabled(event, ctx);

 out:
	raw_spin_unlock_irq(&ctx->lock);
}

static int perf_event_refresh(struct perf_event *event, int refresh)
{
	/*
	 * not supported on inherited events
	 */
	if (event->attr.inherit)
		return -EINVAL;

	atomic_add(refresh, &event->event_limit);
	perf_event_enable(event);

	return 0;
}

enum event_type_t {
	EVENT_FLEXIBLE = 0x1,
	EVENT_PINNED = 0x2,
	EVENT_ALL = EVENT_FLEXIBLE | EVENT_PINNED,
};

static void ctx_sched_out(struct perf_event_context *ctx,
			  struct perf_cpu_context *cpuctx,
			  enum event_type_t event_type)
{
	struct perf_event *event;

	raw_spin_lock(&ctx->lock);
	ctx->is_active = 0;
	if (likely(!ctx->nr_events))
		goto out;
	update_context_time(ctx);

	perf_disable();
	if (!ctx->nr_active)
		goto out_enable;

	if (event_type & EVENT_PINNED)
		list_for_each_entry(event, &ctx->pinned_groups, group_entry)
			group_sched_out(event, cpuctx, ctx);

	if (event_type & EVENT_FLEXIBLE)
		list_for_each_entry(event, &ctx->flexible_groups, group_entry)
			group_sched_out(event, cpuctx, ctx);

 out_enable:
	perf_enable();
 out:
	raw_spin_unlock(&ctx->lock);
}

/*
 * Test whether two contexts are equivalent, i.e. whether they
 * have both been cloned from the same version of the same context
 * and they both have the same number of enabled events.
 * If the number of enabled events is the same, then the set
 * of enabled events should be the same, because these are both
 * inherited contexts, therefore we can't access individual events
 * in them directly with an fd; we can only enable/disable all
 * events via prctl, or enable/disable all events in a family
 * via ioctl, which will have the same effect on both contexts.
 */
static int context_equiv(struct perf_event_context *ctx1,
			 struct perf_event_context *ctx2)
{
	return ctx1->parent_ctx && ctx1->parent_ctx == ctx2->parent_ctx
		&& ctx1->parent_gen == ctx2->parent_gen
		&& !ctx1->pin_count && !ctx2->pin_count;
}

static void __perf_event_sync_stat(struct perf_event *event,
				     struct perf_event *next_event)
{
	u64 value;

	if (!event->attr.inherit_stat)
		return;

	/*
	 * Update the event value, we cannot use perf_event_read()
	 * because we're in the middle of a context switch and have IRQs
	 * disabled, which upsets smp_call_function_single(), however
	 * we know the event must be on the current CPU, therefore we
	 * don't need to use it.
	 */
	switch (event->state) {
	case PERF_EVENT_STATE_ACTIVE:
		event->pmu->read(event);
		/* fall-through */

	case PERF_EVENT_STATE_INACTIVE:
		update_event_times(event);
		break;

	default:
		break;
	}

	/*
	 * In order to keep per-task stats reliable we need to flip the event
	 * values when we flip the contexts.
	 */
	value = local64_read(&next_event->count);
	value = local64_xchg(&event->count, value);
	local64_set(&next_event->count, value);

	swap(event->total_time_enabled, next_event->total_time_enabled);
	swap(event->total_time_running, next_event->total_time_running);

	/*
	 * Since we swizzled the values, update the user visible data too.
	 */
	perf_event_update_userpage(event);
	perf_event_update_userpage(next_event);
}

#define list_next_entry(pos, member) \
	list_entry(pos->member.next, typeof(*pos), member)

static void perf_event_sync_stat(struct perf_event_context *ctx,
				   struct perf_event_context *next_ctx)
{
	struct perf_event *event, *next_event;

	if (!ctx->nr_stat)
		return;

	update_context_time(ctx);

	event = list_first_entry(&ctx->event_list,
				   struct perf_event, event_entry);

	next_event = list_first_entry(&next_ctx->event_list,
					struct perf_event, event_entry);

	while (&event->event_entry != &ctx->event_list &&
	       &next_event->event_entry != &next_ctx->event_list) {

		__perf_event_sync_stat(event, next_event);

		event = list_next_entry(event, event_entry);
		next_event = list_next_entry(next_event, event_entry);
	}
}

/*
 * Called from scheduler to remove the events of the current task,
 * with interrupts disabled.
 *
 * We stop each event and update the event value in event->count.
 *
 * This does not protect us against NMI, but disable()
 * sets the disabled bit in the control field of event _before_
 * accessing the event control register. If a NMI hits, then it will
 * not restart the event.
 */
void perf_event_task_sched_out(struct task_struct *task,
				 struct task_struct *next)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = task->perf_event_ctxp;
	struct perf_event_context *next_ctx;
	struct perf_event_context *parent;
	int do_switch = 1;

	perf_sw_event(PERF_COUNT_SW_CONTEXT_SWITCHES, 1, 1, NULL, 0);

	if (likely(!ctx || !cpuctx->task_ctx))
		return;

	rcu_read_lock();
	parent = rcu_dereference(ctx->parent_ctx);
	next_ctx = next->perf_event_ctxp;
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
		raw_spin_lock(&ctx->lock);
		raw_spin_lock_nested(&next_ctx->lock, SINGLE_DEPTH_NESTING);
		if (context_equiv(ctx, next_ctx)) {
			/*
			 * XXX do we need a memory barrier of sorts
			 * wrt to rcu_dereference() of perf_event_ctxp
			 */
			task->perf_event_ctxp = next_ctx;
			next->perf_event_ctxp = ctx;
			ctx->task = next;
			next_ctx->task = task;
			do_switch = 0;

			perf_event_sync_stat(ctx, next_ctx);
		}
		raw_spin_unlock(&next_ctx->lock);
		raw_spin_unlock(&ctx->lock);
	}
	rcu_read_unlock();

	if (do_switch) {
		ctx_sched_out(ctx, cpuctx, EVENT_ALL);
		cpuctx->task_ctx = NULL;
	}
}

static void task_ctx_sched_out(struct perf_event_context *ctx,
			       enum event_type_t event_type)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);

	if (!cpuctx->task_ctx)
		return;

	if (WARN_ON_ONCE(ctx != cpuctx->task_ctx))
		return;

	ctx_sched_out(ctx, cpuctx, event_type);
	cpuctx->task_ctx = NULL;
}

/*
 * Called with IRQs disabled
 */
static void __perf_event_task_sched_out(struct perf_event_context *ctx)
{
	task_ctx_sched_out(ctx, EVENT_ALL);
}

/*
 * Called with IRQs disabled
 */
static void cpu_ctx_sched_out(struct perf_cpu_context *cpuctx,
			      enum event_type_t event_type)
{
	ctx_sched_out(&cpuctx->ctx, cpuctx, event_type);
}

static void
ctx_pinned_sched_in(struct perf_event_context *ctx,
		    struct perf_cpu_context *cpuctx)
{
	struct perf_event *event;

	list_for_each_entry(event, &ctx->pinned_groups, group_entry) {
		if (event->state <= PERF_EVENT_STATE_OFF)
			continue;
		if (event->cpu != -1 && event->cpu != smp_processor_id())
			continue;

		if (group_can_go_on(event, cpuctx, 1))
			group_sched_in(event, cpuctx, ctx);

		/*
		 * If this pinned group hasn't been scheduled,
		 * put it in error state.
		 */
		if (event->state == PERF_EVENT_STATE_INACTIVE) {
			update_group_times(event);
			event->state = PERF_EVENT_STATE_ERROR;
		}
	}
}

static void
ctx_flexible_sched_in(struct perf_event_context *ctx,
		      struct perf_cpu_context *cpuctx)
{
	struct perf_event *event;
	int can_add_hw = 1;

	list_for_each_entry(event, &ctx->flexible_groups, group_entry) {
		/* Ignore events in OFF or ERROR state */
		if (event->state <= PERF_EVENT_STATE_OFF)
			continue;
		/*
		 * Listen to the 'cpu' scheduling filter constraint
		 * of events:
		 */
		if (event->cpu != -1 && event->cpu != smp_processor_id())
			continue;

		if (group_can_go_on(event, cpuctx, can_add_hw))
			if (group_sched_in(event, cpuctx, ctx))
				can_add_hw = 0;
	}
}

static void
ctx_sched_in(struct perf_event_context *ctx,
	     struct perf_cpu_context *cpuctx,
	     enum event_type_t event_type)
{
	raw_spin_lock(&ctx->lock);
	ctx->is_active = 1;
	if (likely(!ctx->nr_events))
		goto out;

	ctx->timestamp = perf_clock();

	perf_disable();

	/*
	 * First go through the list and put on any pinned groups
	 * in order to give them the best chance of going on.
	 */
	if (event_type & EVENT_PINNED)
		ctx_pinned_sched_in(ctx, cpuctx);

	/* Then walk through the lower prio flexible groups */
	if (event_type & EVENT_FLEXIBLE)
		ctx_flexible_sched_in(ctx, cpuctx);

	perf_enable();
 out:
	raw_spin_unlock(&ctx->lock);
}

static void cpu_ctx_sched_in(struct perf_cpu_context *cpuctx,
			     enum event_type_t event_type)
{
	struct perf_event_context *ctx = &cpuctx->ctx;

	ctx_sched_in(ctx, cpuctx, event_type);
}

static void task_ctx_sched_in(struct task_struct *task,
			      enum event_type_t event_type)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = task->perf_event_ctxp;

	if (likely(!ctx))
		return;
	if (cpuctx->task_ctx == ctx)
		return;
	ctx_sched_in(ctx, cpuctx, event_type);
	cpuctx->task_ctx = ctx;
}
/*
 * Called from scheduler to add the events of the current task
 * with interrupts disabled.
 *
 * We restore the event value and then enable it.
 *
 * This does not protect us against NMI, but enable()
 * sets the enabled bit in the control field of event _before_
 * accessing the event control register. If a NMI hits, then it will
 * keep the event running.
 */
void perf_event_task_sched_in(struct task_struct *task)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = task->perf_event_ctxp;

	if (likely(!ctx))
		return;

	if (cpuctx->task_ctx == ctx)
		return;

	perf_disable();

	/*
	 * We want to keep the following priority order:
	 * cpu pinned (that don't need to move), task pinned,
	 * cpu flexible, task flexible.
	 */
	cpu_ctx_sched_out(cpuctx, EVENT_FLEXIBLE);

	ctx_sched_in(ctx, cpuctx, EVENT_PINNED);
	cpu_ctx_sched_in(cpuctx, EVENT_FLEXIBLE);
	ctx_sched_in(ctx, cpuctx, EVENT_FLEXIBLE);

	cpuctx->task_ctx = ctx;

	perf_enable();
}

#define MAX_INTERRUPTS (~0ULL)

static void perf_log_throttle(struct perf_event *event, int enable);

static u64 perf_calculate_period(struct perf_event *event, u64 nsec, u64 count)
{
	u64 frequency = event->attr.sample_freq;
	u64 sec = NSEC_PER_SEC;
	u64 divisor, dividend;

	int count_fls, nsec_fls, frequency_fls, sec_fls;

	count_fls = fls64(count);
	nsec_fls = fls64(nsec);
	frequency_fls = fls64(frequency);
	sec_fls = 30;

	/*
	 * We got @count in @nsec, with a target of sample_freq HZ
	 * the target period becomes:
	 *
	 *             @count * 10^9
	 * period = -------------------
	 *          @nsec * sample_freq
	 *
	 */

	/*
	 * Reduce accuracy by one bit such that @a and @b converge
	 * to a similar magnitude.
	 */
#define REDUCE_FLS(a, b) 		\
do {					\
	if (a##_fls > b##_fls) {	\
		a >>= 1;		\
		a##_fls--;		\
	} else {			\
		b >>= 1;		\
		b##_fls--;		\
	}				\
} while (0)

	/*
	 * Reduce accuracy until either term fits in a u64, then proceed with
	 * the other, so that finally we can do a u64/u64 division.
	 */
	while (count_fls + sec_fls > 64 && nsec_fls + frequency_fls > 64) {
		REDUCE_FLS(nsec, frequency);
		REDUCE_FLS(sec, count);
	}

	if (count_fls + sec_fls > 64) {
		divisor = nsec * frequency;

		while (count_fls + sec_fls > 64) {
			REDUCE_FLS(count, sec);
			divisor >>= 1;
		}

		dividend = count * sec;
	} else {
		dividend = count * sec;

		while (nsec_fls + frequency_fls > 64) {
			REDUCE_FLS(nsec, frequency);
			dividend >>= 1;
		}

		divisor = nsec * frequency;
	}

	if (!divisor)
		return dividend;

	return div64_u64(dividend, divisor);
}

static void perf_event_stop(struct perf_event *event)
{
	if (!event->pmu->stop)
		return event->pmu->disable(event);

	return event->pmu->stop(event);
}

static int perf_event_start(struct perf_event *event)
{
	if (!event->pmu->start)
		return event->pmu->enable(event);

	return event->pmu->start(event);
}

static void perf_adjust_period(struct perf_event *event, u64 nsec, u64 count)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 period, sample_period;
	s64 delta;

	period = perf_calculate_period(event, nsec, count);

	delta = (s64)(period - hwc->sample_period);
	delta = (delta + 7) / 8; /* low pass filter */

	sample_period = hwc->sample_period + delta;

	if (!sample_period)
		sample_period = 1;

	hwc->sample_period = sample_period;

	if (local64_read(&hwc->period_left) > 8*sample_period) {
		perf_disable();
		perf_event_stop(event);
		local64_set(&hwc->period_left, 0);
		perf_event_start(event);
		perf_enable();
	}
}

static void perf_ctx_adjust_freq(struct perf_event_context *ctx)
{
	struct perf_event *event;
	struct hw_perf_event *hwc;
	u64 interrupts, now;
	s64 delta;

	raw_spin_lock(&ctx->lock);
	list_for_each_entry_rcu(event, &ctx->event_list, event_entry) {
		if (event->state != PERF_EVENT_STATE_ACTIVE)
			continue;

		if (event->cpu != -1 && event->cpu != smp_processor_id())
			continue;

		hwc = &event->hw;

		interrupts = hwc->interrupts;
		hwc->interrupts = 0;

		/*
		 * unthrottle events on the tick
		 */
		if (interrupts == MAX_INTERRUPTS) {
			perf_log_throttle(event, 1);
			perf_disable();
			event->pmu->unthrottle(event);
			perf_enable();
		}

		if (!event->attr.freq || !event->attr.sample_freq)
			continue;

		perf_disable();
		event->pmu->read(event);
		now = local64_read(&event->count);
		delta = now - hwc->freq_count_stamp;
		hwc->freq_count_stamp = now;

		if (delta > 0)
			perf_adjust_period(event, TICK_NSEC, delta);
		perf_enable();
	}
	raw_spin_unlock(&ctx->lock);
}

/*
 * Round-robin a context's events:
 */
static void rotate_ctx(struct perf_event_context *ctx)
{
	raw_spin_lock(&ctx->lock);

	/* Rotate the first entry last of non-pinned groups */
	list_rotate_left(&ctx->flexible_groups);

	raw_spin_unlock(&ctx->lock);
}

void perf_event_task_tick(struct task_struct *curr)
{
	struct perf_cpu_context *cpuctx;
	struct perf_event_context *ctx;
	int rotate = 0;

	if (!atomic_read(&nr_events))
		return;

	cpuctx = &__get_cpu_var(perf_cpu_context);
	if (cpuctx->ctx.nr_events &&
	    cpuctx->ctx.nr_events != cpuctx->ctx.nr_active)
		rotate = 1;

	ctx = curr->perf_event_ctxp;
	if (ctx && ctx->nr_events && ctx->nr_events != ctx->nr_active)
		rotate = 1;

	perf_ctx_adjust_freq(&cpuctx->ctx);
	if (ctx)
		perf_ctx_adjust_freq(ctx);

	if (!rotate)
		return;

	perf_disable();
	cpu_ctx_sched_out(cpuctx, EVENT_FLEXIBLE);
	if (ctx)
		task_ctx_sched_out(ctx, EVENT_FLEXIBLE);

	rotate_ctx(&cpuctx->ctx);
	if (ctx)
		rotate_ctx(ctx);

	cpu_ctx_sched_in(cpuctx, EVENT_FLEXIBLE);
	if (ctx)
		task_ctx_sched_in(curr, EVENT_FLEXIBLE);
	perf_enable();
}

static int event_enable_on_exec(struct perf_event *event,
				struct perf_event_context *ctx)
{
	if (!event->attr.enable_on_exec)
		return 0;

	event->attr.enable_on_exec = 0;
	if (event->state >= PERF_EVENT_STATE_INACTIVE)
		return 0;

	__perf_event_mark_enabled(event, ctx);

	return 1;
}

/*
 * Enable all of a task's events that have been marked enable-on-exec.
 * This expects task == current.
 */
static void perf_event_enable_on_exec(struct task_struct *task)
{
	struct perf_event_context *ctx;
	struct perf_event *event;
	unsigned long flags;
	int enabled = 0;
	int ret;

	local_irq_save(flags);
	ctx = task->perf_event_ctxp;
	if (!ctx || !ctx->nr_events)
		goto out;

	__perf_event_task_sched_out(ctx);

	raw_spin_lock(&ctx->lock);

	list_for_each_entry(event, &ctx->pinned_groups, group_entry) {
		ret = event_enable_on_exec(event, ctx);
		if (ret)
			enabled = 1;
	}

	list_for_each_entry(event, &ctx->flexible_groups, group_entry) {
		ret = event_enable_on_exec(event, ctx);
		if (ret)
			enabled = 1;
	}

	/*
	 * Unclone this context if we enabled any event.
	 */
	if (enabled)
		unclone_ctx(ctx);

	raw_spin_unlock(&ctx->lock);

	perf_event_task_sched_in(task);
 out:
	local_irq_restore(flags);
}

/*
 * Cross CPU call to read the hardware event
 */
static void __perf_event_read(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event *event = info;
	struct perf_event_context *ctx = event->ctx;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu.  If not it has been
	 * scheduled out before the smp call arrived.  In that case
	 * event->count would have been updated to a recent sample
	 * when the event was scheduled out.
	 */
	if (ctx->task && cpuctx->task_ctx != ctx)
		return;

	raw_spin_lock(&ctx->lock);
	update_context_time(ctx);
	update_event_times(event);
	raw_spin_unlock(&ctx->lock);

	event->pmu->read(event);
}

static inline u64 perf_event_count(struct perf_event *event)
{
	return local64_read(&event->count) + atomic64_read(&event->child_count);
}

static u64 perf_event_read(struct perf_event *event)
{
	/*
	 * If event is enabled and currently active on a CPU, update the
	 * value in the event structure:
	 */
	if (event->state == PERF_EVENT_STATE_ACTIVE) {
		smp_call_function_single(event->oncpu,
					 __perf_event_read, event, 1);
	} else if (event->state == PERF_EVENT_STATE_INACTIVE) {
		struct perf_event_context *ctx = event->ctx;
		unsigned long flags;

		raw_spin_lock_irqsave(&ctx->lock, flags);
		update_context_time(ctx);
		update_event_times(event);
		raw_spin_unlock_irqrestore(&ctx->lock, flags);
	}

	return perf_event_count(event);
}

/*
 * Initialize the perf_event context in a task_struct:
 */
static void
__perf_event_init_context(struct perf_event_context *ctx,
			    struct task_struct *task)
{
	raw_spin_lock_init(&ctx->lock);
	mutex_init(&ctx->mutex);
	INIT_LIST_HEAD(&ctx->pinned_groups);
	INIT_LIST_HEAD(&ctx->flexible_groups);
	INIT_LIST_HEAD(&ctx->event_list);
	atomic_set(&ctx->refcount, 1);
	ctx->task = task;
}

static struct perf_event_context *find_get_context(pid_t pid, int cpu)
{
	struct perf_event_context *ctx;
	struct perf_cpu_context *cpuctx;
	struct task_struct *task;
	unsigned long flags;
	int err;

	if (pid == -1 && cpu != -1) {
		/* Must be root to operate on a CPU event: */
		if (perf_paranoid_cpu() && !capable(CAP_SYS_ADMIN))
			return ERR_PTR(-EACCES);

		if (cpu < 0 || cpu >= nr_cpumask_bits)
			return ERR_PTR(-EINVAL);

		/*
		 * We could be clever and allow to attach a event to an
		 * offline CPU and activate it when the CPU comes up, but
		 * that's for later.
		 */
		if (!cpu_online(cpu))
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
	 * Can't attach events to a dying task.
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
		unclone_ctx(ctx);
		raw_spin_unlock_irqrestore(&ctx->lock, flags);
	}

	if (!ctx) {
		ctx = kzalloc(sizeof(struct perf_event_context), GFP_KERNEL);
		err = -ENOMEM;
		if (!ctx)
			goto errout;
		__perf_event_init_context(ctx, task);
		get_ctx(ctx);
		if (cmpxchg(&task->perf_event_ctxp, NULL, ctx)) {
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

static void perf_event_free_filter(struct perf_event *event);

static void free_event_rcu(struct rcu_head *head)
{
	struct perf_event *event;

	event = container_of(head, struct perf_event, rcu_head);
	if (event->ns)
		put_pid_ns(event->ns);
	perf_event_free_filter(event);
	kfree(event);
}

static void perf_pending_sync(struct perf_event *event);
static void perf_buffer_put(struct perf_buffer *buffer);

static void free_event(struct perf_event *event)
{
	perf_pending_sync(event);

	if (!event->parent) {
		atomic_dec(&nr_events);
		if (event->attr.mmap || event->attr.mmap_data)
			atomic_dec(&nr_mmap_events);
		if (event->attr.comm)
			atomic_dec(&nr_comm_events);
		if (event->attr.task)
			atomic_dec(&nr_task_events);
	}

	if (event->buffer) {
		perf_buffer_put(event->buffer);
		event->buffer = NULL;
	}

	if (event->destroy)
		event->destroy(event);

	put_ctx(event->ctx);
	call_rcu(&event->rcu_head, free_event_rcu);
}

int perf_event_release_kernel(struct perf_event *event)
{
	struct perf_event_context *ctx = event->ctx;

	/*
	 * Remove from the PMU, can't get re-enabled since we got
	 * here because the last ref went.
	 */
	perf_event_disable(event);

	WARN_ON_ONCE(ctx->parent_ctx);
	/*
	 * There are two ways this annotation is useful:
	 *
	 *  1) there is a lock recursion from perf_event_exit_task
	 *     see the comment there.
	 *
	 *  2) there is a lock-inversion with mmap_sem through
	 *     perf_event_read_group(), which takes faults while
	 *     holding ctx->mutex, however this is called after
	 *     the last filedesc died, so there is no possibility
	 *     to trigger the AB-BA case.
	 */
	mutex_lock_nested(&ctx->mutex, SINGLE_DEPTH_NESTING);
	raw_spin_lock_irq(&ctx->lock);
	perf_group_detach(event);
	list_del_event(event, ctx);
	raw_spin_unlock_irq(&ctx->lock);
	mutex_unlock(&ctx->mutex);

	mutex_lock(&event->owner->perf_event_mutex);
	list_del_init(&event->owner_entry);
	mutex_unlock(&event->owner->perf_event_mutex);
	put_task_struct(event->owner);

	free_event(event);

	return 0;
}
EXPORT_SYMBOL_GPL(perf_event_release_kernel);

/*
 * Called when the last reference to the file is gone.
 */
static int perf_release(struct inode *inode, struct file *file)
{
	struct perf_event *event = file->private_data;

	file->private_data = NULL;

	return perf_event_release_kernel(event);
}

static int perf_event_read_size(struct perf_event *event)
{
	int entry = sizeof(u64); /* value */
	int size = 0;
	int nr = 1;

	if (event->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		size += sizeof(u64);

	if (event->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		size += sizeof(u64);

	if (event->attr.read_format & PERF_FORMAT_ID)
		entry += sizeof(u64);

	if (event->attr.read_format & PERF_FORMAT_GROUP) {
		nr += event->group_leader->nr_siblings;
		size += sizeof(u64);
	}

	size += entry * nr;

	return size;
}

u64 perf_event_read_value(struct perf_event *event, u64 *enabled, u64 *running)
{
	struct perf_event *child;
	u64 total = 0;

	*enabled = 0;
	*running = 0;

	mutex_lock(&event->child_mutex);
	total += perf_event_read(event);
	*enabled += event->total_time_enabled +
			atomic64_read(&event->child_total_time_enabled);
	*running += event->total_time_running +
			atomic64_read(&event->child_total_time_running);

	list_for_each_entry(child, &event->child_list, child_list) {
		total += perf_event_read(child);
		*enabled += child->total_time_enabled;
		*running += child->total_time_running;
	}
	mutex_unlock(&event->child_mutex);

	return total;
}
EXPORT_SYMBOL_GPL(perf_event_read_value);

static int perf_event_read_group(struct perf_event *event,
				   u64 read_format, char __user *buf)
{
	struct perf_event *leader = event->group_leader, *sub;
	int n = 0, size = 0, ret = -EFAULT;
	struct perf_event_context *ctx = leader->ctx;
	u64 values[5];
	u64 count, enabled, running;

	mutex_lock(&ctx->mutex);
	count = perf_event_read_value(leader, &enabled, &running);

	values[n++] = 1 + leader->nr_siblings;
	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = enabled;
	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = running;
	values[n++] = count;
	if (read_format & PERF_FORMAT_ID)
		values[n++] = primary_event_id(leader);

	size = n * sizeof(u64);

	if (copy_to_user(buf, values, size))
		goto unlock;

	ret = size;

	list_for_each_entry(sub, &leader->sibling_list, group_entry) {
		n = 0;

		values[n++] = perf_event_read_value(sub, &enabled, &running);
		if (read_format & PERF_FORMAT_ID)
			values[n++] = primary_event_id(sub);

		size = n * sizeof(u64);

		if (copy_to_user(buf + ret, values, size)) {
			ret = -EFAULT;
			goto unlock;
		}

		ret += size;
	}
unlock:
	mutex_unlock(&ctx->mutex);

	return ret;
}

static int perf_event_read_one(struct perf_event *event,
				 u64 read_format, char __user *buf)
{
	u64 enabled, running;
	u64 values[4];
	int n = 0;

	values[n++] = perf_event_read_value(event, &enabled, &running);
	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = enabled;
	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = running;
	if (read_format & PERF_FORMAT_ID)
		values[n++] = primary_event_id(event);

	if (copy_to_user(buf, values, n * sizeof(u64)))
		return -EFAULT;

	return n * sizeof(u64);
}

/*
 * Read the performance event - simple non blocking version for now
 */
static ssize_t
perf_read_hw(struct perf_event *event, char __user *buf, size_t count)
{
	u64 read_format = event->attr.read_format;
	int ret;

	/*
	 * Return end-of-file for a read on a event that is in
	 * error state (i.e. because it was pinned but it couldn't be
	 * scheduled on to the CPU at some point).
	 */
	if (event->state == PERF_EVENT_STATE_ERROR)
		return 0;

	if (count < perf_event_read_size(event))
		return -ENOSPC;

	WARN_ON_ONCE(event->ctx->parent_ctx);
	if (read_format & PERF_FORMAT_GROUP)
		ret = perf_event_read_group(event, read_format, buf);
	else
		ret = perf_event_read_one(event, read_format, buf);

	return ret;
}

static ssize_t
perf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct perf_event *event = file->private_data;

	return perf_read_hw(event, buf, count);
}

static unsigned int perf_poll(struct file *file, poll_table *wait)
{
	struct perf_event *event = file->private_data;
	struct perf_buffer *buffer;
	unsigned int events = POLL_HUP;

	rcu_read_lock();
	buffer = rcu_dereference(event->buffer);
	if (buffer)
		events = atomic_xchg(&buffer->poll, 0);
	rcu_read_unlock();

	poll_wait(file, &event->waitq, wait);

	return events;
}

static void perf_event_reset(struct perf_event *event)
{
	(void)perf_event_read(event);
	local64_set(&event->count, 0);
	perf_event_update_userpage(event);
}

/*
 * Holding the top-level event's child_mutex means that any
 * descendant process that has inherited this event will block
 * in sync_child_event if it goes to exit, thus satisfying the
 * task existence requirements of perf_event_enable/disable.
 */
static void perf_event_for_each_child(struct perf_event *event,
					void (*func)(struct perf_event *))
{
	struct perf_event *child;

	WARN_ON_ONCE(event->ctx->parent_ctx);
	mutex_lock(&event->child_mutex);
	func(event);
	list_for_each_entry(child, &event->child_list, child_list)
		func(child);
	mutex_unlock(&event->child_mutex);
}

static void perf_event_for_each(struct perf_event *event,
				  void (*func)(struct perf_event *))
{
	struct perf_event_context *ctx = event->ctx;
	struct perf_event *sibling;

	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	event = event->group_leader;

	perf_event_for_each_child(event, func);
	func(event);
	list_for_each_entry(sibling, &event->sibling_list, group_entry)
		perf_event_for_each_child(event, func);
	mutex_unlock(&ctx->mutex);
}

static int perf_event_period(struct perf_event *event, u64 __user *arg)
{
	struct perf_event_context *ctx = event->ctx;
	unsigned long size;
	int ret = 0;
	u64 value;

	if (!event->attr.sample_period)
		return -EINVAL;

	size = copy_from_user(&value, arg, sizeof(value));
	if (size != sizeof(value))
		return -EFAULT;

	if (!value)
		return -EINVAL;

	raw_spin_lock_irq(&ctx->lock);
	if (event->attr.freq) {
		if (value > sysctl_perf_event_sample_rate) {
			ret = -EINVAL;
			goto unlock;
		}

		event->attr.sample_freq = value;
	} else {
		event->attr.sample_period = value;
		event->hw.sample_period = value;
	}
unlock:
	raw_spin_unlock_irq(&ctx->lock);

	return ret;
}

static const struct file_operations perf_fops;

static struct perf_event *perf_fget_light(int fd, int *fput_needed)
{
	struct file *file;

	file = fget_light(fd, fput_needed);
	if (!file)
		return ERR_PTR(-EBADF);

	if (file->f_op != &perf_fops) {
		fput_light(file, *fput_needed);
		*fput_needed = 0;
		return ERR_PTR(-EBADF);
	}

	return file->private_data;
}

static int perf_event_set_output(struct perf_event *event,
				 struct perf_event *output_event);
static int perf_event_set_filter(struct perf_event *event, void __user *arg);

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct perf_event *event = file->private_data;
	void (*func)(struct perf_event *);
	u32 flags = arg;

	switch (cmd) {
	case PERF_EVENT_IOC_ENABLE:
		func = perf_event_enable;
		break;
	case PERF_EVENT_IOC_DISABLE:
		func = perf_event_disable;
		break;
	case PERF_EVENT_IOC_RESET:
		func = perf_event_reset;
		break;

	case PERF_EVENT_IOC_REFRESH:
		return perf_event_refresh(event, arg);

	case PERF_EVENT_IOC_PERIOD:
		return perf_event_period(event, (u64 __user *)arg);

	case PERF_EVENT_IOC_SET_OUTPUT:
	{
		struct perf_event *output_event = NULL;
		int fput_needed = 0;
		int ret;

		if (arg != -1) {
			output_event = perf_fget_light(arg, &fput_needed);
			if (IS_ERR(output_event))
				return PTR_ERR(output_event);
		}

		ret = perf_event_set_output(event, output_event);
		if (output_event)
			fput_light(output_event->filp, fput_needed);

		return ret;
	}

	case PERF_EVENT_IOC_SET_FILTER:
		return perf_event_set_filter(event, (void __user *)arg);

	default:
		return -ENOTTY;
	}

	if (flags & PERF_IOC_FLAG_GROUP)
		perf_event_for_each(event, func);
	else
		perf_event_for_each_child(event, func);

	return 0;
}

int perf_event_task_enable(void)
{
	struct perf_event *event;

	mutex_lock(&current->perf_event_mutex);
	list_for_each_entry(event, &current->perf_event_list, owner_entry)
		perf_event_for_each_child(event, perf_event_enable);
	mutex_unlock(&current->perf_event_mutex);

	return 0;
}

int perf_event_task_disable(void)
{
	struct perf_event *event;

	mutex_lock(&current->perf_event_mutex);
	list_for_each_entry(event, &current->perf_event_list, owner_entry)
		perf_event_for_each_child(event, perf_event_disable);
	mutex_unlock(&current->perf_event_mutex);

	return 0;
}

#ifndef PERF_EVENT_INDEX_OFFSET
# define PERF_EVENT_INDEX_OFFSET 0
#endif

static int perf_event_index(struct perf_event *event)
{
	if (event->state != PERF_EVENT_STATE_ACTIVE)
		return 0;

	return event->hw.idx + 1 - PERF_EVENT_INDEX_OFFSET;
}

/*
 * Callers need to ensure there can be no nesting of this function, otherwise
 * the seqlock logic goes bad. We can not serialize this because the arch
 * code calls this from NMI context.
 */
void perf_event_update_userpage(struct perf_event *event)
{
	struct perf_event_mmap_page *userpg;
	struct perf_buffer *buffer;

	rcu_read_lock();
	buffer = rcu_dereference(event->buffer);
	if (!buffer)
		goto unlock;

	userpg = buffer->user_page;

	/*
	 * Disable preemption so as to not let the corresponding user-space
	 * spin too long if we get preempted.
	 */
	preempt_disable();
	++userpg->lock;
	barrier();
	userpg->index = perf_event_index(event);
	userpg->offset = perf_event_count(event);
	if (event->state == PERF_EVENT_STATE_ACTIVE)
		userpg->offset -= local64_read(&event->hw.prev_count);

	userpg->time_enabled = event->total_time_enabled +
			atomic64_read(&event->child_total_time_enabled);

	userpg->time_running = event->total_time_running +
			atomic64_read(&event->child_total_time_running);

	barrier();
	++userpg->lock;
	preempt_enable();
unlock:
	rcu_read_unlock();
}

static unsigned long perf_data_size(struct perf_buffer *buffer);

static void
perf_buffer_init(struct perf_buffer *buffer, long watermark, int flags)
{
	long max_size = perf_data_size(buffer);

	if (watermark)
		buffer->watermark = min(max_size, watermark);

	if (!buffer->watermark)
		buffer->watermark = max_size / 2;

	if (flags & PERF_BUFFER_WRITABLE)
		buffer->writable = 1;

	atomic_set(&buffer->refcount, 1);
}

#ifndef CONFIG_PERF_USE_VMALLOC

/*
 * Back perf_mmap() with regular GFP_KERNEL-0 pages.
 */

static struct page *
perf_mmap_to_page(struct perf_buffer *buffer, unsigned long pgoff)
{
	if (pgoff > buffer->nr_pages)
		return NULL;

	if (pgoff == 0)
		return virt_to_page(buffer->user_page);

	return virt_to_page(buffer->data_pages[pgoff - 1]);
}

static void *perf_mmap_alloc_page(int cpu)
{
	struct page *page;
	int node;

	node = (cpu == -1) ? cpu : cpu_to_node(cpu);
	page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
	if (!page)
		return NULL;

	return page_address(page);
}

static struct perf_buffer *
perf_buffer_alloc(int nr_pages, long watermark, int cpu, int flags)
{
	struct perf_buffer *buffer;
	unsigned long size;
	int i;

	size = sizeof(struct perf_buffer);
	size += nr_pages * sizeof(void *);

	buffer = kzalloc(size, GFP_KERNEL);
	if (!buffer)
		goto fail;

	buffer->user_page = perf_mmap_alloc_page(cpu);
	if (!buffer->user_page)
		goto fail_user_page;

	for (i = 0; i < nr_pages; i++) {
		buffer->data_pages[i] = perf_mmap_alloc_page(cpu);
		if (!buffer->data_pages[i])
			goto fail_data_pages;
	}

	buffer->nr_pages = nr_pages;

	perf_buffer_init(buffer, watermark, flags);

	return buffer;

fail_data_pages:
	for (i--; i >= 0; i--)
		free_page((unsigned long)buffer->data_pages[i]);

	free_page((unsigned long)buffer->user_page);

fail_user_page:
	kfree(buffer);

fail:
	return NULL;
}

static void perf_mmap_free_page(unsigned long addr)
{
	struct page *page = virt_to_page((void *)addr);

	page->mapping = NULL;
	__free_page(page);
}

static void perf_buffer_free(struct perf_buffer *buffer)
{
	int i;

	perf_mmap_free_page((unsigned long)buffer->user_page);
	for (i = 0; i < buffer->nr_pages; i++)
		perf_mmap_free_page((unsigned long)buffer->data_pages[i]);
	kfree(buffer);
}

static inline int page_order(struct perf_buffer *buffer)
{
	return 0;
}

#else

/*
 * Back perf_mmap() with vmalloc memory.
 *
 * Required for architectures that have d-cache aliasing issues.
 */

static inline int page_order(struct perf_buffer *buffer)
{
	return buffer->page_order;
}

static struct page *
perf_mmap_to_page(struct perf_buffer *buffer, unsigned long pgoff)
{
	if (pgoff > (1UL << page_order(buffer)))
		return NULL;

	return vmalloc_to_page((void *)buffer->user_page + pgoff * PAGE_SIZE);
}

static void perf_mmap_unmark_page(void *addr)
{
	struct page *page = vmalloc_to_page(addr);

	page->mapping = NULL;
}

static void perf_buffer_free_work(struct work_struct *work)
{
	struct perf_buffer *buffer;
	void *base;
	int i, nr;

	buffer = container_of(work, struct perf_buffer, work);
	nr = 1 << page_order(buffer);

	base = buffer->user_page;
	for (i = 0; i < nr + 1; i++)
		perf_mmap_unmark_page(base + (i * PAGE_SIZE));

	vfree(base);
	kfree(buffer);
}

static void perf_buffer_free(struct perf_buffer *buffer)
{
	schedule_work(&buffer->work);
}

static struct perf_buffer *
perf_buffer_alloc(int nr_pages, long watermark, int cpu, int flags)
{
	struct perf_buffer *buffer;
	unsigned long size;
	void *all_buf;

	size = sizeof(struct perf_buffer);
	size += sizeof(void *);

	buffer = kzalloc(size, GFP_KERNEL);
	if (!buffer)
		goto fail;

	INIT_WORK(&buffer->work, perf_buffer_free_work);

	all_buf = vmalloc_user((nr_pages + 1) * PAGE_SIZE);
	if (!all_buf)
		goto fail_all_buf;

	buffer->user_page = all_buf;
	buffer->data_pages[0] = all_buf + PAGE_SIZE;
	buffer->page_order = ilog2(nr_pages);
	buffer->nr_pages = 1;

	perf_buffer_init(buffer, watermark, flags);

	return buffer;

fail_all_buf:
	kfree(buffer);

fail:
	return NULL;
}

#endif

static unsigned long perf_data_size(struct perf_buffer *buffer)
{
	return buffer->nr_pages << (PAGE_SHIFT + page_order(buffer));
}

static int perf_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct perf_event *event = vma->vm_file->private_data;
	struct perf_buffer *buffer;
	int ret = VM_FAULT_SIGBUS;

	if (vmf->flags & FAULT_FLAG_MKWRITE) {
		if (vmf->pgoff == 0)
			ret = 0;
		return ret;
	}

	rcu_read_lock();
	buffer = rcu_dereference(event->buffer);
	if (!buffer)
		goto unlock;

	if (vmf->pgoff && (vmf->flags & FAULT_FLAG_WRITE))
		goto unlock;

	vmf->page = perf_mmap_to_page(buffer, vmf->pgoff);
	if (!vmf->page)
		goto unlock;

	get_page(vmf->page);
	vmf->page->mapping = vma->vm_file->f_mapping;
	vmf->page->index   = vmf->pgoff;

	ret = 0;
unlock:
	rcu_read_unlock();

	return ret;
}

static void perf_buffer_free_rcu(struct rcu_head *rcu_head)
{
	struct perf_buffer *buffer;

	buffer = container_of(rcu_head, struct perf_buffer, rcu_head);
	perf_buffer_free(buffer);
}

static struct perf_buffer *perf_buffer_get(struct perf_event *event)
{
	struct perf_buffer *buffer;

	rcu_read_lock();
	buffer = rcu_dereference(event->buffer);
	if (buffer) {
		if (!atomic_inc_not_zero(&buffer->refcount))
			buffer = NULL;
	}
	rcu_read_unlock();

	return buffer;
}

static void perf_buffer_put(struct perf_buffer *buffer)
{
	if (!atomic_dec_and_test(&buffer->refcount))
		return;

	call_rcu(&buffer->rcu_head, perf_buffer_free_rcu);
}

static void perf_mmap_open(struct vm_area_struct *vma)
{
	struct perf_event *event = vma->vm_file->private_data;

	atomic_inc(&event->mmap_count);
}

static void perf_mmap_close(struct vm_area_struct *vma)
{
	struct perf_event *event = vma->vm_file->private_data;

	if (atomic_dec_and_mutex_lock(&event->mmap_count, &event->mmap_mutex)) {
		unsigned long size = perf_data_size(event->buffer);
		struct user_struct *user = event->mmap_user;
		struct perf_buffer *buffer = event->buffer;

		atomic_long_sub((size >> PAGE_SHIFT) + 1, &user->locked_vm);
		vma->vm_mm->locked_vm -= event->mmap_locked;
		rcu_assign_pointer(event->buffer, NULL);
		mutex_unlock(&event->mmap_mutex);

		perf_buffer_put(buffer);
		free_uid(user);
	}
}

static const struct vm_operations_struct perf_mmap_vmops = {
	.open		= perf_mmap_open,
	.close		= perf_mmap_close,
	.fault		= perf_mmap_fault,
	.page_mkwrite	= perf_mmap_fault,
};

static int perf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct perf_event *event = file->private_data;
	unsigned long user_locked, user_lock_limit;
	struct user_struct *user = current_user();
	unsigned long locked, lock_limit;
	struct perf_buffer *buffer;
	unsigned long vma_size;
	unsigned long nr_pages;
	long user_extra, extra;
	int ret = 0, flags = 0;

	/*
	 * Don't allow mmap() of inherited per-task counters. This would
	 * create a performance issue due to all children writing to the
	 * same buffer.
	 */
	if (event->cpu == -1 && event->attr.inherit)
		return -EINVAL;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	vma_size = vma->vm_end - vma->vm_start;
	nr_pages = (vma_size / PAGE_SIZE) - 1;

	/*
	 * If we have buffer pages ensure they're a power-of-two number, so we
	 * can do bitmasks instead of modulo.
	 */
	if (nr_pages != 0 && !is_power_of_2(nr_pages))
		return -EINVAL;

	if (vma_size != PAGE_SIZE * (1 + nr_pages))
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	WARN_ON_ONCE(event->ctx->parent_ctx);
	mutex_lock(&event->mmap_mutex);
	if (event->buffer) {
		if (event->buffer->nr_pages == nr_pages)
			atomic_inc(&event->buffer->refcount);
		else
			ret = -EINVAL;
		goto unlock;
	}

	user_extra = nr_pages + 1;
	user_lock_limit = sysctl_perf_event_mlock >> (PAGE_SHIFT - 10);

	/*
	 * Increase the limit linearly with more CPUs:
	 */
	user_lock_limit *= num_online_cpus();

	user_locked = atomic_long_read(&user->locked_vm) + user_extra;

	extra = 0;
	if (user_locked > user_lock_limit)
		extra = user_locked - user_lock_limit;

	lock_limit = rlimit(RLIMIT_MEMLOCK);
	lock_limit >>= PAGE_SHIFT;
	locked = vma->vm_mm->locked_vm + extra;

	if ((locked > lock_limit) && perf_paranoid_tracepoint_raw() &&
		!capable(CAP_IPC_LOCK)) {
		ret = -EPERM;
		goto unlock;
	}

	WARN_ON(event->buffer);

	if (vma->vm_flags & VM_WRITE)
		flags |= PERF_BUFFER_WRITABLE;

	buffer = perf_buffer_alloc(nr_pages, event->attr.wakeup_watermark,
				   event->cpu, flags);
	if (!buffer) {
		ret = -ENOMEM;
		goto unlock;
	}
	rcu_assign_pointer(event->buffer, buffer);

	atomic_long_add(user_extra, &user->locked_vm);
	event->mmap_locked = extra;
	event->mmap_user = get_current_user();
	vma->vm_mm->locked_vm += event->mmap_locked;

unlock:
	if (!ret)
		atomic_inc(&event->mmap_count);
	mutex_unlock(&event->mmap_mutex);

	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &perf_mmap_vmops;

	return ret;
}

static int perf_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct perf_event *event = filp->private_data;
	int retval;

	mutex_lock(&inode->i_mutex);
	retval = fasync_helper(fd, filp, on, &event->fasync);
	mutex_unlock(&inode->i_mutex);

	if (retval < 0)
		return retval;

	return 0;
}

static const struct file_operations perf_fops = {
	.llseek			= no_llseek,
	.release		= perf_release,
	.read			= perf_read,
	.poll			= perf_poll,
	.unlocked_ioctl		= perf_ioctl,
	.compat_ioctl		= perf_ioctl,
	.mmap			= perf_mmap,
	.fasync			= perf_fasync,
};

/*
 * Perf event wakeup
 *
 * If there's data, ensure we set the poll() state and publish everything
 * to user-space before waking everybody up.
 */

void perf_event_wakeup(struct perf_event *event)
{
	wake_up_all(&event->waitq);

	if (event->pending_kill) {
		kill_fasync(&event->fasync, SIGIO, event->pending_kill);
		event->pending_kill = 0;
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

static void perf_pending_event(struct perf_pending_entry *entry)
{
	struct perf_event *event = container_of(entry,
			struct perf_event, pending);

	if (event->pending_disable) {
		event->pending_disable = 0;
		__perf_event_disable(event);
	}

	if (event->pending_wakeup) {
		event->pending_wakeup = 0;
		perf_event_wakeup(event);
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

	set_perf_event_pending();

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

static inline int perf_not_pending(struct perf_event *event)
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
	return event->pending.next == NULL;
}

static void perf_pending_sync(struct perf_event *event)
{
	wait_event(event->waitq, perf_not_pending(event));
}

void perf_event_do_pending(void)
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
 * We assume there is only KVM supporting the callbacks.
 * Later on, we might change it to a list if there is
 * another virtualization implementation supporting the callbacks.
 */
struct perf_guest_info_callbacks *perf_guest_cbs;

int perf_register_guest_info_callbacks(struct perf_guest_info_callbacks *cbs)
{
	perf_guest_cbs = cbs;
	return 0;
}
EXPORT_SYMBOL_GPL(perf_register_guest_info_callbacks);

int perf_unregister_guest_info_callbacks(struct perf_guest_info_callbacks *cbs)
{
	perf_guest_cbs = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(perf_unregister_guest_info_callbacks);

/*
 * Output
 */
static bool perf_output_space(struct perf_buffer *buffer, unsigned long tail,
			      unsigned long offset, unsigned long head)
{
	unsigned long mask;

	if (!buffer->writable)
		return true;

	mask = perf_data_size(buffer) - 1;

	offset = (offset - tail) & mask;
	head   = (head   - tail) & mask;

	if ((int)(head - offset) < 0)
		return false;

	return true;
}

static void perf_output_wakeup(struct perf_output_handle *handle)
{
	atomic_set(&handle->buffer->poll, POLL_IN);

	if (handle->nmi) {
		handle->event->pending_wakeup = 1;
		perf_pending_queue(&handle->event->pending,
				   perf_pending_event);
	} else
		perf_event_wakeup(handle->event);
}

/*
 * We need to ensure a later event_id doesn't publish a head when a former
 * event isn't done writing. However since we need to deal with NMIs we
 * cannot fully serialize things.
 *
 * We only publish the head (and generate a wakeup) when the outer-most
 * event completes.
 */
static void perf_output_get_handle(struct perf_output_handle *handle)
{
	struct perf_buffer *buffer = handle->buffer;

	preempt_disable();
	local_inc(&buffer->nest);
	handle->wakeup = local_read(&buffer->wakeup);
}

static void perf_output_put_handle(struct perf_output_handle *handle)
{
	struct perf_buffer *buffer = handle->buffer;
	unsigned long head;

again:
	head = local_read(&buffer->head);

	/*
	 * IRQ/NMI can happen here, which means we can miss a head update.
	 */

	if (!local_dec_and_test(&buffer->nest))
		goto out;

	/*
	 * Publish the known good head. Rely on the full barrier implied
	 * by atomic_dec_and_test() order the buffer->head read and this
	 * write.
	 */
	buffer->user_page->data_head = head;

	/*
	 * Now check if we missed an update, rely on the (compiler)
	 * barrier in atomic_dec_and_test() to re-read buffer->head.
	 */
	if (unlikely(head != local_read(&buffer->head))) {
		local_inc(&buffer->nest);
		goto again;
	}

	if (handle->wakeup != local_read(&buffer->wakeup))
		perf_output_wakeup(handle);

 out:
	preempt_enable();
}

__always_inline void perf_output_copy(struct perf_output_handle *handle,
		      const void *buf, unsigned int len)
{
	do {
		unsigned long size = min_t(unsigned long, handle->size, len);

		memcpy(handle->addr, buf, size);

		len -= size;
		handle->addr += size;
		buf += size;
		handle->size -= size;
		if (!handle->size) {
			struct perf_buffer *buffer = handle->buffer;

			handle->page++;
			handle->page &= buffer->nr_pages - 1;
			handle->addr = buffer->data_pages[handle->page];
			handle->size = PAGE_SIZE << page_order(buffer);
		}
	} while (len);
}

int perf_output_begin(struct perf_output_handle *handle,
		      struct perf_event *event, unsigned int size,
		      int nmi, int sample)
{
	struct perf_buffer *buffer;
	unsigned long tail, offset, head;
	int have_lost;
	struct {
		struct perf_event_header header;
		u64			 id;
		u64			 lost;
	} lost_event;

	rcu_read_lock();
	/*
	 * For inherited events we send all the output towards the parent.
	 */
	if (event->parent)
		event = event->parent;

	buffer = rcu_dereference(event->buffer);
	if (!buffer)
		goto out;

	handle->buffer	= buffer;
	handle->event	= event;
	handle->nmi	= nmi;
	handle->sample	= sample;

	if (!buffer->nr_pages)
		goto out;

	have_lost = local_read(&buffer->lost);
	if (have_lost)
		size += sizeof(lost_event);

	perf_output_get_handle(handle);

	do {
		/*
		 * Userspace could choose to issue a mb() before updating the
		 * tail pointer. So that all reads will be completed before the
		 * write is issued.
		 */
		tail = ACCESS_ONCE(buffer->user_page->data_tail);
		smp_rmb();
		offset = head = local_read(&buffer->head);
		head += size;
		if (unlikely(!perf_output_space(buffer, tail, offset, head)))
			goto fail;
	} while (local_cmpxchg(&buffer->head, offset, head) != offset);

	if (head - local_read(&buffer->wakeup) > buffer->watermark)
		local_add(buffer->watermark, &buffer->wakeup);

	handle->page = offset >> (PAGE_SHIFT + page_order(buffer));
	handle->page &= buffer->nr_pages - 1;
	handle->size = offset & ((PAGE_SIZE << page_order(buffer)) - 1);
	handle->addr = buffer->data_pages[handle->page];
	handle->addr += handle->size;
	handle->size = (PAGE_SIZE << page_order(buffer)) - handle->size;

	if (have_lost) {
		lost_event.header.type = PERF_RECORD_LOST;
		lost_event.header.misc = 0;
		lost_event.header.size = sizeof(lost_event);
		lost_event.id          = event->id;
		lost_event.lost        = local_xchg(&buffer->lost, 0);

		perf_output_put(handle, lost_event);
	}

	return 0;

fail:
	local_inc(&buffer->lost);
	perf_output_put_handle(handle);
out:
	rcu_read_unlock();

	return -ENOSPC;
}

void perf_output_end(struct perf_output_handle *handle)
{
	struct perf_event *event = handle->event;
	struct perf_buffer *buffer = handle->buffer;

	int wakeup_events = event->attr.wakeup_events;

	if (handle->sample && wakeup_events) {
		int events = local_inc_return(&buffer->events);
		if (events >= wakeup_events) {
			local_sub(wakeup_events, &buffer->events);
			local_inc(&buffer->wakeup);
		}
	}

	perf_output_put_handle(handle);
	rcu_read_unlock();
}

static u32 perf_event_pid(struct perf_event *event, struct task_struct *p)
{
	/*
	 * only top level events have the pid namespace they were created in
	 */
	if (event->parent)
		event = event->parent;

	return task_tgid_nr_ns(p, event->ns);
}

static u32 perf_event_tid(struct perf_event *event, struct task_struct *p)
{
	/*
	 * only top level events have the pid namespace they were created in
	 */
	if (event->parent)
		event = event->parent;

	return task_pid_nr_ns(p, event->ns);
}

static void perf_output_read_one(struct perf_output_handle *handle,
				 struct perf_event *event)
{
	u64 read_format = event->attr.read_format;
	u64 values[4];
	int n = 0;

	values[n++] = perf_event_count(event);
	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
		values[n++] = event->total_time_enabled +
			atomic64_read(&event->child_total_time_enabled);
	}
	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
		values[n++] = event->total_time_running +
			atomic64_read(&event->child_total_time_running);
	}
	if (read_format & PERF_FORMAT_ID)
		values[n++] = primary_event_id(event);

	perf_output_copy(handle, values, n * sizeof(u64));
}

/*
 * XXX PERF_FORMAT_GROUP vs inherited events seems difficult.
 */
static void perf_output_read_group(struct perf_output_handle *handle,
			    struct perf_event *event)
{
	struct perf_event *leader = event->group_leader, *sub;
	u64 read_format = event->attr.read_format;
	u64 values[5];
	int n = 0;

	values[n++] = 1 + leader->nr_siblings;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = leader->total_time_enabled;

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = leader->total_time_running;

	if (leader != event)
		leader->pmu->read(leader);

	values[n++] = perf_event_count(leader);
	if (read_format & PERF_FORMAT_ID)
		values[n++] = primary_event_id(leader);

	perf_output_copy(handle, values, n * sizeof(u64));

	list_for_each_entry(sub, &leader->sibling_list, group_entry) {
		n = 0;

		if (sub != event)
			sub->pmu->read(sub);

		values[n++] = perf_event_count(sub);
		if (read_format & PERF_FORMAT_ID)
			values[n++] = primary_event_id(sub);

		perf_output_copy(handle, values, n * sizeof(u64));
	}
}

static void perf_output_read(struct perf_output_handle *handle,
			     struct perf_event *event)
{
	if (event->attr.read_format & PERF_FORMAT_GROUP)
		perf_output_read_group(handle, event);
	else
		perf_output_read_one(handle, event);
}

void perf_output_sample(struct perf_output_handle *handle,
			struct perf_event_header *header,
			struct perf_sample_data *data,
			struct perf_event *event)
{
	u64 sample_type = data->type;

	perf_output_put(handle, *header);

	if (sample_type & PERF_SAMPLE_IP)
		perf_output_put(handle, data->ip);

	if (sample_type & PERF_SAMPLE_TID)
		perf_output_put(handle, data->tid_entry);

	if (sample_type & PERF_SAMPLE_TIME)
		perf_output_put(handle, data->time);

	if (sample_type & PERF_SAMPLE_ADDR)
		perf_output_put(handle, data->addr);

	if (sample_type & PERF_SAMPLE_ID)
		perf_output_put(handle, data->id);

	if (sample_type & PERF_SAMPLE_STREAM_ID)
		perf_output_put(handle, data->stream_id);

	if (sample_type & PERF_SAMPLE_CPU)
		perf_output_put(handle, data->cpu_entry);

	if (sample_type & PERF_SAMPLE_PERIOD)
		perf_output_put(handle, data->period);

	if (sample_type & PERF_SAMPLE_READ)
		perf_output_read(handle, event);

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
		if (data->callchain) {
			int size = 1;

			if (data->callchain)
				size += data->callchain->nr;

			size *= sizeof(u64);

			perf_output_copy(handle, data->callchain, size);
		} else {
			u64 nr = 0;
			perf_output_put(handle, nr);
		}
	}

	if (sample_type & PERF_SAMPLE_RAW) {
		if (data->raw) {
			perf_output_put(handle, data->raw->size);
			perf_output_copy(handle, data->raw->data,
					 data->raw->size);
		} else {
			struct {
				u32	size;
				u32	data;
			} raw = {
				.size = sizeof(u32),
				.data = 0,
			};
			perf_output_put(handle, raw);
		}
	}
}

void perf_prepare_sample(struct perf_event_header *header,
			 struct perf_sample_data *data,
			 struct perf_event *event,
			 struct pt_regs *regs)
{
	u64 sample_type = event->attr.sample_type;

	data->type = sample_type;

	header->type = PERF_RECORD_SAMPLE;
	header->size = sizeof(*header);

	header->misc = 0;
	header->misc |= perf_misc_flags(regs);

	if (sample_type & PERF_SAMPLE_IP) {
		data->ip = perf_instruction_pointer(regs);

		header->size += sizeof(data->ip);
	}

	if (sample_type & PERF_SAMPLE_TID) {
		/* namespace issues */
		data->tid_entry.pid = perf_event_pid(event, current);
		data->tid_entry.tid = perf_event_tid(event, current);

		header->size += sizeof(data->tid_entry);
	}

	if (sample_type & PERF_SAMPLE_TIME) {
		data->time = perf_clock();

		header->size += sizeof(data->time);
	}

	if (sample_type & PERF_SAMPLE_ADDR)
		header->size += sizeof(data->addr);

	if (sample_type & PERF_SAMPLE_ID) {
		data->id = primary_event_id(event);

		header->size += sizeof(data->id);
	}

	if (sample_type & PERF_SAMPLE_STREAM_ID) {
		data->stream_id = event->id;

		header->size += sizeof(data->stream_id);
	}

	if (sample_type & PERF_SAMPLE_CPU) {
		data->cpu_entry.cpu		= raw_smp_processor_id();
		data->cpu_entry.reserved	= 0;

		header->size += sizeof(data->cpu_entry);
	}

	if (sample_type & PERF_SAMPLE_PERIOD)
		header->size += sizeof(data->period);

	if (sample_type & PERF_SAMPLE_READ)
		header->size += perf_event_read_size(event);

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
		int size = 1;

		data->callchain = perf_callchain(regs);

		if (data->callchain)
			size += data->callchain->nr;

		header->size += size * sizeof(u64);
	}

	if (sample_type & PERF_SAMPLE_RAW) {
		int size = sizeof(u32);

		if (data->raw)
			size += data->raw->size;
		else
			size += sizeof(u32);

		WARN_ON_ONCE(size & (sizeof(u64)-1));
		header->size += size;
	}
}

static void perf_event_output(struct perf_event *event, int nmi,
				struct perf_sample_data *data,
				struct pt_regs *regs)
{
	struct perf_output_handle handle;
	struct perf_event_header header;

	perf_prepare_sample(&header, data, event, regs);

	if (perf_output_begin(&handle, event, header.size, nmi, 1))
		return;

	perf_output_sample(&handle, &header, data, event);

	perf_output_end(&handle);
}

/*
 * read event_id
 */

struct perf_read_event {
	struct perf_event_header	header;

	u32				pid;
	u32				tid;
};

static void
perf_event_read_event(struct perf_event *event,
			struct task_struct *task)
{
	struct perf_output_handle handle;
	struct perf_read_event read_event = {
		.header = {
			.type = PERF_RECORD_READ,
			.misc = 0,
			.size = sizeof(read_event) + perf_event_read_size(event),
		},
		.pid = perf_event_pid(event, task),
		.tid = perf_event_tid(event, task),
	};
	int ret;

	ret = perf_output_begin(&handle, event, read_event.header.size, 0, 0);
	if (ret)
		return;

	perf_output_put(&handle, read_event);
	perf_output_read(&handle, event);

	perf_output_end(&handle);
}

/*
 * task tracking -- fork/exit
 *
 * enabled by: attr.comm | attr.mmap | attr.mmap_data | attr.task
 */

struct perf_task_event {
	struct task_struct		*task;
	struct perf_event_context	*task_ctx;

	struct {
		struct perf_event_header	header;

		u32				pid;
		u32				ppid;
		u32				tid;
		u32				ptid;
		u64				time;
	} event_id;
};

static void perf_event_task_output(struct perf_event *event,
				     struct perf_task_event *task_event)
{
	struct perf_output_handle handle;
	struct task_struct *task = task_event->task;
	int size, ret;

	size  = task_event->event_id.header.size;
	ret = perf_output_begin(&handle, event, size, 0, 0);

	if (ret)
		return;

	task_event->event_id.pid = perf_event_pid(event, task);
	task_event->event_id.ppid = perf_event_pid(event, current);

	task_event->event_id.tid = perf_event_tid(event, task);
	task_event->event_id.ptid = perf_event_tid(event, current);

	perf_output_put(&handle, task_event->event_id);

	perf_output_end(&handle);
}

static int perf_event_task_match(struct perf_event *event)
{
	if (event->state < PERF_EVENT_STATE_INACTIVE)
		return 0;

	if (event->cpu != -1 && event->cpu != smp_processor_id())
		return 0;

	if (event->attr.comm || event->attr.mmap ||
	    event->attr.mmap_data || event->attr.task)
		return 1;

	return 0;
}

static void perf_event_task_ctx(struct perf_event_context *ctx,
				  struct perf_task_event *task_event)
{
	struct perf_event *event;

	list_for_each_entry_rcu(event, &ctx->event_list, event_entry) {
		if (perf_event_task_match(event))
			perf_event_task_output(event, task_event);
	}
}

static void perf_event_task_event(struct perf_task_event *task_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_event_context *ctx = task_event->task_ctx;

	rcu_read_lock();
	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_event_task_ctx(&cpuctx->ctx, task_event);
	if (!ctx)
		ctx = rcu_dereference(current->perf_event_ctxp);
	if (ctx)
		perf_event_task_ctx(ctx, task_event);
	put_cpu_var(perf_cpu_context);
	rcu_read_unlock();
}

static void perf_event_task(struct task_struct *task,
			      struct perf_event_context *task_ctx,
			      int new)
{
	struct perf_task_event task_event;

	if (!atomic_read(&nr_comm_events) &&
	    !atomic_read(&nr_mmap_events) &&
	    !atomic_read(&nr_task_events))
		return;

	task_event = (struct perf_task_event){
		.task	  = task,
		.task_ctx = task_ctx,
		.event_id    = {
			.header = {
				.type = new ? PERF_RECORD_FORK : PERF_RECORD_EXIT,
				.misc = 0,
				.size = sizeof(task_event.event_id),
			},
			/* .pid  */
			/* .ppid */
			/* .tid  */
			/* .ptid */
			.time = perf_clock(),
		},
	};

	perf_event_task_event(&task_event);
}

void perf_event_fork(struct task_struct *task)
{
	perf_event_task(task, NULL, 1);
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
	} event_id;
};

static void perf_event_comm_output(struct perf_event *event,
				     struct perf_comm_event *comm_event)
{
	struct perf_output_handle handle;
	int size = comm_event->event_id.header.size;
	int ret = perf_output_begin(&handle, event, size, 0, 0);

	if (ret)
		return;

	comm_event->event_id.pid = perf_event_pid(event, comm_event->task);
	comm_event->event_id.tid = perf_event_tid(event, comm_event->task);

	perf_output_put(&handle, comm_event->event_id);
	perf_output_copy(&handle, comm_event->comm,
				   comm_event->comm_size);
	perf_output_end(&handle);
}

static int perf_event_comm_match(struct perf_event *event)
{
	if (event->state < PERF_EVENT_STATE_INACTIVE)
		return 0;

	if (event->cpu != -1 && event->cpu != smp_processor_id())
		return 0;

	if (event->attr.comm)
		return 1;

	return 0;
}

static void perf_event_comm_ctx(struct perf_event_context *ctx,
				  struct perf_comm_event *comm_event)
{
	struct perf_event *event;

	list_for_each_entry_rcu(event, &ctx->event_list, event_entry) {
		if (perf_event_comm_match(event))
			perf_event_comm_output(event, comm_event);
	}
}

static void perf_event_comm_event(struct perf_comm_event *comm_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_event_context *ctx;
	unsigned int size;
	char comm[TASK_COMM_LEN];

	memset(comm, 0, sizeof(comm));
	strlcpy(comm, comm_event->task->comm, sizeof(comm));
	size = ALIGN(strlen(comm)+1, sizeof(u64));

	comm_event->comm = comm;
	comm_event->comm_size = size;

	comm_event->event_id.header.size = sizeof(comm_event->event_id) + size;

	rcu_read_lock();
	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_event_comm_ctx(&cpuctx->ctx, comm_event);
	ctx = rcu_dereference(current->perf_event_ctxp);
	if (ctx)
		perf_event_comm_ctx(ctx, comm_event);
	put_cpu_var(perf_cpu_context);
	rcu_read_unlock();
}

void perf_event_comm(struct task_struct *task)
{
	struct perf_comm_event comm_event;

	if (task->perf_event_ctxp)
		perf_event_enable_on_exec(task);

	if (!atomic_read(&nr_comm_events))
		return;

	comm_event = (struct perf_comm_event){
		.task	= task,
		/* .comm      */
		/* .comm_size */
		.event_id  = {
			.header = {
				.type = PERF_RECORD_COMM,
				.misc = 0,
				/* .size */
			},
			/* .pid */
			/* .tid */
		},
	};

	perf_event_comm_event(&comm_event);
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
	} event_id;
};

static void perf_event_mmap_output(struct perf_event *event,
				     struct perf_mmap_event *mmap_event)
{
	struct perf_output_handle handle;
	int size = mmap_event->event_id.header.size;
	int ret = perf_output_begin(&handle, event, size, 0, 0);

	if (ret)
		return;

	mmap_event->event_id.pid = perf_event_pid(event, current);
	mmap_event->event_id.tid = perf_event_tid(event, current);

	perf_output_put(&handle, mmap_event->event_id);
	perf_output_copy(&handle, mmap_event->file_name,
				   mmap_event->file_size);
	perf_output_end(&handle);
}

static int perf_event_mmap_match(struct perf_event *event,
				   struct perf_mmap_event *mmap_event,
				   int executable)
{
	if (event->state < PERF_EVENT_STATE_INACTIVE)
		return 0;

	if (event->cpu != -1 && event->cpu != smp_processor_id())
		return 0;

	if ((!executable && event->attr.mmap_data) ||
	    (executable && event->attr.mmap))
		return 1;

	return 0;
}

static void perf_event_mmap_ctx(struct perf_event_context *ctx,
				  struct perf_mmap_event *mmap_event,
				  int executable)
{
	struct perf_event *event;

	list_for_each_entry_rcu(event, &ctx->event_list, event_entry) {
		if (perf_event_mmap_match(event, mmap_event, executable))
			perf_event_mmap_output(event, mmap_event);
	}
}

static void perf_event_mmap_event(struct perf_mmap_event *mmap_event)
{
	struct perf_cpu_context *cpuctx;
	struct perf_event_context *ctx;
	struct vm_area_struct *vma = mmap_event->vma;
	struct file *file = vma->vm_file;
	unsigned int size;
	char tmp[16];
	char *buf = NULL;
	const char *name;

	memset(tmp, 0, sizeof(tmp));

	if (file) {
		/*
		 * d_path works from the end of the buffer backwards, so we
		 * need to add enough zero bytes after the string to handle
		 * the 64bit alignment we do later.
		 */
		buf = kzalloc(PATH_MAX + sizeof(u64), GFP_KERNEL);
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
		if (arch_vma_name(mmap_event->vma)) {
			name = strncpy(tmp, arch_vma_name(mmap_event->vma),
				       sizeof(tmp));
			goto got_name;
		}

		if (!vma->vm_mm) {
			name = strncpy(tmp, "[vdso]", sizeof(tmp));
			goto got_name;
		} else if (vma->vm_start <= vma->vm_mm->start_brk &&
				vma->vm_end >= vma->vm_mm->brk) {
			name = strncpy(tmp, "[heap]", sizeof(tmp));
			goto got_name;
		} else if (vma->vm_start <= vma->vm_mm->start_stack &&
				vma->vm_end >= vma->vm_mm->start_stack) {
			name = strncpy(tmp, "[stack]", sizeof(tmp));
			goto got_name;
		}

		name = strncpy(tmp, "//anon", sizeof(tmp));
		goto got_name;
	}

got_name:
	size = ALIGN(strlen(name)+1, sizeof(u64));

	mmap_event->file_name = name;
	mmap_event->file_size = size;

	mmap_event->event_id.header.size = sizeof(mmap_event->event_id) + size;

	rcu_read_lock();
	cpuctx = &get_cpu_var(perf_cpu_context);
	perf_event_mmap_ctx(&cpuctx->ctx, mmap_event, vma->vm_flags & VM_EXEC);
	ctx = rcu_dereference(current->perf_event_ctxp);
	if (ctx)
		perf_event_mmap_ctx(ctx, mmap_event, vma->vm_flags & VM_EXEC);
	put_cpu_var(perf_cpu_context);
	rcu_read_unlock();

	kfree(buf);
}

void perf_event_mmap(struct vm_area_struct *vma)
{
	struct perf_mmap_event mmap_event;

	if (!atomic_read(&nr_mmap_events))
		return;

	mmap_event = (struct perf_mmap_event){
		.vma	= vma,
		/* .file_name */
		/* .file_size */
		.event_id  = {
			.header = {
				.type = PERF_RECORD_MMAP,
				.misc = PERF_RECORD_MISC_USER,
				/* .size */
			},
			/* .pid */
			/* .tid */
			.start  = vma->vm_start,
			.len    = vma->vm_end - vma->vm_start,
			.pgoff  = (u64)vma->vm_pgoff << PAGE_SHIFT,
		},
	};

	perf_event_mmap_event(&mmap_event);
}

/*
 * IRQ throttle logging
 */

static void perf_log_throttle(struct perf_event *event, int enable)
{
	struct perf_output_handle handle;
	int ret;

	struct {
		struct perf_event_header	header;
		u64				time;
		u64				id;
		u64				stream_id;
	} throttle_event = {
		.header = {
			.type = PERF_RECORD_THROTTLE,
			.misc = 0,
			.size = sizeof(throttle_event),
		},
		.time		= perf_clock(),
		.id		= primary_event_id(event),
		.stream_id	= event->id,
	};

	if (enable)
		throttle_event.header.type = PERF_RECORD_UNTHROTTLE;

	ret = perf_output_begin(&handle, event, sizeof(throttle_event), 1, 0);
	if (ret)
		return;

	perf_output_put(&handle, throttle_event);
	perf_output_end(&handle);
}

/*
 * Generic event overflow handling, sampling.
 */

static int __perf_event_overflow(struct perf_event *event, int nmi,
				   int throttle, struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	int events = atomic_read(&event->event_limit);
	struct hw_perf_event *hwc = &event->hw;
	int ret = 0;

	throttle = (throttle && event->pmu->unthrottle != NULL);

	if (!throttle) {
		hwc->interrupts++;
	} else {
		if (hwc->interrupts != MAX_INTERRUPTS) {
			hwc->interrupts++;
			if (HZ * hwc->interrupts >
					(u64)sysctl_perf_event_sample_rate) {
				hwc->interrupts = MAX_INTERRUPTS;
				perf_log_throttle(event, 0);
				ret = 1;
			}
		} else {
			/*
			 * Keep re-disabling events even though on the previous
			 * pass we disabled it - just in case we raced with a
			 * sched-in and the event got enabled again:
			 */
			ret = 1;
		}
	}

	if (event->attr.freq) {
		u64 now = perf_clock();
		s64 delta = now - hwc->freq_time_stamp;

		hwc->freq_time_stamp = now;

		if (delta > 0 && delta < 2*TICK_NSEC)
			perf_adjust_period(event, delta, hwc->last_period);
	}

	/*
	 * XXX event_limit might not quite work as expected on inherited
	 * events
	 */

	event->pending_kill = POLL_IN;
	if (events && atomic_dec_and_test(&event->event_limit)) {
		ret = 1;
		event->pending_kill = POLL_HUP;
		if (nmi) {
			event->pending_disable = 1;
			perf_pending_queue(&event->pending,
					   perf_pending_event);
		} else
			perf_event_disable(event);
	}

	if (event->overflow_handler)
		event->overflow_handler(event, nmi, data, regs);
	else
		perf_event_output(event, nmi, data, regs);

	return ret;
}

int perf_event_overflow(struct perf_event *event, int nmi,
			  struct perf_sample_data *data,
			  struct pt_regs *regs)
{
	return __perf_event_overflow(event, nmi, 1, data, regs);
}

/*
 * Generic software event infrastructure
 */

/*
 * We directly increment event->count and keep a second value in
 * event->hw.period_left to count intervals. This period event
 * is kept in the range [-sample_period, 0] so that we can use the
 * sign as trigger.
 */

static u64 perf_swevent_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 period = hwc->last_period;
	u64 nr, offset;
	s64 old, val;

	hwc->last_period = hwc->sample_period;

again:
	old = val = local64_read(&hwc->period_left);
	if (val < 0)
		return 0;

	nr = div64_u64(period + val, period);
	offset = nr * period;
	val -= offset;
	if (local64_cmpxchg(&hwc->period_left, old, val) != old)
		goto again;

	return nr;
}

static void perf_swevent_overflow(struct perf_event *event, u64 overflow,
				    int nmi, struct perf_sample_data *data,
				    struct pt_regs *regs)
{
	struct hw_perf_event *hwc = &event->hw;
	int throttle = 0;

	data->period = event->hw.last_period;
	if (!overflow)
		overflow = perf_swevent_set_period(event);

	if (hwc->interrupts == MAX_INTERRUPTS)
		return;

	for (; overflow; overflow--) {
		if (__perf_event_overflow(event, nmi, throttle,
					    data, regs)) {
			/*
			 * We inhibit the overflow from happening when
			 * hwc->interrupts == MAX_INTERRUPTS.
			 */
			break;
		}
		throttle = 1;
	}
}

static void perf_swevent_add(struct perf_event *event, u64 nr,
			       int nmi, struct perf_sample_data *data,
			       struct pt_regs *regs)
{
	struct hw_perf_event *hwc = &event->hw;

	local64_add(nr, &event->count);

	if (!regs)
		return;

	if (!hwc->sample_period)
		return;

	if (nr == 1 && hwc->sample_period == 1 && !event->attr.freq)
		return perf_swevent_overflow(event, 1, nmi, data, regs);

	if (local64_add_negative(nr, &hwc->period_left))
		return;

	perf_swevent_overflow(event, 0, nmi, data, regs);
}

static int perf_exclude_event(struct perf_event *event,
			      struct pt_regs *regs)
{
	if (regs) {
		if (event->attr.exclude_user && user_mode(regs))
			return 1;

		if (event->attr.exclude_kernel && !user_mode(regs))
			return 1;
	}

	return 0;
}

static int perf_swevent_match(struct perf_event *event,
				enum perf_type_id type,
				u32 event_id,
				struct perf_sample_data *data,
				struct pt_regs *regs)
{
	if (event->attr.type != type)
		return 0;

	if (event->attr.config != event_id)
		return 0;

	if (perf_exclude_event(event, regs))
		return 0;

	return 1;
}

static inline u64 swevent_hash(u64 type, u32 event_id)
{
	u64 val = event_id | (type << 32);

	return hash_64(val, SWEVENT_HLIST_BITS);
}

static inline struct hlist_head *
__find_swevent_head(struct swevent_hlist *hlist, u64 type, u32 event_id)
{
	u64 hash = swevent_hash(type, event_id);

	return &hlist->heads[hash];
}

/* For the read side: events when they trigger */
static inline struct hlist_head *
find_swevent_head_rcu(struct perf_cpu_context *ctx, u64 type, u32 event_id)
{
	struct swevent_hlist *hlist;

	hlist = rcu_dereference(ctx->swevent_hlist);
	if (!hlist)
		return NULL;

	return __find_swevent_head(hlist, type, event_id);
}

/* For the event head insertion and removal in the hlist */
static inline struct hlist_head *
find_swevent_head(struct perf_cpu_context *ctx, struct perf_event *event)
{
	struct swevent_hlist *hlist;
	u32 event_id = event->attr.config;
	u64 type = event->attr.type;

	/*
	 * Event scheduling is always serialized against hlist allocation
	 * and release. Which makes the protected version suitable here.
	 * The context lock guarantees that.
	 */
	hlist = rcu_dereference_protected(ctx->swevent_hlist,
					  lockdep_is_held(&event->ctx->lock));
	if (!hlist)
		return NULL;

	return __find_swevent_head(hlist, type, event_id);
}

static void do_perf_sw_event(enum perf_type_id type, u32 event_id,
				    u64 nr, int nmi,
				    struct perf_sample_data *data,
				    struct pt_regs *regs)
{
	struct perf_cpu_context *cpuctx;
	struct perf_event *event;
	struct hlist_node *node;
	struct hlist_head *head;

	cpuctx = &__get_cpu_var(perf_cpu_context);

	rcu_read_lock();

	head = find_swevent_head_rcu(cpuctx, type, event_id);

	if (!head)
		goto end;

	hlist_for_each_entry_rcu(event, node, head, hlist_entry) {
		if (perf_swevent_match(event, type, event_id, data, regs))
			perf_swevent_add(event, nr, nmi, data, regs);
	}
end:
	rcu_read_unlock();
}

int perf_swevent_get_recursion_context(void)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	int rctx;

	if (in_nmi())
		rctx = 3;
	else if (in_irq())
		rctx = 2;
	else if (in_softirq())
		rctx = 1;
	else
		rctx = 0;

	if (cpuctx->recursion[rctx])
		return -1;

	cpuctx->recursion[rctx]++;
	barrier();

	return rctx;
}
EXPORT_SYMBOL_GPL(perf_swevent_get_recursion_context);

void inline perf_swevent_put_recursion_context(int rctx)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	barrier();
	cpuctx->recursion[rctx]--;
}

void __perf_sw_event(u32 event_id, u64 nr, int nmi,
			    struct pt_regs *regs, u64 addr)
{
	struct perf_sample_data data;
	int rctx;

	preempt_disable_notrace();
	rctx = perf_swevent_get_recursion_context();
	if (rctx < 0)
		return;

	perf_sample_data_init(&data, addr);

	do_perf_sw_event(PERF_TYPE_SOFTWARE, event_id, nr, nmi, &data, regs);

	perf_swevent_put_recursion_context(rctx);
	preempt_enable_notrace();
}

static void perf_swevent_read(struct perf_event *event)
{
}

static int perf_swevent_enable(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_cpu_context *cpuctx;
	struct hlist_head *head;

	cpuctx = &__get_cpu_var(perf_cpu_context);

	if (hwc->sample_period) {
		hwc->last_period = hwc->sample_period;
		perf_swevent_set_period(event);
	}

	head = find_swevent_head(cpuctx, event);
	if (WARN_ON_ONCE(!head))
		return -EINVAL;

	hlist_add_head_rcu(&event->hlist_entry, head);

	return 0;
}

static void perf_swevent_disable(struct perf_event *event)
{
	hlist_del_rcu(&event->hlist_entry);
}

static void perf_swevent_void(struct perf_event *event)
{
}

static int perf_swevent_int(struct perf_event *event)
{
	return 0;
}

static const struct pmu perf_ops_generic = {
	.enable		= perf_swevent_enable,
	.disable	= perf_swevent_disable,
	.start		= perf_swevent_int,
	.stop		= perf_swevent_void,
	.read		= perf_swevent_read,
	.unthrottle	= perf_swevent_void, /* hwc->interrupts already reset */
};

/*
 * hrtimer based swevent callback
 */

static enum hrtimer_restart perf_swevent_hrtimer(struct hrtimer *hrtimer)
{
	enum hrtimer_restart ret = HRTIMER_RESTART;
	struct perf_sample_data data;
	struct pt_regs *regs;
	struct perf_event *event;
	u64 period;

	event = container_of(hrtimer, struct perf_event, hw.hrtimer);
	event->pmu->read(event);

	perf_sample_data_init(&data, 0);
	data.period = event->hw.last_period;
	regs = get_irq_regs();

	if (regs && !perf_exclude_event(event, regs)) {
		if (!(event->attr.exclude_idle && current->pid == 0))
			if (perf_event_overflow(event, 0, &data, regs))
				ret = HRTIMER_NORESTART;
	}

	period = max_t(u64, 10000, event->hw.sample_period);
	hrtimer_forward_now(hrtimer, ns_to_ktime(period));

	return ret;
}

static void perf_swevent_start_hrtimer(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	hrtimer_init(&hwc->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hwc->hrtimer.function = perf_swevent_hrtimer;
	if (hwc->sample_period) {
		u64 period;

		if (hwc->remaining) {
			if (hwc->remaining < 0)
				period = 10000;
			else
				period = hwc->remaining;
			hwc->remaining = 0;
		} else {
			period = max_t(u64, 10000, hwc->sample_period);
		}
		__hrtimer_start_range_ns(&hwc->hrtimer,
				ns_to_ktime(period), 0,
				HRTIMER_MODE_REL, 0);
	}
}

static void perf_swevent_cancel_hrtimer(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->sample_period) {
		ktime_t remaining = hrtimer_get_remaining(&hwc->hrtimer);
		hwc->remaining = ktime_to_ns(remaining);

		hrtimer_cancel(&hwc->hrtimer);
	}
}

/*
 * Software event: cpu wall time clock
 */

static void cpu_clock_perf_event_update(struct perf_event *event)
{
	int cpu = raw_smp_processor_id();
	s64 prev;
	u64 now;

	now = cpu_clock(cpu);
	prev = local64_xchg(&event->hw.prev_count, now);
	local64_add(now - prev, &event->count);
}

static int cpu_clock_perf_event_enable(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int cpu = raw_smp_processor_id();

	local64_set(&hwc->prev_count, cpu_clock(cpu));
	perf_swevent_start_hrtimer(event);

	return 0;
}

static void cpu_clock_perf_event_disable(struct perf_event *event)
{
	perf_swevent_cancel_hrtimer(event);
	cpu_clock_perf_event_update(event);
}

static void cpu_clock_perf_event_read(struct perf_event *event)
{
	cpu_clock_perf_event_update(event);
}

static const struct pmu perf_ops_cpu_clock = {
	.enable		= cpu_clock_perf_event_enable,
	.disable	= cpu_clock_perf_event_disable,
	.read		= cpu_clock_perf_event_read,
};

/*
 * Software event: task time clock
 */

static void task_clock_perf_event_update(struct perf_event *event, u64 now)
{
	u64 prev;
	s64 delta;

	prev = local64_xchg(&event->hw.prev_count, now);
	delta = now - prev;
	local64_add(delta, &event->count);
}

static int task_clock_perf_event_enable(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 now;

	now = event->ctx->time;

	local64_set(&hwc->prev_count, now);

	perf_swevent_start_hrtimer(event);

	return 0;
}

static void task_clock_perf_event_disable(struct perf_event *event)
{
	perf_swevent_cancel_hrtimer(event);
	task_clock_perf_event_update(event, event->ctx->time);

}

static void task_clock_perf_event_read(struct perf_event *event)
{
	u64 time;

	if (!in_nmi()) {
		update_context_time(event->ctx);
		time = event->ctx->time;
	} else {
		u64 now = perf_clock();
		u64 delta = now - event->ctx->timestamp;
		time = event->ctx->time + delta;
	}

	task_clock_perf_event_update(event, time);
}

static const struct pmu perf_ops_task_clock = {
	.enable		= task_clock_perf_event_enable,
	.disable	= task_clock_perf_event_disable,
	.read		= task_clock_perf_event_read,
};

/* Deref the hlist from the update side */
static inline struct swevent_hlist *
swevent_hlist_deref(struct perf_cpu_context *cpuctx)
{
	return rcu_dereference_protected(cpuctx->swevent_hlist,
					 lockdep_is_held(&cpuctx->hlist_mutex));
}

static void swevent_hlist_release_rcu(struct rcu_head *rcu_head)
{
	struct swevent_hlist *hlist;

	hlist = container_of(rcu_head, struct swevent_hlist, rcu_head);
	kfree(hlist);
}

static void swevent_hlist_release(struct perf_cpu_context *cpuctx)
{
	struct swevent_hlist *hlist = swevent_hlist_deref(cpuctx);

	if (!hlist)
		return;

	rcu_assign_pointer(cpuctx->swevent_hlist, NULL);
	call_rcu(&hlist->rcu_head, swevent_hlist_release_rcu);
}

static void swevent_hlist_put_cpu(struct perf_event *event, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);

	mutex_lock(&cpuctx->hlist_mutex);

	if (!--cpuctx->hlist_refcount)
		swevent_hlist_release(cpuctx);

	mutex_unlock(&cpuctx->hlist_mutex);
}

static void swevent_hlist_put(struct perf_event *event)
{
	int cpu;

	if (event->cpu != -1) {
		swevent_hlist_put_cpu(event, event->cpu);
		return;
	}

	for_each_possible_cpu(cpu)
		swevent_hlist_put_cpu(event, cpu);
}

static int swevent_hlist_get_cpu(struct perf_event *event, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	int err = 0;

	mutex_lock(&cpuctx->hlist_mutex);

	if (!swevent_hlist_deref(cpuctx) && cpu_online(cpu)) {
		struct swevent_hlist *hlist;

		hlist = kzalloc(sizeof(*hlist), GFP_KERNEL);
		if (!hlist) {
			err = -ENOMEM;
			goto exit;
		}
		rcu_assign_pointer(cpuctx->swevent_hlist, hlist);
	}
	cpuctx->hlist_refcount++;
 exit:
	mutex_unlock(&cpuctx->hlist_mutex);

	return err;
}

static int swevent_hlist_get(struct perf_event *event)
{
	int err;
	int cpu, failed_cpu;

	if (event->cpu != -1)
		return swevent_hlist_get_cpu(event, event->cpu);

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		err = swevent_hlist_get_cpu(event, cpu);
		if (err) {
			failed_cpu = cpu;
			goto fail;
		}
	}
	put_online_cpus();

	return 0;
 fail:
	for_each_possible_cpu(cpu) {
		if (cpu == failed_cpu)
			break;
		swevent_hlist_put_cpu(event, cpu);
	}

	put_online_cpus();
	return err;
}

#ifdef CONFIG_EVENT_TRACING

static const struct pmu perf_ops_tracepoint = {
	.enable		= perf_trace_enable,
	.disable	= perf_trace_disable,
	.start		= perf_swevent_int,
	.stop		= perf_swevent_void,
	.read		= perf_swevent_read,
	.unthrottle	= perf_swevent_void,
};

static int perf_tp_filter_match(struct perf_event *event,
				struct perf_sample_data *data)
{
	void *record = data->raw->data;

	if (likely(!event->filter) || filter_match_preds(event->filter, record))
		return 1;
	return 0;
}

static int perf_tp_event_match(struct perf_event *event,
				struct perf_sample_data *data,
				struct pt_regs *regs)
{
	/*
	 * All tracepoints are from kernel-space.
	 */
	if (event->attr.exclude_kernel)
		return 0;

	if (!perf_tp_filter_match(event, data))
		return 0;

	return 1;
}

void perf_tp_event(u64 addr, u64 count, void *record, int entry_size,
		   struct pt_regs *regs, struct hlist_head *head, int rctx)
{
	struct perf_sample_data data;
	struct perf_event *event;
	struct hlist_node *node;

	struct perf_raw_record raw = {
		.size = entry_size,
		.data = record,
	};

	perf_sample_data_init(&data, addr);
	data.raw = &raw;

	hlist_for_each_entry_rcu(event, node, head, hlist_entry) {
		if (perf_tp_event_match(event, &data, regs))
			perf_swevent_add(event, count, 1, &data, regs);
	}

	perf_swevent_put_recursion_context(rctx);
}
EXPORT_SYMBOL_GPL(perf_tp_event);

static void tp_perf_event_destroy(struct perf_event *event)
{
	perf_trace_destroy(event);
}

static const struct pmu *tp_perf_event_init(struct perf_event *event)
{
	int err;

	/*
	 * Raw tracepoint data is a severe data leak, only allow root to
	 * have these.
	 */
	if ((event->attr.sample_type & PERF_SAMPLE_RAW) &&
			perf_paranoid_tracepoint_raw() &&
			!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	err = perf_trace_init(event);
	if (err)
		return NULL;

	event->destroy = tp_perf_event_destroy;

	return &perf_ops_tracepoint;
}

static int perf_event_set_filter(struct perf_event *event, void __user *arg)
{
	char *filter_str;
	int ret;

	if (event->attr.type != PERF_TYPE_TRACEPOINT)
		return -EINVAL;

	filter_str = strndup_user(arg, PAGE_SIZE);
	if (IS_ERR(filter_str))
		return PTR_ERR(filter_str);

	ret = ftrace_profile_set_filter(event, event->attr.config, filter_str);

	kfree(filter_str);
	return ret;
}

static void perf_event_free_filter(struct perf_event *event)
{
	ftrace_profile_free_filter(event);
}

#else

static const struct pmu *tp_perf_event_init(struct perf_event *event)
{
	return NULL;
}

static int perf_event_set_filter(struct perf_event *event, void __user *arg)
{
	return -ENOENT;
}

static void perf_event_free_filter(struct perf_event *event)
{
}

#endif /* CONFIG_EVENT_TRACING */

#ifdef CONFIG_HAVE_HW_BREAKPOINT
static void bp_perf_event_destroy(struct perf_event *event)
{
	release_bp_slot(event);
}

static const struct pmu *bp_perf_event_init(struct perf_event *bp)
{
	int err;

	err = register_perf_hw_breakpoint(bp);
	if (err)
		return ERR_PTR(err);

	bp->destroy = bp_perf_event_destroy;

	return &perf_ops_bp;
}

void perf_bp_event(struct perf_event *bp, void *data)
{
	struct perf_sample_data sample;
	struct pt_regs *regs = data;

	perf_sample_data_init(&sample, bp->attr.bp_addr);

	if (!perf_exclude_event(bp, regs))
		perf_swevent_add(bp, 1, 1, &sample, regs);
}
#else
static const struct pmu *bp_perf_event_init(struct perf_event *bp)
{
	return NULL;
}

void perf_bp_event(struct perf_event *bp, void *regs)
{
}
#endif

atomic_t perf_swevent_enabled[PERF_COUNT_SW_MAX];

static void sw_perf_event_destroy(struct perf_event *event)
{
	u64 event_id = event->attr.config;

	WARN_ON(event->parent);

	atomic_dec(&perf_swevent_enabled[event_id]);
	swevent_hlist_put(event);
}

static const struct pmu *sw_perf_event_init(struct perf_event *event)
{
	const struct pmu *pmu = NULL;
	u64 event_id = event->attr.config;

	/*
	 * Software events (currently) can't in general distinguish
	 * between user, kernel and hypervisor events.
	 * However, context switches and cpu migrations are considered
	 * to be kernel events, and page faults are never hypervisor
	 * events.
	 */
	switch (event_id) {
	case PERF_COUNT_SW_CPU_CLOCK:
		pmu = &perf_ops_cpu_clock;

		break;
	case PERF_COUNT_SW_TASK_CLOCK:
		/*
		 * If the user instantiates this as a per-cpu event,
		 * use the cpu_clock event instead.
		 */
		if (event->ctx->task)
			pmu = &perf_ops_task_clock;
		else
			pmu = &perf_ops_cpu_clock;

		break;
	case PERF_COUNT_SW_PAGE_FAULTS:
	case PERF_COUNT_SW_PAGE_FAULTS_MIN:
	case PERF_COUNT_SW_PAGE_FAULTS_MAJ:
	case PERF_COUNT_SW_CONTEXT_SWITCHES:
	case PERF_COUNT_SW_CPU_MIGRATIONS:
	case PERF_COUNT_SW_ALIGNMENT_FAULTS:
	case PERF_COUNT_SW_EMULATION_FAULTS:
		if (!event->parent) {
			int err;

			err = swevent_hlist_get(event);
			if (err)
				return ERR_PTR(err);

			atomic_inc(&perf_swevent_enabled[event_id]);
			event->destroy = sw_perf_event_destroy;
		}
		pmu = &perf_ops_generic;
		break;
	}

	return pmu;
}

/*
 * Allocate and initialize a event structure
 */
static struct perf_event *
perf_event_alloc(struct perf_event_attr *attr,
		   int cpu,
		   struct perf_event_context *ctx,
		   struct perf_event *group_leader,
		   struct perf_event *parent_event,
		   perf_overflow_handler_t overflow_handler,
		   gfp_t gfpflags)
{
	const struct pmu *pmu;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	long err;

	event = kzalloc(sizeof(*event), gfpflags);
	if (!event)
		return ERR_PTR(-ENOMEM);

	/*
	 * Single events are their own group leaders, with an
	 * empty sibling list:
	 */
	if (!group_leader)
		group_leader = event;

	mutex_init(&event->child_mutex);
	INIT_LIST_HEAD(&event->child_list);

	INIT_LIST_HEAD(&event->group_entry);
	INIT_LIST_HEAD(&event->event_entry);
	INIT_LIST_HEAD(&event->sibling_list);
	init_waitqueue_head(&event->waitq);

	mutex_init(&event->mmap_mutex);

	event->cpu		= cpu;
	event->attr		= *attr;
	event->group_leader	= group_leader;
	event->pmu		= NULL;
	event->ctx		= ctx;
	event->oncpu		= -1;

	event->parent		= parent_event;

	event->ns		= get_pid_ns(current->nsproxy->pid_ns);
	event->id		= atomic64_inc_return(&perf_event_id);

	event->state		= PERF_EVENT_STATE_INACTIVE;

	if (!overflow_handler && parent_event)
		overflow_handler = parent_event->overflow_handler;
	
	event->overflow_handler	= overflow_handler;

	if (attr->disabled)
		event->state = PERF_EVENT_STATE_OFF;

	pmu = NULL;

	hwc = &event->hw;
	hwc->sample_period = attr->sample_period;
	if (attr->freq && attr->sample_freq)
		hwc->sample_period = 1;
	hwc->last_period = hwc->sample_period;

	local64_set(&hwc->period_left, hwc->sample_period);

	/*
	 * we currently do not support PERF_FORMAT_GROUP on inherited events
	 */
	if (attr->inherit && (attr->read_format & PERF_FORMAT_GROUP))
		goto done;

	switch (attr->type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		pmu = hw_perf_event_init(event);
		break;

	case PERF_TYPE_SOFTWARE:
		pmu = sw_perf_event_init(event);
		break;

	case PERF_TYPE_TRACEPOINT:
		pmu = tp_perf_event_init(event);
		break;

	case PERF_TYPE_BREAKPOINT:
		pmu = bp_perf_event_init(event);
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
		if (event->ns)
			put_pid_ns(event->ns);
		kfree(event);
		return ERR_PTR(err);
	}

	event->pmu = pmu;

	if (!event->parent) {
		atomic_inc(&nr_events);
		if (event->attr.mmap || event->attr.mmap_data)
			atomic_inc(&nr_mmap_events);
		if (event->attr.comm)
			atomic_inc(&nr_comm_events);
		if (event->attr.task)
			atomic_inc(&nr_task_events);
	}

	return event;
}

static int perf_copy_attr(struct perf_event_attr __user *uattr,
			  struct perf_event_attr *attr)
{
	u32 size;
	int ret;

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
	 * ensure all the unknown bits are 0 - i.e. new
	 * user-space does not rely on any kernel feature
	 * extensions we dont know about yet.
	 */
	if (size > sizeof(*attr)) {
		unsigned char __user *addr;
		unsigned char __user *end;
		unsigned char val;

		addr = (void __user *)uattr + sizeof(*attr);
		end  = (void __user *)uattr + size;

		for (; addr < end; addr++) {
			ret = get_user(val, addr);
			if (ret)
				return ret;
			if (val)
				goto err_size;
		}
		size = sizeof(*attr);
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

	if (attr->__reserved_1)
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

static int
perf_event_set_output(struct perf_event *event, struct perf_event *output_event)
{
	struct perf_buffer *buffer = NULL, *old_buffer = NULL;
	int ret = -EINVAL;

	if (!output_event)
		goto set;

	/* don't allow circular references */
	if (event == output_event)
		goto out;

	/*
	 * Don't allow cross-cpu buffers
	 */
	if (output_event->cpu != event->cpu)
		goto out;

	/*
	 * If its not a per-cpu buffer, it must be the same task.
	 */
	if (output_event->cpu == -1 && output_event->ctx != event->ctx)
		goto out;

set:
	mutex_lock(&event->mmap_mutex);
	/* Can't redirect output if we've got an active mmap() */
	if (atomic_read(&event->mmap_count))
		goto unlock;

	if (output_event) {
		/* get the buffer we want to redirect to */
		buffer = perf_buffer_get(output_event);
		if (!buffer)
			goto unlock;
	}

	old_buffer = event->buffer;
	rcu_assign_pointer(event->buffer, buffer);
	ret = 0;
unlock:
	mutex_unlock(&event->mmap_mutex);

	if (old_buffer)
		perf_buffer_put(old_buffer);
out:
	return ret;
}

/**
 * sys_perf_event_open - open a performance event, associate it to a task/cpu
 *
 * @attr_uptr:	event_id type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader event fd
 */
SYSCALL_DEFINE5(perf_event_open,
		struct perf_event_attr __user *, attr_uptr,
		pid_t, pid, int, cpu, int, group_fd, unsigned long, flags)
{
	struct perf_event *event, *group_leader = NULL, *output_event = NULL;
	struct perf_event_attr attr;
	struct perf_event_context *ctx;
	struct file *event_file = NULL;
	struct file *group_file = NULL;
	int event_fd;
	int fput_needed = 0;
	int err;

	/* for future expandability... */
	if (flags & ~(PERF_FLAG_FD_NO_GROUP | PERF_FLAG_FD_OUTPUT))
		return -EINVAL;

	err = perf_copy_attr(attr_uptr, &attr);
	if (err)
		return err;

	if (!attr.exclude_kernel) {
		if (perf_paranoid_kernel() && !capable(CAP_SYS_ADMIN))
			return -EACCES;
	}

	if (attr.freq) {
		if (attr.sample_freq > sysctl_perf_event_sample_rate)
			return -EINVAL;
	}

	event_fd = get_unused_fd_flags(O_RDWR);
	if (event_fd < 0)
		return event_fd;

	/*
	 * Get the target context (task or percpu):
	 */
	ctx = find_get_context(pid, cpu);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err_fd;
	}

	if (group_fd != -1) {
		group_leader = perf_fget_light(group_fd, &fput_needed);
		if (IS_ERR(group_leader)) {
			err = PTR_ERR(group_leader);
			goto err_put_context;
		}
		group_file = group_leader->filp;
		if (flags & PERF_FLAG_FD_OUTPUT)
			output_event = group_leader;
		if (flags & PERF_FLAG_FD_NO_GROUP)
			group_leader = NULL;
	}

	/*
	 * Look up the group leader (we will attach this event to it):
	 */
	if (group_leader) {
		err = -EINVAL;

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

	event = perf_event_alloc(&attr, cpu, ctx, group_leader,
				     NULL, NULL, GFP_KERNEL);
	if (IS_ERR(event)) {
		err = PTR_ERR(event);
		goto err_put_context;
	}

	if (output_event) {
		err = perf_event_set_output(event, output_event);
		if (err)
			goto err_free_put_context;
	}

	event_file = anon_inode_getfile("[perf_event]", &perf_fops, event, O_RDWR);
	if (IS_ERR(event_file)) {
		err = PTR_ERR(event_file);
		goto err_free_put_context;
	}

	event->filp = event_file;
	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	perf_install_in_context(ctx, event, cpu);
	++ctx->generation;
	mutex_unlock(&ctx->mutex);

	event->owner = current;
	get_task_struct(current);
	mutex_lock(&current->perf_event_mutex);
	list_add_tail(&event->owner_entry, &current->perf_event_list);
	mutex_unlock(&current->perf_event_mutex);

	/*
	 * Drop the reference on the group_event after placing the
	 * new event on the sibling_list. This ensures destruction
	 * of the group leader will find the pointer to itself in
	 * perf_group_detach().
	 */
	fput_light(group_file, fput_needed);
	fd_install(event_fd, event_file);
	return event_fd;

err_free_put_context:
	free_event(event);
err_put_context:
	fput_light(group_file, fput_needed);
	put_ctx(ctx);
err_fd:
	put_unused_fd(event_fd);
	return err;
}

/**
 * perf_event_create_kernel_counter
 *
 * @attr: attributes of the counter to create
 * @cpu: cpu in which the counter is bound
 * @pid: task to profile
 */
struct perf_event *
perf_event_create_kernel_counter(struct perf_event_attr *attr, int cpu,
				 pid_t pid,
				 perf_overflow_handler_t overflow_handler)
{
	struct perf_event *event;
	struct perf_event_context *ctx;
	int err;

	/*
	 * Get the target context (task or percpu):
	 */

	ctx = find_get_context(pid, cpu);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err_exit;
	}

	event = perf_event_alloc(attr, cpu, ctx, NULL,
				 NULL, overflow_handler, GFP_KERNEL);
	if (IS_ERR(event)) {
		err = PTR_ERR(event);
		goto err_put_context;
	}

	event->filp = NULL;
	WARN_ON_ONCE(ctx->parent_ctx);
	mutex_lock(&ctx->mutex);
	perf_install_in_context(ctx, event, cpu);
	++ctx->generation;
	mutex_unlock(&ctx->mutex);

	event->owner = current;
	get_task_struct(current);
	mutex_lock(&current->perf_event_mutex);
	list_add_tail(&event->owner_entry, &current->perf_event_list);
	mutex_unlock(&current->perf_event_mutex);

	return event;

 err_put_context:
	put_ctx(ctx);
 err_exit:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(perf_event_create_kernel_counter);

/*
 * inherit a event from parent task to child task:
 */
static struct perf_event *
inherit_event(struct perf_event *parent_event,
	      struct task_struct *parent,
	      struct perf_event_context *parent_ctx,
	      struct task_struct *child,
	      struct perf_event *group_leader,
	      struct perf_event_context *child_ctx)
{
	struct perf_event *child_event;

	/*
	 * Instead of creating recursive hierarchies of events,
	 * we link inherited events back to the original parent,
	 * which has a filp for sure, which we use as the reference
	 * count:
	 */
	if (parent_event->parent)
		parent_event = parent_event->parent;

	child_event = perf_event_alloc(&parent_event->attr,
					   parent_event->cpu, child_ctx,
					   group_leader, parent_event,
					   NULL, GFP_KERNEL);
	if (IS_ERR(child_event))
		return child_event;
	get_ctx(child_ctx);

	/*
	 * Make the child state follow the state of the parent event,
	 * not its attr.disabled bit.  We hold the parent's mutex,
	 * so we won't race with perf_event_{en, dis}able_family.
	 */
	if (parent_event->state >= PERF_EVENT_STATE_INACTIVE)
		child_event->state = PERF_EVENT_STATE_INACTIVE;
	else
		child_event->state = PERF_EVENT_STATE_OFF;

	if (parent_event->attr.freq) {
		u64 sample_period = parent_event->hw.sample_period;
		struct hw_perf_event *hwc = &child_event->hw;

		hwc->sample_period = sample_period;
		hwc->last_period   = sample_period;

		local64_set(&hwc->period_left, sample_period);
	}

	child_event->overflow_handler = parent_event->overflow_handler;

	/*
	 * Link it up in the child's context:
	 */
	add_event_to_ctx(child_event, child_ctx);

	/*
	 * Get a reference to the parent filp - we will fput it
	 * when the child event exits. This is safe to do because
	 * we are in the parent and we know that the filp still
	 * exists and has a nonzero count:
	 */
	atomic_long_inc(&parent_event->filp->f_count);

	/*
	 * Link this into the parent event's child list
	 */
	WARN_ON_ONCE(parent_event->ctx->parent_ctx);
	mutex_lock(&parent_event->child_mutex);
	list_add_tail(&child_event->child_list, &parent_event->child_list);
	mutex_unlock(&parent_event->child_mutex);

	return child_event;
}

static int inherit_group(struct perf_event *parent_event,
	      struct task_struct *parent,
	      struct perf_event_context *parent_ctx,
	      struct task_struct *child,
	      struct perf_event_context *child_ctx)
{
	struct perf_event *leader;
	struct perf_event *sub;
	struct perf_event *child_ctr;

	leader = inherit_event(parent_event, parent, parent_ctx,
				 child, NULL, child_ctx);
	if (IS_ERR(leader))
		return PTR_ERR(leader);
	list_for_each_entry(sub, &parent_event->sibling_list, group_entry) {
		child_ctr = inherit_event(sub, parent, parent_ctx,
					    child, leader, child_ctx);
		if (IS_ERR(child_ctr))
			return PTR_ERR(child_ctr);
	}
	return 0;
}

static void sync_child_event(struct perf_event *child_event,
			       struct task_struct *child)
{
	struct perf_event *parent_event = child_event->parent;
	u64 child_val;

	if (child_event->attr.inherit_stat)
		perf_event_read_event(child_event, child);

	child_val = perf_event_count(child_event);

	/*
	 * Add back the child's count to the parent's count:
	 */
	atomic64_add(child_val, &parent_event->child_count);
	atomic64_add(child_event->total_time_enabled,
		     &parent_event->child_total_time_enabled);
	atomic64_add(child_event->total_time_running,
		     &parent_event->child_total_time_running);

	/*
	 * Remove this event from the parent's list
	 */
	WARN_ON_ONCE(parent_event->ctx->parent_ctx);
	mutex_lock(&parent_event->child_mutex);
	list_del_init(&child_event->child_list);
	mutex_unlock(&parent_event->child_mutex);

	/*
	 * Release the parent event, if this was the last
	 * reference to it.
	 */
	fput(parent_event->filp);
}

static void
__perf_event_exit_task(struct perf_event *child_event,
			 struct perf_event_context *child_ctx,
			 struct task_struct *child)
{
	struct perf_event *parent_event;

	perf_event_remove_from_context(child_event);

	parent_event = child_event->parent;
	/*
	 * It can happen that parent exits first, and has events
	 * that are still around due to the child reference. These
	 * events need to be zapped - but otherwise linger.
	 */
	if (parent_event) {
		sync_child_event(child_event, child);
		free_event(child_event);
	}
}

/*
 * When a child task exits, feed back event values to parent events.
 */
void perf_event_exit_task(struct task_struct *child)
{
	struct perf_event *child_event, *tmp;
	struct perf_event_context *child_ctx;
	unsigned long flags;

	if (likely(!child->perf_event_ctxp)) {
		perf_event_task(child, NULL, 0);
		return;
	}

	local_irq_save(flags);
	/*
	 * We can't reschedule here because interrupts are disabled,
	 * and either child is current or it is a task that can't be
	 * scheduled, so we are now safe from rescheduling changing
	 * our context.
	 */
	child_ctx = child->perf_event_ctxp;
	__perf_event_task_sched_out(child_ctx);

	/*
	 * Take the context lock here so that if find_get_context is
	 * reading child->perf_event_ctxp, we wait until it has
	 * incremented the context's refcount before we do put_ctx below.
	 */
	raw_spin_lock(&child_ctx->lock);
	child->perf_event_ctxp = NULL;
	/*
	 * If this context is a clone; unclone it so it can't get
	 * swapped to another process while we're removing all
	 * the events from it.
	 */
	unclone_ctx(child_ctx);
	update_context_time(child_ctx);
	raw_spin_unlock_irqrestore(&child_ctx->lock, flags);

	/*
	 * Report the task dead after unscheduling the events so that we
	 * won't get any samples after PERF_RECORD_EXIT. We can however still
	 * get a few PERF_RECORD_READ events.
	 */
	perf_event_task(child, child_ctx, 0);

	/*
	 * We can recurse on the same lock type through:
	 *
	 *   __perf_event_exit_task()
	 *     sync_child_event()
	 *       fput(parent_event->filp)
	 *         perf_release()
	 *           mutex_lock(&ctx->mutex)
	 *
	 * But since its the parent context it won't be the same instance.
	 */
	mutex_lock(&child_ctx->mutex);

again:
	list_for_each_entry_safe(child_event, tmp, &child_ctx->pinned_groups,
				 group_entry)
		__perf_event_exit_task(child_event, child_ctx, child);

	list_for_each_entry_safe(child_event, tmp, &child_ctx->flexible_groups,
				 group_entry)
		__perf_event_exit_task(child_event, child_ctx, child);

	/*
	 * If the last event was a group event, it will have appended all
	 * its siblings to the list, but we obtained 'tmp' before that which
	 * will still point to the list head terminating the iteration.
	 */
	if (!list_empty(&child_ctx->pinned_groups) ||
	    !list_empty(&child_ctx->flexible_groups))
		goto again;

	mutex_unlock(&child_ctx->mutex);

	put_ctx(child_ctx);
}

static void perf_free_event(struct perf_event *event,
			    struct perf_event_context *ctx)
{
	struct perf_event *parent = event->parent;

	if (WARN_ON_ONCE(!parent))
		return;

	mutex_lock(&parent->child_mutex);
	list_del_init(&event->child_list);
	mutex_unlock(&parent->child_mutex);

	fput(parent->filp);

	perf_group_detach(event);
	list_del_event(event, ctx);
	free_event(event);
}

/*
 * free an unexposed, unused context as created by inheritance by
 * init_task below, used by fork() in case of fail.
 */
void perf_event_free_task(struct task_struct *task)
{
	struct perf_event_context *ctx = task->perf_event_ctxp;
	struct perf_event *event, *tmp;

	if (!ctx)
		return;

	mutex_lock(&ctx->mutex);
again:
	list_for_each_entry_safe(event, tmp, &ctx->pinned_groups, group_entry)
		perf_free_event(event, ctx);

	list_for_each_entry_safe(event, tmp, &ctx->flexible_groups,
				 group_entry)
		perf_free_event(event, ctx);

	if (!list_empty(&ctx->pinned_groups) ||
	    !list_empty(&ctx->flexible_groups))
		goto again;

	mutex_unlock(&ctx->mutex);

	put_ctx(ctx);
}

static int
inherit_task_group(struct perf_event *event, struct task_struct *parent,
		   struct perf_event_context *parent_ctx,
		   struct task_struct *child,
		   int *inherited_all)
{
	int ret;
	struct perf_event_context *child_ctx = child->perf_event_ctxp;

	if (!event->attr.inherit) {
		*inherited_all = 0;
		return 0;
	}

	if (!child_ctx) {
		/*
		 * This is executed from the parent task context, so
		 * inherit events that have been marked for cloning.
		 * First allocate and initialize a context for the
		 * child.
		 */

		child_ctx = kzalloc(sizeof(struct perf_event_context),
				    GFP_KERNEL);
		if (!child_ctx)
			return -ENOMEM;

		__perf_event_init_context(child_ctx, child);
		child->perf_event_ctxp = child_ctx;
		get_task_struct(child);
	}

	ret = inherit_group(event, parent, parent_ctx,
			    child, child_ctx);

	if (ret)
		*inherited_all = 0;

	return ret;
}


/*
 * Initialize the perf_event context in task_struct
 */
int perf_event_init_task(struct task_struct *child)
{
	struct perf_event_context *child_ctx, *parent_ctx;
	struct perf_event_context *cloned_ctx;
	struct perf_event *event;
	struct task_struct *parent = current;
	int inherited_all = 1;
	int ret = 0;

	child->perf_event_ctxp = NULL;

	mutex_init(&child->perf_event_mutex);
	INIT_LIST_HEAD(&child->perf_event_list);

	if (likely(!parent->perf_event_ctxp))
		return 0;

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
	list_for_each_entry(event, &parent_ctx->pinned_groups, group_entry) {
		ret = inherit_task_group(event, parent, parent_ctx, child,
					 &inherited_all);
		if (ret)
			break;
	}

	list_for_each_entry(event, &parent_ctx->flexible_groups, group_entry) {
		ret = inherit_task_group(event, parent, parent_ctx, child,
					 &inherited_all);
		if (ret)
			break;
	}

	child_ctx = child->perf_event_ctxp;

	if (child_ctx && inherited_all) {
		/*
		 * Mark the child context as a clone of the parent
		 * context, or of whatever the parent is a clone of.
		 * Note that if the parent is a clone, it could get
		 * uncloned at any point, but that doesn't matter
		 * because the list of events and the generation
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

static void __init perf_event_init_all_cpus(void)
{
	int cpu;
	struct perf_cpu_context *cpuctx;

	for_each_possible_cpu(cpu) {
		cpuctx = &per_cpu(perf_cpu_context, cpu);
		mutex_init(&cpuctx->hlist_mutex);
		__perf_event_init_context(&cpuctx->ctx, NULL);
	}
}

static void __cpuinit perf_event_init_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx;

	cpuctx = &per_cpu(perf_cpu_context, cpu);

	spin_lock(&perf_resource_lock);
	cpuctx->max_pertask = perf_max_events - perf_reserved_percpu;
	spin_unlock(&perf_resource_lock);

	mutex_lock(&cpuctx->hlist_mutex);
	if (cpuctx->hlist_refcount > 0) {
		struct swevent_hlist *hlist;

		hlist = kzalloc(sizeof(*hlist), GFP_KERNEL);
		WARN_ON_ONCE(!hlist);
		rcu_assign_pointer(cpuctx->swevent_hlist, hlist);
	}
	mutex_unlock(&cpuctx->hlist_mutex);
}

#ifdef CONFIG_HOTPLUG_CPU
static void __perf_event_exit_cpu(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_event_context *ctx = &cpuctx->ctx;
	struct perf_event *event, *tmp;

	list_for_each_entry_safe(event, tmp, &ctx->pinned_groups, group_entry)
		__perf_event_remove_from_context(event);
	list_for_each_entry_safe(event, tmp, &ctx->flexible_groups, group_entry)
		__perf_event_remove_from_context(event);
}
static void perf_event_exit_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_event_context *ctx = &cpuctx->ctx;

	mutex_lock(&cpuctx->hlist_mutex);
	swevent_hlist_release(cpuctx);
	mutex_unlock(&cpuctx->hlist_mutex);

	mutex_lock(&ctx->mutex);
	smp_call_function_single(cpu, __perf_event_exit_cpu, NULL, 1);
	mutex_unlock(&ctx->mutex);
}
#else
static inline void perf_event_exit_cpu(int cpu) { }
#endif

static int __cpuinit
perf_cpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {

	case CPU_UP_PREPARE:
	case CPU_DOWN_FAILED:
		perf_event_init_cpu(cpu);
		break;

	case CPU_UP_CANCELED:
	case CPU_DOWN_PREPARE:
		perf_event_exit_cpu(cpu);
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

void __init perf_event_init(void)
{
	perf_event_init_all_cpus();
	perf_cpu_notify(&perf_cpu_nb, (unsigned long)CPU_UP_PREPARE,
			(void *)(long)smp_processor_id());
	perf_cpu_notify(&perf_cpu_nb, (unsigned long)CPU_ONLINE,
			(void *)(long)smp_processor_id());
	register_cpu_notifier(&perf_cpu_nb);
}

static ssize_t perf_show_reserve_percpu(struct sysdev_class *class,
					struct sysdev_class_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", perf_reserved_percpu);
}

static ssize_t
perf_set_reserve_percpu(struct sysdev_class *class,
			struct sysdev_class_attribute *attr,
			const char *buf,
			size_t count)
{
	struct perf_cpu_context *cpuctx;
	unsigned long val;
	int err, cpu, mpt;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > perf_max_events)
		return -EINVAL;

	spin_lock(&perf_resource_lock);
	perf_reserved_percpu = val;
	for_each_online_cpu(cpu) {
		cpuctx = &per_cpu(perf_cpu_context, cpu);
		raw_spin_lock_irq(&cpuctx->ctx.lock);
		mpt = min(perf_max_events - cpuctx->ctx.nr_events,
			  perf_max_events - perf_reserved_percpu);
		cpuctx->max_pertask = mpt;
		raw_spin_unlock_irq(&cpuctx->ctx.lock);
	}
	spin_unlock(&perf_resource_lock);

	return count;
}

static ssize_t perf_show_overcommit(struct sysdev_class *class,
				    struct sysdev_class_attribute *attr,
				    char *buf)
{
	return sprintf(buf, "%d\n", perf_overcommit);
}

static ssize_t
perf_set_overcommit(struct sysdev_class *class,
		    struct sysdev_class_attribute *attr,
		    const char *buf, size_t count)
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
	.name			= "perf_events",
};

static int __init perf_event_sysfs_init(void)
{
	return sysfs_create_group(&cpu_sysdev_class.kset.kobj,
				  &perfclass_attr_group);
}
device_initcall(perf_event_sysfs_init);

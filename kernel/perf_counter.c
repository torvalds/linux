/*
 * Performance counter core code
 *
 *  Copyright(C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2008 Red Hat, Inc., Ingo Molnar
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/fs.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sysfs.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/perf_counter.h>

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
	return ERR_PTR(-EINVAL);
}

u64 __weak hw_perf_save_disable(void)		{ return 0; }
void __weak hw_perf_restore(u64 ctrl)		{ }
void __weak hw_perf_counter_setup(void)		{ }

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
	else
		list_add_tail(&counter->list_entry, &group_leader->sibling_list);
}

static void
list_del_counter(struct perf_counter *counter, struct perf_counter_context *ctx)
{
	struct perf_counter *sibling, *tmp;

	list_del_init(&counter->list_entry);

	/*
	 * If this was a group counter with sibling counters then
	 * upgrade the siblings to singleton counters by adding them
	 * to the context list directly:
	 */
	list_for_each_entry_safe(sibling, tmp,
				 &counter->sibling_list, list_entry) {

		list_del_init(&sibling->list_entry);
		list_add_tail(&sibling->list_entry, &ctx->counter_list);
		sibling->group_leader = sibling;
	}
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

	if (counter->state == PERF_COUNTER_STATE_ACTIVE) {
		counter->hw_ops->hw_perf_counter_disable(counter);
		counter->state = PERF_COUNTER_STATE_INACTIVE;
		ctx->nr_active--;
		cpuctx->active_oncpu--;
		counter->task = NULL;
	}
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
 * Must be called with counter->mutex held.
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

/*
 * Cross CPU call to install and enable a preformance counter
 */
static void __perf_install_in_context(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	int cpu = smp_processor_id();
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

	/*
	 * Protect the list operation against NMI by disabling the
	 * counters on a global level. NOP for non NMI based counters.
	 */
	perf_flags = hw_perf_save_disable();
	list_add_counter(counter, ctx);
	hw_perf_restore(perf_flags);

	ctx->nr_counters++;

	if (cpuctx->active_oncpu < perf_max_counters) {
		counter->state = PERF_COUNTER_STATE_ACTIVE;
		counter->oncpu = cpu;
		ctx->nr_active++;
		cpuctx->active_oncpu++;
		counter->hw_ops->hw_perf_counter_enable(counter);
	}

	if (!ctx->task && cpuctx->max_pertask)
		cpuctx->max_pertask--;

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
 */
static void
perf_install_in_context(struct perf_counter_context *ctx,
			struct perf_counter *counter,
			int cpu)
{
	struct task_struct *task = ctx->task;

	counter->ctx = ctx;
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
	if (ctx->nr_active && list_empty(&counter->list_entry)) {
		spin_unlock_irq(&ctx->lock);
		goto retry;
	}

	/*
	 * The lock prevents that this context is scheduled in so we
	 * can add the counter safely, if it the call above did not
	 * succeed.
	 */
	if (list_empty(&counter->list_entry)) {
		list_add_counter(counter, ctx);
		ctx->nr_counters++;
	}
	spin_unlock_irq(&ctx->lock);
}

static void
counter_sched_out(struct perf_counter *counter,
		  struct perf_cpu_context *cpuctx,
		  struct perf_counter_context *ctx)
{
	if (counter->state != PERF_COUNTER_STATE_ACTIVE)
		return;

	counter->hw_ops->hw_perf_counter_disable(counter);
	counter->state = PERF_COUNTER_STATE_INACTIVE;
	counter->oncpu = -1;

	cpuctx->active_oncpu--;
	ctx->nr_active--;
}

static void
group_sched_out(struct perf_counter *group_counter,
		struct perf_cpu_context *cpuctx,
		struct perf_counter_context *ctx)
{
	struct perf_counter *counter;

	counter_sched_out(group_counter, cpuctx, ctx);

	/*
	 * Schedule out siblings (if any):
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry)
		counter_sched_out(counter, cpuctx, ctx);
}

/*
 * Called from scheduler to remove the counters of the current task,
 * with interrupts disabled.
 *
 * We stop each counter and update the counter value in counter->count.
 *
 * This does not protect us against NMI, but hw_perf_counter_disable()
 * sets the disabled bit in the control field of counter _before_
 * accessing the counter control register. If a NMI hits, then it will
 * not restart the counter.
 */
void perf_counter_task_sched_out(struct task_struct *task, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = &task->perf_counter_ctx;
	struct perf_counter *counter;

	if (likely(!cpuctx->task_ctx))
		return;

	spin_lock(&ctx->lock);
	if (ctx->nr_active) {
		list_for_each_entry(counter, &ctx->counter_list, list_entry)
			group_sched_out(counter, cpuctx, ctx);
	}
	spin_unlock(&ctx->lock);
	cpuctx->task_ctx = NULL;
}

static void
counter_sched_in(struct perf_counter *counter,
		 struct perf_cpu_context *cpuctx,
		 struct perf_counter_context *ctx,
		 int cpu)
{
	if (counter->state == PERF_COUNTER_STATE_OFF)
		return;

	counter->hw_ops->hw_perf_counter_enable(counter);
	counter->state = PERF_COUNTER_STATE_ACTIVE;
	counter->oncpu = cpu;	/* TODO: put 'cpu' into cpuctx->cpu */

	cpuctx->active_oncpu++;
	ctx->nr_active++;
}

static int
group_sched_in(struct perf_counter *group_counter,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx,
	       int cpu)
{
	struct perf_counter *counter;
	int was_group = 0;

	counter_sched_in(group_counter, cpuctx, ctx, cpu);

	/*
	 * Schedule in siblings as one group (if any):
	 */
	list_for_each_entry(counter, &group_counter->sibling_list, list_entry) {
		counter_sched_in(counter, cpuctx, ctx, cpu);
		was_group = 1;
	}

	return was_group;
}

/*
 * Called from scheduler to add the counters of the current task
 * with interrupts disabled.
 *
 * We restore the counter value and then enable it.
 *
 * This does not protect us against NMI, but hw_perf_counter_enable()
 * sets the enabled bit in the control field of counter _before_
 * accessing the counter control register. If a NMI hits, then it will
 * keep the counter running.
 */
void perf_counter_task_sched_in(struct task_struct *task, int cpu)
{
	struct perf_cpu_context *cpuctx = &per_cpu(perf_cpu_context, cpu);
	struct perf_counter_context *ctx = &task->perf_counter_ctx;
	struct perf_counter *counter;

	if (likely(!ctx->nr_counters))
		return;

	spin_lock(&ctx->lock);
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (ctx->nr_active == cpuctx->max_pertask)
			break;

		/*
		 * Listen to the 'cpu' scheduling filter constraint
		 * of counters:
		 */
		if (counter->cpu != -1 && counter->cpu != cpu)
			continue;

		/*
		 * If we scheduled in a group atomically and
		 * exclusively, break out:
		 */
		if (group_sched_in(counter, cpuctx, ctx, cpu))
			break;
	}
	spin_unlock(&ctx->lock);

	cpuctx->task_ctx = ctx;
}

int perf_counter_task_disable(void)
{
	struct task_struct *curr = current;
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	struct perf_counter *counter;
	u64 perf_flags;
	int cpu;

	if (likely(!ctx->nr_counters))
		return 0;

	local_irq_disable();
	cpu = smp_processor_id();

	perf_counter_task_sched_out(curr, cpu);

	spin_lock(&ctx->lock);

	/*
	 * Disable all the counters:
	 */
	perf_flags = hw_perf_save_disable();

	list_for_each_entry(counter, &ctx->counter_list, list_entry)
		counter->state = PERF_COUNTER_STATE_OFF;

	hw_perf_restore(perf_flags);

	spin_unlock(&ctx->lock);

	local_irq_enable();

	return 0;
}

int perf_counter_task_enable(void)
{
	struct task_struct *curr = current;
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	struct perf_counter *counter;
	u64 perf_flags;
	int cpu;

	if (likely(!ctx->nr_counters))
		return 0;

	local_irq_disable();
	cpu = smp_processor_id();

	spin_lock(&ctx->lock);

	/*
	 * Disable all the counters:
	 */
	perf_flags = hw_perf_save_disable();

	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		if (counter->state != PERF_COUNTER_STATE_OFF)
			continue;
		counter->state = PERF_COUNTER_STATE_INACTIVE;
	}
	hw_perf_restore(perf_flags);

	spin_unlock(&ctx->lock);

	perf_counter_task_sched_in(curr, cpu);

	local_irq_enable();

	return 0;
}

void perf_counter_task_tick(struct task_struct *curr, int cpu)
{
	struct perf_counter_context *ctx = &curr->perf_counter_ctx;
	struct perf_counter *counter;
	u64 perf_flags;

	if (likely(!ctx->nr_counters))
		return;

	perf_counter_task_sched_out(curr, cpu);

	spin_lock(&ctx->lock);

	/*
	 * Rotate the first entry last (works just fine for group counters too):
	 */
	perf_flags = hw_perf_save_disable();
	list_for_each_entry(counter, &ctx->counter_list, list_entry) {
		list_del(&counter->list_entry);
		list_add_tail(&counter->list_entry, &ctx->counter_list);
		break;
	}
	hw_perf_restore(perf_flags);

	spin_unlock(&ctx->lock);

	perf_counter_task_sched_in(curr, cpu);
}

/*
 * Cross CPU call to read the hardware counter
 */
static void __hw_perf_counter_read(void *info)
{
	struct perf_counter *counter = info;

	counter->hw_ops->hw_perf_counter_read(counter);
}

static u64 perf_counter_read(struct perf_counter *counter)
{
	/*
	 * If counter is enabled and currently active on a CPU, update the
	 * value in the counter structure:
	 */
	if (counter->state == PERF_COUNTER_STATE_ACTIVE) {
		smp_call_function_single(counter->oncpu,
					 __hw_perf_counter_read, counter, 1);
	}

	return atomic64_read(&counter->count);
}

/*
 * Cross CPU call to switch performance data pointers
 */
static void __perf_switch_irq_data(void *info)
{
	struct perf_cpu_context *cpuctx = &__get_cpu_var(perf_cpu_context);
	struct perf_counter *counter = info;
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_data *oldirqdata = counter->irqdata;

	/*
	 * If this is a task context, we need to check whether it is
	 * the current task context of this cpu. If not it has been
	 * scheduled out before the smp call arrived.
	 */
	if (ctx->task) {
		if (cpuctx->task_ctx != ctx)
			return;
		spin_lock(&ctx->lock);
	}

	/* Change the pointer NMI safe */
	atomic_long_set((atomic_long_t *)&counter->irqdata,
			(unsigned long) counter->usrdata);
	counter->usrdata = oldirqdata;

	if (ctx->task)
		spin_unlock(&ctx->lock);
}

static struct perf_data *perf_switch_irq_data(struct perf_counter *counter)
{
	struct perf_counter_context *ctx = counter->ctx;
	struct perf_data *oldirqdata = counter->irqdata;
	struct task_struct *task = ctx->task;

	if (!task) {
		smp_call_function_single(counter->cpu,
					 __perf_switch_irq_data,
					 counter, 1);
		return counter->usrdata;
	}

retry:
	spin_lock_irq(&ctx->lock);
	if (counter->state != PERF_COUNTER_STATE_ACTIVE) {
		counter->irqdata = counter->usrdata;
		counter->usrdata = oldirqdata;
		spin_unlock_irq(&ctx->lock);
		return oldirqdata;
	}
	spin_unlock_irq(&ctx->lock);
	task_oncpu_function_call(task, __perf_switch_irq_data, counter);
	/* Might have failed, because task was scheduled out */
	if (counter->irqdata == oldirqdata)
		goto retry;

	return counter->usrdata;
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

/*
 * Called when the last reference to the file is gone.
 */
static int perf_release(struct inode *inode, struct file *file)
{
	struct perf_counter *counter = file->private_data;
	struct perf_counter_context *ctx = counter->ctx;

	file->private_data = NULL;

	mutex_lock(&counter->mutex);

	perf_counter_remove_from_context(counter);
	put_context(ctx);

	mutex_unlock(&counter->mutex);

	kfree(counter);

	return 0;
}

/*
 * Read the performance counter - simple non blocking version for now
 */
static ssize_t
perf_read_hw(struct perf_counter *counter, char __user *buf, size_t count)
{
	u64 cntval;

	if (count != sizeof(cntval))
		return -EINVAL;

	mutex_lock(&counter->mutex);
	cntval = perf_counter_read(counter);
	mutex_unlock(&counter->mutex);

	return put_user(cntval, (u64 __user *) buf) ? -EFAULT : sizeof(cntval);
}

static ssize_t
perf_copy_usrdata(struct perf_data *usrdata, char __user *buf, size_t count)
{
	if (!usrdata->len)
		return 0;

	count = min(count, (size_t)usrdata->len);
	if (copy_to_user(buf, usrdata->data + usrdata->rd_idx, count))
		return -EFAULT;

	/* Adjust the counters */
	usrdata->len -= count;
	if (!usrdata->len)
		usrdata->rd_idx = 0;
	else
		usrdata->rd_idx += count;

	return count;
}

static ssize_t
perf_read_irq_data(struct perf_counter	*counter,
		   char __user		*buf,
		   size_t		count,
		   int			nonblocking)
{
	struct perf_data *irqdata, *usrdata;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t res;

	irqdata = counter->irqdata;
	usrdata = counter->usrdata;

	if (usrdata->len + irqdata->len >= count)
		goto read_pending;

	if (nonblocking)
		return -EAGAIN;

	spin_lock_irq(&counter->waitq.lock);
	__add_wait_queue(&counter->waitq, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (usrdata->len + irqdata->len >= count)
			break;

		if (signal_pending(current))
			break;

		spin_unlock_irq(&counter->waitq.lock);
		schedule();
		spin_lock_irq(&counter->waitq.lock);
	}
	__remove_wait_queue(&counter->waitq, &wait);
	__set_current_state(TASK_RUNNING);
	spin_unlock_irq(&counter->waitq.lock);

	if (usrdata->len + irqdata->len < count)
		return -ERESTARTSYS;
read_pending:
	mutex_lock(&counter->mutex);

	/* Drain pending data first: */
	res = perf_copy_usrdata(usrdata, buf, count);
	if (res < 0 || res == count)
		goto out;

	/* Switch irq buffer: */
	usrdata = perf_switch_irq_data(counter);
	if (perf_copy_usrdata(usrdata, buf + res, count - res) < 0) {
		if (!res)
			res = -EFAULT;
	} else {
		res = count;
	}
out:
	mutex_unlock(&counter->mutex);

	return res;
}

static ssize_t
perf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct perf_counter *counter = file->private_data;

	switch (counter->hw_event.record_type) {
	case PERF_RECORD_SIMPLE:
		return perf_read_hw(counter, buf, count);

	case PERF_RECORD_IRQ:
	case PERF_RECORD_GROUP:
		return perf_read_irq_data(counter, buf, count,
					  file->f_flags & O_NONBLOCK);
	}
	return -EINVAL;
}

static unsigned int perf_poll(struct file *file, poll_table *wait)
{
	struct perf_counter *counter = file->private_data;
	unsigned int events = 0;
	unsigned long flags;

	poll_wait(file, &counter->waitq, wait);

	spin_lock_irqsave(&counter->waitq.lock, flags);
	if (counter->usrdata->len || counter->irqdata->len)
		events |= POLLIN;
	spin_unlock_irqrestore(&counter->waitq.lock, flags);

	return events;
}

static const struct file_operations perf_fops = {
	.release		= perf_release,
	.read			= perf_read,
	.poll			= perf_poll,
};

static void cpu_clock_perf_counter_enable(struct perf_counter *counter)
{
}

static void cpu_clock_perf_counter_disable(struct perf_counter *counter)
{
}

static void cpu_clock_perf_counter_read(struct perf_counter *counter)
{
	int cpu = raw_smp_processor_id();

	atomic64_set(&counter->count, cpu_clock(cpu));
}

static const struct hw_perf_counter_ops perf_ops_cpu_clock = {
	.hw_perf_counter_enable		= cpu_clock_perf_counter_enable,
	.hw_perf_counter_disable	= cpu_clock_perf_counter_disable,
	.hw_perf_counter_read		= cpu_clock_perf_counter_read,
};

static void task_clock_perf_counter_update(struct perf_counter *counter)
{
	u64 prev, now;
	s64 delta;

	prev = atomic64_read(&counter->hw.prev_count);
	now = current->se.sum_exec_runtime;

	atomic64_set(&counter->hw.prev_count, now);

	delta = now - prev;

	atomic64_add(delta, &counter->count);
}

static void task_clock_perf_counter_read(struct perf_counter *counter)
{
	task_clock_perf_counter_update(counter);
}

static void task_clock_perf_counter_enable(struct perf_counter *counter)
{
	atomic64_set(&counter->hw.prev_count, current->se.sum_exec_runtime);
}

static void task_clock_perf_counter_disable(struct perf_counter *counter)
{
	task_clock_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_task_clock = {
	.hw_perf_counter_enable		= task_clock_perf_counter_enable,
	.hw_perf_counter_disable	= task_clock_perf_counter_disable,
	.hw_perf_counter_read		= task_clock_perf_counter_read,
};

static u64 get_page_faults(void)
{
	struct task_struct *curr = current;

	return curr->maj_flt + curr->min_flt;
}

static void page_faults_perf_counter_update(struct perf_counter *counter)
{
	u64 prev, now;
	s64 delta;

	prev = atomic64_read(&counter->hw.prev_count);
	now = get_page_faults();

	atomic64_set(&counter->hw.prev_count, now);

	delta = now - prev;

	atomic64_add(delta, &counter->count);
}

static void page_faults_perf_counter_read(struct perf_counter *counter)
{
	page_faults_perf_counter_update(counter);
}

static void page_faults_perf_counter_enable(struct perf_counter *counter)
{
	/*
	 * page-faults is a per-task value already,
	 * so we dont have to clear it on switch-in.
	 */
}

static void page_faults_perf_counter_disable(struct perf_counter *counter)
{
	page_faults_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_page_faults = {
	.hw_perf_counter_enable		= page_faults_perf_counter_enable,
	.hw_perf_counter_disable	= page_faults_perf_counter_disable,
	.hw_perf_counter_read		= page_faults_perf_counter_read,
};

static u64 get_context_switches(void)
{
	struct task_struct *curr = current;

	return curr->nvcsw + curr->nivcsw;
}

static void context_switches_perf_counter_update(struct perf_counter *counter)
{
	u64 prev, now;
	s64 delta;

	prev = atomic64_read(&counter->hw.prev_count);
	now = get_context_switches();

	atomic64_set(&counter->hw.prev_count, now);

	delta = now - prev;

	atomic64_add(delta, &counter->count);
}

static void context_switches_perf_counter_read(struct perf_counter *counter)
{
	context_switches_perf_counter_update(counter);
}

static void context_switches_perf_counter_enable(struct perf_counter *counter)
{
	/*
	 * ->nvcsw + curr->nivcsw is a per-task value already,
	 * so we dont have to clear it on switch-in.
	 */
}

static void context_switches_perf_counter_disable(struct perf_counter *counter)
{
	context_switches_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_context_switches = {
	.hw_perf_counter_enable		= context_switches_perf_counter_enable,
	.hw_perf_counter_disable	= context_switches_perf_counter_disable,
	.hw_perf_counter_read		= context_switches_perf_counter_read,
};

static inline u64 get_cpu_migrations(void)
{
	return current->se.nr_migrations;
}

static void cpu_migrations_perf_counter_update(struct perf_counter *counter)
{
	u64 prev, now;
	s64 delta;

	prev = atomic64_read(&counter->hw.prev_count);
	now = get_cpu_migrations();

	atomic64_set(&counter->hw.prev_count, now);

	delta = now - prev;

	atomic64_add(delta, &counter->count);
}

static void cpu_migrations_perf_counter_read(struct perf_counter *counter)
{
	cpu_migrations_perf_counter_update(counter);
}

static void cpu_migrations_perf_counter_enable(struct perf_counter *counter)
{
	/*
	 * se.nr_migrations is a per-task value already,
	 * so we dont have to clear it on switch-in.
	 */
}

static void cpu_migrations_perf_counter_disable(struct perf_counter *counter)
{
	cpu_migrations_perf_counter_update(counter);
}

static const struct hw_perf_counter_ops perf_ops_cpu_migrations = {
	.hw_perf_counter_enable		= cpu_migrations_perf_counter_enable,
	.hw_perf_counter_disable	= cpu_migrations_perf_counter_disable,
	.hw_perf_counter_read		= cpu_migrations_perf_counter_read,
};

static const struct hw_perf_counter_ops *
sw_perf_counter_init(struct perf_counter *counter)
{
	const struct hw_perf_counter_ops *hw_ops = NULL;

	switch (counter->hw_event.type) {
	case PERF_COUNT_CPU_CLOCK:
		hw_ops = &perf_ops_cpu_clock;
		break;
	case PERF_COUNT_TASK_CLOCK:
		hw_ops = &perf_ops_task_clock;
		break;
	case PERF_COUNT_PAGE_FAULTS:
		hw_ops = &perf_ops_page_faults;
		break;
	case PERF_COUNT_CONTEXT_SWITCHES:
		hw_ops = &perf_ops_context_switches;
		break;
	case PERF_COUNT_CPU_MIGRATIONS:
		hw_ops = &perf_ops_cpu_migrations;
		break;
	default:
		break;
	}
	return hw_ops;
}

/*
 * Allocate and initialize a counter structure
 */
static struct perf_counter *
perf_counter_alloc(struct perf_counter_hw_event *hw_event,
		   int cpu,
		   struct perf_counter *group_leader,
		   gfp_t gfpflags)
{
	const struct hw_perf_counter_ops *hw_ops;
	struct perf_counter *counter;

	counter = kzalloc(sizeof(*counter), gfpflags);
	if (!counter)
		return NULL;

	/*
	 * Single counters are their own group leaders, with an
	 * empty sibling list:
	 */
	if (!group_leader)
		group_leader = counter;

	mutex_init(&counter->mutex);
	INIT_LIST_HEAD(&counter->list_entry);
	INIT_LIST_HEAD(&counter->sibling_list);
	init_waitqueue_head(&counter->waitq);

	counter->irqdata		= &counter->data[0];
	counter->usrdata		= &counter->data[1];
	counter->cpu			= cpu;
	counter->hw_event		= *hw_event;
	counter->wakeup_pending		= 0;
	counter->group_leader		= group_leader;
	counter->hw_ops			= NULL;

	if (hw_event->disabled)
		counter->state = PERF_COUNTER_STATE_OFF;

	hw_ops = NULL;
	if (!hw_event->raw && hw_event->type < 0)
		hw_ops = sw_perf_counter_init(counter);
	if (!hw_ops)
		hw_ops = hw_perf_counter_init(counter);

	if (!hw_ops) {
		kfree(counter);
		return NULL;
	}
	counter->hw_ops = hw_ops;

	return counter;
}

/**
 * sys_perf_task_open - open a performance counter, associate it to a task/cpu
 *
 * @hw_event_uptr:	event type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader counter fd
 */
asmlinkage int
sys_perf_counter_open(struct perf_counter_hw_event *hw_event_uptr __user,
		      pid_t pid, int cpu, int group_fd)
{
	struct perf_counter *counter, *group_leader;
	struct perf_counter_hw_event hw_event;
	struct perf_counter_context *ctx;
	struct file *counter_file = NULL;
	struct file *group_file = NULL;
	int fput_needed = 0;
	int fput_needed2 = 0;
	int ret;

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
	}

	ret = -EINVAL;
	counter = perf_counter_alloc(&hw_event, cpu, group_leader, GFP_KERNEL);
	if (!counter)
		goto err_put_context;

	ret = anon_inode_getfd("[perf_counter]", &perf_fops, counter, 0);
	if (ret < 0)
		goto err_free_put_context;

	counter_file = fget_light(ret, &fput_needed2);
	if (!counter_file)
		goto err_free_put_context;

	counter->filp = counter_file;
	perf_install_in_context(ctx, counter, cpu);

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
	INIT_LIST_HEAD(&ctx->counter_list);
	ctx->task = task;
}

/*
 * inherit a counter from parent task to child task:
 */
static int
inherit_counter(struct perf_counter *parent_counter,
	      struct task_struct *parent,
	      struct perf_counter_context *parent_ctx,
	      struct task_struct *child,
	      struct perf_counter_context *child_ctx)
{
	struct perf_counter *child_counter;

	child_counter = perf_counter_alloc(&parent_counter->hw_event,
					    parent_counter->cpu, NULL,
					    GFP_ATOMIC);
	if (!child_counter)
		return -ENOMEM;

	/*
	 * Link it up in the child's context:
	 */
	child_counter->ctx = child_ctx;
	child_counter->task = child;
	list_add_counter(child_counter, child_ctx);
	child_ctx->nr_counters++;

	child_counter->parent = parent_counter;
	parent_counter->nr_inherited++;
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

	return 0;
}

static void
__perf_counter_exit_task(struct task_struct *child,
			 struct perf_counter *child_counter,
			 struct perf_counter_context *child_ctx)
{
	struct perf_counter *parent_counter;
	u64 parent_val, child_val;
	u64 perf_flags;

	/*
	 * Disable and unlink this counter.
	 *
	 * Be careful about zapping the list - IRQ/NMI context
	 * could still be processing it:
	 */
	local_irq_disable();
	perf_flags = hw_perf_save_disable();

	if (child_counter->state == PERF_COUNTER_STATE_ACTIVE) {
		struct perf_cpu_context *cpuctx;

		cpuctx = &__get_cpu_var(perf_cpu_context);

		child_counter->hw_ops->hw_perf_counter_disable(child_counter);
		child_counter->state = PERF_COUNTER_STATE_INACTIVE;
		child_counter->oncpu = -1;

		cpuctx->active_oncpu--;
		child_ctx->nr_active--;
	}

	list_del_init(&child_counter->list_entry);

	hw_perf_restore(perf_flags);
	local_irq_enable();

	parent_counter = child_counter->parent;
	/*
	 * It can happen that parent exits first, and has counters
	 * that are still around due to the child reference. These
	 * counters need to be zapped - but otherwise linger.
	 */
	if (!parent_counter)
		return;

	parent_val = atomic64_read(&parent_counter->count);
	child_val = atomic64_read(&child_counter->count);

	/*
	 * Add back the child's count to the parent's count:
	 */
	atomic64_add(child_val, &parent_counter->count);

	fput(parent_counter->filp);

	kfree(child_counter);
}

/*
 * When a child task exist, feed back counter values to parent counters.
 *
 * Note: we are running in child context, but the PID is not hashed
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
	struct perf_counter *counter, *parent_counter;
	struct task_struct *parent = current;
	unsigned long flags;

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
	spin_lock_irqsave(&parent_ctx->lock, flags);

	/*
	 * We dont have to disable NMIs - we are only looking at
	 * the list, not manipulating it:
	 */
	list_for_each_entry(counter, &parent_ctx->counter_list, list_entry) {
		if (!counter->hw_event.inherit || counter->group_leader != counter)
			continue;

		/*
		 * Instead of creating recursive hierarchies of counters,
		 * we link inheritd counters back to the original parent,
		 * which has a filp for sure, which we use as the reference
		 * count:
		 */
		parent_counter = counter;
		if (counter->parent)
			parent_counter = counter->parent;

		if (inherit_counter(parent_counter, parent,
				  parent_ctx, child, child_ctx))
			break;
	}

	spin_unlock_irqrestore(&parent_ctx->lock, flags);
}

static void __cpuinit perf_counter_init_cpu(int cpu)
{
	struct perf_cpu_context *cpuctx;

	cpuctx = &per_cpu(perf_cpu_context, cpu);
	__perf_counter_init_context(&cpuctx->ctx, NULL);

	mutex_lock(&perf_resource_mutex);
	cpuctx->max_pertask = perf_max_counters - perf_reserved_percpu;
	mutex_unlock(&perf_resource_mutex);

	hw_perf_counter_setup();
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
	smp_call_function_single(cpu, __perf_counter_exit_cpu, NULL, 1);
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

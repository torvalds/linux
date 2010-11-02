/*
 * kernel/stop_machine.c
 *
 * Copyright (C) 2008, 2005	IBM Corporation.
 * Copyright (C) 2008, 2005	Rusty Russell rusty@rustcorp.com.au
 * Copyright (C) 2010		SUSE Linux Products GmbH
 * Copyright (C) 2010		Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2 and any later version.
 */
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/stop_machine.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>

#include <asm/atomic.h>

/*
 * Structure to determine completion condition and record errors.  May
 * be shared by works on different cpus.
 */
struct cpu_stop_done {
	atomic_t		nr_todo;	/* nr left to execute */
	bool			executed;	/* actually executed? */
	int			ret;		/* collected return value */
	struct completion	completion;	/* fired if nr_todo reaches 0 */
};

/* the actual stopper, one per every possible cpu, enabled on online cpus */
struct cpu_stopper {
	spinlock_t		lock;
	bool			enabled;	/* is this stopper enabled? */
	struct list_head	works;		/* list of pending works */
	struct task_struct	*thread;	/* stopper thread */
};

static DEFINE_PER_CPU(struct cpu_stopper, cpu_stopper);

static void cpu_stop_init_done(struct cpu_stop_done *done, unsigned int nr_todo)
{
	memset(done, 0, sizeof(*done));
	atomic_set(&done->nr_todo, nr_todo);
	init_completion(&done->completion);
}

/* signal completion unless @done is NULL */
static void cpu_stop_signal_done(struct cpu_stop_done *done, bool executed)
{
	if (done) {
		if (executed)
			done->executed = true;
		if (atomic_dec_and_test(&done->nr_todo))
			complete(&done->completion);
	}
}

/* queue @work to @stopper.  if offline, @work is completed immediately */
static void cpu_stop_queue_work(struct cpu_stopper *stopper,
				struct cpu_stop_work *work)
{
	unsigned long flags;

	spin_lock_irqsave(&stopper->lock, flags);

	if (stopper->enabled) {
		list_add_tail(&work->list, &stopper->works);
		wake_up_process(stopper->thread);
	} else
		cpu_stop_signal_done(work->done, false);

	spin_unlock_irqrestore(&stopper->lock, flags);
}

/**
 * stop_one_cpu - stop a cpu
 * @cpu: cpu to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Execute @fn(@arg) on @cpu.  @fn is run in a process context with
 * the highest priority preempting any task on the cpu and
 * monopolizing it.  This function returns after the execution is
 * complete.
 *
 * This function doesn't guarantee @cpu stays online till @fn
 * completes.  If @cpu goes down in the middle, execution may happen
 * partially or fully on different cpus.  @fn should either be ready
 * for that or the caller should ensure that @cpu stays online until
 * this function completes.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -ENOENT if @fn(@arg) was not executed because @cpu was offline;
 * otherwise, the return value of @fn.
 */
int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg)
{
	struct cpu_stop_done done;
	struct cpu_stop_work work = { .fn = fn, .arg = arg, .done = &done };

	cpu_stop_init_done(&done, 1);
	cpu_stop_queue_work(&per_cpu(cpu_stopper, cpu), &work);
	wait_for_completion(&done.completion);
	return done.executed ? done.ret : -ENOENT;
}

/**
 * stop_one_cpu_nowait - stop a cpu but don't wait for completion
 * @cpu: cpu to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Similar to stop_one_cpu() but doesn't wait for completion.  The
 * caller is responsible for ensuring @work_buf is currently unused
 * and will remain untouched until stopper starts executing @fn.
 *
 * CONTEXT:
 * Don't care.
 */
void stop_one_cpu_nowait(unsigned int cpu, cpu_stop_fn_t fn, void *arg,
			struct cpu_stop_work *work_buf)
{
	*work_buf = (struct cpu_stop_work){ .fn = fn, .arg = arg, };
	cpu_stop_queue_work(&per_cpu(cpu_stopper, cpu), work_buf);
}

/* static data for stop_cpus */
static DEFINE_MUTEX(stop_cpus_mutex);
static DEFINE_PER_CPU(struct cpu_stop_work, stop_cpus_work);

int __stop_cpus(const struct cpumask *cpumask, cpu_stop_fn_t fn, void *arg)
{
	struct cpu_stop_work *work;
	struct cpu_stop_done done;
	unsigned int cpu;

	/* initialize works and done */
	for_each_cpu(cpu, cpumask) {
		work = &per_cpu(stop_cpus_work, cpu);
		work->fn = fn;
		work->arg = arg;
		work->done = &done;
	}
	cpu_stop_init_done(&done, cpumask_weight(cpumask));

	/*
	 * Disable preemption while queueing to avoid getting
	 * preempted by a stopper which might wait for other stoppers
	 * to enter @fn which can lead to deadlock.
	 */
	preempt_disable();
	for_each_cpu(cpu, cpumask)
		cpu_stop_queue_work(&per_cpu(cpu_stopper, cpu),
				    &per_cpu(stop_cpus_work, cpu));
	preempt_enable();

	wait_for_completion(&done.completion);
	return done.executed ? done.ret : -ENOENT;
}

/**
 * stop_cpus - stop multiple cpus
 * @cpumask: cpus to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Execute @fn(@arg) on online cpus in @cpumask.  On each target cpu,
 * @fn is run in a process context with the highest priority
 * preempting any task on the cpu and monopolizing it.  This function
 * returns after all executions are complete.
 *
 * This function doesn't guarantee the cpus in @cpumask stay online
 * till @fn completes.  If some cpus go down in the middle, execution
 * on the cpu may happen partially or fully on different cpus.  @fn
 * should either be ready for that or the caller should ensure that
 * the cpus stay online until this function completes.
 *
 * All stop_cpus() calls are serialized making it safe for @fn to wait
 * for all cpus to start executing it.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -ENOENT if @fn(@arg) was not executed at all because all cpus in
 * @cpumask were offline; otherwise, 0 if all executions of @fn
 * returned 0, any non zero return value if any returned non zero.
 */
int stop_cpus(const struct cpumask *cpumask, cpu_stop_fn_t fn, void *arg)
{
	int ret;

	/* static works are used, process one request at a time */
	mutex_lock(&stop_cpus_mutex);
	ret = __stop_cpus(cpumask, fn, arg);
	mutex_unlock(&stop_cpus_mutex);
	return ret;
}

/**
 * try_stop_cpus - try to stop multiple cpus
 * @cpumask: cpus to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Identical to stop_cpus() except that it fails with -EAGAIN if
 * someone else is already using the facility.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -EAGAIN if someone else is already stopping cpus, -ENOENT if
 * @fn(@arg) was not executed at all because all cpus in @cpumask were
 * offline; otherwise, 0 if all executions of @fn returned 0, any non
 * zero return value if any returned non zero.
 */
int try_stop_cpus(const struct cpumask *cpumask, cpu_stop_fn_t fn, void *arg)
{
	int ret;

	/* static works are used, process one request at a time */
	if (!mutex_trylock(&stop_cpus_mutex))
		return -EAGAIN;
	ret = __stop_cpus(cpumask, fn, arg);
	mutex_unlock(&stop_cpus_mutex);
	return ret;
}

static int cpu_stopper_thread(void *data)
{
	struct cpu_stopper *stopper = data;
	struct cpu_stop_work *work;
	int ret;

repeat:
	set_current_state(TASK_INTERRUPTIBLE);	/* mb paired w/ kthread_stop */

	if (kthread_should_stop()) {
		__set_current_state(TASK_RUNNING);
		return 0;
	}

	work = NULL;
	spin_lock_irq(&stopper->lock);
	if (!list_empty(&stopper->works)) {
		work = list_first_entry(&stopper->works,
					struct cpu_stop_work, list);
		list_del_init(&work->list);
	}
	spin_unlock_irq(&stopper->lock);

	if (work) {
		cpu_stop_fn_t fn = work->fn;
		void *arg = work->arg;
		struct cpu_stop_done *done = work->done;
		char ksym_buf[KSYM_NAME_LEN] __maybe_unused;

		__set_current_state(TASK_RUNNING);

		/* cpu stop callbacks are not allowed to sleep */
		preempt_disable();

		ret = fn(arg);
		if (ret)
			done->ret = ret;

		/* restore preemption and check it's still balanced */
		preempt_enable();
		WARN_ONCE(preempt_count(),
			  "cpu_stop: %s(%p) leaked preempt count\n",
			  kallsyms_lookup((unsigned long)fn, NULL, NULL, NULL,
					  ksym_buf), arg);

		cpu_stop_signal_done(done, true);
	} else
		schedule();

	goto repeat;
}

extern void sched_set_stop_task(int cpu, struct task_struct *stop);

/* manage stopper for a cpu, mostly lifted from sched migration thread mgmt */
static int __cpuinit cpu_stop_cpu_callback(struct notifier_block *nfb,
					   unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
	struct task_struct *p;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		BUG_ON(stopper->thread || stopper->enabled ||
		       !list_empty(&stopper->works));
		p = kthread_create(cpu_stopper_thread, stopper, "migration/%d",
				   cpu);
		if (IS_ERR(p))
			return notifier_from_errno(PTR_ERR(p));
		get_task_struct(p);
		kthread_bind(p, cpu);
		sched_set_stop_task(cpu, p);
		stopper->thread = p;
		break;

	case CPU_ONLINE:
		/* strictly unnecessary, as first user will wake it */
		wake_up_process(stopper->thread);
		/* mark enabled */
		spin_lock_irq(&stopper->lock);
		stopper->enabled = true;
		spin_unlock_irq(&stopper->lock);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_POST_DEAD:
	{
		struct cpu_stop_work *work;

		sched_set_stop_task(cpu, NULL);
		/* kill the stopper */
		kthread_stop(stopper->thread);
		/* drain remaining works */
		spin_lock_irq(&stopper->lock);
		list_for_each_entry(work, &stopper->works, list)
			cpu_stop_signal_done(work->done, false);
		stopper->enabled = false;
		spin_unlock_irq(&stopper->lock);
		/* release the stopper */
		put_task_struct(stopper->thread);
		stopper->thread = NULL;
		break;
	}
#endif
	}

	return NOTIFY_OK;
}

/*
 * Give it a higher priority so that cpu stopper is available to other
 * cpu notifiers.  It currently shares the same priority as sched
 * migration_notifier.
 */
static struct notifier_block __cpuinitdata cpu_stop_cpu_notifier = {
	.notifier_call	= cpu_stop_cpu_callback,
	.priority	= 10,
};

static int __init cpu_stop_init(void)
{
	void *bcpu = (void *)(long)smp_processor_id();
	unsigned int cpu;
	int err;

	for_each_possible_cpu(cpu) {
		struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);

		spin_lock_init(&stopper->lock);
		INIT_LIST_HEAD(&stopper->works);
	}

	/* start one for the boot cpu */
	err = cpu_stop_cpu_callback(&cpu_stop_cpu_notifier, CPU_UP_PREPARE,
				    bcpu);
	BUG_ON(err != NOTIFY_OK);
	cpu_stop_cpu_callback(&cpu_stop_cpu_notifier, CPU_ONLINE, bcpu);
	register_cpu_notifier(&cpu_stop_cpu_notifier);

	return 0;
}
early_initcall(cpu_stop_init);

#ifdef CONFIG_STOP_MACHINE

/* This controls the threads on each CPU. */
enum stopmachine_state {
	/* Dummy starting state for thread. */
	STOPMACHINE_NONE,
	/* Awaiting everyone to be scheduled. */
	STOPMACHINE_PREPARE,
	/* Disable interrupts. */
	STOPMACHINE_DISABLE_IRQ,
	/* Run the function */
	STOPMACHINE_RUN,
	/* Exit */
	STOPMACHINE_EXIT,
};

struct stop_machine_data {
	int			(*fn)(void *);
	void			*data;
	/* Like num_online_cpus(), but hotplug cpu uses us, so we need this. */
	unsigned int		num_threads;
	const struct cpumask	*active_cpus;

	enum stopmachine_state	state;
	atomic_t		thread_ack;
};

static void set_state(struct stop_machine_data *smdata,
		      enum stopmachine_state newstate)
{
	/* Reset ack counter. */
	atomic_set(&smdata->thread_ack, smdata->num_threads);
	smp_wmb();
	smdata->state = newstate;
}

/* Last one to ack a state moves to the next state. */
static void ack_state(struct stop_machine_data *smdata)
{
	if (atomic_dec_and_test(&smdata->thread_ack))
		set_state(smdata, smdata->state + 1);
}

/* This is the cpu_stop function which stops the CPU. */
static int stop_machine_cpu_stop(void *data)
{
	struct stop_machine_data *smdata = data;
	enum stopmachine_state curstate = STOPMACHINE_NONE;
	int cpu = smp_processor_id(), err = 0;
	bool is_active;

	if (!smdata->active_cpus)
		is_active = cpu == cpumask_first(cpu_online_mask);
	else
		is_active = cpumask_test_cpu(cpu, smdata->active_cpus);

	/* Simple state machine */
	do {
		/* Chill out and ensure we re-read stopmachine_state. */
		cpu_relax();
		if (smdata->state != curstate) {
			curstate = smdata->state;
			switch (curstate) {
			case STOPMACHINE_DISABLE_IRQ:
				local_irq_disable();
				hard_irq_disable();
				break;
			case STOPMACHINE_RUN:
				if (is_active)
					err = smdata->fn(smdata->data);
				break;
			default:
				break;
			}
			ack_state(smdata);
		}
	} while (curstate != STOPMACHINE_EXIT);

	local_irq_enable();
	return err;
}

int __stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	struct stop_machine_data smdata = { .fn = fn, .data = data,
					    .num_threads = num_online_cpus(),
					    .active_cpus = cpus };

	/* Set the initial state and stop all online cpus. */
	set_state(&smdata, STOPMACHINE_PREPARE);
	return stop_cpus(cpu_online_mask, stop_machine_cpu_stop, &smdata);
}

int stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	int ret;

	/* No CPUs can come up or down during this. */
	get_online_cpus();
	ret = __stop_machine(fn, data, cpus);
	put_online_cpus();
	return ret;
}
EXPORT_SYMBOL_GPL(stop_machine);

#endif	/* CONFIG_STOP_MACHINE */

/* Copyright 2008, 2005 Rusty Russell rusty@rustcorp.com.au IBM Corporation.
 * GPL v2 and any later version.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stop_machine.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>

#include <asm/atomic.h>
#include <asm/uaccess.h>

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
static enum stopmachine_state state;

struct stop_machine_data {
	int (*fn)(void *);
	void *data;
	int fnret;
};

/* Like num_online_cpus(), but hotplug cpu uses us, so we need this. */
static unsigned int num_threads;
static atomic_t thread_ack;
static DEFINE_MUTEX(lock);
/* setup_lock protects refcount, stop_machine_wq and stop_machine_work. */
static DEFINE_MUTEX(setup_lock);
/* Users of stop_machine. */
static int refcount;
static struct workqueue_struct *stop_machine_wq;
static struct stop_machine_data active, idle;
static const struct cpumask *active_cpus;
static void __percpu *stop_machine_work;

static void set_state(enum stopmachine_state newstate)
{
	/* Reset ack counter. */
	atomic_set(&thread_ack, num_threads);
	smp_wmb();
	state = newstate;
}

/* Last one to ack a state moves to the next state. */
static void ack_state(void)
{
	if (atomic_dec_and_test(&thread_ack))
		set_state(state + 1);
}

/* This is the actual function which stops the CPU. It runs
 * in the context of a dedicated stopmachine workqueue. */
static void stop_cpu(struct work_struct *unused)
{
	enum stopmachine_state curstate = STOPMACHINE_NONE;
	struct stop_machine_data *smdata = &idle;
	int cpu = smp_processor_id();
	int err;

	if (!active_cpus) {
		if (cpu == cpumask_first(cpu_online_mask))
			smdata = &active;
	} else {
		if (cpumask_test_cpu(cpu, active_cpus))
			smdata = &active;
	}
	/* Simple state machine */
	do {
		/* Chill out and ensure we re-read stopmachine_state. */
		cpu_relax();
		if (state != curstate) {
			curstate = state;
			switch (curstate) {
			case STOPMACHINE_DISABLE_IRQ:
				local_irq_disable();
				hard_irq_disable();
				break;
			case STOPMACHINE_RUN:
				/* On multiple CPUs only a single error code
				 * is needed to tell that something failed. */
				err = smdata->fn(smdata->data);
				if (err)
					smdata->fnret = err;
				break;
			default:
				break;
			}
			ack_state();
		}
	} while (curstate != STOPMACHINE_EXIT);

	local_irq_enable();
}

/* Callback for CPUs which aren't supposed to do anything. */
static int chill(void *unused)
{
	return 0;
}

int stop_machine_create(void)
{
	mutex_lock(&setup_lock);
	if (refcount)
		goto done;
	stop_machine_wq = create_rt_workqueue("kstop");
	if (!stop_machine_wq)
		goto err_out;
	stop_machine_work = alloc_percpu(struct work_struct);
	if (!stop_machine_work)
		goto err_out;
done:
	refcount++;
	mutex_unlock(&setup_lock);
	return 0;

err_out:
	if (stop_machine_wq)
		destroy_workqueue(stop_machine_wq);
	mutex_unlock(&setup_lock);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(stop_machine_create);

void stop_machine_destroy(void)
{
	mutex_lock(&setup_lock);
	refcount--;
	if (refcount)
		goto done;
	destroy_workqueue(stop_machine_wq);
	free_percpu(stop_machine_work);
done:
	mutex_unlock(&setup_lock);
}
EXPORT_SYMBOL_GPL(stop_machine_destroy);

int __stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	struct work_struct *sm_work;
	int i, ret;

	/* Set up initial state. */
	mutex_lock(&lock);
	num_threads = num_online_cpus();
	active_cpus = cpus;
	active.fn = fn;
	active.data = data;
	active.fnret = 0;
	idle.fn = chill;
	idle.data = NULL;

	set_state(STOPMACHINE_PREPARE);

	/* Schedule the stop_cpu work on all cpus: hold this CPU so one
	 * doesn't hit this CPU until we're ready. */
	get_cpu();
	for_each_online_cpu(i) {
		sm_work = per_cpu_ptr(stop_machine_work, i);
		INIT_WORK(sm_work, stop_cpu);
		queue_work_on(i, stop_machine_wq, sm_work);
	}
	/* This will release the thread on our CPU. */
	put_cpu();
	flush_workqueue(stop_machine_wq);
	ret = active.fnret;
	mutex_unlock(&lock);
	return ret;
}

int stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	int ret;

	ret = stop_machine_create();
	if (ret)
		return ret;
	/* No CPUs can come up or down during this. */
	get_online_cpus();
	ret = __stop_machine(fn, data, cpus);
	put_online_cpus();
	stop_machine_destroy();
	return ret;
}
EXPORT_SYMBOL_GPL(stop_machine);

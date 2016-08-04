/*
 *  Copyright (C)  2015 Michael Turquette <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/percpu.h>
#include <linux/irq_work.h>
#include <linux/delay.h>
#include <linux/string.h>

#define CREATE_TRACE_POINTS
#include <trace/events/cpufreq_sched.h>

#include "sched.h"

#define THROTTLE_DOWN_NSEC	50000000 /* 50ms default */
#define THROTTLE_UP_NSEC	500000 /* 500us default */

struct static_key __read_mostly __sched_freq = STATIC_KEY_INIT_FALSE;
static bool __read_mostly cpufreq_driver_slow;

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHED
static struct cpufreq_governor cpufreq_gov_sched;
#endif

static DEFINE_PER_CPU(unsigned long, enabled);
DEFINE_PER_CPU(struct sched_capacity_reqs, cpu_sched_capacity_reqs);

/**
 * gov_data - per-policy data internal to the governor
 * @up_throttle: next throttling period expiry if increasing OPP
 * @down_throttle: next throttling period expiry if decreasing OPP
 * @up_throttle_nsec: throttle period length in nanoseconds if increasing OPP
 * @down_throttle_nsec: throttle period length in nanoseconds if decreasing OPP
 * @task: worker thread for dvfs transition that may block/sleep
 * @irq_work: callback used to wake up worker thread
 * @requested_freq: last frequency requested by the sched governor
 *
 * struct gov_data is the per-policy cpufreq_sched-specific data structure. A
 * per-policy instance of it is created when the cpufreq_sched governor receives
 * the CPUFREQ_GOV_START condition and a pointer to it exists in the gov_data
 * member of struct cpufreq_policy.
 *
 * Readers of this data must call down_read(policy->rwsem). Writers must
 * call down_write(policy->rwsem).
 */
struct gov_data {
	ktime_t up_throttle;
	ktime_t down_throttle;
	unsigned int up_throttle_nsec;
	unsigned int down_throttle_nsec;
	struct task_struct *task;
	struct irq_work irq_work;
	unsigned int requested_freq;
};

static void cpufreq_sched_try_driver_target(struct cpufreq_policy *policy,
					    unsigned int freq)
{
	struct gov_data *gd = policy->governor_data;

	/* avoid race with cpufreq_sched_stop */
	if (!down_write_trylock(&policy->rwsem))
		return;

	__cpufreq_driver_target(policy, freq, CPUFREQ_RELATION_L);

	gd->up_throttle = ktime_add_ns(ktime_get(), gd->up_throttle_nsec);
	gd->down_throttle = ktime_add_ns(ktime_get(), gd->down_throttle_nsec);
	up_write(&policy->rwsem);
}

static bool finish_last_request(struct gov_data *gd, unsigned int cur_freq)
{
	ktime_t now = ktime_get();

	ktime_t throttle = gd->requested_freq < cur_freq ?
		gd->down_throttle : gd->up_throttle;

	if (ktime_after(now, throttle))
		return false;

	while (1) {
		int usec_left = ktime_to_ns(ktime_sub(throttle, now));

		usec_left /= NSEC_PER_USEC;
		trace_cpufreq_sched_throttled(usec_left);
		usleep_range(usec_left, usec_left + 100);
		now = ktime_get();
		if (ktime_after(now, throttle))
			return true;
	}
}

/*
 * we pass in struct cpufreq_policy. This is safe because changing out the
 * policy requires a call to __cpufreq_governor(policy, CPUFREQ_GOV_STOP),
 * which tears down all of the data structures and __cpufreq_governor(policy,
 * CPUFREQ_GOV_START) will do a full rebuild, including this kthread with the
 * new policy pointer
 */
static int cpufreq_sched_thread(void *data)
{
	struct sched_param param;
	struct cpufreq_policy *policy;
	struct gov_data *gd;
	unsigned int new_request = 0;
	unsigned int last_request = 0;
	int ret;

	policy = (struct cpufreq_policy *) data;
	gd = policy->governor_data;

	param.sched_priority = 50;
	ret = sched_setscheduler_nocheck(gd->task, SCHED_FIFO, &param);
	if (ret) {
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		do_exit(-EINVAL);
	} else {
		pr_debug("%s: kthread (%d) set to SCHED_FIFO\n",
				__func__, gd->task->pid);
	}

	do {
		new_request = gd->requested_freq;
		if (new_request == last_request) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			/*
			 * if the frequency thread sleeps while waiting to be
			 * unthrottled, start over to check for a newer request
			 */
			if (finish_last_request(gd, policy->cur))
				continue;
			last_request = new_request;
			cpufreq_sched_try_driver_target(policy, new_request);
		}
	} while (!kthread_should_stop());

	return 0;
}

static void cpufreq_sched_irq_work(struct irq_work *irq_work)
{
	struct gov_data *gd;

	gd = container_of(irq_work, struct gov_data, irq_work);
	if (!gd)
		return;

	wake_up_process(gd->task);
}

static void update_fdomain_capacity_request(int cpu)
{
	unsigned int freq_new, index_new, cpu_tmp;
	struct cpufreq_policy *policy;
	struct gov_data *gd;
	unsigned long capacity = 0;

	/*
	 * Avoid grabbing the policy if possible. A test is still
	 * required after locking the CPU's policy to avoid racing
	 * with the governor changing.
	 */
	if (!per_cpu(enabled, cpu))
		return;

	policy = cpufreq_cpu_get(cpu);
	if (IS_ERR_OR_NULL(policy))
		return;

	if (policy->governor != &cpufreq_gov_sched ||
	    !policy->governor_data)
		goto out;

	gd = policy->governor_data;

	/* find max capacity requested by cpus in this policy */
	for_each_cpu(cpu_tmp, policy->cpus) {
		struct sched_capacity_reqs *scr;

		scr = &per_cpu(cpu_sched_capacity_reqs, cpu_tmp);
		capacity = max(capacity, scr->total);
	}

	/* Convert the new maximum capacity request into a cpu frequency */
	freq_new = capacity * policy->max >> SCHED_CAPACITY_SHIFT;
	if (cpufreq_frequency_table_target(policy, policy->freq_table,
					   freq_new, CPUFREQ_RELATION_L,
					   &index_new))
		goto out;
	freq_new = policy->freq_table[index_new].frequency;

	if (freq_new > policy->max)
		freq_new = policy->max;

	if (freq_new < policy->min)
		freq_new = policy->min;

	trace_cpufreq_sched_request_opp(cpu, capacity, freq_new,
					gd->requested_freq);
	if (freq_new == gd->requested_freq)
		goto out;

	gd->requested_freq = freq_new;

	/*
	 * Throttling is not yet supported on platforms with fast cpufreq
	 * drivers.
	 */
	if (cpufreq_driver_slow)
		irq_work_queue_on(&gd->irq_work, cpu);
	else
		cpufreq_sched_try_driver_target(policy, freq_new);

out:
	cpufreq_cpu_put(policy);
}

void update_cpu_capacity_request(int cpu, bool request)
{
	unsigned long new_capacity;
	struct sched_capacity_reqs *scr;

	/* The rq lock serializes access to the CPU's sched_capacity_reqs. */
	lockdep_assert_held(&cpu_rq(cpu)->lock);

	scr = &per_cpu(cpu_sched_capacity_reqs, cpu);

	new_capacity = scr->cfs + scr->rt;
	new_capacity = new_capacity * capacity_margin
		/ SCHED_CAPACITY_SCALE;
	new_capacity += scr->dl;

	if (new_capacity == scr->total)
		return;

	trace_cpufreq_sched_update_capacity(cpu, request, scr, new_capacity);

	scr->total = new_capacity;
	if (request)
		update_fdomain_capacity_request(cpu);
}

static inline void set_sched_freq(void)
{
	static_key_slow_inc(&__sched_freq);
}

static inline void clear_sched_freq(void)
{
	static_key_slow_dec(&__sched_freq);
}

static struct attribute_group sched_attr_group_gov_pol;
static struct attribute_group *get_sysfs_attr(void)
{
	return &sched_attr_group_gov_pol;
}

static int cpufreq_sched_policy_init(struct cpufreq_policy *policy)
{
	struct gov_data *gd;
	int cpu;
	int rc;

	for_each_cpu(cpu, policy->cpus)
		memset(&per_cpu(cpu_sched_capacity_reqs, cpu), 0,
		       sizeof(struct sched_capacity_reqs));

	gd = kzalloc(sizeof(*gd), GFP_KERNEL);
	if (!gd)
		return -ENOMEM;

	gd->up_throttle_nsec = policy->cpuinfo.transition_latency ?
			    policy->cpuinfo.transition_latency :
			    THROTTLE_UP_NSEC;
	gd->down_throttle_nsec = THROTTLE_DOWN_NSEC;
	pr_debug("%s: throttle threshold = %u [ns]\n",
		  __func__, gd->up_throttle_nsec);

	rc = sysfs_create_group(get_governor_parent_kobj(policy), get_sysfs_attr());
	if (rc) {
		pr_err("%s: couldn't create sysfs attributes: %d\n", __func__, rc);
		goto err;
	}

	if (cpufreq_driver_is_slow()) {
		cpufreq_driver_slow = true;
		gd->task = kthread_create(cpufreq_sched_thread, policy,
					  "kschedfreq:%d",
					  cpumask_first(policy->related_cpus));
		if (IS_ERR_OR_NULL(gd->task)) {
			pr_err("%s: failed to create kschedfreq thread\n",
			       __func__);
			goto err;
		}
		get_task_struct(gd->task);
		kthread_bind_mask(gd->task, policy->related_cpus);
		wake_up_process(gd->task);
		init_irq_work(&gd->irq_work, cpufreq_sched_irq_work);
	}

	policy->governor_data = gd;
	set_sched_freq();

	return 0;

err:
	kfree(gd);
	return -ENOMEM;
}

static int cpufreq_sched_policy_exit(struct cpufreq_policy *policy)
{
	struct gov_data *gd = policy->governor_data;

	clear_sched_freq();
	if (cpufreq_driver_slow) {
		kthread_stop(gd->task);
		put_task_struct(gd->task);
	}

	sysfs_remove_group(get_governor_parent_kobj(policy), get_sysfs_attr());

	policy->governor_data = NULL;

	kfree(gd);
	return 0;
}

static int cpufreq_sched_start(struct cpufreq_policy *policy)
{
	int cpu;

	for_each_cpu(cpu, policy->cpus)
		per_cpu(enabled, cpu) = 1;

	return 0;
}

static void cpufreq_sched_limits(struct cpufreq_policy *policy)
{
	unsigned int clamp_freq;
	struct gov_data *gd = policy->governor_data;;

	pr_debug("limit event for cpu %u: %u - %u kHz, currently %u kHz\n",
		policy->cpu, policy->min, policy->max,
		policy->cur);

	clamp_freq = clamp(gd->requested_freq, policy->min, policy->max);

	if (policy->cur != clamp_freq)
		__cpufreq_driver_target(policy, clamp_freq, CPUFREQ_RELATION_L);
}

static int cpufreq_sched_stop(struct cpufreq_policy *policy)
{
	int cpu;

	for_each_cpu(cpu, policy->cpus)
		per_cpu(enabled, cpu) = 0;

	return 0;
}

static int cpufreq_sched_setup(struct cpufreq_policy *policy,
			       unsigned int event)
{
	switch (event) {
	case CPUFREQ_GOV_POLICY_INIT:
		return cpufreq_sched_policy_init(policy);
	case CPUFREQ_GOV_POLICY_EXIT:
		return cpufreq_sched_policy_exit(policy);
	case CPUFREQ_GOV_START:
		return cpufreq_sched_start(policy);
	case CPUFREQ_GOV_STOP:
		return cpufreq_sched_stop(policy);
	case CPUFREQ_GOV_LIMITS:
		cpufreq_sched_limits(policy);
		break;
	}
	return 0;
}

/* Tunables */
static ssize_t show_up_throttle_nsec(struct gov_data *gd, char *buf)
{
	return sprintf(buf, "%u\n", gd->up_throttle_nsec);
}

static ssize_t store_up_throttle_nsec(struct gov_data *gd,
		const char *buf, size_t count)
{
	int ret;
	long unsigned int val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	gd->up_throttle_nsec = val;
	return count;
}

static ssize_t show_down_throttle_nsec(struct gov_data *gd, char *buf)
{
	return sprintf(buf, "%u\n", gd->down_throttle_nsec);
}

static ssize_t store_down_throttle_nsec(struct gov_data *gd,
		const char *buf, size_t count)
{
	int ret;
	long unsigned int val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	gd->down_throttle_nsec = val;
	return count;
}

/*
 * Create show/store routines
 * - sys: One governor instance for complete SYSTEM
 * - pol: One governor instance per struct cpufreq_policy
 */
#define show_gov_pol_sys(file_name)					\
static ssize_t show_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	return show_##file_name(policy->governor_data, buf);		\
}

#define store_gov_pol_sys(file_name)					\
static ssize_t store_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	return store_##file_name(policy->governor_data, buf, count);	\
}

#define gov_pol_attr_rw(_name)						\
	static struct freq_attr _name##_gov_pol =				\
	__ATTR(_name, 0644, show_##_name##_gov_pol, store_##_name##_gov_pol)

#define show_store_gov_pol_sys(file_name)				\
	show_gov_pol_sys(file_name);						\
	store_gov_pol_sys(file_name)
#define tunable_handlers(file_name) \
	show_gov_pol_sys(file_name); \
	store_gov_pol_sys(file_name); \
	gov_pol_attr_rw(file_name)

tunable_handlers(down_throttle_nsec);
tunable_handlers(up_throttle_nsec);

/* Per policy governor instance */
static struct attribute *sched_attributes_gov_pol[] = {
	&up_throttle_nsec_gov_pol.attr,
	&down_throttle_nsec_gov_pol.attr,
	NULL,
};

static struct attribute_group sched_attr_group_gov_pol = {
	.attrs = sched_attributes_gov_pol,
	.name = "sched",
};

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHED
static
#endif
struct cpufreq_governor cpufreq_gov_sched = {
	.name			= "sched",
	.governor		= cpufreq_sched_setup,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_sched_init(void)
{
	int cpu;

	for_each_cpu(cpu, cpu_possible_mask)
		per_cpu(enabled, cpu) = 0;
	return cpufreq_register_governor(&cpufreq_gov_sched);
}

/* Try to make this the default governor */
fs_initcall(cpufreq_sched_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015,2017,2019-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "input-boost: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <linux/pm_qos.h>

#include "walt.h"

#define input_boost_attr_rw(_name)		\
static struct kobj_attribute _name##_attr =	\
__ATTR(_name, 0644, show_##_name, store_##_name)

#define show_one(file_name)			\
static ssize_t show_##file_name			\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
{								\
	return scnprintf(buf, PAGE_SIZE, "%u\n", file_name);	\
}

#define store_one(file_name)					\
static ssize_t store_##file_name				\
(struct kobject *kobj, struct kobj_attribute *attr,		\
const char *buf, size_t count)					\
{								\
								\
	sscanf(buf, "%u", &file_name);				\
	return count;						\
}

struct cpu_sync {
	int		cpu;
	unsigned int	input_boost_min;
	unsigned int	input_boost_freq;
};

static DEFINE_PER_CPU(struct cpu_sync, sync_info);
static struct workqueue_struct *input_boost_wq;

static struct work_struct input_boost_work;

static bool sched_boost_active;

static struct delayed_work input_boost_rem;
static u64 last_input_time;
#define MIN_INPUT_INTERVAL (150 * USEC_PER_MSEC)

static DEFINE_PER_CPU(struct freq_qos_request, qos_req);

static void boost_adjust_notify(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct cpu_sync *s = &per_cpu(sync_info, cpu);
	unsigned int ib_min = s->input_boost_min;
	struct freq_qos_request *req = &per_cpu(qos_req, cpu);
	int ret;

	pr_debug("CPU%u policy min before boost: %u kHz\n",
			 cpu, policy->min);
	pr_debug("CPU%u boost min: %u kHz\n", cpu, ib_min);

	ret = freq_qos_update_request(req, ib_min);

	if (ret < 0)
		pr_err("Failed to update freq constraint in boost_adjust: %d\n",
								ib_min);

	pr_debug("CPU%u policy min after boost: %u kHz\n", cpu, policy->min);
}

static void update_policy_online(void)
{
	unsigned int i;
	struct cpufreq_policy *policy;
	struct cpumask online_cpus;

	/* Re-evaluate policy to trigger adjust notifier for online CPUs */
	get_online_cpus();
	online_cpus = *cpu_online_mask;
	for_each_cpu(i, &online_cpus) {
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
							__func__, i);
			return;
		}

		cpumask_andnot(&online_cpus, &online_cpus,
						policy->related_cpus);
		boost_adjust_notify(policy);
	}
	put_online_cpus();
}

static void do_input_boost_rem(struct work_struct *work)
{
	unsigned int i, ret;
	struct cpu_sync *i_sync_info;

	/* Reset the input_boost_min for all CPUs in the system */
	pr_debug("Resetting input boost min for all CPUs\n");
	for_each_possible_cpu(i) {
		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->input_boost_min = 0;
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	if (sched_boost_active) {
		ret = sched_set_boost(0);
		if (!ret)
			pr_err("input-boost: sched boost disable failed\n");
		sched_boost_active = false;
	}
}

static void do_input_boost(struct work_struct *work)
{
	unsigned int i, ret;
	struct cpu_sync *i_sync_info;

	cancel_delayed_work_sync(&input_boost_rem);
	if (sched_boost_active) {
		sched_set_boost(0);
		sched_boost_active = false;
	}

	/* Set the input_boost_min for all CPUs in the system */
	pr_debug("Setting input boost min for all CPUs\n");
	for (i = 0; i < 8; i++) {
		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->input_boost_min = sysctl_input_boost_freq[i];
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	/* Enable scheduler boost to migrate tasks to big cluster */
	if (sysctl_sched_boost_on_input > 0) {
		ret = sched_set_boost(sysctl_sched_boost_on_input);
		if (ret)
			pr_err("input-boost: sched boost enable failed\n");
		else
			sched_boost_active = true;
	}

	queue_delayed_work(input_boost_wq, &input_boost_rem,
					msecs_to_jiffies(sysctl_input_boost_ms));
}

static void inputboost_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	u64 now;
	int cpu;
	int enabled = 0;

	for_each_possible_cpu(cpu) {
		if (sysctl_input_boost_freq[cpu] > 0) {
			enabled = 1;
			break;
		}
	}
	if (!enabled)
		return;

	now = ktime_to_us(ktime_get());
	if (now - last_input_time < MIN_INPUT_INTERVAL)
		return;

	if (work_pending(&input_boost_work))
		return;

	queue_work(input_boost_wq, &input_boost_work);
	last_input_time = ktime_to_us(ktime_get());
}

static int inputboost_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void inputboost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id inputboost_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y)
		},
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y)
		},
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler inputboost_input_handler = {
	.event		= inputboost_input_event,
	.connect	= inputboost_input_connect,
	.disconnect	= inputboost_input_disconnect,
	.name		= "input-boost",
	.id_table	= inputboost_ids,
};

struct kobject *input_boost_kobj;
int input_boost_init(void)
{
	int cpu, ret;
	struct cpu_sync *s;
	struct cpufreq_policy *policy;
	struct freq_qos_request *req;

	input_boost_wq = alloc_workqueue("inputboost_wq", WQ_HIGHPRI, 0);
	if (!input_boost_wq)
		return -EFAULT;

	INIT_WORK(&input_boost_work, do_input_boost);
	INIT_DELAYED_WORK(&input_boost_rem, do_input_boost_rem);

	for_each_possible_cpu(cpu) {
		s = &per_cpu(sync_info, cpu);
		s->cpu = cpu;
		req = &per_cpu(qos_req, cpu);
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
							__func__, cpu);
			return -ESRCH;
		}

		ret = freq_qos_add_request(&policy->constraints, req,
						FREQ_QOS_MIN, policy->min);
		if (ret < 0) {
			pr_err("%s: Failed to add freq constraint (%d)\n",
							__func__, ret);
			return ret;
		}
	}

	ret = input_register_handler(&inputboost_input_handler);
	return 0;
}

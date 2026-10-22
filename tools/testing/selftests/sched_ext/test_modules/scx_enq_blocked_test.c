// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Kernel mutex fixture for the sched_ext SCX_OPS_ENQ_BLOCKED selftest.
 */

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "../enq_blocked.h"

#define DONOR_WAIT_TIMEOUT	msecs_to_jiffies(2000)
#define ATTACH_WAIT_TIMEOUT	msecs_to_jiffies(10000)
#define MUTEX_HOLD_TIME		msecs_to_jiffies(200)

static DEFINE_MUTEX(test_mutex);
static DEFINE_SPINLOCK(donor_lock);
static struct task_struct *donor_task;
static atomic_t owner_ready = ATOMIC_INIT(0);
static atomic_t donor_started = ATOMIC_INIT(0);
static atomic_t attach_pending = ATOMIC_INIT(0);
static atomic_t attach_done = ATOMIC_INIT(0);
static atomic64_t hold_time_ns = ATOMIC64_INIT(0);
static atomic64_t wait_time_ns = ATOMIC64_INIT(0);
static atomic64_t nr_holds = ATOMIC64_INIT(0);
static atomic64_t nr_waits = ATOMIC64_INIT(0);

static long run_owner(void)
{
	unsigned long timeout;
	u64 start_ns;
	long ret = 0;

	atomic_set(&donor_started, 0);
	mutex_lock(&test_mutex);
	start_ns = ktime_get_ns();
	atomic_set(&owner_ready, 1);

	timeout = jiffies + DONOR_WAIT_TIMEOUT;
	while (!atomic_read(&donor_started)) {
		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}
	if (atomic_xchg(&attach_pending, 0)) {
		timeout = jiffies + ATTACH_WAIT_TIMEOUT;
		while (!atomic_read(&attach_done)) {
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		}
	}

	/* Keep yielding while the donor blocks on test_mutex. */
	timeout = jiffies + MUTEX_HOLD_TIME;
	while (time_before(jiffies, timeout))
		cond_resched();

out:
	atomic_set(&owner_ready, 0);
	atomic64_add(ktime_get_ns() - start_ns, &hold_time_ns);
	atomic64_inc(&nr_holds);
	mutex_unlock(&test_mutex);
	return ret;
}

static long run_donor(void)
{
	unsigned long flags;
	u64 start_ns;

	if (!atomic_read(&owner_ready))
		return -EAGAIN;

	get_task_struct(current);
	spin_lock_irqsave(&donor_lock, flags);
	WARN_ON_ONCE(donor_task);
	donor_task = current;
	spin_unlock_irqrestore(&donor_lock, flags);

	atomic_set(&donor_started, 1);
	start_ns = ktime_get_ns();
	mutex_lock(&test_mutex);

	spin_lock_irqsave(&donor_lock, flags);
	donor_task = NULL;
	spin_unlock_irqrestore(&donor_lock, flags);
	put_task_struct(current);

	atomic64_add(ktime_get_ns() - start_ns, &wait_time_ns);
	atomic64_inc(&nr_waits);
	mutex_unlock(&test_mutex);
	return 0;
}

static long get_donor_state(void)
{
	struct task_struct *task;
	unsigned long flags;
	long state = 0;

	spin_lock_irqsave(&donor_lock, flags);
	task = donor_task;
	if (task)
		get_task_struct(task);
	spin_unlock_irqrestore(&donor_lock, flags);
	if (!task)
		return -ENOENT;

	if (READ_ONCE(task->__state) != TASK_RUNNING)
		state |= ENQ_BLOCKED_DONOR_SLEEPING;
	if (READ_ONCE(task->on_rq))
		state |= ENQ_BLOCKED_DONOR_ON_RQ;
	put_task_struct(task);
	return state;
}

static void reset_stats(void)
{
	atomic64_set(&hold_time_ns, 0);
	atomic64_set(&wait_time_ns, 0);
	atomic64_set(&nr_holds, 0);
	atomic64_set(&nr_waits, 0);
}

static long get_stats(unsigned long arg)
{
	struct enq_blocked_stats stats = {
		.hold_time_ns = atomic64_read(&hold_time_ns),
		.wait_time_ns = atomic64_read(&wait_time_ns),
		.nr_holds = atomic64_read(&nr_holds),
		.nr_waits = atomic64_read(&nr_waits),
	};

	return copy_to_user((void __user *)arg, &stats, sizeof(stats)) ?
		-EFAULT : 0;
}

static long enq_blocked_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	switch (cmd) {
	case ENQ_BLOCKED_IOCTL_OWNER:
		return run_owner();
	case ENQ_BLOCKED_IOCTL_DONOR:
		return run_donor();
	case ENQ_BLOCKED_IOCTL_RESET_STATS:
		reset_stats();
		return 0;
	case ENQ_BLOCKED_IOCTL_GET_STATS:
		return get_stats(arg);
	case ENQ_BLOCKED_IOCTL_PREP_ATTACH:
		atomic_set(&attach_done, 0);
		atomic_set(&attach_pending, 1);
		return 0;
	case ENQ_BLOCKED_IOCTL_ATTACH_DONE:
		atomic_set(&attach_done, 1);
		return 0;
	case ENQ_BLOCKED_IOCTL_DONOR_STATE:
		return get_donor_state();
	case ENQ_BLOCKED_IOCTL_PROXY_SUPPORTED:
		return IS_ENABLED(CONFIG_SCHED_PROXY_EXEC);
	default:
		return -EINVAL;
	}
}

static const struct file_operations enq_blocked_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl		= enq_blocked_ioctl,
};

static struct miscdevice enq_blocked_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "scx_enq_blocked",
	.fops	= &enq_blocked_fops,
	.mode	= 0600,
};

module_misc_device(enq_blocked_device);
MODULE_AUTHOR("Andrea Righi <arighi@nvidia.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sched_ext blocked donor test module");

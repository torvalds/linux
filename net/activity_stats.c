/* net/activity_stats.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Mike Chan (mike@android.com)
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <net/net_namespace.h>

/*
 * Track transmission rates in buckets (power of 2).
 * 1,2,4,8...512 seconds.
 *
 * Buckets represent the count of network transmissions at least
 * N seconds apart, where N is 1 << bucket index.
 */
#define BUCKET_MAX 10

/* Track network activity frequency */
static unsigned long activity_stats[BUCKET_MAX];
static ktime_t last_transmit;
static ktime_t suspend_time;
static DEFINE_SPINLOCK(activity_lock);

void activity_stats_update(void)
{
	int i;
	unsigned long flags;
	ktime_t now;
	s64 delta;

	spin_lock_irqsave(&activity_lock, flags);
	now = ktime_get();
	delta = ktime_to_ns(ktime_sub(now, last_transmit));

	for (i = BUCKET_MAX - 1; i >= 0; i--) {
		/*
		 * Check if the time delta between network activity is within the
		 * minimum bucket range.
		 */
		if (delta < (1000000000ULL << i))
			continue;

		activity_stats[i]++;
		last_transmit = now;
		break;
	}
	spin_unlock_irqrestore(&activity_lock, flags);
}

static int activity_stats_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "Min Bucket(sec) Count\n");

	for (i = 0; i < BUCKET_MAX; i++) {
		seq_printf(m, "%15d %lu\n", 1 << i, activity_stats[i]);
		if (seq_has_overflowed(m))
			return -ENOSPC;
	}

	return 0;
}

static int activity_stats_notifier(struct notifier_block *nb,
					unsigned long event, void *dummy)
{
	switch (event) {
		case PM_SUSPEND_PREPARE:
			suspend_time = ktime_get_real();
			break;

		case PM_POST_SUSPEND:
			suspend_time = ktime_sub(ktime_get_real(), suspend_time);
			last_transmit = ktime_sub(last_transmit, suspend_time);
	}

	return 0;
}

static int activity_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, activity_stats_show, PDE_DATA(inode));
}

static const struct file_operations activity_stats_fops = {
	.open		= activity_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct notifier_block activity_stats_notifier_block = {
	.notifier_call = activity_stats_notifier,
};

static int  __init activity_stats_init(void)
{
	proc_create("activity", S_IRUGO,
		    init_net.proc_net_stat, &activity_stats_fops);
	return register_pm_notifier(&activity_stats_notifier_block);
}

subsys_initcall(activity_stats_init);


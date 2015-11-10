/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: Waiman Long <waiman.long@hpe.com>
 */

/*
 * When queued spinlock statistical counters are enabled, the following
 * debugfs files will be created for reporting the counter values:
 *
 * <debugfs>/qlockstat/
 *   pv_hash_hops	- average # of hops per hashing operation
 *   pv_kick_unlock	- # of vCPU kicks issued at unlock time
 *   pv_kick_wake	- # of vCPU kicks used for computing pv_latency_wake
 *   pv_latency_kick	- average latency (ns) of vCPU kick operation
 *   pv_latency_wake	- average latency (ns) from vCPU kick to wakeup
 *   pv_spurious_wakeup	- # of spurious wakeups
 *   pv_wait_again	- # of vCPU wait's that happened after a vCPU kick
 *   pv_wait_head	- # of vCPU wait's at the queue head
 *   pv_wait_node	- # of vCPU wait's at a non-head queue node
 *
 * Writing to the "reset_counters" file will reset all the above counter
 * values.
 *
 * These statistical counters are implemented as per-cpu variables which are
 * summed and computed whenever the corresponding debugfs files are read. This
 * minimizes added overhead making the counters usable even in a production
 * environment.
 *
 * There may be slight difference between pv_kick_wake and pv_kick_unlock.
 */
enum qlock_stats {
	qstat_pv_hash_hops,
	qstat_pv_kick_unlock,
	qstat_pv_kick_wake,
	qstat_pv_latency_kick,
	qstat_pv_latency_wake,
	qstat_pv_spurious_wakeup,
	qstat_pv_wait_again,
	qstat_pv_wait_head,
	qstat_pv_wait_node,
	qstat_num,	/* Total number of statistical counters */
	qstat_reset_cnts = qstat_num,
};

#ifdef CONFIG_QUEUED_LOCK_STAT
/*
 * Collect pvqspinlock statistics
 */
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/fs.h>

static const char * const qstat_names[qstat_num + 1] = {
	[qstat_pv_hash_hops]	   = "pv_hash_hops",
	[qstat_pv_kick_unlock]     = "pv_kick_unlock",
	[qstat_pv_kick_wake]       = "pv_kick_wake",
	[qstat_pv_spurious_wakeup] = "pv_spurious_wakeup",
	[qstat_pv_latency_kick]	   = "pv_latency_kick",
	[qstat_pv_latency_wake]    = "pv_latency_wake",
	[qstat_pv_wait_again]      = "pv_wait_again",
	[qstat_pv_wait_head]       = "pv_wait_head",
	[qstat_pv_wait_node]       = "pv_wait_node",
	[qstat_reset_cnts]         = "reset_counters",
};

/*
 * Per-cpu counters
 */
static DEFINE_PER_CPU(unsigned long, qstats[qstat_num]);
static DEFINE_PER_CPU(u64, pv_kick_time);

/*
 * Function to read and return the qlock statistical counter values
 *
 * The following counters are handled specially:
 * 1. qstat_pv_latency_kick
 *    Average kick latency (ns) = pv_latency_kick/pv_kick_unlock
 * 2. qstat_pv_latency_wake
 *    Average wake latency (ns) = pv_latency_wake/pv_kick_wake
 * 3. qstat_pv_hash_hops
 *    Average hops/hash = pv_hash_hops/pv_kick_unlock
 */
static ssize_t qstat_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	char buf[64];
	int cpu, counter, len;
	u64 stat = 0, kicks = 0;

	/*
	 * Get the counter ID stored in file->f_inode->i_private
	 */
	if (!file->f_inode) {
		WARN_ON_ONCE(1);
		return -EBADF;
	}
	counter = (long)(file->f_inode->i_private);

	if (counter >= qstat_num)
		return -EBADF;

	for_each_possible_cpu(cpu) {
		stat += per_cpu(qstats[counter], cpu);
		/*
		 * Need to sum additional counter for some of them
		 */
		switch (counter) {

		case qstat_pv_latency_kick:
		case qstat_pv_hash_hops:
			kicks += per_cpu(qstats[qstat_pv_kick_unlock], cpu);
			break;

		case qstat_pv_latency_wake:
			kicks += per_cpu(qstats[qstat_pv_kick_wake], cpu);
			break;
		}
	}

	if (counter == qstat_pv_hash_hops) {
		u64 frac;

		frac = 100ULL * do_div(stat, kicks);
		frac = DIV_ROUND_CLOSEST_ULL(frac, kicks);

		/*
		 * Return a X.XX decimal number
		 */
		len = snprintf(buf, sizeof(buf) - 1, "%llu.%02llu\n", stat, frac);
	} else {
		/*
		 * Round to the nearest ns
		 */
		if ((counter == qstat_pv_latency_kick) ||
		    (counter == qstat_pv_latency_wake)) {
			stat = 0;
			if (kicks)
				stat = DIV_ROUND_CLOSEST_ULL(stat, kicks);
		}
		len = snprintf(buf, sizeof(buf) - 1, "%llu\n", stat);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

/*
 * Function to handle write request
 *
 * When counter = reset_cnts, reset all the counter values.
 * Since the counter updates aren't atomic, the resetting is done twice
 * to make sure that the counters are very likely to be all cleared.
 */
static ssize_t qstat_write(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	int cpu;

	/*
	 * Get the counter ID stored in file->f_inode->i_private
	 */
	if (!file->f_inode) {
		WARN_ON_ONCE(1);
		return -EBADF;
	}
	if ((long)(file->f_inode->i_private) != qstat_reset_cnts)
		return count;

	for_each_possible_cpu(cpu) {
		int i;
		unsigned long *ptr = per_cpu_ptr(qstats, cpu);

		for (i = 0 ; i < qstat_num; i++)
			WRITE_ONCE(ptr[i], 0);
		for (i = 0 ; i < qstat_num; i++)
			WRITE_ONCE(ptr[i], 0);
	}
	return count;
}

/*
 * Debugfs data structures
 */
static const struct file_operations fops_qstat = {
	.read = qstat_read,
	.write = qstat_write,
	.llseek = default_llseek,
};

/*
 * Initialize debugfs for the qspinlock statistical counters
 */
static int __init init_qspinlock_stat(void)
{
	struct dentry *d_qstat = debugfs_create_dir("qlockstat", NULL);
	int i;

	if (!d_qstat) {
		pr_warn("Could not create 'qlockstat' debugfs directory\n");
		return 0;
	}

	/*
	 * Create the debugfs files
	 *
	 * As reading from and writing to the stat files can be slow, only
	 * root is allowed to do the read/write to limit impact to system
	 * performance.
	 */
	for (i = 0; i < qstat_num; i++)
		debugfs_create_file(qstat_names[i], 0400, d_qstat,
				   (void *)(long)i, &fops_qstat);

	debugfs_create_file(qstat_names[qstat_reset_cnts], 0200, d_qstat,
			   (void *)(long)qstat_reset_cnts, &fops_qstat);
	return 0;
}
fs_initcall(init_qspinlock_stat);

/*
 * Increment the PV qspinlock statistical counters
 */
static inline void qstat_inc(enum qlock_stats stat, bool cond)
{
	if (cond)
		this_cpu_inc(qstats[stat]);
}

/*
 * PV hash hop count
 */
static inline void qstat_hop(int hopcnt)
{
	this_cpu_add(qstats[qstat_pv_hash_hops], hopcnt);
}

/*
 * Replacement function for pv_kick()
 */
static inline void __pv_kick(int cpu)
{
	u64 start = sched_clock();

	per_cpu(pv_kick_time, cpu) = start;
	pv_kick(cpu);
	this_cpu_add(qstats[qstat_pv_latency_kick], sched_clock() - start);
}

/*
 * Replacement function for pv_wait()
 */
static inline void __pv_wait(u8 *ptr, u8 val)
{
	u64 *pkick_time = this_cpu_ptr(&pv_kick_time);

	*pkick_time = 0;
	pv_wait(ptr, val);
	if (*pkick_time) {
		this_cpu_add(qstats[qstat_pv_latency_wake],
			     sched_clock() - *pkick_time);
		qstat_inc(qstat_pv_kick_wake, true);
	}
}

#define pv_kick(c)	__pv_kick(c)
#define pv_wait(p, v)	__pv_wait(p, v)

#else /* CONFIG_QUEUED_LOCK_STAT */

static inline void qstat_inc(enum qlock_stats stat, bool cond)	{ }
static inline void qstat_hop(int hopcnt)			{ }

#endif /* CONFIG_QUEUED_LOCK_STAT */

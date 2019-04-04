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
 *   pv_lock_stealing	- # of lock stealing operations
 *   pv_spurious_wakeup	- # of spurious wakeups in non-head vCPUs
 *   pv_wait_again	- # of wait's after a queue head vCPU kick
 *   pv_wait_early	- # of early vCPU wait's
 *   pv_wait_head	- # of vCPU wait's at the queue head
 *   pv_wait_node	- # of vCPU wait's at a non-head queue node
 *   lock_pending	- # of locking operations via pending code
 *   lock_slowpath	- # of locking operations via MCS lock queue
 *   lock_use_node2	- # of locking operations that use 2nd per-CPU node
 *   lock_use_node3	- # of locking operations that use 3rd per-CPU node
 *   lock_use_node4	- # of locking operations that use 4th per-CPU node
 *   lock_no_node	- # of locking operations without using per-CPU node
 *
 * Subtracting lock_use_node[234] from lock_slowpath will give you
 * lock_use_node1.
 *
 * Writing to the special ".reset_counts" file will reset all the above
 * counter values.
 *
 * These statistical counters are implemented as per-cpu variables which are
 * summed and computed whenever the corresponding debugfs files are read. This
 * minimizes added overhead making the counters usable even in a production
 * environment.
 *
 * There may be slight difference between pv_kick_wake and pv_kick_unlock.
 */
#include "lock_events.h"

#ifdef CONFIG_QUEUED_LOCK_STAT
/*
 * Collect pvqspinlock statistics
 */
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/fs.h>

#define EVENT_COUNT(ev)	lockevents[LOCKEVENT_ ## ev]

#undef  LOCK_EVENT
#define LOCK_EVENT(name)	[LOCKEVENT_ ## name] = #name,

static const char * const lockevent_names[lockevent_num + 1] = {

#include "lock_events_list.h"

	[LOCKEVENT_reset_cnts] = ".reset_counts",
};

/*
 * Per-cpu counters
 */
DEFINE_PER_CPU(unsigned long, lockevents[lockevent_num]);
static DEFINE_PER_CPU(u64, pv_kick_time);

/*
 * Function to read and return the qlock statistical counter values
 *
 * The following counters are handled specially:
 * 1. pv_latency_kick
 *    Average kick latency (ns) = pv_latency_kick/pv_kick_unlock
 * 2. pv_latency_wake
 *    Average wake latency (ns) = pv_latency_wake/pv_kick_wake
 * 3. pv_hash_hops
 *    Average hops/hash = pv_hash_hops/pv_kick_unlock
 */
static ssize_t lockevent_read(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char buf[64];
	int cpu, id, len;
	u64 sum = 0, kicks = 0;

	/*
	 * Get the counter ID stored in file->f_inode->i_private
	 */
	id = (long)file_inode(file)->i_private;

	if (id >= lockevent_num)
		return -EBADF;

	for_each_possible_cpu(cpu) {
		sum += per_cpu(lockevents[id], cpu);
		/*
		 * Need to sum additional counters for some of them
		 */
		switch (id) {

		case LOCKEVENT_pv_latency_kick:
		case LOCKEVENT_pv_hash_hops:
			kicks += per_cpu(EVENT_COUNT(pv_kick_unlock), cpu);
			break;

		case LOCKEVENT_pv_latency_wake:
			kicks += per_cpu(EVENT_COUNT(pv_kick_wake), cpu);
			break;
		}
	}

	if (id == LOCKEVENT_pv_hash_hops) {
		u64 frac = 0;

		if (kicks) {
			frac = 100ULL * do_div(sum, kicks);
			frac = DIV_ROUND_CLOSEST_ULL(frac, kicks);
		}

		/*
		 * Return a X.XX decimal number
		 */
		len = snprintf(buf, sizeof(buf) - 1, "%llu.%02llu\n",
			       sum, frac);
	} else {
		/*
		 * Round to the nearest ns
		 */
		if ((id == LOCKEVENT_pv_latency_kick) ||
		    (id == LOCKEVENT_pv_latency_wake)) {
			if (kicks)
				sum = DIV_ROUND_CLOSEST_ULL(sum, kicks);
		}
		len = snprintf(buf, sizeof(buf) - 1, "%llu\n", sum);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

/*
 * Function to handle write request
 *
 * When id = .reset_cnts, reset all the counter values.
 */
static ssize_t lockevent_write(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	int cpu;

	/*
	 * Get the counter ID stored in file->f_inode->i_private
	 */
	if ((long)file_inode(file)->i_private != LOCKEVENT_reset_cnts)
		return count;

	for_each_possible_cpu(cpu) {
		int i;
		unsigned long *ptr = per_cpu_ptr(lockevents, cpu);

		for (i = 0 ; i < lockevent_num; i++)
			WRITE_ONCE(ptr[i], 0);
	}
	return count;
}

/*
 * Debugfs data structures
 */
static const struct file_operations fops_lockevent = {
	.read = lockevent_read,
	.write = lockevent_write,
	.llseek = default_llseek,
};

/*
 * Initialize debugfs for the qspinlock statistical counters
 */
static int __init init_qspinlock_stat(void)
{
	struct dentry *d_counts = debugfs_create_dir("qlockstat", NULL);
	int i;

	if (!d_counts)
		goto out;

	/*
	 * Create the debugfs files
	 *
	 * As reading from and writing to the stat files can be slow, only
	 * root is allowed to do the read/write to limit impact to system
	 * performance.
	 */
	for (i = 0; i < lockevent_num; i++)
		if (!debugfs_create_file(lockevent_names[i], 0400, d_counts,
					 (void *)(long)i, &fops_lockevent))
			goto fail_undo;

	if (!debugfs_create_file(lockevent_names[LOCKEVENT_reset_cnts], 0200,
				 d_counts, (void *)(long)LOCKEVENT_reset_cnts,
				 &fops_lockevent))
		goto fail_undo;

	return 0;
fail_undo:
	debugfs_remove_recursive(d_counts);
out:
	pr_warn("Could not create 'qlockstat' debugfs entries\n");
	return -ENOMEM;
}
fs_initcall(init_qspinlock_stat);

/*
 * PV hash hop count
 */
static inline void lockevent_pv_hop(int hopcnt)
{
	this_cpu_add(EVENT_COUNT(pv_hash_hops), hopcnt);
}

/*
 * Replacement function for pv_kick()
 */
static inline void __pv_kick(int cpu)
{
	u64 start = sched_clock();

	per_cpu(pv_kick_time, cpu) = start;
	pv_kick(cpu);
	this_cpu_add(EVENT_COUNT(pv_latency_kick), sched_clock() - start);
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
		this_cpu_add(EVENT_COUNT(pv_latency_wake),
			     sched_clock() - *pkick_time);
		lockevent_inc(pv_kick_wake);
	}
}

#define pv_kick(c)	__pv_kick(c)
#define pv_wait(p, v)	__pv_wait(p, v)

#else /* CONFIG_QUEUED_LOCK_STAT */

static inline void lockevent_pv_hop(int hopcnt)	{ }

#endif /* CONFIG_QUEUED_LOCK_STAT */

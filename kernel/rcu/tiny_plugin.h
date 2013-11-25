/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition
 * Internal non-public definitions that provide either classic
 * or preemptible semantics.
 *
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (c) 2010 Linaro
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	struct rcu_head *rcucblist;	/* List of pending callbacks (CBs). */
	struct rcu_head **donetail;	/* ->next pointer of last "done" CB. */
	struct rcu_head **curtail;	/* ->next pointer of last CB. */
	RCU_TRACE(long qlen);		/* Number of pending CBs. */
	RCU_TRACE(unsigned long gp_start); /* Start time for stalls. */
	RCU_TRACE(unsigned long ticks_this_gp); /* Statistic for stalls. */
	RCU_TRACE(unsigned long jiffies_stall); /* Jiffies at next stall. */
	RCU_TRACE(const char *name);	/* Name of RCU type. */
};

/* Definition for rcupdate control block. */
static struct rcu_ctrlblk rcu_sched_ctrlblk = {
	.donetail	= &rcu_sched_ctrlblk.rcucblist,
	.curtail	= &rcu_sched_ctrlblk.rcucblist,
	RCU_TRACE(.name = "rcu_sched")
};

static struct rcu_ctrlblk rcu_bh_ctrlblk = {
	.donetail	= &rcu_bh_ctrlblk.rcucblist,
	.curtail	= &rcu_bh_ctrlblk.rcucblist,
	RCU_TRACE(.name = "rcu_bh")
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#include <linux/kernel_stat.h>

int rcu_scheduler_active __read_mostly;
EXPORT_SYMBOL_GPL(rcu_scheduler_active);

/*
 * During boot, we forgive RCU lockdep issues.  After this function is
 * invoked, we start taking RCU lockdep issues seriously.
 */
void __init rcu_scheduler_starting(void)
{
	WARN_ON(nr_context_switches() > 0);
	rcu_scheduler_active = 1;
}

#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_RCU_TRACE

static void rcu_trace_sub_qlen(struct rcu_ctrlblk *rcp, int n)
{
	unsigned long flags;

	local_irq_save(flags);
	rcp->qlen -= n;
	local_irq_restore(flags);
}

/*
 * Dump statistics for TINY_RCU, such as they are.
 */
static int show_tiny_stats(struct seq_file *m, void *unused)
{
	seq_printf(m, "rcu_sched: qlen: %ld\n", rcu_sched_ctrlblk.qlen);
	seq_printf(m, "rcu_bh: qlen: %ld\n", rcu_bh_ctrlblk.qlen);
	return 0;
}

static int show_tiny_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_tiny_stats, NULL);
}

static const struct file_operations show_tiny_stats_fops = {
	.owner = THIS_MODULE,
	.open = show_tiny_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *rcudir;

static int __init rcutiny_trace_init(void)
{
	struct dentry *retval;

	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto free_out;
	retval = debugfs_create_file("rcudata", 0444, rcudir,
				     NULL, &show_tiny_stats_fops);
	if (!retval)
		goto free_out;
	return 0;
free_out:
	debugfs_remove_recursive(rcudir);
	return 1;
}

static void __exit rcutiny_trace_cleanup(void)
{
	debugfs_remove_recursive(rcudir);
}

module_init(rcutiny_trace_init);
module_exit(rcutiny_trace_cleanup);

MODULE_AUTHOR("Paul E. McKenney");
MODULE_DESCRIPTION("Read-Copy Update tracing for tiny implementation");
MODULE_LICENSE("GPL");

static void check_cpu_stall(struct rcu_ctrlblk *rcp)
{
	unsigned long j;
	unsigned long js;

	if (rcu_cpu_stall_suppress)
		return;
	rcp->ticks_this_gp++;
	j = jiffies;
	js = rcp->jiffies_stall;
	if (*rcp->curtail && ULONG_CMP_GE(j, js)) {
		pr_err("INFO: %s stall on CPU (%lu ticks this GP) idle=%llx (t=%lu jiffies q=%ld)\n",
		       rcp->name, rcp->ticks_this_gp, rcu_dynticks_nesting,
		       jiffies - rcp->gp_start, rcp->qlen);
		dump_stack();
	}
	if (*rcp->curtail && ULONG_CMP_GE(j, js))
		rcp->jiffies_stall = jiffies +
			3 * rcu_jiffies_till_stall_check() + 3;
	else if (ULONG_CMP_GE(j, js))
		rcp->jiffies_stall = jiffies + rcu_jiffies_till_stall_check();
}

static void reset_cpu_stall_ticks(struct rcu_ctrlblk *rcp)
{
	rcp->ticks_this_gp = 0;
	rcp->gp_start = jiffies;
	rcp->jiffies_stall = jiffies + rcu_jiffies_till_stall_check();
}

static void check_cpu_stalls(void)
{
	RCU_TRACE(check_cpu_stall(&rcu_bh_ctrlblk));
	RCU_TRACE(check_cpu_stall(&rcu_sched_ctrlblk));
}

#endif /* #ifdef CONFIG_RCU_TRACE */

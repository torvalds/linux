/*
 * Read-Copy Update tracing for classic implementation
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
 * Copyright IBM Corporation, 2008
 *
 * Papers:  http://www.rdrop.com/users/paulmck/RCU
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define RCU_TREE_NONCORE
#include "rcutree.h"

static void print_one_rcu_data(struct seq_file *m, struct rcu_data *rdp)
{
	if (!rdp->beenonline)
		return;
	seq_printf(m, "%3d%cc=%lu g=%lu pq=%d pqc=%lu qp=%d",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   rdp->completed, rdp->gpnum,
		   rdp->passed_quiesc, rdp->passed_quiesc_completed,
		   rdp->qs_pending);
#ifdef CONFIG_NO_HZ
	seq_printf(m, " dt=%d/%d dn=%d df=%lu",
		   rdp->dynticks->dynticks,
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi,
		   rdp->dynticks_fqs);
#endif /* #ifdef CONFIG_NO_HZ */
	seq_printf(m, " of=%lu ri=%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, " ql=%ld b=%ld", rdp->qlen, rdp->blimit);
	seq_printf(m, " ci=%lu co=%lu ca=%lu\n",
		   rdp->n_cbs_invoked, rdp->n_cbs_orphaned, rdp->n_cbs_adopted);
}

#define PRINT_RCU_DATA(name, func, m) \
	do { \
		int _p_r_d_i; \
		\
		for_each_possible_cpu(_p_r_d_i) \
			func(m, &per_cpu(name, _p_r_d_i)); \
	} while (0)

static int show_rcudata(struct seq_file *m, void *unused)
{
#ifdef CONFIG_TREE_PREEMPT_RCU
	seq_puts(m, "rcu_preempt:\n");
	PRINT_RCU_DATA(rcu_preempt_data, print_one_rcu_data, m);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	seq_puts(m, "rcu_sched:\n");
	PRINT_RCU_DATA(rcu_sched_data, print_one_rcu_data, m);
	seq_puts(m, "rcu_bh:\n");
	PRINT_RCU_DATA(rcu_bh_data, print_one_rcu_data, m);
	return 0;
}

static int rcudata_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcudata, NULL);
}

static const struct file_operations rcudata_fops = {
	.owner = THIS_MODULE,
	.open = rcudata_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void print_one_rcu_data_csv(struct seq_file *m, struct rcu_data *rdp)
{
	if (!rdp->beenonline)
		return;
	seq_printf(m, "%d,%s,%lu,%lu,%d,%lu,%d",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? "\"N\"" : "\"Y\"",
		   rdp->completed, rdp->gpnum,
		   rdp->passed_quiesc, rdp->passed_quiesc_completed,
		   rdp->qs_pending);
#ifdef CONFIG_NO_HZ
	seq_printf(m, ",%d,%d,%d,%lu",
		   rdp->dynticks->dynticks,
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi,
		   rdp->dynticks_fqs);
#endif /* #ifdef CONFIG_NO_HZ */
	seq_printf(m, ",%lu,%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, ",%ld,%ld", rdp->qlen, rdp->blimit);
	seq_printf(m, ",%lu,%lu,%lu\n",
		   rdp->n_cbs_invoked, rdp->n_cbs_orphaned, rdp->n_cbs_adopted);
}

static int show_rcudata_csv(struct seq_file *m, void *unused)
{
	seq_puts(m, "\"CPU\",\"Online?\",\"c\",\"g\",\"pq\",\"pqc\",\"pq\",");
#ifdef CONFIG_NO_HZ
	seq_puts(m, "\"dt\",\"dt nesting\",\"dn\",\"df\",");
#endif /* #ifdef CONFIG_NO_HZ */
	seq_puts(m, "\"of\",\"ri\",\"ql\",\"b\",\"ci\",\"co\",\"ca\"\n");
#ifdef CONFIG_TREE_PREEMPT_RCU
	seq_puts(m, "\"rcu_preempt:\"\n");
	PRINT_RCU_DATA(rcu_preempt_data, print_one_rcu_data_csv, m);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	seq_puts(m, "\"rcu_sched:\"\n");
	PRINT_RCU_DATA(rcu_sched_data, print_one_rcu_data_csv, m);
	seq_puts(m, "\"rcu_bh:\"\n");
	PRINT_RCU_DATA(rcu_bh_data, print_one_rcu_data_csv, m);
	return 0;
}

static int rcudata_csv_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcudata_csv, NULL);
}

static const struct file_operations rcudata_csv_fops = {
	.owner = THIS_MODULE,
	.open = rcudata_csv_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void print_one_rcu_state(struct seq_file *m, struct rcu_state *rsp)
{
	unsigned long gpnum;
	int level = 0;
	int phase;
	struct rcu_node *rnp;

	gpnum = rsp->gpnum;
	seq_printf(m, "c=%lu g=%lu s=%d jfq=%ld j=%x "
		      "nfqs=%lu/nfqsng=%lu(%lu) fqlh=%lu oqlen=%ld\n",
		   rsp->completed, gpnum, rsp->signaled,
		   (long)(rsp->jiffies_force_qs - jiffies),
		   (int)(jiffies & 0xffff),
		   rsp->n_force_qs, rsp->n_force_qs_ngp,
		   rsp->n_force_qs - rsp->n_force_qs_ngp,
		   rsp->n_force_qs_lh, rsp->orphan_qlen);
	for (rnp = &rsp->node[0]; rnp - &rsp->node[0] < NUM_RCU_NODES; rnp++) {
		if (rnp->level != level) {
			seq_puts(m, "\n");
			level = rnp->level;
		}
		phase = gpnum & 0x1;
		seq_printf(m, "%lx/%lx %c%c>%c%c %d:%d ^%d    ",
			   rnp->qsmask, rnp->qsmaskinit,
			   "T."[list_empty(&rnp->blocked_tasks[phase])],
			   "E."[list_empty(&rnp->blocked_tasks[phase + 2])],
			   "T."[list_empty(&rnp->blocked_tasks[!phase])],
			   "E."[list_empty(&rnp->blocked_tasks[!phase + 2])],
			   rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	seq_puts(m, "\n");
}

static int show_rcuhier(struct seq_file *m, void *unused)
{
#ifdef CONFIG_TREE_PREEMPT_RCU
	seq_puts(m, "rcu_preempt:\n");
	print_one_rcu_state(m, &rcu_preempt_state);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	seq_puts(m, "rcu_sched:\n");
	print_one_rcu_state(m, &rcu_sched_state);
	seq_puts(m, "rcu_bh:\n");
	print_one_rcu_state(m, &rcu_bh_state);
	return 0;
}

static int rcuhier_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcuhier, NULL);
}

static const struct file_operations rcuhier_fops = {
	.owner = THIS_MODULE,
	.open = rcuhier_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int show_rcugp(struct seq_file *m, void *unused)
{
#ifdef CONFIG_TREE_PREEMPT_RCU
	seq_printf(m, "rcu_preempt: completed=%ld  gpnum=%lu\n",
		   rcu_preempt_state.completed, rcu_preempt_state.gpnum);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	seq_printf(m, "rcu_sched: completed=%ld  gpnum=%lu\n",
		   rcu_sched_state.completed, rcu_sched_state.gpnum);
	seq_printf(m, "rcu_bh: completed=%ld  gpnum=%lu\n",
		   rcu_bh_state.completed, rcu_bh_state.gpnum);
	return 0;
}

static int rcugp_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcugp, NULL);
}

static const struct file_operations rcugp_fops = {
	.owner = THIS_MODULE,
	.open = rcugp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void print_one_rcu_pending(struct seq_file *m, struct rcu_data *rdp)
{
	seq_printf(m, "%3d%cnp=%ld "
		   "qsp=%ld rpq=%ld cbr=%ld cng=%ld "
		   "gpc=%ld gps=%ld nf=%ld nn=%ld\n",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   rdp->n_rcu_pending,
		   rdp->n_rp_qs_pending,
		   rdp->n_rp_report_qs,
		   rdp->n_rp_cb_ready,
		   rdp->n_rp_cpu_needs_gp,
		   rdp->n_rp_gp_completed,
		   rdp->n_rp_gp_started,
		   rdp->n_rp_need_fqs,
		   rdp->n_rp_need_nothing);
}

static void print_rcu_pendings(struct seq_file *m, struct rcu_state *rsp)
{
	int cpu;
	struct rcu_data *rdp;

	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (rdp->beenonline)
			print_one_rcu_pending(m, rdp);
	}
}

static int show_rcu_pending(struct seq_file *m, void *unused)
{
#ifdef CONFIG_TREE_PREEMPT_RCU
	seq_puts(m, "rcu_preempt:\n");
	print_rcu_pendings(m, &rcu_preempt_state);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	seq_puts(m, "rcu_sched:\n");
	print_rcu_pendings(m, &rcu_sched_state);
	seq_puts(m, "rcu_bh:\n");
	print_rcu_pendings(m, &rcu_bh_state);
	return 0;
}

static int rcu_pending_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcu_pending, NULL);
}

static const struct file_operations rcu_pending_fops = {
	.owner = THIS_MODULE,
	.open = rcu_pending_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *rcudir;

static int __init rcuclassic_trace_init(void)
{
	struct dentry *retval;

	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto free_out;

	retval = debugfs_create_file("rcudata", 0444, rcudir,
						NULL, &rcudata_fops);
	if (!retval)
		goto free_out;

	retval = debugfs_create_file("rcudata.csv", 0444, rcudir,
						NULL, &rcudata_csv_fops);
	if (!retval)
		goto free_out;

	retval = debugfs_create_file("rcugp", 0444, rcudir, NULL, &rcugp_fops);
	if (!retval)
		goto free_out;

	retval = debugfs_create_file("rcuhier", 0444, rcudir,
						NULL, &rcuhier_fops);
	if (!retval)
		goto free_out;

	retval = debugfs_create_file("rcu_pending", 0444, rcudir,
						NULL, &rcu_pending_fops);
	if (!retval)
		goto free_out;
	return 0;
free_out:
	debugfs_remove_recursive(rcudir);
	return 1;
}

static void __exit rcuclassic_trace_cleanup(void)
{
	debugfs_remove_recursive(rcudir);
}


module_init(rcuclassic_trace_init);
module_exit(rcuclassic_trace_cleanup);

MODULE_AUTHOR("Paul E. McKenney");
MODULE_DESCRIPTION("Read-Copy Update tracing for hierarchical implementation");
MODULE_LICENSE("GPL");

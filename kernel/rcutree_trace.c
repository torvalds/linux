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
#include <linux/atomic.h>
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

#ifdef CONFIG_RCU_BOOST

static char convert_kthread_status(unsigned int kthread_status)
{
	if (kthread_status > RCU_KTHREAD_MAX)
		return '?';
	return "SRWOY"[kthread_status];
}

#endif /* #ifdef CONFIG_RCU_BOOST */

static void print_one_rcu_data(struct seq_file *m, struct rcu_data *rdp)
{
	if (!rdp->beenonline)
		return;
	seq_printf(m, "%3d%cc=%lu g=%lu pq=%d pgp=%lu qp=%d",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   rdp->completed, rdp->gpnum,
		   rdp->passed_quiesce, rdp->passed_quiesce_gpnum,
		   rdp->qs_pending);
	seq_printf(m, " dt=%d/%llx/%d df=%lu",
		   atomic_read(&rdp->dynticks->dynticks),
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi_nesting,
		   rdp->dynticks_fqs);
	seq_printf(m, " of=%lu ri=%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, " ql=%ld qs=%c%c%c%c",
		   rdp->qlen,
		   ".N"[rdp->nxttail[RCU_NEXT_READY_TAIL] !=
			rdp->nxttail[RCU_NEXT_TAIL]],
		   ".R"[rdp->nxttail[RCU_WAIT_TAIL] !=
			rdp->nxttail[RCU_NEXT_READY_TAIL]],
		   ".W"[rdp->nxttail[RCU_DONE_TAIL] !=
			rdp->nxttail[RCU_WAIT_TAIL]],
		   ".D"[&rdp->nxtlist != rdp->nxttail[RCU_DONE_TAIL]]);
#ifdef CONFIG_RCU_BOOST
	seq_printf(m, " kt=%d/%c/%d ktl=%x",
		   per_cpu(rcu_cpu_has_work, rdp->cpu),
		   convert_kthread_status(per_cpu(rcu_cpu_kthread_status,
					  rdp->cpu)),
		   per_cpu(rcu_cpu_kthread_cpu, rdp->cpu),
		   per_cpu(rcu_cpu_kthread_loops, rdp->cpu) & 0xffff);
#endif /* #ifdef CONFIG_RCU_BOOST */
	seq_printf(m, " b=%ld", rdp->blimit);
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
		   rdp->passed_quiesce, rdp->passed_quiesce_gpnum,
		   rdp->qs_pending);
	seq_printf(m, ",%d,%llx,%d,%lu",
		   atomic_read(&rdp->dynticks->dynticks),
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi_nesting,
		   rdp->dynticks_fqs);
	seq_printf(m, ",%lu,%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, ",%ld,\"%c%c%c%c\"", rdp->qlen,
		   ".N"[rdp->nxttail[RCU_NEXT_READY_TAIL] !=
			rdp->nxttail[RCU_NEXT_TAIL]],
		   ".R"[rdp->nxttail[RCU_WAIT_TAIL] !=
			rdp->nxttail[RCU_NEXT_READY_TAIL]],
		   ".W"[rdp->nxttail[RCU_DONE_TAIL] !=
			rdp->nxttail[RCU_WAIT_TAIL]],
		   ".D"[&rdp->nxtlist != rdp->nxttail[RCU_DONE_TAIL]]);
#ifdef CONFIG_RCU_BOOST
	seq_printf(m, ",%d,\"%c\"",
		   per_cpu(rcu_cpu_has_work, rdp->cpu),
		   convert_kthread_status(per_cpu(rcu_cpu_kthread_status,
					  rdp->cpu)));
#endif /* #ifdef CONFIG_RCU_BOOST */
	seq_printf(m, ",%ld", rdp->blimit);
	seq_printf(m, ",%lu,%lu,%lu\n",
		   rdp->n_cbs_invoked, rdp->n_cbs_orphaned, rdp->n_cbs_adopted);
}

static int show_rcudata_csv(struct seq_file *m, void *unused)
{
	seq_puts(m, "\"CPU\",\"Online?\",\"c\",\"g\",\"pq\",\"pgp\",\"pq\",");
	seq_puts(m, "\"dt\",\"dt nesting\",\"dt NMI nesting\",\"df\",");
	seq_puts(m, "\"of\",\"ri\",\"ql\",\"qs\"");
#ifdef CONFIG_RCU_BOOST
	seq_puts(m, "\"kt\",\"ktl\"");
#endif /* #ifdef CONFIG_RCU_BOOST */
	seq_puts(m, ",\"b\",\"ci\",\"co\",\"ca\"\n");
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

#ifdef CONFIG_RCU_BOOST

static void print_one_rcu_node_boost(struct seq_file *m, struct rcu_node *rnp)
{
	seq_printf(m,  "%d:%d tasks=%c%c%c%c kt=%c ntb=%lu neb=%lu nnb=%lu "
		   "j=%04x bt=%04x\n",
		   rnp->grplo, rnp->grphi,
		   "T."[list_empty(&rnp->blkd_tasks)],
		   "N."[!rnp->gp_tasks],
		   "E."[!rnp->exp_tasks],
		   "B."[!rnp->boost_tasks],
		   convert_kthread_status(rnp->boost_kthread_status),
		   rnp->n_tasks_boosted, rnp->n_exp_boosts,
		   rnp->n_normal_boosts,
		   (int)(jiffies & 0xffff),
		   (int)(rnp->boost_time & 0xffff));
	seq_printf(m, "%s: nt=%lu egt=%lu bt=%lu nb=%lu ny=%lu nos=%lu\n",
		   "     balk",
		   rnp->n_balk_blkd_tasks,
		   rnp->n_balk_exp_gp_tasks,
		   rnp->n_balk_boost_tasks,
		   rnp->n_balk_notblocked,
		   rnp->n_balk_notyet,
		   rnp->n_balk_nos);
}

static int show_rcu_node_boost(struct seq_file *m, void *unused)
{
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(&rcu_preempt_state, rnp)
		print_one_rcu_node_boost(m, rnp);
	return 0;
}

static int rcu_node_boost_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcu_node_boost, NULL);
}

static const struct file_operations rcu_node_boost_fops = {
	.owner = THIS_MODULE,
	.open = rcu_node_boost_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Create the rcuboost debugfs entry.  Standard error return.
 */
static int rcu_boost_trace_create_file(struct dentry *rcudir)
{
	return !debugfs_create_file("rcuboost", 0444, rcudir, NULL,
				    &rcu_node_boost_fops);
}

#else /* #ifdef CONFIG_RCU_BOOST */

static int rcu_boost_trace_create_file(struct dentry *rcudir)
{
	return 0;  /* There cannot be an error if we didn't create it! */
}

#endif /* #else #ifdef CONFIG_RCU_BOOST */

static void print_one_rcu_state(struct seq_file *m, struct rcu_state *rsp)
{
	unsigned long gpnum;
	int level = 0;
	struct rcu_node *rnp;

	gpnum = rsp->gpnum;
	seq_printf(m, "c=%lu g=%lu s=%d jfq=%ld j=%x "
		      "nfqs=%lu/nfqsng=%lu(%lu) fqlh=%lu\n",
		   rsp->completed, gpnum, rsp->fqs_state,
		   (long)(rsp->jiffies_force_qs - jiffies),
		   (int)(jiffies & 0xffff),
		   rsp->n_force_qs, rsp->n_force_qs_ngp,
		   rsp->n_force_qs - rsp->n_force_qs_ngp,
		   rsp->n_force_qs_lh);
	for (rnp = &rsp->node[0]; rnp - &rsp->node[0] < NUM_RCU_NODES; rnp++) {
		if (rnp->level != level) {
			seq_puts(m, "\n");
			level = rnp->level;
		}
		seq_printf(m, "%lx/%lx %c%c>%c %d:%d ^%d    ",
			   rnp->qsmask, rnp->qsmaskinit,
			   ".G"[rnp->gp_tasks != NULL],
			   ".E"[rnp->exp_tasks != NULL],
			   ".T"[!list_empty(&rnp->blkd_tasks)],
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

static void show_one_rcugp(struct seq_file *m, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long completed;
	unsigned long gpnum;
	unsigned long gpage;
	unsigned long gpmax;
	struct rcu_node *rnp = &rsp->node[0];

	raw_spin_lock_irqsave(&rnp->lock, flags);
	completed = rsp->completed;
	gpnum = rsp->gpnum;
	if (rsp->completed == rsp->gpnum)
		gpage = 0;
	else
		gpage = jiffies - rsp->gp_start;
	gpmax = rsp->gp_max;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	seq_printf(m, "%s: completed=%ld  gpnum=%lu  age=%ld  max=%ld\n",
		   rsp->name, completed, gpnum, gpage, gpmax);
}

static int show_rcugp(struct seq_file *m, void *unused)
{
#ifdef CONFIG_TREE_PREEMPT_RCU
	show_one_rcugp(m, &rcu_preempt_state);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */
	show_one_rcugp(m, &rcu_sched_state);
	show_one_rcugp(m, &rcu_bh_state);
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

static int show_rcutorture(struct seq_file *m, void *unused)
{
	seq_printf(m, "rcutorture test sequence: %lu %s\n",
		   rcutorture_testseq >> 1,
		   (rcutorture_testseq & 0x1) ? "(test in progress)" : "");
	seq_printf(m, "rcutorture update version number: %lu\n",
		   rcutorture_vernum);
	return 0;
}

static int rcutorture_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcutorture, NULL);
}

static const struct file_operations rcutorture_fops = {
	.owner = THIS_MODULE,
	.open = rcutorture_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *rcudir;

static int __init rcutree_trace_init(void)
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

	if (rcu_boost_trace_create_file(rcudir))
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

	retval = debugfs_create_file("rcutorture", 0444, rcudir,
						NULL, &rcutorture_fops);
	if (!retval)
		goto free_out;
	return 0;
free_out:
	debugfs_remove_recursive(rcudir);
	return 1;
}

static void __exit rcutree_trace_cleanup(void)
{
	debugfs_remove_recursive(rcudir);
}


module_init(rcutree_trace_init);
module_exit(rcutree_trace_cleanup);

MODULE_AUTHOR("Paul E. McKenney");
MODULE_DESCRIPTION("Read-Copy Update tracing for hierarchical implementation");
MODULE_LICENSE("GPL");

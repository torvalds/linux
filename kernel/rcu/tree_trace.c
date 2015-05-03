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
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
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
#include "tree.h"

DECLARE_PER_CPU_SHARED_ALIGNED(unsigned long, rcu_qs_ctr);

static int r_open(struct inode *inode, struct file *file,
					const struct seq_operations *op)
{
	int ret = seq_open(file, op);
	if (!ret) {
		struct seq_file *m = (struct seq_file *)file->private_data;
		m->private = inode->i_private;
	}
	return ret;
}

static void *r_start(struct seq_file *m, loff_t *pos)
{
	struct rcu_state *rsp = (struct rcu_state *)m->private;
	*pos = cpumask_next(*pos - 1, cpu_possible_mask);
	if ((*pos) < nr_cpu_ids)
		return per_cpu_ptr(rsp->rda, *pos);
	return NULL;
}

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return r_start(m, pos);
}

static void r_stop(struct seq_file *m, void *v)
{
}

static int show_rcubarrier(struct seq_file *m, void *v)
{
	struct rcu_state *rsp = (struct rcu_state *)m->private;
	seq_printf(m, "bcc: %d nbd: %lu\n",
		   atomic_read(&rsp->barrier_cpu_count),
		   rsp->n_barrier_done);
	return 0;
}

static int rcubarrier_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcubarrier, inode->i_private);
}

static const struct file_operations rcubarrier_fops = {
	.owner = THIS_MODULE,
	.open = rcubarrier_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = single_release,
};

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
	long ql, qll;

	if (!rdp->beenonline)
		return;
	seq_printf(m, "%3d%cc=%ld g=%ld pq=%d/%d qp=%d",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   ulong2long(rdp->completed), ulong2long(rdp->gpnum),
		   rdp->passed_quiesce,
		   rdp->rcu_qs_ctr_snap == per_cpu(rcu_qs_ctr, rdp->cpu),
		   rdp->qs_pending);
	seq_printf(m, " dt=%d/%llx/%d df=%lu",
		   atomic_read(&rdp->dynticks->dynticks),
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi_nesting,
		   rdp->dynticks_fqs);
	seq_printf(m, " of=%lu", rdp->offline_fqs);
	rcu_nocb_q_lengths(rdp, &ql, &qll);
	qll += rdp->qlen_lazy;
	ql += rdp->qlen;
	seq_printf(m, " ql=%ld/%ld qs=%c%c%c%c",
		   qll, ql,
		   ".N"[rdp->nxttail[RCU_NEXT_READY_TAIL] !=
			rdp->nxttail[RCU_NEXT_TAIL]],
		   ".R"[rdp->nxttail[RCU_WAIT_TAIL] !=
			rdp->nxttail[RCU_NEXT_READY_TAIL]],
		   ".W"[rdp->nxttail[RCU_DONE_TAIL] !=
			rdp->nxttail[RCU_WAIT_TAIL]],
		   ".D"[&rdp->nxtlist != rdp->nxttail[RCU_DONE_TAIL]]);
#ifdef CONFIG_RCU_BOOST
	seq_printf(m, " kt=%d/%c ktl=%x",
		   per_cpu(rcu_cpu_has_work, rdp->cpu),
		   convert_kthread_status(per_cpu(rcu_cpu_kthread_status,
					  rdp->cpu)),
		   per_cpu(rcu_cpu_kthread_loops, rdp->cpu) & 0xffff);
#endif /* #ifdef CONFIG_RCU_BOOST */
	seq_printf(m, " b=%ld", rdp->blimit);
	seq_printf(m, " ci=%lu nci=%lu co=%lu ca=%lu\n",
		   rdp->n_cbs_invoked, rdp->n_nocbs_invoked,
		   rdp->n_cbs_orphaned, rdp->n_cbs_adopted);
}

static int show_rcudata(struct seq_file *m, void *v)
{
	print_one_rcu_data(m, (struct rcu_data *)v);
	return 0;
}

static const struct seq_operations rcudate_op = {
	.start = r_start,
	.next  = r_next,
	.stop  = r_stop,
	.show  = show_rcudata,
};

static int rcudata_open(struct inode *inode, struct file *file)
{
	return r_open(inode, file, &rcudate_op);
}

static const struct file_operations rcudata_fops = {
	.owner = THIS_MODULE,
	.open = rcudata_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = seq_release,
};

static int show_rcuexp(struct seq_file *m, void *v)
{
	struct rcu_state *rsp = (struct rcu_state *)m->private;

	seq_printf(m, "s=%lu d=%lu w=%lu tf=%lu wd1=%lu wd2=%lu n=%lu sc=%lu dt=%lu dl=%lu dx=%lu\n",
		   atomic_long_read(&rsp->expedited_start),
		   atomic_long_read(&rsp->expedited_done),
		   atomic_long_read(&rsp->expedited_wrap),
		   atomic_long_read(&rsp->expedited_tryfail),
		   atomic_long_read(&rsp->expedited_workdone1),
		   atomic_long_read(&rsp->expedited_workdone2),
		   atomic_long_read(&rsp->expedited_normal),
		   atomic_long_read(&rsp->expedited_stoppedcpus),
		   atomic_long_read(&rsp->expedited_done_tries),
		   atomic_long_read(&rsp->expedited_done_lost),
		   atomic_long_read(&rsp->expedited_done_exit));
	return 0;
}

static int rcuexp_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcuexp, inode->i_private);
}

static const struct file_operations rcuexp_fops = {
	.owner = THIS_MODULE,
	.open = rcuexp_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = single_release,
};

#ifdef CONFIG_RCU_BOOST

static void print_one_rcu_node_boost(struct seq_file *m, struct rcu_node *rnp)
{
	seq_printf(m, "%d:%d tasks=%c%c%c%c kt=%c ntb=%lu neb=%lu nnb=%lu ",
		   rnp->grplo, rnp->grphi,
		   "T."[list_empty(&rnp->blkd_tasks)],
		   "N."[!rnp->gp_tasks],
		   "E."[!rnp->exp_tasks],
		   "B."[!rnp->boost_tasks],
		   convert_kthread_status(rnp->boost_kthread_status),
		   rnp->n_tasks_boosted, rnp->n_exp_boosts,
		   rnp->n_normal_boosts);
	seq_printf(m, "j=%04x bt=%04x\n",
		   (int)(jiffies & 0xffff),
		   (int)(rnp->boost_time & 0xffff));
	seq_printf(m, "    balk: nt=%lu egt=%lu bt=%lu nb=%lu ny=%lu nos=%lu\n",
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
	.llseek = no_llseek,
	.release = single_release,
};

#endif /* #ifdef CONFIG_RCU_BOOST */

static void print_one_rcu_state(struct seq_file *m, struct rcu_state *rsp)
{
	unsigned long gpnum;
	int level = 0;
	struct rcu_node *rnp;

	gpnum = rsp->gpnum;
	seq_printf(m, "c=%ld g=%ld s=%d jfq=%ld j=%x ",
		   ulong2long(rsp->completed), ulong2long(gpnum),
		   rsp->fqs_state,
		   (long)(rsp->jiffies_force_qs - jiffies),
		   (int)(jiffies & 0xffff));
	seq_printf(m, "nfqs=%lu/nfqsng=%lu(%lu) fqlh=%lu oqlen=%ld/%ld\n",
		   rsp->n_force_qs, rsp->n_force_qs_ngp,
		   rsp->n_force_qs - rsp->n_force_qs_ngp,
		   ACCESS_ONCE(rsp->n_force_qs_lh), rsp->qlen_lazy, rsp->qlen);
	for (rnp = &rsp->node[0]; rnp - &rsp->node[0] < rcu_num_nodes; rnp++) {
		if (rnp->level != level) {
			seq_puts(m, "\n");
			level = rnp->level;
		}
		seq_printf(m, "%lx/%lx->%lx %c%c>%c %d:%d ^%d    ",
			   rnp->qsmask, rnp->qsmaskinit, rnp->qsmaskinitnext,
			   ".G"[rnp->gp_tasks != NULL],
			   ".E"[rnp->exp_tasks != NULL],
			   ".T"[!list_empty(&rnp->blkd_tasks)],
			   rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	seq_puts(m, "\n");
}

static int show_rcuhier(struct seq_file *m, void *v)
{
	struct rcu_state *rsp = (struct rcu_state *)m->private;
	print_one_rcu_state(m, rsp);
	return 0;
}

static int rcuhier_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcuhier, inode->i_private);
}

static const struct file_operations rcuhier_fops = {
	.owner = THIS_MODULE,
	.open = rcuhier_open,
	.read = seq_read,
	.llseek = no_llseek,
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
	completed = ACCESS_ONCE(rsp->completed);
	gpnum = ACCESS_ONCE(rsp->gpnum);
	if (completed == gpnum)
		gpage = 0;
	else
		gpage = jiffies - rsp->gp_start;
	gpmax = rsp->gp_max;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	seq_printf(m, "completed=%ld  gpnum=%ld  age=%ld  max=%ld\n",
		   ulong2long(completed), ulong2long(gpnum), gpage, gpmax);
}

static int show_rcugp(struct seq_file *m, void *v)
{
	struct rcu_state *rsp = (struct rcu_state *)m->private;
	show_one_rcugp(m, rsp);
	return 0;
}

static int rcugp_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcugp, inode->i_private);
}

static const struct file_operations rcugp_fops = {
	.owner = THIS_MODULE,
	.open = rcugp_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = single_release,
};

static void print_one_rcu_pending(struct seq_file *m, struct rcu_data *rdp)
{
	if (!rdp->beenonline)
		return;
	seq_printf(m, "%3d%cnp=%ld ",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   rdp->n_rcu_pending);
	seq_printf(m, "qsp=%ld rpq=%ld cbr=%ld cng=%ld ",
		   rdp->n_rp_qs_pending,
		   rdp->n_rp_report_qs,
		   rdp->n_rp_cb_ready,
		   rdp->n_rp_cpu_needs_gp);
	seq_printf(m, "gpc=%ld gps=%ld nn=%ld ndw%ld\n",
		   rdp->n_rp_gp_completed,
		   rdp->n_rp_gp_started,
		   rdp->n_rp_nocb_defer_wakeup,
		   rdp->n_rp_need_nothing);
}

static int show_rcu_pending(struct seq_file *m, void *v)
{
	print_one_rcu_pending(m, (struct rcu_data *)v);
	return 0;
}

static const struct seq_operations rcu_pending_op = {
	.start = r_start,
	.next  = r_next,
	.stop  = r_stop,
	.show  = show_rcu_pending,
};

static int rcu_pending_open(struct inode *inode, struct file *file)
{
	return r_open(inode, file, &rcu_pending_op);
}

static const struct file_operations rcu_pending_fops = {
	.owner = THIS_MODULE,
	.open = rcu_pending_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = seq_release,
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
	struct rcu_state *rsp;
	struct dentry *retval;
	struct dentry *rspdir;

	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto free_out;

	for_each_rcu_flavor(rsp) {
		rspdir = debugfs_create_dir(rsp->name, rcudir);
		if (!rspdir)
			goto free_out;

		retval = debugfs_create_file("rcudata", 0444,
				rspdir, rsp, &rcudata_fops);
		if (!retval)
			goto free_out;

		retval = debugfs_create_file("rcuexp", 0444,
				rspdir, rsp, &rcuexp_fops);
		if (!retval)
			goto free_out;

		retval = debugfs_create_file("rcu_pending", 0444,
				rspdir, rsp, &rcu_pending_fops);
		if (!retval)
			goto free_out;

		retval = debugfs_create_file("rcubarrier", 0444,
				rspdir, rsp, &rcubarrier_fops);
		if (!retval)
			goto free_out;

#ifdef CONFIG_RCU_BOOST
		if (rsp == &rcu_preempt_state) {
			retval = debugfs_create_file("rcuboost", 0444,
				rspdir, NULL, &rcu_node_boost_fops);
			if (!retval)
				goto free_out;
		}
#endif

		retval = debugfs_create_file("rcugp", 0444,
				rspdir, rsp, &rcugp_fops);
		if (!retval)
			goto free_out;

		retval = debugfs_create_file("rcuhier", 0444,
				rspdir, rsp, &rcuhier_fops);
		if (!retval)
			goto free_out;
	}

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

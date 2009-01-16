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
 * 		Documentation/RCU
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

static void print_one_rcu_data(struct seq_file *m, struct rcu_data *rdp)
{
	if (!rdp->beenonline)
		return;
	seq_printf(m, "%3d%cc=%ld g=%ld pq=%d pqc=%ld qp=%d rpfq=%ld rp=%x",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? '!' : ' ',
		   rdp->completed, rdp->gpnum,
		   rdp->passed_quiesc, rdp->passed_quiesc_completed,
		   rdp->qs_pending,
		   rdp->n_rcu_pending_force_qs - rdp->n_rcu_pending,
		   (int)(rdp->n_rcu_pending & 0xffff));
#ifdef CONFIG_NO_HZ
	seq_printf(m, " dt=%d/%d dn=%d df=%lu",
		   rdp->dynticks->dynticks,
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi,
		   rdp->dynticks_fqs);
#endif /* #ifdef CONFIG_NO_HZ */
	seq_printf(m, " of=%lu ri=%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, " ql=%ld b=%ld\n", rdp->qlen, rdp->blimit);
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
	seq_puts(m, "rcu:\n");
	PRINT_RCU_DATA(rcu_data, print_one_rcu_data, m);
	seq_puts(m, "rcu_bh:\n");
	PRINT_RCU_DATA(rcu_bh_data, print_one_rcu_data, m);
	return 0;
}

static int rcudata_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcudata, NULL);
}

static struct file_operations rcudata_fops = {
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
	seq_printf(m, "%d,%s,%ld,%ld,%d,%ld,%d,%ld,%ld",
		   rdp->cpu,
		   cpu_is_offline(rdp->cpu) ? "\"Y\"" : "\"N\"",
		   rdp->completed, rdp->gpnum,
		   rdp->passed_quiesc, rdp->passed_quiesc_completed,
		   rdp->qs_pending,
		   rdp->n_rcu_pending_force_qs - rdp->n_rcu_pending,
		   rdp->n_rcu_pending);
#ifdef CONFIG_NO_HZ
	seq_printf(m, ",%d,%d,%d,%lu",
		   rdp->dynticks->dynticks,
		   rdp->dynticks->dynticks_nesting,
		   rdp->dynticks->dynticks_nmi,
		   rdp->dynticks_fqs);
#endif /* #ifdef CONFIG_NO_HZ */
	seq_printf(m, ",%lu,%lu", rdp->offline_fqs, rdp->resched_ipi);
	seq_printf(m, ",%ld,%ld\n", rdp->qlen, rdp->blimit);
}

static int show_rcudata_csv(struct seq_file *m, void *unused)
{
	seq_puts(m, "\"CPU\",\"Online?\",\"c\",\"g\",\"pq\",\"pqc\",\"pq\",\"rpfq\",\"rp\",");
#ifdef CONFIG_NO_HZ
	seq_puts(m, "\"dt\",\"dt nesting\",\"dn\",\"df\",");
#endif /* #ifdef CONFIG_NO_HZ */
	seq_puts(m, "\"of\",\"ri\",\"ql\",\"b\"\n");
	seq_puts(m, "\"rcu:\"\n");
	PRINT_RCU_DATA(rcu_data, print_one_rcu_data_csv, m);
	seq_puts(m, "\"rcu_bh:\"\n");
	PRINT_RCU_DATA(rcu_bh_data, print_one_rcu_data_csv, m);
	return 0;
}

static int rcudata_csv_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcudata_csv, NULL);
}

static struct file_operations rcudata_csv_fops = {
	.owner = THIS_MODULE,
	.open = rcudata_csv_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void print_one_rcu_state(struct seq_file *m, struct rcu_state *rsp)
{
	int level = 0;
	struct rcu_node *rnp;

	seq_printf(m, "c=%ld g=%ld s=%d jfq=%ld j=%x "
	              "nfqs=%lu/nfqsng=%lu(%lu) fqlh=%lu\n",
		   rsp->completed, rsp->gpnum, rsp->signaled,
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
		seq_printf(m, "%lx/%lx %d:%d ^%d    ",
			   rnp->qsmask, rnp->qsmaskinit,
			   rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	seq_puts(m, "\n");
}

static int show_rcuhier(struct seq_file *m, void *unused)
{
	seq_puts(m, "rcu:\n");
	print_one_rcu_state(m, &rcu_state);
	seq_puts(m, "rcu_bh:\n");
	print_one_rcu_state(m, &rcu_bh_state);
	return 0;
}

static int rcuhier_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcuhier, NULL);
}

static struct file_operations rcuhier_fops = {
	.owner = THIS_MODULE,
	.open = rcuhier_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int show_rcugp(struct seq_file *m, void *unused)
{
	seq_printf(m, "rcu: completed=%ld  gpnum=%ld\n",
		   rcu_state.completed, rcu_state.gpnum);
	seq_printf(m, "rcu_bh: completed=%ld  gpnum=%ld\n",
		   rcu_bh_state.completed, rcu_bh_state.gpnum);
	return 0;
}

static int rcugp_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_rcugp, NULL);
}

static struct file_operations rcugp_fops = {
	.owner = THIS_MODULE,
	.open = rcugp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *rcudir, *datadir, *datadir_csv, *hierdir, *gpdir;
static int __init rcuclassic_trace_init(void)
{
	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto out;

	datadir = debugfs_create_file("rcudata", 0444, rcudir,
						NULL, &rcudata_fops);
	if (!datadir)
		goto free_out;

	datadir_csv = debugfs_create_file("rcudata.csv", 0444, rcudir,
						NULL, &rcudata_csv_fops);
	if (!datadir_csv)
		goto free_out;

	gpdir = debugfs_create_file("rcugp", 0444, rcudir, NULL, &rcugp_fops);
	if (!gpdir)
		goto free_out;

	hierdir = debugfs_create_file("rcuhier", 0444, rcudir,
						NULL, &rcuhier_fops);
	if (!hierdir)
		goto free_out;
	return 0;
free_out:
	if (datadir)
		debugfs_remove(datadir);
	if (datadir_csv)
		debugfs_remove(datadir_csv);
	if (gpdir)
		debugfs_remove(gpdir);
	debugfs_remove(rcudir);
out:
	return 1;
}

static void __exit rcuclassic_trace_cleanup(void)
{
	debugfs_remove(datadir);
	debugfs_remove(datadir_csv);
	debugfs_remove(gpdir);
	debugfs_remove(hierdir);
	debugfs_remove(rcudir);
}


module_init(rcuclassic_trace_init);
module_exit(rcuclassic_trace_cleanup);

MODULE_AUTHOR("Paul E. McKenney");
MODULE_DESCRIPTION("Read-Copy Update tracing for hierarchical implementation");
MODULE_LICENSE("GPL");

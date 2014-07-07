
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "sched.h"

/*
 * bump this up when changing the output format or the meaning of an existing
 * format, so that tools can adapt (or abort)
 */
#define SCHEDSTAT_VERSION 15

static int show_schedstat(struct seq_file *seq, void *v)
{
	int cpu;
	int mask_len = DIV_ROUND_UP(NR_CPUS, 32) * 9;
	char *mask_str = kmalloc(mask_len, GFP_KERNEL);

	if (mask_str == NULL)
		return -ENOMEM;

	if (v == (void *)1) {
		seq_printf(seq, "version %d\n", SCHEDSTAT_VERSION);
		seq_printf(seq, "timestamp %lu\n", jiffies);
	} else {
		struct rq *rq;
#ifdef CONFIG_SMP
		struct sched_domain *sd;
		int dcount = 0;
#endif
		cpu = (unsigned long)(v - 2);
		rq = cpu_rq(cpu);

		/* runqueue-specific stats */
		seq_printf(seq,
		    "cpu%d %u 0 %u %u %u %u %llu %llu %lu",
		    cpu, rq->yld_count,
		    rq->sched_count, rq->sched_goidle,
		    rq->ttwu_count, rq->ttwu_local,
		    rq->rq_cpu_time,
		    rq->rq_sched_info.run_delay, rq->rq_sched_info.pcount);

		seq_printf(seq, "\n");

#ifdef CONFIG_SMP
		/* domain-specific stats */
		rcu_read_lock();
		for_each_domain(cpu, sd) {
			enum cpu_idle_type itype;

			cpumask_scnprintf(mask_str, mask_len,
					  sched_domain_span(sd));
			seq_printf(seq, "domain%d %s", dcount++, mask_str);
			for (itype = CPU_IDLE; itype < CPU_MAX_IDLE_TYPES;
					itype++) {
				seq_printf(seq, " %u %u %u %u %u %u %u %u",
				    sd->lb_count[itype],
				    sd->lb_balanced[itype],
				    sd->lb_failed[itype],
				    sd->lb_imbalance[itype],
				    sd->lb_gained[itype],
				    sd->lb_hot_gained[itype],
				    sd->lb_nobusyq[itype],
				    sd->lb_nobusyg[itype]);
			}
			seq_printf(seq,
				   " %u %u %u %u %u %u %u %u %u %u %u %u\n",
			    sd->alb_count, sd->alb_failed, sd->alb_pushed,
			    sd->sbe_count, sd->sbe_balanced, sd->sbe_pushed,
			    sd->sbf_count, sd->sbf_balanced, sd->sbf_pushed,
			    sd->ttwu_wake_remote, sd->ttwu_move_affine,
			    sd->ttwu_move_balance);
		}
		rcu_read_unlock();
#endif
	}
	kfree(mask_str);
	return 0;
}

/*
 * This itererator needs some explanation.
 * It returns 1 for the header position.
 * This means 2 is cpu 0.
 * In a hotplugged system some cpus, including cpu 0, may be missing so we have
 * to use cpumask_* to iterate over the cpus.
 */
static void *schedstat_start(struct seq_file *file, loff_t *offset)
{
	unsigned long n = *offset;

	if (n == 0)
		return (void *) 1;

	n--;

	if (n > 0)
		n = cpumask_next(n - 1, cpu_online_mask);
	else
		n = cpumask_first(cpu_online_mask);

	*offset = n + 1;

	if (n < nr_cpu_ids)
		return (void *)(unsigned long)(n + 2);
	return NULL;
}

static void *schedstat_next(struct seq_file *file, void *data, loff_t *offset)
{
	(*offset)++;
	return schedstat_start(file, offset);
}

static void schedstat_stop(struct seq_file *file, void *data)
{
}

static const struct seq_operations schedstat_sops = {
	.start = schedstat_start,
	.next  = schedstat_next,
	.stop  = schedstat_stop,
	.show  = show_schedstat,
};

static int schedstat_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &schedstat_sops);
}

static const struct file_operations proc_schedstat_operations = {
	.open    = schedstat_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int __init proc_schedstat_init(void)
{
	proc_create("schedstat", 0, NULL, &proc_schedstat_operations);
	return 0;
}
subsys_initcall(proc_schedstat_init);

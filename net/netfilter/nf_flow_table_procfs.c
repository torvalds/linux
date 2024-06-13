// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <net/netfilter/nf_flow_table.h>

static void *nf_flow_table_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos - 1; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(net->ft.stat, cpu);
	}

	return NULL;
}

static void *nf_flow_table_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	int cpu;

	for (cpu = *pos; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(net->ft.stat, cpu);
	}
	(*pos)++;
	return NULL;
}

static void nf_flow_table_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int nf_flow_table_cpu_seq_show(struct seq_file *seq, void *v)
{
	const struct nf_flow_table_stat *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "wq_add   wq_del   wq_stats\n");
		return 0;
	}

	seq_printf(seq, "%8d %8d %8d\n",
		   st->count_wq_add,
		   st->count_wq_del,
		   st->count_wq_stats
		);
	return 0;
}

static const struct seq_operations nf_flow_table_cpu_seq_ops = {
	.start	= nf_flow_table_cpu_seq_start,
	.next	= nf_flow_table_cpu_seq_next,
	.stop	= nf_flow_table_cpu_seq_stop,
	.show	= nf_flow_table_cpu_seq_show,
};

int nf_flow_table_init_proc(struct net *net)
{
	struct proc_dir_entry *pde;

	pde = proc_create_net("nf_flowtable", 0444, net->proc_net_stat,
			      &nf_flow_table_cpu_seq_ops,
			      sizeof(struct seq_net_private));
	return pde ? 0 : -ENOMEM;
}

void nf_flow_table_fini_proc(struct net *net)
{
	remove_proc_entry("nf_flowtable", net->proc_net_stat);
}

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>

MODULE_LICENSE("GPL");

#ifdef CONFIG_PROC_FS
int
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
            const struct nf_conntrack_l3proto *l3proto,
            const struct nf_conntrack_l4proto *l4proto)
{
	return l3proto->print_tuple(s, tuple) || l4proto->print_tuple(s, tuple);
}
EXPORT_SYMBOL_GPL(print_tuple);

struct ct_iter_state {
	unsigned int bucket;
};

static struct hlist_node *ct_get_first(struct seq_file *seq)
{
	struct ct_iter_state *st = seq->private;
	struct hlist_node *n;

	for (st->bucket = 0;
	     st->bucket < nf_conntrack_htable_size;
	     st->bucket++) {
		n = rcu_dereference(nf_conntrack_hash[st->bucket].first);
		if (n)
			return n;
	}
	return NULL;
}

static struct hlist_node *ct_get_next(struct seq_file *seq,
				      struct hlist_node *head)
{
	struct ct_iter_state *st = seq->private;

	head = rcu_dereference(head->next);
	while (head == NULL) {
		if (++st->bucket >= nf_conntrack_htable_size)
			return NULL;
		head = rcu_dereference(nf_conntrack_hash[st->bucket].first);
	}
	return head;
}

static struct hlist_node *ct_get_idx(struct seq_file *seq, loff_t pos)
{
	struct hlist_node *head = ct_get_first(seq);

	if (head)
		while (pos && (head = ct_get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *ct_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return ct_get_idx(seq, *pos);
}

static void *ct_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_get_next(s, v);
}

static void ct_seq_stop(struct seq_file *s, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

/* return 0 on success, 1 in case of error */
static int ct_seq_show(struct seq_file *s, void *v)
{
	const struct nf_conntrack_tuple_hash *hash = v;
	const struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(hash);
	const struct nf_conntrack_l3proto *l3proto;
	const struct nf_conntrack_l4proto *l4proto;

	NF_CT_ASSERT(ct);

	/* we only want to print DIR_ORIGINAL */
	if (NF_CT_DIRECTION(hash))
		return 0;

	l3proto = __nf_ct_l3proto_find(nf_ct_l3num(ct));
	NF_CT_ASSERT(l3proto);
	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	NF_CT_ASSERT(l4proto);

	if (seq_printf(s, "%-8s %u %-8s %u %ld ",
		       l3proto->name, nf_ct_l3num(ct),
		       l4proto->name, nf_ct_protonum(ct),
		       timer_pending(&ct->timeout)
		       ? (long)(ct->timeout.expires - jiffies)/HZ : 0) != 0)
		return -ENOSPC;

	if (l4proto->print_conntrack && l4proto->print_conntrack(s, ct))
		return -ENOSPC;

	if (print_tuple(s, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
			l3proto, l4proto))
		return -ENOSPC;

	if (seq_print_acct(s, ct, IP_CT_DIR_ORIGINAL))
		return -ENOSPC;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &ct->status)))
		if (seq_printf(s, "[UNREPLIED] "))
			return -ENOSPC;

	if (print_tuple(s, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
			l3proto, l4proto))
		return -ENOSPC;

	if (seq_print_acct(s, ct, IP_CT_DIR_REPLY))
		return -ENOSPC;

	if (test_bit(IPS_ASSURED_BIT, &ct->status))
		if (seq_printf(s, "[ASSURED] "))
			return -ENOSPC;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (seq_printf(s, "mark=%u ", ct->mark))
		return -ENOSPC;
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
	if (seq_printf(s, "secmark=%u ", ct->secmark))
		return -ENOSPC;
#endif

	if (seq_printf(s, "use=%u\n", atomic_read(&ct->ct_general.use)))
		return -ENOSPC;

	return 0;
}

static const struct seq_operations ct_seq_ops = {
	.start = ct_seq_start,
	.next  = ct_seq_next,
	.stop  = ct_seq_stop,
	.show  = ct_seq_show
};

static int ct_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &ct_seq_ops,
			sizeof(struct ct_iter_state));
}

static const struct file_operations ct_file_ops = {
	.owner   = THIS_MODULE,
	.open    = ct_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

static void *ct_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos-1; cpu < NR_CPUS; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return &per_cpu(nf_conntrack_stat, cpu);
	}

	return NULL;
}

static void *ct_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int cpu;

	for (cpu = *pos; cpu < NR_CPUS; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return &per_cpu(nf_conntrack_stat, cpu);
	}

	return NULL;
}

static void ct_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int ct_cpu_seq_show(struct seq_file *seq, void *v)
{
	unsigned int nr_conntracks = atomic_read(&init_net.ct.count);
	const struct ip_conntrack_stat *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "entries  searched found new invalid ignore delete delete_list insert insert_failed drop early_drop icmp_error  expect_new expect_create expect_delete\n");
		return 0;
	}

	seq_printf(seq, "%08x  %08x %08x %08x %08x %08x %08x %08x "
			"%08x %08x %08x %08x %08x  %08x %08x %08x \n",
		   nr_conntracks,
		   st->searched,
		   st->found,
		   st->new,
		   st->invalid,
		   st->ignore,
		   st->delete,
		   st->delete_list,
		   st->insert,
		   st->insert_failed,
		   st->drop,
		   st->early_drop,
		   st->error,

		   st->expect_new,
		   st->expect_create,
		   st->expect_delete
		);
	return 0;
}

static const struct seq_operations ct_cpu_seq_ops = {
	.start	= ct_cpu_seq_start,
	.next	= ct_cpu_seq_next,
	.stop	= ct_cpu_seq_stop,
	.show	= ct_cpu_seq_show,
};

static int ct_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_cpu_seq_ops);
}

static const struct file_operations ct_cpu_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ct_cpu_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

static int nf_conntrack_standalone_init_proc(void)
{
	struct proc_dir_entry *pde;

	pde = proc_net_fops_create(&init_net, "nf_conntrack", 0440, &ct_file_ops);
	if (!pde)
		goto out_nf_conntrack;

	pde = proc_create("nf_conntrack", S_IRUGO, init_net.proc_net_stat,
			  &ct_cpu_seq_fops);
	if (!pde)
		goto out_stat_nf_conntrack;
	return 0;

out_stat_nf_conntrack:
	proc_net_remove(&init_net, "nf_conntrack");
out_nf_conntrack:
	return -ENOMEM;
}

static void nf_conntrack_standalone_fini_proc(void)
{
	remove_proc_entry("nf_conntrack", init_net.proc_net_stat);
	proc_net_remove(&init_net, "nf_conntrack");
}
#else
static int nf_conntrack_standalone_init_proc(void)
{
	return 0;
}

static void nf_conntrack_standalone_fini_proc(void)
{
}
#endif /* CONFIG_PROC_FS */

/* Sysctl support */

int nf_conntrack_checksum __read_mostly = 1;
EXPORT_SYMBOL_GPL(nf_conntrack_checksum);

#ifdef CONFIG_SYSCTL
/* Log invalid packets of a given protocol */
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;

static struct ctl_table_header *nf_ct_sysctl_header;
static struct ctl_table_header *nf_ct_netfilter_header;

static ctl_table nf_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_NF_CONNTRACK_MAX,
		.procname	= "nf_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_COUNT,
		.procname	= "nf_conntrack_count",
		.data		= &init_net.ct.count,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name       = NET_NF_CONNTRACK_BUCKETS,
		.procname       = "nf_conntrack_buckets",
		.data           = &nf_conntrack_htable_size,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0444,
		.proc_handler   = &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_CHECKSUM,
		.procname	= "nf_conntrack_checksum",
		.data		= &nf_conntrack_checksum,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_LOG_INVALID,
		.procname	= "nf_conntrack_log_invalid",
		.data		= &nf_ct_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nf_conntrack_expect_max",
		.data		= &nf_ct_expect_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

#define NET_NF_CONNTRACK_MAX 2089

static ctl_table nf_ct_netfilter_table[] = {
	{
		.ctl_name	= NET_NF_CONNTRACK_MAX,
		.procname	= "nf_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static struct ctl_path nf_ct_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ }
};

EXPORT_SYMBOL_GPL(nf_ct_log_invalid);

static int nf_conntrack_standalone_init_sysctl(void)
{
	nf_ct_netfilter_header =
		register_sysctl_paths(nf_ct_path, nf_ct_netfilter_table);
	if (!nf_ct_netfilter_header)
		goto out;

	nf_ct_sysctl_header =
		 register_sysctl_paths(nf_net_netfilter_sysctl_path,
					nf_ct_sysctl_table);
	if (!nf_ct_sysctl_header)
		goto out_unregister_netfilter;

	return 0;

out_unregister_netfilter:
	unregister_sysctl_table(nf_ct_netfilter_header);
out:
	printk("nf_conntrack: can't register to sysctl.\n");
	return -ENOMEM;
}

static void nf_conntrack_standalone_fini_sysctl(void)
{
	unregister_sysctl_table(nf_ct_netfilter_header);
	unregister_sysctl_table(nf_ct_sysctl_header);
}
#else
static int nf_conntrack_standalone_init_sysctl(void)
{
	return 0;
}

static void nf_conntrack_standalone_fini_sysctl(void)
{
}
#endif /* CONFIG_SYSCTL */

static int nf_conntrack_net_init(struct net *net)
{
	return nf_conntrack_init(net);
}

static void nf_conntrack_net_exit(struct net *net)
{
	nf_conntrack_cleanup(net);
}

static struct pernet_operations nf_conntrack_net_ops = {
	.init = nf_conntrack_net_init,
	.exit = nf_conntrack_net_exit,
};

static int __init nf_conntrack_standalone_init(void)
{
	int ret;

	ret = register_pernet_subsys(&nf_conntrack_net_ops);
	if (ret < 0)
		goto out;
	ret = nf_conntrack_standalone_init_proc();
	if (ret < 0)
		goto out_proc;
	ret = nf_conntrack_standalone_init_sysctl();
	if (ret < 0)
		goto out_sysctl;
	return 0;

out_sysctl:
	nf_conntrack_standalone_fini_proc();
out_proc:
	unregister_pernet_subsys(&nf_conntrack_net_ops);
out:
	return ret;
}

static void __exit nf_conntrack_standalone_fini(void)
{
	nf_conntrack_standalone_fini_sysctl();
	nf_conntrack_standalone_fini_proc();
	unregister_pernet_subsys(&nf_conntrack_net_ops);
}

module_init(nf_conntrack_standalone_init);
module_exit(nf_conntrack_standalone_fini);

/* Some modules need us, but don't depend directly on any symbol.
   They should call this. */
void need_conntrack(void)
{
}
EXPORT_SYMBOL_GPL(need_conntrack);

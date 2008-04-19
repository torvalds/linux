/* ip_conntrack proc compat - based on ip_conntrack_standalone.c
 *
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <net/net_namespace.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_expect.h>

#ifdef CONFIG_NF_CT_ACCT
static unsigned int
seq_print_counters(struct seq_file *s,
		   const struct ip_conntrack_counter *counter)
{
	return seq_printf(s, "packets=%llu bytes=%llu ",
			  (unsigned long long)counter->packets,
			  (unsigned long long)counter->bytes);
}
#else
#define seq_print_counters(x, y)	0
#endif

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
	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num != AF_INET)
		return 0;

	l3proto = __nf_ct_l3proto_find(ct->tuplehash[IP_CT_DIR_ORIGINAL]
				       .tuple.src.l3num);
	NF_CT_ASSERT(l3proto);
	l4proto = __nf_ct_l4proto_find(ct->tuplehash[IP_CT_DIR_ORIGINAL]
				       .tuple.src.l3num,
				       ct->tuplehash[IP_CT_DIR_ORIGINAL]
				       .tuple.dst.protonum);
	NF_CT_ASSERT(l4proto);

	if (seq_printf(s, "%-8s %u %ld ",
		      l4proto->name,
		      ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum,
		      timer_pending(&ct->timeout)
		      ? (long)(ct->timeout.expires - jiffies)/HZ : 0) != 0)
		return -ENOSPC;

	if (l4proto->print_conntrack && l4proto->print_conntrack(s, ct))
		return -ENOSPC;

	if (print_tuple(s, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
			l3proto, l4proto))
		return -ENOSPC;

	if (seq_print_counters(s, &ct->counters[IP_CT_DIR_ORIGINAL]))
		return -ENOSPC;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &ct->status)))
		if (seq_printf(s, "[UNREPLIED] "))
			return -ENOSPC;

	if (print_tuple(s, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
			l3proto, l4proto))
		return -ENOSPC;

	if (seq_print_counters(s, &ct->counters[IP_CT_DIR_REPLY]))
		return -ENOSPC;

	if (test_bit(IPS_ASSURED_BIT, &ct->status))
		if (seq_printf(s, "[ASSURED] "))
			return -ENOSPC;

#ifdef CONFIG_NF_CONNTRACK_MARK
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

/* expects */
struct ct_expect_iter_state {
	unsigned int bucket;
};

static struct hlist_node *ct_expect_get_first(struct seq_file *seq)
{
	struct ct_expect_iter_state *st = seq->private;
	struct hlist_node *n;

	for (st->bucket = 0; st->bucket < nf_ct_expect_hsize; st->bucket++) {
		n = rcu_dereference(nf_ct_expect_hash[st->bucket].first);
		if (n)
			return n;
	}
	return NULL;
}

static struct hlist_node *ct_expect_get_next(struct seq_file *seq,
					     struct hlist_node *head)
{
	struct ct_expect_iter_state *st = seq->private;

	head = rcu_dereference(head->next);
	while (head == NULL) {
		if (++st->bucket >= nf_ct_expect_hsize)
			return NULL;
		head = rcu_dereference(nf_ct_expect_hash[st->bucket].first);
	}
	return head;
}

static struct hlist_node *ct_expect_get_idx(struct seq_file *seq, loff_t pos)
{
	struct hlist_node *head = ct_expect_get_first(seq);

	if (head)
		while (pos && (head = ct_expect_get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *exp_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return ct_expect_get_idx(seq, *pos);
}

static void *exp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_expect_get_next(seq, v);
}

static void exp_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_expect *exp;
	const struct hlist_node *n = v;

	exp = hlist_entry(n, struct nf_conntrack_expect, hnode);

	if (exp->tuple.src.l3num != AF_INET)
		return 0;

	if (exp->timeout.function)
		seq_printf(s, "%ld ", timer_pending(&exp->timeout)
			   ? (long)(exp->timeout.expires - jiffies)/HZ : 0);
	else
		seq_printf(s, "- ");

	seq_printf(s, "proto=%u ", exp->tuple.dst.protonum);

	print_tuple(s, &exp->tuple,
		    __nf_ct_l3proto_find(exp->tuple.src.l3num),
		    __nf_ct_l4proto_find(exp->tuple.src.l3num,
					 exp->tuple.dst.protonum));
	return seq_putc(s, '\n');
}

static const struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};

static int exp_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &exp_seq_ops,
			sizeof(struct ct_expect_iter_state));
}

static const struct file_operations ip_exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
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
		*pos = cpu+1;
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
		*pos = cpu+1;
		return &per_cpu(nf_conntrack_stat, cpu);
	}

	return NULL;
}

static void ct_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int ct_cpu_seq_show(struct seq_file *seq, void *v)
{
	unsigned int nr_conntracks = atomic_read(&nf_conntrack_count);
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
	.start  = ct_cpu_seq_start,
	.next   = ct_cpu_seq_next,
	.stop   = ct_cpu_seq_stop,
	.show   = ct_cpu_seq_show,
};

static int ct_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_cpu_seq_ops);
}

static const struct file_operations ct_cpu_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = ct_cpu_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

int __init nf_conntrack_ipv4_compat_init(void)
{
	struct proc_dir_entry *proc, *proc_exp, *proc_stat;

	proc = proc_net_fops_create(&init_net, "ip_conntrack", 0440, &ct_file_ops);
	if (!proc)
		goto err1;

	proc_exp = proc_net_fops_create(&init_net, "ip_conntrack_expect", 0440,
					&ip_exp_file_ops);
	if (!proc_exp)
		goto err2;

	proc_stat = proc_create("ip_conntrack", S_IRUGO,
				init_net.proc_net_stat, &ct_cpu_seq_fops);
	if (!proc_stat)
		goto err3;
	return 0;

err3:
	proc_net_remove(&init_net, "ip_conntrack_expect");
err2:
	proc_net_remove(&init_net, "ip_conntrack");
err1:
	return -ENOMEM;
}

void __exit nf_conntrack_ipv4_compat_fini(void)
{
	remove_proc_entry("ip_conntrack", init_net.proc_net_stat);
	proc_net_remove(&init_net, "ip_conntrack_expect");
	proc_net_remove(&init_net, "ip_conntrack");
}

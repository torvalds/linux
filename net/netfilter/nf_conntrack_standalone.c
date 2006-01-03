/* This file contains all the functions required for the standalone
   nf_conntrack module.

   These are not required by the compatibility layer.
*/

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from net/ipv4/netfilter/ip_conntrack_standalone.c
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/netdevice.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#define ASSERT_READ_LOCK(x)
#define ASSERT_WRITE_LOCK(x)

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");

extern atomic_t nf_conntrack_count;
DECLARE_PER_CPU(struct ip_conntrack_stat, nf_conntrack_stat);

static int kill_l3proto(struct nf_conn *i, void *data)
{
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num == 
			((struct nf_conntrack_l3proto *)data)->l3proto);
}

static int kill_proto(struct nf_conn *i, void *data)
{
	struct nf_conntrack_protocol *proto;
	proto = (struct nf_conntrack_protocol *)data;
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum == 
			proto->proto) &&
	       (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num ==
			proto->l3proto);
}

#ifdef CONFIG_PROC_FS
static int
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
	    struct nf_conntrack_l3proto *l3proto,
	    struct nf_conntrack_protocol *proto)
{
	return l3proto->print_tuple(s, tuple) || proto->print_tuple(s, tuple);
}

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

static struct list_head *ct_get_first(struct seq_file *seq)
{
	struct ct_iter_state *st = seq->private;

	for (st->bucket = 0;
	     st->bucket < nf_conntrack_htable_size;
	     st->bucket++) {
		if (!list_empty(&nf_conntrack_hash[st->bucket]))
			return nf_conntrack_hash[st->bucket].next;
	}
	return NULL;
}

static struct list_head *ct_get_next(struct seq_file *seq, struct list_head *head)
{
	struct ct_iter_state *st = seq->private;

	head = head->next;
	while (head == &nf_conntrack_hash[st->bucket]) {
		if (++st->bucket >= nf_conntrack_htable_size)
			return NULL;
		head = nf_conntrack_hash[st->bucket].next;
	}
	return head;
}

static struct list_head *ct_get_idx(struct seq_file *seq, loff_t pos)
{
	struct list_head *head = ct_get_first(seq);

	if (head)
		while (pos && (head = ct_get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *ct_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&nf_conntrack_lock);
	return ct_get_idx(seq, *pos);
}

static void *ct_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_get_next(s, v);
}

static void ct_seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&nf_conntrack_lock);
}

/* return 0 on success, 1 in case of error */
static int ct_seq_show(struct seq_file *s, void *v)
{
	const struct nf_conntrack_tuple_hash *hash = v;
	const struct nf_conn *conntrack = nf_ct_tuplehash_to_ctrack(hash);
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_protocol *proto;

	ASSERT_READ_LOCK(&nf_conntrack_lock);
	NF_CT_ASSERT(conntrack);

	/* we only want to print DIR_ORIGINAL */
	if (NF_CT_DIRECTION(hash))
		return 0;

	l3proto = nf_ct_find_l3proto(conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
				     .tuple.src.l3num);

	NF_CT_ASSERT(l3proto);
	proto = nf_ct_find_proto(conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
				 .tuple.src.l3num,
				 conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
				 .tuple.dst.protonum);
	NF_CT_ASSERT(proto);

	if (seq_printf(s, "%-8s %u %-8s %u %ld ",
		       l3proto->name,
		       conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num,
		       proto->name,
		       conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum,
		       timer_pending(&conntrack->timeout)
		       ? (long)(conntrack->timeout.expires - jiffies)/HZ : 0) != 0)
		return -ENOSPC;

	if (l3proto->print_conntrack(s, conntrack))
		return -ENOSPC;

	if (proto->print_conntrack(s, conntrack))
		return -ENOSPC;

	if (print_tuple(s, &conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
			l3proto, proto))
		return -ENOSPC;

	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_ORIGINAL]))
		return -ENOSPC;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &conntrack->status)))
		if (seq_printf(s, "[UNREPLIED] "))
			return -ENOSPC;

	if (print_tuple(s, &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple,
			l3proto, proto))
		return -ENOSPC;

	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_REPLY]))
		return -ENOSPC;

	if (test_bit(IPS_ASSURED_BIT, &conntrack->status))
		if (seq_printf(s, "[ASSURED] "))
			return -ENOSPC;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (seq_printf(s, "mark=%u ", conntrack->mark))
		return -ENOSPC;
#endif

	if (seq_printf(s, "use=%u\n", atomic_read(&conntrack->ct_general.use)))
		return -ENOSPC;
	
	return 0;
}

static struct seq_operations ct_seq_ops = {
	.start = ct_seq_start,
	.next  = ct_seq_next,
	.stop  = ct_seq_stop,
	.show  = ct_seq_show
};

static int ct_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct ct_iter_state *st;
	int ret;

	st = kmalloc(sizeof(struct ct_iter_state), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;
	ret = seq_open(file, &ct_seq_ops);
	if (ret)
		goto out_free;
	seq          = file->private_data;
	seq->private = st;
	memset(st, 0, sizeof(struct ct_iter_state));
	return ret;
out_free:
	kfree(st);
	return ret;
}

static struct file_operations ct_file_ops = {
	.owner   = THIS_MODULE,
	.open    = ct_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

/* expects */
static void *exp_seq_start(struct seq_file *s, loff_t *pos)
{
	struct list_head *e = &nf_conntrack_expect_list;
	loff_t i;

	/* strange seq_file api calls stop even if we fail,
	 * thus we need to grab lock since stop unlocks */
	read_lock_bh(&nf_conntrack_lock);

	if (list_empty(e))
		return NULL;

	for (i = 0; i <= *pos; i++) {
		e = e->next;
		if (e == &nf_conntrack_expect_list)
			return NULL;
	}
	return e;
}

static void *exp_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct list_head *e = v;

	++*pos;
	e = e->next;

	if (e == &nf_conntrack_expect_list)
		return NULL;

	return e;
}

static void exp_seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&nf_conntrack_lock);
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_expect *expect = v;

	if (expect->timeout.function)
		seq_printf(s, "%ld ", timer_pending(&expect->timeout)
			   ? (long)(expect->timeout.expires - jiffies)/HZ : 0);
	else
		seq_printf(s, "- ");
	seq_printf(s, "l3proto = %u proto=%u ",
		   expect->tuple.src.l3num,
		   expect->tuple.dst.protonum);
	print_tuple(s, &expect->tuple,
		    nf_ct_find_l3proto(expect->tuple.src.l3num),
		    nf_ct_find_proto(expect->tuple.src.l3num,
				     expect->tuple.dst.protonum));
	return seq_putc(s, '\n');
}

static struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};

static int exp_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &exp_seq_ops);
}

static struct file_operations exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
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
	unsigned int nr_conntracks = atomic_read(&nf_conntrack_count);
	struct ip_conntrack_stat *st = v;

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

static struct seq_operations ct_cpu_seq_ops = {
	.start	= ct_cpu_seq_start,
	.next	= ct_cpu_seq_next,
	.stop	= ct_cpu_seq_stop,
	.show	= ct_cpu_seq_show,
};

static int ct_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_cpu_seq_ops);
}

static struct file_operations ct_cpu_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ct_cpu_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};
#endif /* CONFIG_PROC_FS */

/* Sysctl support */

#ifdef CONFIG_SYSCTL

/* From nf_conntrack_core.c */
extern int nf_conntrack_max;
extern unsigned int nf_conntrack_htable_size;

/* From nf_conntrack_proto_tcp.c */
extern unsigned long nf_ct_tcp_timeout_syn_sent;
extern unsigned long nf_ct_tcp_timeout_syn_recv;
extern unsigned long nf_ct_tcp_timeout_established;
extern unsigned long nf_ct_tcp_timeout_fin_wait;
extern unsigned long nf_ct_tcp_timeout_close_wait;
extern unsigned long nf_ct_tcp_timeout_last_ack;
extern unsigned long nf_ct_tcp_timeout_time_wait;
extern unsigned long nf_ct_tcp_timeout_close;
extern unsigned long nf_ct_tcp_timeout_max_retrans;
extern int nf_ct_tcp_loose;
extern int nf_ct_tcp_be_liberal;
extern int nf_ct_tcp_max_retrans;

/* From nf_conntrack_proto_udp.c */
extern unsigned long nf_ct_udp_timeout;
extern unsigned long nf_ct_udp_timeout_stream;

/* From nf_conntrack_proto_generic.c */
extern unsigned long nf_ct_generic_timeout;

/* Log invalid packets of a given protocol */
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;

static struct ctl_table_header *nf_ct_sysctl_header;

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
		.data		= &nf_conntrack_count,
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
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_SYN_SENT,
		.procname	= "nf_conntrack_tcp_timeout_syn_sent",
		.data		= &nf_ct_tcp_timeout_syn_sent,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_SYN_RECV,
		.procname	= "nf_conntrack_tcp_timeout_syn_recv",
		.data		= &nf_ct_tcp_timeout_syn_recv,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_ESTABLISHED,
		.procname	= "nf_conntrack_tcp_timeout_established",
		.data		= &nf_ct_tcp_timeout_established,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_FIN_WAIT,
		.procname	= "nf_conntrack_tcp_timeout_fin_wait",
		.data		= &nf_ct_tcp_timeout_fin_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_CLOSE_WAIT,
		.procname	= "nf_conntrack_tcp_timeout_close_wait",
		.data		= &nf_ct_tcp_timeout_close_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_LAST_ACK,
		.procname	= "nf_conntrack_tcp_timeout_last_ack",
		.data		= &nf_ct_tcp_timeout_last_ack,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_TIME_WAIT,
		.procname	= "nf_conntrack_tcp_timeout_time_wait",
		.data		= &nf_ct_tcp_timeout_time_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_CLOSE,
		.procname	= "nf_conntrack_tcp_timeout_close",
		.data		= &nf_ct_tcp_timeout_close,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_UDP_TIMEOUT,
		.procname	= "nf_conntrack_udp_timeout",
		.data		= &nf_ct_udp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_UDP_TIMEOUT_STREAM,
		.procname	= "nf_conntrack_udp_timeout_stream",
		.data		= &nf_ct_udp_timeout_stream,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_GENERIC_TIMEOUT,
		.procname	= "nf_conntrack_generic_timeout",
		.data		= &nf_ct_generic_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
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
		.ctl_name	= NET_NF_CONNTRACK_TCP_TIMEOUT_MAX_RETRANS,
		.procname	= "nf_conntrack_tcp_timeout_max_retrans",
		.data		= &nf_ct_tcp_timeout_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_LOOSE,
		.procname	= "nf_conntrack_tcp_loose",
		.data		= &nf_ct_tcp_loose,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_BE_LIBERAL,
		.procname       = "nf_conntrack_tcp_be_liberal",
		.data           = &nf_ct_tcp_be_liberal,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_MAX_RETRANS,
		.procname	= "nf_conntrack_tcp_max_retrans",
		.data		= &nf_ct_tcp_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},

	{ .ctl_name = 0 }
};

#define NET_NF_CONNTRACK_MAX 2089

static ctl_table nf_ct_netfilter_table[] = {
	{
		.ctl_name	= NET_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
		.child		= nf_ct_sysctl_table,
	},
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

static ctl_table nf_ct_net_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= nf_ct_netfilter_table,
	},
	{ .ctl_name = 0 }
};
EXPORT_SYMBOL(nf_ct_log_invalid);
#endif /* CONFIG_SYSCTL */

static int init_or_cleanup(int init)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc, *proc_exp, *proc_stat;
#endif
	int ret = 0;

	if (!init) goto cleanup;

	ret = nf_conntrack_init();
	if (ret < 0)
		goto cleanup_nothing;

#ifdef CONFIG_PROC_FS
	proc = proc_net_fops_create("nf_conntrack", 0440, &ct_file_ops);
	if (!proc) goto cleanup_init;

	proc_exp = proc_net_fops_create("nf_conntrack_expect", 0440,
					&exp_file_ops);
	if (!proc_exp) goto cleanup_proc;

	proc_stat = create_proc_entry("nf_conntrack", S_IRUGO, proc_net_stat);
	if (!proc_stat)
		goto cleanup_proc_exp;

	proc_stat->proc_fops = &ct_cpu_seq_fops;
	proc_stat->owner = THIS_MODULE;
#endif
#ifdef CONFIG_SYSCTL
	nf_ct_sysctl_header = register_sysctl_table(nf_ct_net_table, 0);
	if (nf_ct_sysctl_header == NULL) {
		printk("nf_conntrack: can't register to sysctl.\n");
		ret = -ENOMEM;
		goto cleanup_proc_stat;
	}
#endif

	return ret;

 cleanup:
#ifdef CONFIG_SYSCTL
 	unregister_sysctl_table(nf_ct_sysctl_header);
 cleanup_proc_stat:
#endif
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nf_conntrack", proc_net_stat);
 cleanup_proc_exp:
	proc_net_remove("nf_conntrack_expect");
 cleanup_proc:
	proc_net_remove("nf_conntrack");
 cleanup_init:
#endif /* CNFIG_PROC_FS */
	nf_conntrack_cleanup();
 cleanup_nothing:
	return ret;
}

int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto)
{
	int ret = 0;

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_l3protos[proto->l3proto] != &nf_conntrack_generic_l3proto) {
		ret = -EBUSY;
		goto out;
	}
	nf_ct_l3protos[proto->l3proto] = proto;
out:
	write_unlock_bh(&nf_conntrack_lock);

	return ret;
}

void nf_conntrack_l3proto_unregister(struct nf_conntrack_l3proto *proto)
{
	write_lock_bh(&nf_conntrack_lock);
	nf_ct_l3protos[proto->l3proto] = &nf_conntrack_generic_l3proto;
	write_unlock_bh(&nf_conntrack_lock);
	
	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l3proto, proto);
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int nf_conntrack_protocol_register(struct nf_conntrack_protocol *proto)
{
	int ret = 0;

retry:
	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_protos[proto->l3proto]) {
		if (nf_ct_protos[proto->l3proto][proto->proto]
				!= &nf_conntrack_generic_protocol) {
			ret = -EBUSY;
			goto out_unlock;
		}
	} else {
		/* l3proto may be loaded latter. */
		struct nf_conntrack_protocol **proto_array;
		int i;

		write_unlock_bh(&nf_conntrack_lock);

		proto_array = (struct nf_conntrack_protocol **)
				kmalloc(MAX_NF_CT_PROTO *
					 sizeof(struct nf_conntrack_protocol *),
					GFP_KERNEL);
		if (proto_array == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < MAX_NF_CT_PROTO; i++)
			proto_array[i] = &nf_conntrack_generic_protocol;

		write_lock_bh(&nf_conntrack_lock);
		if (nf_ct_protos[proto->l3proto]) {
			/* bad timing, but no problem */
			write_unlock_bh(&nf_conntrack_lock);
			kfree(proto_array);
		} else {
			nf_ct_protos[proto->l3proto] = proto_array;
			write_unlock_bh(&nf_conntrack_lock);
		}

		/*
		 * Just once because array is never freed until unloading
		 * nf_conntrack.ko
		 */
		goto retry;
	}

	nf_ct_protos[proto->l3proto][proto->proto] = proto;

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
out:
	return ret;
}

void nf_conntrack_protocol_unregister(struct nf_conntrack_protocol *proto)
{
	write_lock_bh(&nf_conntrack_lock);
	nf_ct_protos[proto->l3proto][proto->proto]
		= &nf_conntrack_generic_protocol;
	write_unlock_bh(&nf_conntrack_lock);
	
	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_proto, proto);
}

static int __init init(void)
{
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

module_init(init);
module_exit(fini);

/* Some modules need us, but don't depend directly on any symbol.
   They should call this. */
void need_nf_conntrack(void)
{
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
EXPORT_SYMBOL_GPL(nf_conntrack_chain);
EXPORT_SYMBOL_GPL(nf_conntrack_expect_chain);
EXPORT_SYMBOL_GPL(nf_conntrack_register_notifier);
EXPORT_SYMBOL_GPL(nf_conntrack_unregister_notifier);
EXPORT_SYMBOL_GPL(__nf_ct_event_cache_init);
EXPORT_PER_CPU_SYMBOL_GPL(nf_conntrack_ecache);
EXPORT_SYMBOL_GPL(nf_ct_deliver_cached_events);
#endif
EXPORT_SYMBOL(nf_conntrack_l3proto_register);
EXPORT_SYMBOL(nf_conntrack_l3proto_unregister);
EXPORT_SYMBOL(nf_conntrack_protocol_register);
EXPORT_SYMBOL(nf_conntrack_protocol_unregister);
EXPORT_SYMBOL(nf_ct_invert_tuplepr);
EXPORT_SYMBOL(nf_conntrack_alter_reply);
EXPORT_SYMBOL(nf_conntrack_destroyed);
EXPORT_SYMBOL(need_nf_conntrack);
EXPORT_SYMBOL(nf_conntrack_helper_register);
EXPORT_SYMBOL(nf_conntrack_helper_unregister);
EXPORT_SYMBOL(nf_ct_iterate_cleanup);
EXPORT_SYMBOL(__nf_ct_refresh_acct);
EXPORT_SYMBOL(nf_ct_protos);
EXPORT_SYMBOL(nf_ct_find_proto);
EXPORT_SYMBOL(nf_ct_l3protos);
EXPORT_SYMBOL(nf_conntrack_expect_alloc);
EXPORT_SYMBOL(nf_conntrack_expect_put);
EXPORT_SYMBOL(nf_conntrack_expect_related);
EXPORT_SYMBOL(nf_conntrack_unexpect_related);
EXPORT_SYMBOL(nf_conntrack_tuple_taken);
EXPORT_SYMBOL(nf_conntrack_htable_size);
EXPORT_SYMBOL(nf_conntrack_lock);
EXPORT_SYMBOL(nf_conntrack_hash);
EXPORT_SYMBOL(nf_conntrack_untracked);
EXPORT_SYMBOL_GPL(nf_conntrack_find_get);
#ifdef CONFIG_IP_NF_NAT_NEEDED
EXPORT_SYMBOL(nf_conntrack_tcp_update);
#endif
EXPORT_SYMBOL(__nf_conntrack_confirm);
EXPORT_SYMBOL(nf_ct_get_tuple);
EXPORT_SYMBOL(nf_ct_invert_tuple);
EXPORT_SYMBOL(nf_conntrack_in);
EXPORT_SYMBOL(__nf_conntrack_attach);

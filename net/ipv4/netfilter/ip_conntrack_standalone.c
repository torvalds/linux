/* This file contains all the functions required for the standalone
   ip_conntrack module.

   These are not required by the compatibility layer.
*/

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <net/checksum.h>
#include <net/ip.h>
#include <net/route.h>

#define ASSERT_READ_LOCK(x)
#define ASSERT_WRITE_LOCK(x)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");

extern atomic_t ip_conntrack_count;
DECLARE_PER_CPU(struct ip_conntrack_stat, ip_conntrack_stat);

static int kill_proto(struct ip_conntrack *i, void *data)
{
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum == 
			*((u_int8_t *) data));
}

#ifdef CONFIG_PROC_FS
static int
print_tuple(struct seq_file *s, const struct ip_conntrack_tuple *tuple,
	    struct ip_conntrack_protocol *proto)
{
	seq_printf(s, "src=%u.%u.%u.%u dst=%u.%u.%u.%u ",
		   NIPQUAD(tuple->src.ip), NIPQUAD(tuple->dst.ip));
	return proto->print_tuple(s, tuple);
}

#ifdef CONFIG_IP_NF_CT_ACCT
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
	     st->bucket < ip_conntrack_htable_size;
	     st->bucket++) {
		if (!list_empty(&ip_conntrack_hash[st->bucket]))
			return ip_conntrack_hash[st->bucket].next;
	}
	return NULL;
}

static struct list_head *ct_get_next(struct seq_file *seq, struct list_head *head)
{
	struct ct_iter_state *st = seq->private;

	head = head->next;
	while (head == &ip_conntrack_hash[st->bucket]) {
		if (++st->bucket >= ip_conntrack_htable_size)
			return NULL;
		head = ip_conntrack_hash[st->bucket].next;
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
	read_lock_bh(&ip_conntrack_lock);
	return ct_get_idx(seq, *pos);
}

static void *ct_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_get_next(s, v);
}
  
static void ct_seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&ip_conntrack_lock);
}
 
static int ct_seq_show(struct seq_file *s, void *v)
{
	const struct ip_conntrack_tuple_hash *hash = v;
	const struct ip_conntrack *conntrack = tuplehash_to_ctrack(hash);
	struct ip_conntrack_protocol *proto;

	ASSERT_READ_LOCK(&ip_conntrack_lock);
	IP_NF_ASSERT(conntrack);

	/* we only want to print DIR_ORIGINAL */
	if (DIRECTION(hash))
		return 0;

	proto = __ip_conntrack_proto_find(conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum);
	IP_NF_ASSERT(proto);

	if (seq_printf(s, "%-8s %u %ld ",
		      proto->name,
		      conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum,
		      timer_pending(&conntrack->timeout)
		      ? (long)(conntrack->timeout.expires - jiffies)/HZ
		      : 0) != 0)
		return -ENOSPC;

	if (proto->print_conntrack(s, conntrack))
		return -ENOSPC;
  
	if (print_tuple(s, &conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
			proto))
		return -ENOSPC;

 	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_ORIGINAL]))
		return -ENOSPC;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &conntrack->status)))
		if (seq_printf(s, "[UNREPLIED] "))
			return -ENOSPC;

	if (print_tuple(s, &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple,
			proto))
		return -ENOSPC;

 	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_REPLY]))
		return -ENOSPC;

	if (test_bit(IPS_ASSURED_BIT, &conntrack->status))
		if (seq_printf(s, "[ASSURED] "))
			return -ENOSPC;

#if defined(CONFIG_IP_NF_CONNTRACK_MARK)
	if (seq_printf(s, "mark=%u ", conntrack->mark))
		return -ENOSPC;
#endif

#ifdef CONFIG_IP_NF_CONNTRACK_SECMARK
	if (seq_printf(s, "secmark=%u ", conntrack->secmark))
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
	struct list_head *e = &ip_conntrack_expect_list;
	loff_t i;

	/* strange seq_file api calls stop even if we fail,
	 * thus we need to grab lock since stop unlocks */
	read_lock_bh(&ip_conntrack_lock);

	if (list_empty(e))
		return NULL;

	for (i = 0; i <= *pos; i++) {
		e = e->next;
		if (e == &ip_conntrack_expect_list)
			return NULL;
	}
	return e;
}

static void *exp_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
 	struct list_head *e = v;

	++*pos;
	e = e->next;

	if (e == &ip_conntrack_expect_list)
		return NULL;

	return e;
}

static void exp_seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&ip_conntrack_lock);
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct ip_conntrack_expect *expect = v;

	if (expect->timeout.function)
		seq_printf(s, "%ld ", timer_pending(&expect->timeout)
			   ? (long)(expect->timeout.expires - jiffies)/HZ : 0);
	else
		seq_printf(s, "- ");

	seq_printf(s, "proto=%u ", expect->tuple.dst.protonum);

	print_tuple(s, &expect->tuple,
		    __ip_conntrack_proto_find(expect->tuple.dst.protonum));
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
		*pos = cpu+1;
		return &per_cpu(ip_conntrack_stat, cpu);
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
		return &per_cpu(ip_conntrack_stat, cpu);
	}

	return NULL;
}

static void ct_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int ct_cpu_seq_show(struct seq_file *seq, void *v)
{
	unsigned int nr_conntracks = atomic_read(&ip_conntrack_count);
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
	.start  = ct_cpu_seq_start,
	.next   = ct_cpu_seq_next,
	.stop   = ct_cpu_seq_stop,
	.show   = ct_cpu_seq_show,
};

static int ct_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_cpu_seq_ops);
}

static struct file_operations ct_cpu_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = ct_cpu_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};
#endif

static unsigned int ip_confirm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	/* We've seen it coming out the other side: confirm it */
	return ip_conntrack_confirm(pskb);
}

static unsigned int ip_conntrack_help(unsigned int hooknum,
				      struct sk_buff **pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;

	/* This is where we call the helper: as the packet goes out. */
	ct = ip_conntrack_get(*pskb, &ctinfo);
	if (ct && ct->helper && ctinfo != IP_CT_RELATED + IP_CT_IS_REPLY) {
		unsigned int ret;
		ret = ct->helper->help(pskb, ct, ctinfo);
		if (ret != NF_ACCEPT)
			return ret;
	}
	return NF_ACCEPT;
}

static unsigned int ip_conntrack_defrag(unsigned int hooknum,
				        struct sk_buff **pskb,
				        const struct net_device *in,
				        const struct net_device *out,
				        int (*okfn)(struct sk_buff *))
{
#if !defined(CONFIG_IP_NF_NAT) && !defined(CONFIG_IP_NF_NAT_MODULE)
	/* Previously seen (loopback)?  Ignore.  Do this before
           fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;
#endif

	/* Gather fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb,
		                           hooknum == NF_IP_PRE_ROUTING ? 
					   IP_DEFRAG_CONNTRACK_IN :
					   IP_DEFRAG_CONNTRACK_OUT);
		if (!*pskb)
			return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static unsigned int ip_conntrack_local(unsigned int hooknum,
				       struct sk_buff **pskb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
	return ip_conntrack_in(hooknum, pskb, in, out, okfn);
}

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ip_conntrack_ops[] = {
	{
		.hook		= ip_conntrack_defrag,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ip_conntrack_in,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ip_conntrack_defrag,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ip_conntrack_local,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ip_conntrack_help,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ip_conntrack_help,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ip_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
	{
		.hook		= ip_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

/* Sysctl support */

#ifdef CONFIG_SYSCTL

/* From ip_conntrack_core.c */
extern int ip_conntrack_max;
extern unsigned int ip_conntrack_htable_size;

/* From ip_conntrack_proto_tcp.c */
extern unsigned int ip_ct_tcp_timeout_syn_sent;
extern unsigned int ip_ct_tcp_timeout_syn_recv;
extern unsigned int ip_ct_tcp_timeout_established;
extern unsigned int ip_ct_tcp_timeout_fin_wait;
extern unsigned int ip_ct_tcp_timeout_close_wait;
extern unsigned int ip_ct_tcp_timeout_last_ack;
extern unsigned int ip_ct_tcp_timeout_time_wait;
extern unsigned int ip_ct_tcp_timeout_close;
extern unsigned int ip_ct_tcp_timeout_max_retrans;
extern int ip_ct_tcp_loose;
extern int ip_ct_tcp_be_liberal;
extern int ip_ct_tcp_max_retrans;

/* From ip_conntrack_proto_udp.c */
extern unsigned int ip_ct_udp_timeout;
extern unsigned int ip_ct_udp_timeout_stream;

/* From ip_conntrack_proto_icmp.c */
extern unsigned int ip_ct_icmp_timeout;

/* From ip_conntrack_proto_icmp.c */
extern unsigned int ip_ct_generic_timeout;

/* Log invalid packets of a given protocol */
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;

int ip_conntrack_checksum = 1;

static struct ctl_table_header *ip_ct_sysctl_header;

static ctl_table ip_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &ip_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_COUNT,
		.procname	= "ip_conntrack_count",
		.data		= &ip_conntrack_count,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_BUCKETS,
		.procname	= "ip_conntrack_buckets",
		.data		= &ip_conntrack_htable_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_CHECKSUM,
		.procname	= "ip_conntrack_checksum",
		.data		= &ip_conntrack_checksum,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_SYN_SENT,
		.procname	= "ip_conntrack_tcp_timeout_syn_sent",
		.data		= &ip_ct_tcp_timeout_syn_sent,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_SYN_RECV,
		.procname	= "ip_conntrack_tcp_timeout_syn_recv",
		.data		= &ip_ct_tcp_timeout_syn_recv,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_ESTABLISHED,
		.procname	= "ip_conntrack_tcp_timeout_established",
		.data		= &ip_ct_tcp_timeout_established,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_FIN_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_fin_wait",
		.data		= &ip_ct_tcp_timeout_fin_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_CLOSE_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_close_wait",
		.data		= &ip_ct_tcp_timeout_close_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_LAST_ACK,
		.procname	= "ip_conntrack_tcp_timeout_last_ack",
		.data		= &ip_ct_tcp_timeout_last_ack,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_TIME_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_time_wait",
		.data		= &ip_ct_tcp_timeout_time_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_CLOSE,
		.procname	= "ip_conntrack_tcp_timeout_close",
		.data		= &ip_ct_tcp_timeout_close,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_UDP_TIMEOUT,
		.procname	= "ip_conntrack_udp_timeout",
		.data		= &ip_ct_udp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_UDP_TIMEOUT_STREAM,
		.procname	= "ip_conntrack_udp_timeout_stream",
		.data		= &ip_ct_udp_timeout_stream,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_ICMP_TIMEOUT,
		.procname	= "ip_conntrack_icmp_timeout",
		.data		= &ip_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_GENERIC_TIMEOUT,
		.procname	= "ip_conntrack_generic_timeout",
		.data		= &ip_ct_generic_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_LOG_INVALID,
		.procname	= "ip_conntrack_log_invalid",
		.data		= &ip_ct_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_MAX_RETRANS,
		.procname	= "ip_conntrack_tcp_timeout_max_retrans",
		.data		= &ip_ct_tcp_timeout_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_LOOSE,
		.procname	= "ip_conntrack_tcp_loose",
		.data		= &ip_ct_tcp_loose,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_BE_LIBERAL,
		.procname	= "ip_conntrack_tcp_be_liberal",
		.data		= &ip_ct_tcp_be_liberal,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_MAX_RETRANS,
		.procname	= "ip_conntrack_tcp_max_retrans",
		.data		= &ip_ct_tcp_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

#define NET_IP_CONNTRACK_MAX 2089

static ctl_table ip_ct_netfilter_table[] = {
	{
		.ctl_name	= NET_IPV4_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
		.child		= ip_ct_sysctl_table,
	},
	{
		.ctl_name	= NET_IP_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &ip_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

static ctl_table ip_ct_ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4,
		.procname	= "ipv4",
		.mode		= 0555,
		.child		= ip_ct_netfilter_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table ip_ct_net_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555, 
		.child		= ip_ct_ipv4_table,
	},
	{ .ctl_name = 0 }
};

EXPORT_SYMBOL(ip_ct_log_invalid);
#endif /* CONFIG_SYSCTL */

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int ip_conntrack_protocol_register(struct ip_conntrack_protocol *proto)
{
	int ret = 0;

	write_lock_bh(&ip_conntrack_lock);
	if (ip_ct_protos[proto->proto] != &ip_conntrack_generic_protocol) {
		ret = -EBUSY;
		goto out;
	}
	ip_ct_protos[proto->proto] = proto;
 out:
	write_unlock_bh(&ip_conntrack_lock);
	return ret;
}

void ip_conntrack_protocol_unregister(struct ip_conntrack_protocol *proto)
{
	write_lock_bh(&ip_conntrack_lock);
	ip_ct_protos[proto->proto] = &ip_conntrack_generic_protocol;
	write_unlock_bh(&ip_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	ip_ct_iterate_cleanup(kill_proto, &proto->proto);
}

static int __init ip_conntrack_standalone_init(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc, *proc_exp, *proc_stat;
#endif
	int ret = 0;

	ret = ip_conntrack_init();
	if (ret < 0)
		return ret;

#ifdef CONFIG_PROC_FS
	ret = -ENOMEM;
	proc = proc_net_fops_create("ip_conntrack", 0440, &ct_file_ops);
	if (!proc) goto cleanup_init;

	proc_exp = proc_net_fops_create("ip_conntrack_expect", 0440,
					&exp_file_ops);
	if (!proc_exp) goto cleanup_proc;

	proc_stat = create_proc_entry("ip_conntrack", S_IRUGO, proc_net_stat);
	if (!proc_stat)
		goto cleanup_proc_exp;

	proc_stat->proc_fops = &ct_cpu_seq_fops;
	proc_stat->owner = THIS_MODULE;
#endif

	ret = nf_register_hooks(ip_conntrack_ops, ARRAY_SIZE(ip_conntrack_ops));
	if (ret < 0) {
		printk("ip_conntrack: can't register hooks.\n");
		goto cleanup_proc_stat;
	}
#ifdef CONFIG_SYSCTL
	ip_ct_sysctl_header = register_sysctl_table(ip_ct_net_table, 0);
	if (ip_ct_sysctl_header == NULL) {
		printk("ip_conntrack: can't register to sysctl.\n");
		ret = -ENOMEM;
		goto cleanup_hooks;
	}
#endif
	return ret;

#ifdef CONFIG_SYSCTL
 cleanup_hooks:
	nf_unregister_hooks(ip_conntrack_ops, ARRAY_SIZE(ip_conntrack_ops));
#endif
 cleanup_proc_stat:
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ip_conntrack", proc_net_stat);
 cleanup_proc_exp:
	proc_net_remove("ip_conntrack_expect");
 cleanup_proc:
	proc_net_remove("ip_conntrack");
 cleanup_init:
#endif /* CONFIG_PROC_FS */
	ip_conntrack_cleanup();
	return ret;
}

static void __exit ip_conntrack_standalone_fini(void)
{
	synchronize_net();
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(ip_ct_sysctl_header);
#endif
	nf_unregister_hooks(ip_conntrack_ops, ARRAY_SIZE(ip_conntrack_ops));
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ip_conntrack", proc_net_stat);
	proc_net_remove("ip_conntrack_expect");
	proc_net_remove("ip_conntrack");
#endif /* CONFIG_PROC_FS */
	ip_conntrack_cleanup();
}

module_init(ip_conntrack_standalone_init);
module_exit(ip_conntrack_standalone_fini);

/* Some modules need us, but don't depend directly on any symbol.
   They should call this. */
void need_conntrack(void)
{
}

#ifdef CONFIG_IP_NF_CONNTRACK_EVENTS
EXPORT_SYMBOL_GPL(ip_conntrack_chain);
EXPORT_SYMBOL_GPL(ip_conntrack_expect_chain);
EXPORT_SYMBOL_GPL(ip_conntrack_register_notifier);
EXPORT_SYMBOL_GPL(ip_conntrack_unregister_notifier);
EXPORT_SYMBOL_GPL(__ip_ct_event_cache_init);
EXPORT_PER_CPU_SYMBOL_GPL(ip_conntrack_ecache);
#endif
EXPORT_SYMBOL(ip_conntrack_protocol_register);
EXPORT_SYMBOL(ip_conntrack_protocol_unregister);
EXPORT_SYMBOL(ip_ct_get_tuple);
EXPORT_SYMBOL(invert_tuplepr);
EXPORT_SYMBOL(ip_conntrack_alter_reply);
EXPORT_SYMBOL(ip_conntrack_destroyed);
EXPORT_SYMBOL(need_conntrack);
EXPORT_SYMBOL(ip_conntrack_helper_register);
EXPORT_SYMBOL(ip_conntrack_helper_unregister);
EXPORT_SYMBOL(ip_ct_iterate_cleanup);
EXPORT_SYMBOL(__ip_ct_refresh_acct);

EXPORT_SYMBOL(ip_conntrack_expect_alloc);
EXPORT_SYMBOL(ip_conntrack_expect_put);
EXPORT_SYMBOL_GPL(__ip_conntrack_expect_find);
EXPORT_SYMBOL_GPL(ip_conntrack_expect_find);
EXPORT_SYMBOL(ip_conntrack_expect_related);
EXPORT_SYMBOL(ip_conntrack_unexpect_related);
EXPORT_SYMBOL_GPL(ip_conntrack_expect_list);
EXPORT_SYMBOL_GPL(ip_ct_unlink_expect);

EXPORT_SYMBOL(ip_conntrack_tuple_taken);
EXPORT_SYMBOL(ip_ct_gather_frags);
EXPORT_SYMBOL(ip_conntrack_htable_size);
EXPORT_SYMBOL(ip_conntrack_lock);
EXPORT_SYMBOL(ip_conntrack_hash);
EXPORT_SYMBOL(ip_conntrack_untracked);
EXPORT_SYMBOL_GPL(ip_conntrack_find_get);
#ifdef CONFIG_IP_NF_NAT_NEEDED
EXPORT_SYMBOL(ip_conntrack_tcp_update);
#endif

EXPORT_SYMBOL_GPL(ip_conntrack_flush);
EXPORT_SYMBOL_GPL(__ip_conntrack_find);

EXPORT_SYMBOL_GPL(ip_conntrack_alloc);
EXPORT_SYMBOL_GPL(ip_conntrack_free);
EXPORT_SYMBOL_GPL(ip_conntrack_hash_insert);

EXPORT_SYMBOL_GPL(ip_ct_remove_expectations);

EXPORT_SYMBOL_GPL(ip_conntrack_helper_find_get);
EXPORT_SYMBOL_GPL(ip_conntrack_helper_put);
EXPORT_SYMBOL_GPL(__ip_conntrack_helper_find_byname);

EXPORT_SYMBOL_GPL(ip_conntrack_proto_find_get);
EXPORT_SYMBOL_GPL(ip_conntrack_proto_put);
EXPORT_SYMBOL_GPL(__ip_conntrack_proto_find);
EXPORT_SYMBOL_GPL(ip_conntrack_checksum);
#if defined(CONFIG_IP_NF_CONNTRACK_NETLINK) || \
    defined(CONFIG_IP_NF_CONNTRACK_NETLINK_MODULE)
EXPORT_SYMBOL_GPL(ip_ct_port_tuple_to_nfattr);
EXPORT_SYMBOL_GPL(ip_ct_port_nfattr_to_tuple);
#endif

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>

#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/nf_nat_helper.h>

int (*nf_nat_seq_adjust_hook)(struct sk_buff *skb,
			      struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo);
EXPORT_SYMBOL_GPL(nf_nat_seq_adjust_hook);

static bool ipv4_pkt_to_tuple(const struct sk_buff *skb, unsigned int nhoff,
			      struct nf_conntrack_tuple *tuple)
{
	const __be32 *ap;
	__be32 _addrs[2];
	ap = skb_header_pointer(skb, nhoff + offsetof(struct iphdr, saddr),
				sizeof(u_int32_t) * 2, _addrs);
	if (ap == NULL)
		return false;

	tuple->src.u3.ip = ap[0];
	tuple->dst.u3.ip = ap[1];

	return true;
}

static bool ipv4_invert_tuple(struct nf_conntrack_tuple *tuple,
			      const struct nf_conntrack_tuple *orig)
{
	tuple->src.u3.ip = orig->dst.u3.ip;
	tuple->dst.u3.ip = orig->src.u3.ip;

	return true;
}

static int ipv4_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "src=%u.%u.%u.%u dst=%u.%u.%u.%u ",
			  NIPQUAD(tuple->src.u3.ip),
			  NIPQUAD(tuple->dst.u3.ip));
}

/* Returns new sk_buff, or NULL */
static int nf_ct_ipv4_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	int err;

	skb_orphan(skb);

	local_bh_disable();
	err = ip_defrag(skb, user);
	local_bh_enable();

	if (!err)
		ip_send_check(ip_hdr(skb));

	return err;
}

static int ipv4_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			    unsigned int *dataoff, u_int8_t *protonum)
{
	const struct iphdr *iph;
	struct iphdr _iph;

	iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
	if (iph == NULL)
		return -NF_DROP;

	/* Conntrack defragments packets, we might still see fragments
	 * inside ICMP packets though. */
	if (iph->frag_off & htons(IP_OFFSET))
		return -NF_DROP;

	*dataoff = nhoff + (iph->ihl << 2);
	*protonum = iph->protocol;

	return NF_ACCEPT;
}

static unsigned int ipv4_confirm(unsigned int hooknum,
				 struct sk_buff *skb,
				 const struct net_device *in,
				 const struct net_device *out,
				 int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn_help *help;
	const struct nf_conntrack_helper *helper;
	unsigned int ret;

	/* This is where we call the helper: as the packet goes out. */
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED + IP_CT_IS_REPLY)
		goto out;

	help = nfct_help(ct);
	if (!help)
		goto out;

	/* rcu_read_lock()ed by nf_hook_slow */
	helper = rcu_dereference(help->helper);
	if (!helper)
		goto out;

	ret = helper->help(skb, skb_network_offset(skb) + ip_hdrlen(skb),
			   ct, ctinfo);
	if (ret != NF_ACCEPT)
		return ret;

	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status)) {
		typeof(nf_nat_seq_adjust_hook) seq_adjust;

		seq_adjust = rcu_dereference(nf_nat_seq_adjust_hook);
		if (!seq_adjust || !seq_adjust(skb, ct, ctinfo))
			return NF_DROP;
	}
out:
	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(skb);
}

static unsigned int ipv4_conntrack_defrag(unsigned int hooknum,
					  struct sk_buff *skb,
					  const struct net_device *in,
					  const struct net_device *out,
					  int (*okfn)(struct sk_buff *))
{
	/* Previously seen (loopback)?  Ignore.  Do this before
	   fragment check. */
	if (skb->nfct)
		return NF_ACCEPT;

	/* Gather fragments. */
	if (ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)) {
		if (nf_ct_ipv4_gather_frags(skb,
					    hooknum == NF_INET_PRE_ROUTING ?
					    IP_DEFRAG_CONNTRACK_IN :
					    IP_DEFRAG_CONNTRACK_OUT))
			return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static unsigned int ipv4_conntrack_in(unsigned int hooknum,
				      struct sk_buff *skb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	return nf_conntrack_in(PF_INET, hooknum, skb);
}

static unsigned int ipv4_conntrack_local(unsigned int hooknum,
					 struct sk_buff *skb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr) ||
	    ip_hdrlen(skb) < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
	return nf_conntrack_in(PF_INET, hooknum, skb);
}

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ipv4_conntrack_ops[] __read_mostly = {
	{
		.hook		= ipv4_conntrack_defrag,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv4_conntrack_in,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook           = ipv4_conntrack_defrag,
		.owner          = THIS_MODULE,
		.pf             = PF_INET,
		.hooknum        = NF_INET_LOCAL_OUT,
		.priority       = NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv4_conntrack_local,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
	{
		.hook		= ipv4_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;

static ctl_table ip_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_COUNT,
		.procname	= "ip_conntrack_count",
		.data		= &nf_conntrack_count,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_BUCKETS,
		.procname	= "ip_conntrack_buckets",
		.data		= &nf_conntrack_htable_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_CHECKSUM,
		.procname	= "ip_conntrack_checksum",
		.data		= &nf_conntrack_checksum,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_LOG_INVALID,
		.procname	= "ip_conntrack_log_invalid",
		.data		= &nf_ct_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
	{
		.ctl_name	= 0
	}
};
#endif /* CONFIG_SYSCTL && CONFIG_NF_CONNTRACK_PROC_COMPAT */

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;

	NF_CT_TUPLE_U_BLANK(&tuple);
	tuple.src.u3.ip = inet->rcv_saddr;
	tuple.src.u.tcp.port = inet->sport;
	tuple.dst.u3.ip = inet->daddr;
	tuple.dst.u.tcp.port = inet->dport;
	tuple.src.l3num = PF_INET;
	tuple.dst.protonum = IPPROTO_TCP;

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->sk_prot->name, "TCP")) {
		pr_debug("SO_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		pr_debug("SO_ORIGINAL_DST: len %d not %Zu\n",
			 *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = nf_conntrack_find_get(&tuple);
	if (h) {
		struct sockaddr_in sin;
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		sin.sin_family = AF_INET;
		sin.sin_port = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u3.ip;
		memset(sin.sin_zero, 0, sizeof(sin.sin_zero));

		pr_debug("SO_ORIGINAL_DST: %u.%u.%u.%u %u\n",
			 NIPQUAD(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		nf_ct_put(ct);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	pr_debug("SO_ORIGINAL_DST: Can't find %u.%u.%u.%u/%u-%u.%u.%u.%u/%u.\n",
		 NIPQUAD(tuple.src.u3.ip), ntohs(tuple.src.u.tcp.port),
		 NIPQUAD(tuple.dst.u3.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int ipv4_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	NLA_PUT_BE32(skb, CTA_IP_V4_SRC, tuple->src.u3.ip);
	NLA_PUT_BE32(skb, CTA_IP_V4_DST, tuple->dst.u3.ip);
	return 0;

nla_put_failure:
	return -1;
}

static const struct nla_policy ipv4_nla_policy[CTA_IP_MAX+1] = {
	[CTA_IP_V4_SRC]	= { .type = NLA_U32 },
	[CTA_IP_V4_DST]	= { .type = NLA_U32 },
};

static int ipv4_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V4_SRC] || !tb[CTA_IP_V4_DST])
		return -EINVAL;

	t->src.u3.ip = nla_get_be32(tb[CTA_IP_V4_SRC]);
	t->dst.u3.ip = nla_get_be32(tb[CTA_IP_V4_DST]);

	return 0;
}
#endif

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET,
	.get_optmin	= SO_ORIGINAL_DST,
	.get_optmax	= SO_ORIGINAL_DST+1,
	.get		= &getorigdst,
	.owner		= THIS_MODULE,
};

struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4 __read_mostly = {
	.l3proto	 = PF_INET,
	.name		 = "ipv4",
	.pkt_to_tuple	 = ipv4_pkt_to_tuple,
	.invert_tuple	 = ipv4_invert_tuple,
	.print_tuple	 = ipv4_print_tuple,
	.get_l4proto	 = ipv4_get_l4proto,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nlattr = ipv4_tuple_to_nlattr,
	.nlattr_to_tuple = ipv4_nlattr_to_tuple,
	.nla_policy	 = ipv4_nla_policy,
#endif
#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	.ctl_table_path  = nf_net_ipv4_netfilter_sysctl_path,
	.ctl_table	 = ip_ct_sysctl_table,
#endif
	.me		 = THIS_MODULE,
};

module_param_call(hashsize, nf_conntrack_set_hashsize, param_get_uint,
		  &nf_conntrack_htable_size, 0600);

MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET));
MODULE_ALIAS("ip_conntrack");
MODULE_LICENSE("GPL");

static int __init nf_conntrack_l3proto_ipv4_init(void)
{
	int ret = 0;

	need_conntrack();

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register netfilter socket option\n");
		return ret;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_tcp4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register tcp.\n");
		goto cleanup_sockopt;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_udp4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register udp.\n");
		goto cleanup_tcp;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_icmp);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register icmp.\n");
		goto cleanup_udp;
	}

	ret = nf_conntrack_l3proto_register(&nf_conntrack_l3proto_ipv4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register ipv4\n");
		goto cleanup_icmp;
	}

	ret = nf_register_hooks(ipv4_conntrack_ops,
				ARRAY_SIZE(ipv4_conntrack_ops));
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register hooks.\n");
		goto cleanup_ipv4;
	}
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	ret = nf_conntrack_ipv4_compat_init();
	if (ret < 0)
		goto cleanup_hooks;
#endif
	return ret;
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
 cleanup_hooks:
	nf_unregister_hooks(ipv4_conntrack_ops, ARRAY_SIZE(ipv4_conntrack_ops));
#endif
 cleanup_ipv4:
	nf_conntrack_l3proto_unregister(&nf_conntrack_l3proto_ipv4);
 cleanup_icmp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_icmp);
 cleanup_udp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udp4);
 cleanup_tcp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_tcp4);
 cleanup_sockopt:
	nf_unregister_sockopt(&so_getorigdst);
	return ret;
}

static void __exit nf_conntrack_l3proto_ipv4_fini(void)
{
	synchronize_net();
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	nf_conntrack_ipv4_compat_fini();
#endif
	nf_unregister_hooks(ipv4_conntrack_ops, ARRAY_SIZE(ipv4_conntrack_ops));
	nf_conntrack_l3proto_unregister(&nf_conntrack_l3proto_ipv4);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_icmp);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udp4);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_tcp4);
	nf_unregister_sockopt(&so_getorigdst);
}

module_init(nf_conntrack_l3proto_ipv4_init);
module_exit(nf_conntrack_l3proto_ipv4_fini);

void need_ipv4_conntrack(void)
{
	return;
}
EXPORT_SYMBOL_GPL(need_ipv4_conntrack);

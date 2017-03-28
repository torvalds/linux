
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
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
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/ipv4/nf_defrag_ipv4.h>
#include <net/netfilter/nf_log.h>

static int conntrack4_net_id __read_mostly;
static DEFINE_MUTEX(register_ipv4_hooks);

struct conntrack4_net {
	unsigned int users;
};

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

static void ipv4_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	seq_printf(s, "src=%pI4 dst=%pI4 ",
		   &tuple->src.u3.ip, &tuple->dst.u3.ip);
}

static int ipv4_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			    unsigned int *dataoff, u_int8_t *protonum)
{
	const struct iphdr *iph;
	struct iphdr _iph;

	iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
	if (iph == NULL)
		return -NF_ACCEPT;

	/* Conntrack defragments packets, we might still see fragments
	 * inside ICMP packets though. */
	if (iph->frag_off & htons(IP_OFFSET))
		return -NF_ACCEPT;

	*dataoff = nhoff + (iph->ihl << 2);
	*protonum = iph->protocol;

	/* Check bogus IP headers */
	if (*dataoff > skb->len) {
		pr_debug("nf_conntrack_ipv4: bogus IPv4 packet: "
			 "nhoff %u, ihl %u, skblen %u\n",
			 nhoff, iph->ihl << 2, skb->len);
		return -NF_ACCEPT;
	}

	return NF_ACCEPT;
}

static unsigned int ipv4_helper(void *priv,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn_help *help;
	const struct nf_conntrack_helper *helper;

	/* This is where we call the helper: as the packet goes out. */
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		return NF_ACCEPT;

	help = nfct_help(ct);
	if (!help)
		return NF_ACCEPT;

	/* rcu_read_lock()ed by nf_hook_thresh */
	helper = rcu_dereference(help->helper);
	if (!helper)
		return NF_ACCEPT;

	return helper->help(skb, skb_network_offset(skb) + ip_hdrlen(skb),
			    ct, ctinfo);
}

static unsigned int ipv4_confirm(void *priv,
				 struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		goto out;

	/* adjust seqs for loopback traffic only in outgoing direction */
	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status) &&
	    !nf_is_loopback_packet(skb)) {
		if (!nf_ct_seq_adjust(skb, ct, ctinfo, ip_hdrlen(skb))) {
			NF_CT_STAT_INC_ATOMIC(nf_ct_net(ct), drop);
			return NF_DROP;
		}
	}
out:
	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(skb);
}

static unsigned int ipv4_conntrack_in(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	return nf_conntrack_in(state->net, PF_INET, state->hook, skb);
}

static unsigned int ipv4_conntrack_local(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr) ||
	    ip_hdrlen(skb) < sizeof(struct iphdr))
		return NF_ACCEPT;

	if (ip_is_fragment(ip_hdr(skb))) /* IP_NODEFRAG setsockopt set */
		return NF_ACCEPT;

	return nf_conntrack_in(state->net, PF_INET, state->hook, skb);
}

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ipv4_conntrack_ops[] __read_mostly = {
	{
		.hook		= ipv4_conntrack_in,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_conntrack_local,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_helper,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv4_confirm,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
	{
		.hook		= ipv4_helper,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv4_confirm,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

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

	memset(&tuple, 0, sizeof(tuple));
	tuple.src.u3.ip = inet->inet_rcv_saddr;
	tuple.src.u.tcp.port = inet->inet_sport;
	tuple.dst.u3.ip = inet->inet_daddr;
	tuple.dst.u.tcp.port = inet->inet_dport;
	tuple.src.l3num = PF_INET;
	tuple.dst.protonum = sk->sk_protocol;

	/* We only do TCP and SCTP at the moment: is there a better way? */
	if (sk->sk_protocol != IPPROTO_TCP && sk->sk_protocol != IPPROTO_SCTP) {
		pr_debug("SO_ORIGINAL_DST: Not a TCP/SCTP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		pr_debug("SO_ORIGINAL_DST: len %d not %zu\n",
			 *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = nf_conntrack_find_get(sock_net(sk), &nf_ct_zone_dflt, &tuple);
	if (h) {
		struct sockaddr_in sin;
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		sin.sin_family = AF_INET;
		sin.sin_port = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u3.ip;
		memset(sin.sin_zero, 0, sizeof(sin.sin_zero));

		pr_debug("SO_ORIGINAL_DST: %pI4 %u\n",
			 &sin.sin_addr.s_addr, ntohs(sin.sin_port));
		nf_ct_put(ct);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	pr_debug("SO_ORIGINAL_DST: Can't find %pI4/%u-%pI4/%u.\n",
		 &tuple.src.u3.ip, ntohs(tuple.src.u.tcp.port),
		 &tuple.dst.u3.ip, ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int ipv4_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	if (nla_put_in_addr(skb, CTA_IP_V4_SRC, tuple->src.u3.ip) ||
	    nla_put_in_addr(skb, CTA_IP_V4_DST, tuple->dst.u3.ip))
		goto nla_put_failure;
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

	t->src.u3.ip = nla_get_in_addr(tb[CTA_IP_V4_SRC]);
	t->dst.u3.ip = nla_get_in_addr(tb[CTA_IP_V4_DST]);

	return 0;
}

static int ipv4_nlattr_tuple_size(void)
{
	return nla_policy_len(ipv4_nla_policy, CTA_IP_MAX + 1);
}
#endif

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET,
	.get_optmin	= SO_ORIGINAL_DST,
	.get_optmax	= SO_ORIGINAL_DST+1,
	.get		= getorigdst,
	.owner		= THIS_MODULE,
};

static int ipv4_hooks_register(struct net *net)
{
	struct conntrack4_net *cnet = net_generic(net, conntrack4_net_id);
	int err = 0;

	mutex_lock(&register_ipv4_hooks);

	cnet->users++;
	if (cnet->users > 1)
		goto out_unlock;

	err = nf_defrag_ipv4_enable(net);
	if (err) {
		cnet->users = 0;
		goto out_unlock;
	}

	err = nf_register_net_hooks(net, ipv4_conntrack_ops,
				    ARRAY_SIZE(ipv4_conntrack_ops));

	if (err)
		cnet->users = 0;
 out_unlock:
	mutex_unlock(&register_ipv4_hooks);
	return err;
}

static void ipv4_hooks_unregister(struct net *net)
{
	struct conntrack4_net *cnet = net_generic(net, conntrack4_net_id);

	mutex_lock(&register_ipv4_hooks);
	if (cnet->users && (--cnet->users == 0))
		nf_unregister_net_hooks(net, ipv4_conntrack_ops,
					ARRAY_SIZE(ipv4_conntrack_ops));
	mutex_unlock(&register_ipv4_hooks);
}

struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4 __read_mostly = {
	.l3proto	 = PF_INET,
	.name		 = "ipv4",
	.pkt_to_tuple	 = ipv4_pkt_to_tuple,
	.invert_tuple	 = ipv4_invert_tuple,
	.print_tuple	 = ipv4_print_tuple,
	.get_l4proto	 = ipv4_get_l4proto,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr = ipv4_tuple_to_nlattr,
	.nlattr_tuple_size = ipv4_nlattr_tuple_size,
	.nlattr_to_tuple = ipv4_nlattr_to_tuple,
	.nla_policy	 = ipv4_nla_policy,
#endif
	.net_ns_get	 = ipv4_hooks_register,
	.net_ns_put	 = ipv4_hooks_unregister,
	.me		 = THIS_MODULE,
};

module_param_call(hashsize, nf_conntrack_set_hashsize, param_get_uint,
		  &nf_conntrack_htable_size, 0600);

MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET));
MODULE_ALIAS("ip_conntrack");
MODULE_LICENSE("GPL");

static struct nf_conntrack_l4proto *builtin_l4proto4[] = {
	&nf_conntrack_l4proto_tcp4,
	&nf_conntrack_l4proto_udp4,
	&nf_conntrack_l4proto_icmp,
#ifdef CONFIG_NF_CT_PROTO_DCCP
	&nf_conntrack_l4proto_dccp4,
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	&nf_conntrack_l4proto_sctp4,
#endif
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	&nf_conntrack_l4proto_udplite4,
#endif
};

static int ipv4_net_init(struct net *net)
{
	int ret = 0;

	ret = nf_ct_l4proto_pernet_register(net, builtin_l4proto4,
					    ARRAY_SIZE(builtin_l4proto4));
	if (ret < 0)
		return ret;
	ret = nf_ct_l3proto_pernet_register(net, &nf_conntrack_l3proto_ipv4);
	if (ret < 0) {
		pr_err("nf_conntrack_ipv4: pernet registration failed\n");
		nf_ct_l4proto_pernet_unregister(net, builtin_l4proto4,
						ARRAY_SIZE(builtin_l4proto4));
	}
	return ret;
}

static void ipv4_net_exit(struct net *net)
{
	nf_ct_l3proto_pernet_unregister(net, &nf_conntrack_l3proto_ipv4);
	nf_ct_l4proto_pernet_unregister(net, builtin_l4proto4,
					ARRAY_SIZE(builtin_l4proto4));
}

static struct pernet_operations ipv4_net_ops = {
	.init = ipv4_net_init,
	.exit = ipv4_net_exit,
	.id = &conntrack4_net_id,
	.size = sizeof(struct conntrack4_net),
};

static int __init nf_conntrack_l3proto_ipv4_init(void)
{
	int ret = 0;

	need_conntrack();

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret < 0) {
		pr_err("Unable to register netfilter socket option\n");
		return ret;
	}

	ret = register_pernet_subsys(&ipv4_net_ops);
	if (ret < 0) {
		pr_err("nf_conntrack_ipv4: can't register pernet ops\n");
		goto cleanup_sockopt;
	}

	ret = nf_ct_l4proto_register(builtin_l4proto4,
				     ARRAY_SIZE(builtin_l4proto4));
	if (ret < 0)
		goto cleanup_pernet;

	ret = nf_ct_l3proto_register(&nf_conntrack_l3proto_ipv4);
	if (ret < 0) {
		pr_err("nf_conntrack_ipv4: can't register ipv4 proto.\n");
		goto cleanup_l4proto;
	}

	return ret;
cleanup_l4proto:
	nf_ct_l4proto_unregister(builtin_l4proto4,
				 ARRAY_SIZE(builtin_l4proto4));
 cleanup_pernet:
	unregister_pernet_subsys(&ipv4_net_ops);
 cleanup_sockopt:
	nf_unregister_sockopt(&so_getorigdst);
	return ret;
}

static void __exit nf_conntrack_l3proto_ipv4_fini(void)
{
	synchronize_net();
	nf_ct_l3proto_unregister(&nf_conntrack_l3proto_ipv4);
	nf_ct_l4proto_unregister(builtin_l4proto4,
				 ARRAY_SIZE(builtin_l4proto4));
	unregister_pernet_subsys(&ipv4_net_ops);
	nf_unregister_sockopt(&so_getorigdst);
}

module_init(nf_conntrack_l3proto_ipv4_init);
module_exit(nf_conntrack_l3proto_ipv4_fini);

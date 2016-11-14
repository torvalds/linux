/*
 * Copyright (C)2004 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 */

#include <linux/types.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <net/ipv6.h>
#include <net/inet_frag.h>

#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include <net/netfilter/nf_log.h>

static bool ipv6_pkt_to_tuple(const struct sk_buff *skb, unsigned int nhoff,
			      struct nf_conntrack_tuple *tuple)
{
	const u_int32_t *ap;
	u_int32_t _addrs[8];

	ap = skb_header_pointer(skb, nhoff + offsetof(struct ipv6hdr, saddr),
				sizeof(_addrs), _addrs);
	if (ap == NULL)
		return false;

	memcpy(tuple->src.u3.ip6, ap, sizeof(tuple->src.u3.ip6));
	memcpy(tuple->dst.u3.ip6, ap + 4, sizeof(tuple->dst.u3.ip6));

	return true;
}

static bool ipv6_invert_tuple(struct nf_conntrack_tuple *tuple,
			      const struct nf_conntrack_tuple *orig)
{
	memcpy(tuple->src.u3.ip6, orig->dst.u3.ip6, sizeof(tuple->src.u3.ip6));
	memcpy(tuple->dst.u3.ip6, orig->src.u3.ip6, sizeof(tuple->dst.u3.ip6));

	return true;
}

static void ipv6_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	seq_printf(s, "src=%pI6 dst=%pI6 ",
		   tuple->src.u3.ip6, tuple->dst.u3.ip6);
}

static int ipv6_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			    unsigned int *dataoff, u_int8_t *protonum)
{
	unsigned int extoff = nhoff + sizeof(struct ipv6hdr);
	__be16 frag_off;
	int protoff;
	u8 nexthdr;

	if (skb_copy_bits(skb, nhoff + offsetof(struct ipv6hdr, nexthdr),
			  &nexthdr, sizeof(nexthdr)) != 0) {
		pr_debug("ip6_conntrack_core: can't get nexthdr\n");
		return -NF_ACCEPT;
	}
	protoff = ipv6_skip_exthdr(skb, extoff, &nexthdr, &frag_off);
	/*
	 * (protoff == skb->len) means the packet has not data, just
	 * IPv6 and possibly extensions headers, but it is tracked anyway
	 */
	if (protoff < 0 || (frag_off & htons(~0x7)) != 0) {
		pr_debug("ip6_conntrack_core: can't find proto in pkt\n");
		return -NF_ACCEPT;
	}

	*dataoff = protoff;
	*protonum = nexthdr;
	return NF_ACCEPT;
}

static unsigned int ipv6_helper(void *priv,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	const struct nf_conn_help *help;
	const struct nf_conntrack_helper *helper;
	enum ip_conntrack_info ctinfo;
	__be16 frag_off;
	int protoff;
	u8 nexthdr;

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

	nexthdr = ipv6_hdr(skb)->nexthdr;
	protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr,
				   &frag_off);
	if (protoff < 0 || (frag_off & htons(~0x7)) != 0) {
		pr_debug("proto header not found\n");
		return NF_ACCEPT;
	}

	return helper->help(skb, protoff, ct, ctinfo);
}

static unsigned int ipv6_confirm(void *priv,
				 struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned char pnum = ipv6_hdr(skb)->nexthdr;
	int protoff;
	__be16 frag_off;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		goto out;

	protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &pnum,
				   &frag_off);
	if (protoff < 0 || (frag_off & htons(~0x7)) != 0) {
		pr_debug("proto header not found\n");
		goto out;
	}

	/* adjust seqs for loopback traffic only in outgoing direction */
	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status) &&
	    !nf_is_loopback_packet(skb)) {
		if (!nf_ct_seq_adjust(skb, ct, ctinfo, protoff)) {
			NF_CT_STAT_INC_ATOMIC(nf_ct_net(ct), drop);
			return NF_DROP;
		}
	}
out:
	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(skb);
}

static unsigned int ipv6_conntrack_in(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	return nf_conntrack_in(state->net, PF_INET6, state->hook, skb);
}

static unsigned int ipv6_conntrack_local(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct ipv6hdr)) {
		net_notice_ratelimited("ipv6_conntrack_local: packet too short\n");
		return NF_ACCEPT;
	}
	return nf_conntrack_in(state->net, PF_INET6, state->hook, skb);
}

static struct nf_hook_ops ipv6_conntrack_ops[] __read_mostly = {
	{
		.hook		= ipv6_conntrack_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_CONNTRACK,
	},
	{
		.hook		= ipv6_conntrack_local,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_CONNTRACK,
	},
	{
		.hook		= ipv6_helper,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv6_confirm,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_LAST,
	},
	{
		.hook		= ipv6_helper,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv6_confirm,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_LAST-1,
	},
};

static int
ipv6_getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *inet6 = inet6_sk(sk);
	const struct nf_conntrack_tuple_hash *h;
	struct sockaddr_in6 sin6;
	struct nf_conntrack_tuple tuple = { .src.l3num = NFPROTO_IPV6 };
	struct nf_conn *ct;

	tuple.src.u3.in6 = sk->sk_v6_rcv_saddr;
	tuple.src.u.tcp.port = inet->inet_sport;
	tuple.dst.u3.in6 = sk->sk_v6_daddr;
	tuple.dst.u.tcp.port = inet->inet_dport;
	tuple.dst.protonum = sk->sk_protocol;

	if (sk->sk_protocol != IPPROTO_TCP && sk->sk_protocol != IPPROTO_SCTP)
		return -ENOPROTOOPT;

	if (*len < 0 || (unsigned int) *len < sizeof(sin6))
		return -EINVAL;

	h = nf_conntrack_find_get(sock_net(sk), &nf_ct_zone_dflt, &tuple);
	if (!h) {
		pr_debug("IP6T_SO_ORIGINAL_DST: Can't find %pI6c/%u-%pI6c/%u.\n",
			 &tuple.src.u3.ip6, ntohs(tuple.src.u.tcp.port),
			 &tuple.dst.u3.ip6, ntohs(tuple.dst.u.tcp.port));
		return -ENOENT;
	}

	ct = nf_ct_tuplehash_to_ctrack(h);

	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.tcp.port;
	sin6.sin6_flowinfo = inet6->flow_label & IPV6_FLOWINFO_MASK;
	memcpy(&sin6.sin6_addr,
		&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.in6,
					sizeof(sin6.sin6_addr));

	nf_ct_put(ct);
	sin6.sin6_scope_id = ipv6_iface_scope_id(&sin6.sin6_addr,
						 sk->sk_bound_dev_if);
	return copy_to_user(user, &sin6, sizeof(sin6)) ? -EFAULT : 0;
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int ipv6_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	if (nla_put_in6_addr(skb, CTA_IP_V6_SRC, &tuple->src.u3.in6) ||
	    nla_put_in6_addr(skb, CTA_IP_V6_DST, &tuple->dst.u3.in6))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nla_policy ipv6_nla_policy[CTA_IP_MAX+1] = {
	[CTA_IP_V6_SRC]	= { .len = sizeof(u_int32_t)*4 },
	[CTA_IP_V6_DST]	= { .len = sizeof(u_int32_t)*4 },
};

static int ipv6_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V6_SRC] || !tb[CTA_IP_V6_DST])
		return -EINVAL;

	t->src.u3.in6 = nla_get_in6_addr(tb[CTA_IP_V6_SRC]);
	t->dst.u3.in6 = nla_get_in6_addr(tb[CTA_IP_V6_DST]);

	return 0;
}

static int ipv6_nlattr_tuple_size(void)
{
	return nla_policy_len(ipv6_nla_policy, CTA_IP_MAX + 1);
}
#endif

struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv6 __read_mostly = {
	.l3proto		= PF_INET6,
	.name			= "ipv6",
	.pkt_to_tuple		= ipv6_pkt_to_tuple,
	.invert_tuple		= ipv6_invert_tuple,
	.print_tuple		= ipv6_print_tuple,
	.get_l4proto		= ipv6_get_l4proto,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr	= ipv6_tuple_to_nlattr,
	.nlattr_tuple_size	= ipv6_nlattr_tuple_size,
	.nlattr_to_tuple	= ipv6_nlattr_to_tuple,
	.nla_policy		= ipv6_nla_policy,
#endif
	.me			= THIS_MODULE,
};

MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET6));
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yasuyuki KOZAKAI @USAGI <yasuyuki.kozakai@toshiba.co.jp>");

static struct nf_sockopt_ops so_getorigdst6 = {
	.pf		= NFPROTO_IPV6,
	.get_optmin	= IP6T_SO_ORIGINAL_DST,
	.get_optmax	= IP6T_SO_ORIGINAL_DST + 1,
	.get		= ipv6_getorigdst,
	.owner		= THIS_MODULE,
};

static struct nf_conntrack_l4proto *builtin_l4proto6[] = {
	&nf_conntrack_l4proto_tcp6,
	&nf_conntrack_l4proto_udp6,
	&nf_conntrack_l4proto_icmpv6,
};

static int ipv6_net_init(struct net *net)
{
	int ret = 0;

	ret = nf_ct_l4proto_pernet_register(net, builtin_l4proto6,
					    ARRAY_SIZE(builtin_l4proto6));
	if (ret < 0)
		return ret;

	ret = nf_ct_l3proto_pernet_register(net, &nf_conntrack_l3proto_ipv6);
	if (ret < 0) {
		pr_err("nf_conntrack_ipv6: pernet registration failed.\n");
		nf_ct_l4proto_pernet_unregister(net, builtin_l4proto6,
						ARRAY_SIZE(builtin_l4proto6));
	}
	return ret;
}

static void ipv6_net_exit(struct net *net)
{
	nf_ct_l3proto_pernet_unregister(net, &nf_conntrack_l3proto_ipv6);
	nf_ct_l4proto_pernet_unregister(net, builtin_l4proto6,
					ARRAY_SIZE(builtin_l4proto6));
}

static struct pernet_operations ipv6_net_ops = {
	.init = ipv6_net_init,
	.exit = ipv6_net_exit,
};

static int __init nf_conntrack_l3proto_ipv6_init(void)
{
	int ret = 0;

	need_conntrack();
	nf_defrag_ipv6_enable();

	ret = nf_register_sockopt(&so_getorigdst6);
	if (ret < 0) {
		pr_err("Unable to register netfilter socket option\n");
		return ret;
	}

	ret = register_pernet_subsys(&ipv6_net_ops);
	if (ret < 0)
		goto cleanup_sockopt;

	ret = nf_register_hooks(ipv6_conntrack_ops,
				ARRAY_SIZE(ipv6_conntrack_ops));
	if (ret < 0) {
		pr_err("nf_conntrack_ipv6: can't register pre-routing defrag "
		       "hook.\n");
		goto cleanup_pernet;
	}

	ret = nf_ct_l4proto_register(builtin_l4proto6,
				     ARRAY_SIZE(builtin_l4proto6));
	if (ret < 0)
		goto cleanup_hooks;

	ret = nf_ct_l3proto_register(&nf_conntrack_l3proto_ipv6);
	if (ret < 0) {
		pr_err("nf_conntrack_ipv6: can't register ipv6 proto.\n");
		goto cleanup_l4proto;
	}
	return ret;
cleanup_l4proto:
	nf_ct_l4proto_unregister(builtin_l4proto6,
				 ARRAY_SIZE(builtin_l4proto6));
 cleanup_hooks:
	nf_unregister_hooks(ipv6_conntrack_ops, ARRAY_SIZE(ipv6_conntrack_ops));
 cleanup_pernet:
	unregister_pernet_subsys(&ipv6_net_ops);
 cleanup_sockopt:
	nf_unregister_sockopt(&so_getorigdst6);
	return ret;
}

static void __exit nf_conntrack_l3proto_ipv6_fini(void)
{
	synchronize_net();
	nf_ct_l3proto_unregister(&nf_conntrack_l3proto_ipv6);
	nf_ct_l4proto_unregister(builtin_l4proto6,
				 ARRAY_SIZE(builtin_l4proto6));
	nf_unregister_hooks(ipv6_conntrack_ops, ARRAY_SIZE(ipv6_conntrack_ops));
	unregister_pernet_subsys(&ipv6_net_ops);
	nf_unregister_sockopt(&so_getorigdst6);
}

module_init(nf_conntrack_l3proto_ipv6_init);
module_exit(nf_conntrack_l3proto_ipv6_fini);

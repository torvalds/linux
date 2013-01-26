/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv4 NAT code. Development of IPv6 NAT
 * funded by Astaro.
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>

static const struct xt_table nf_nat_ipv6_table = {
	.name		= "nat",
	.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
};

static unsigned int alloc_null_binding(struct nf_conn *ct, unsigned int hooknum)
{
	/* Force range to this IP; let proto decide mapping for
	 * per-proto parts (hence not IP_NAT_RANGE_PROTO_SPECIFIED).
	 */
	struct nf_nat_range range;

	range.flags = 0;
	pr_debug("Allocating NULL binding for %p (%pI6)\n", ct,
		 HOOK2MANIP(hooknum) == NF_NAT_MANIP_SRC ?
		 &ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.ip6 :
		 &ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.ip6);

	return nf_nat_setup_info(ct, &range, HOOK2MANIP(hooknum));
}

static unsigned int nf_nat_rule_find(struct sk_buff *skb, unsigned int hooknum,
				     const struct net_device *in,
				     const struct net_device *out,
				     struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	unsigned int ret;

	ret = ip6t_do_table(skb, hooknum, in, out, net->ipv6.ip6table_nat);
	if (ret == NF_ACCEPT) {
		if (!nf_nat_initialized(ct, HOOK2MANIP(hooknum)))
			ret = alloc_null_binding(ct, hooknum);
	}
	return ret;
}

static unsigned int
nf_nat_ipv6_fn(unsigned int hooknum,
	       struct sk_buff *skb,
	       const struct net_device *in,
	       const struct net_device *out,
	       int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	enum nf_nat_manip_type maniptype = HOOK2MANIP(hooknum);
	__be16 frag_off;
	int hdrlen;
	u8 nexthdr;

	ct = nf_ct_get(skb, &ctinfo);
	/* Can't track?  It's not due to stress, or conntrack would
	 * have dropped it.  Hence it's the user's responsibilty to
	 * packet filter it out, or implement conntrack/NAT for that
	 * protocol. 8) --RR
	 */
	if (!ct)
		return NF_ACCEPT;

	/* Don't try to NAT if this packet is not conntracked */
	if (nf_ct_is_untracked(ct))
		return NF_ACCEPT;

	nat = nfct_nat(ct);
	if (!nat) {
		/* NAT module was loaded late. */
		if (nf_ct_is_confirmed(ct))
			return NF_ACCEPT;
		nat = nf_ct_ext_add(ct, NF_CT_EXT_NAT, GFP_ATOMIC);
		if (nat == NULL) {
			pr_debug("failed to add NAT extension\n");
			return NF_ACCEPT;
		}
	}

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		nexthdr = ipv6_hdr(skb)->nexthdr;
		hdrlen = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
					  &nexthdr, &frag_off);

		if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
			if (!nf_nat_icmpv6_reply_translation(skb, ct, ctinfo,
							     hooknum, hdrlen))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall thru... (Only ICMPs can be IP_CT_IS_REPLY) */
	case IP_CT_NEW:
		/* Seen it before?  This can happen for loopback, retrans,
		 * or local packets.
		 */
		if (!nf_nat_initialized(ct, maniptype)) {
			unsigned int ret;

			ret = nf_nat_rule_find(skb, hooknum, in, out, ct);
			if (ret != NF_ACCEPT)
				return ret;
		} else {
			pr_debug("Already setup manip %s for ct %p\n",
				 maniptype == NF_NAT_MANIP_SRC ? "SRC" : "DST",
				 ct);
			if (nf_nat_oif_changed(hooknum, ctinfo, nat, out))
				goto oif_changed;
		}
		break;

	default:
		/* ESTABLISHED */
		NF_CT_ASSERT(ctinfo == IP_CT_ESTABLISHED ||
			     ctinfo == IP_CT_ESTABLISHED_REPLY);
		if (nf_nat_oif_changed(hooknum, ctinfo, nat, out))
			goto oif_changed;
	}

	return nf_nat_packet(ct, ctinfo, hooknum, skb);

oif_changed:
	nf_ct_kill_acct(ct, ctinfo, skb);
	return NF_DROP;
}

static unsigned int
nf_nat_ipv6_in(unsigned int hooknum,
	       struct sk_buff *skb,
	       const struct net_device *in,
	       const struct net_device *out,
	       int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	struct in6_addr daddr = ipv6_hdr(skb)->daddr;

	ret = nf_nat_ipv6_fn(hooknum, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    ipv6_addr_cmp(&daddr, &ipv6_hdr(skb)->daddr))
		skb_dst_drop(skb);

	return ret;
}

static unsigned int
nf_nat_ipv6_out(unsigned int hooknum,
		struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
#ifdef CONFIG_XFRM
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
#endif
	unsigned int ret;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct ipv6hdr))
		return NF_ACCEPT;

	ret = nf_nat_ipv6_fn(hooknum, skb, in, out, okfn);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    !(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
				      &ct->tuplehash[!dir].tuple.dst.u3) ||
		    (ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
		     ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all))
			if (nf_xfrm_me_harder(skb, AF_INET6) < 0)
				ret = NF_DROP;
	}
#endif
	return ret;
}

static unsigned int
nf_nat_ipv6_local_fn(unsigned int hooknum,
		     struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out,
		     int (*okfn)(struct sk_buff *))
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct ipv6hdr))
		return NF_ACCEPT;

	ret = nf_nat_ipv6_fn(hooknum, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.dst.u3,
				      &ct->tuplehash[!dir].tuple.src.u3)) {
			if (ip6_route_me_harder(skb))
				ret = NF_DROP;
		}
#ifdef CONFIG_XFRM
		else if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
			 ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all)
			if (nf_xfrm_me_harder(skb, AF_INET6))
				ret = NF_DROP;
#endif
	}
	return ret;
}

static struct nf_hook_ops nf_nat_ipv6_ops[] __read_mostly = {
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv6_in,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv6_out,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv6_local_fn,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv6_fn,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

static int __net_init ip6table_nat_net_init(struct net *net)
{
	struct ip6t_replace *repl;

	repl = ip6t_alloc_initial_table(&nf_nat_ipv6_table);
	if (repl == NULL)
		return -ENOMEM;
	net->ipv6.ip6table_nat = ip6t_register_table(net, &nf_nat_ipv6_table, repl);
	kfree(repl);
	return PTR_RET(net->ipv6.ip6table_nat);
}

static void __net_exit ip6table_nat_net_exit(struct net *net)
{
	ip6t_unregister_table(net, net->ipv6.ip6table_nat);
}

static struct pernet_operations ip6table_nat_net_ops = {
	.init	= ip6table_nat_net_init,
	.exit	= ip6table_nat_net_exit,
};

static int __init ip6table_nat_init(void)
{
	int err;

	err = register_pernet_subsys(&ip6table_nat_net_ops);
	if (err < 0)
		goto err1;

	err = nf_register_hooks(nf_nat_ipv6_ops, ARRAY_SIZE(nf_nat_ipv6_ops));
	if (err < 0)
		goto err2;
	return 0;

err2:
	unregister_pernet_subsys(&ip6table_nat_net_ops);
err1:
	return err;
}

static void __exit ip6table_nat_exit(void)
{
	nf_unregister_hooks(nf_nat_ipv6_ops, ARRAY_SIZE(nf_nat_ipv6_ops));
	unregister_pernet_subsys(&ip6table_nat_net_ops);
}

module_init(ip6table_nat_init);
module_exit(ip6table_nat_exit);

MODULE_LICENSE("GPL");

/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv6.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/ipv6.h>

/*
 * IPv6 NAT chains
 */

static unsigned int nf_nat_ipv6_fn(const struct nf_hook_ops *ops,
			      struct sk_buff *skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_nat *nat;
	enum nf_nat_manip_type maniptype = HOOK2MANIP(ops->hooknum);
	__be16 frag_off;
	int hdrlen;
	u8 nexthdr;
	struct nft_pktinfo pkt;
	unsigned int ret;

	if (ct == NULL || nf_ct_is_untracked(ct))
		return NF_ACCEPT;

	nat = nfct_nat(ct);
	if (nat == NULL) {
		/* Conntrack module was loaded late, can't add extension. */
		if (nf_ct_is_confirmed(ct))
			return NF_ACCEPT;
		nat = nf_ct_ext_add(ct, NF_CT_EXT_NAT, GFP_ATOMIC);
		if (nat == NULL)
			return NF_ACCEPT;
	}

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED + IP_CT_IS_REPLY:
		nexthdr = ipv6_hdr(skb)->nexthdr;
		hdrlen = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
					  &nexthdr, &frag_off);

		if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
			if (!nf_nat_icmpv6_reply_translation(skb, ct, ctinfo,
							   ops->hooknum,
							   hdrlen))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall through */
	case IP_CT_NEW:
		if (nf_nat_initialized(ct, maniptype))
			break;

		nft_set_pktinfo_ipv6(&pkt, ops, skb, in, out);

		ret = nft_do_chain_pktinfo(&pkt, ops);
		if (ret != NF_ACCEPT)
			return ret;
		if (!nf_nat_initialized(ct, maniptype)) {
			ret = nf_nat_alloc_null_binding(ct, ops->hooknum);
			if (ret != NF_ACCEPT)
				return ret;
		}
	default:
		break;
	}

	return nf_nat_packet(ct, ctinfo, ops->hooknum, skb);
}

static unsigned int nf_nat_ipv6_prerouting(const struct nf_hook_ops *ops,
				      struct sk_buff *skb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	struct in6_addr daddr = ipv6_hdr(skb)->daddr;
	unsigned int ret;

	ret = nf_nat_ipv6_fn(ops, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    ipv6_addr_cmp(&daddr, &ipv6_hdr(skb)->daddr))
		skb_dst_drop(skb);

	return ret;
}

static unsigned int nf_nat_ipv6_postrouting(const struct nf_hook_ops *ops,
				       struct sk_buff *skb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo __maybe_unused;
	const struct nf_conn *ct __maybe_unused;
	unsigned int ret;

	ret = nf_nat_ipv6_fn(ops, skb, in, out, okfn);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    !(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
				      &ct->tuplehash[!dir].tuple.dst.u3) ||
		    (ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all))
			if (nf_xfrm_me_harder(skb, AF_INET6) < 0)
				ret = NF_DROP;
	}
#endif
	return ret;
}

static unsigned int nf_nat_ipv6_output(const struct nf_hook_ops *ops,
				  struct sk_buff *skb,
				  const struct net_device *in,
				  const struct net_device *out,
				  int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int ret;

	ret = nf_nat_ipv6_fn(ops, skb, in, out, okfn);
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
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all)
			if (nf_xfrm_me_harder(skb, AF_INET6))
				ret = NF_DROP;
#endif
	}
	return ret;
}

static struct nf_chain_type nft_chain_nat_ipv6 = {
	.family		= NFPROTO_IPV6,
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.fn		= {
		[NF_INET_PRE_ROUTING]	= nf_nat_ipv6_prerouting,
		[NF_INET_POST_ROUTING]	= nf_nat_ipv6_postrouting,
		[NF_INET_LOCAL_OUT]	= nf_nat_ipv6_output,
		[NF_INET_LOCAL_IN]	= nf_nat_ipv6_fn,
	},
	.me		= THIS_MODULE,
};

static int __init nft_chain_nat_ipv6_init(void)
{
	int err;

	err = nft_register_chain_type(&nft_chain_nat_ipv6);
	if (err < 0)
		return err;

	return 0;
}

static void __exit nft_chain_nat_ipv6_exit(void)
{
	nft_unregister_chain_type(&nft_chain_nat_ipv6);
}

module_init(nft_chain_nat_ipv6_init);
module_exit(nft_chain_nat_ipv6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomasz Bursztyka <tomasz.bursztyka@linux.intel.com>");
MODULE_ALIAS_NFT_CHAIN(AF_INET6, "nat");

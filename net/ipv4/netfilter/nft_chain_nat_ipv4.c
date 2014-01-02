/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
 * Copyright (c) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/ip.h>

/*
 * NAT chains
 */

static unsigned int nf_nat_fn(const struct nf_hook_ops *ops,
			      struct sk_buff *skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_nat *nat;
	enum nf_nat_manip_type maniptype = HOOK2MANIP(ops->hooknum);
	struct nft_pktinfo pkt;
	unsigned int ret;

	if (ct == NULL || nf_ct_is_untracked(ct))
		return NF_ACCEPT;

	NF_CT_ASSERT(!(ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)));

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
		if (ip_hdr(skb)->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(skb, ct, ctinfo,
							   ops->hooknum))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall through */
	case IP_CT_NEW:
		if (nf_nat_initialized(ct, maniptype))
			break;

		nft_set_pktinfo_ipv4(&pkt, ops, skb, in, out);

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

static unsigned int nf_nat_prerouting(const struct nf_hook_ops *ops,
				      struct sk_buff *skb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	__be32 daddr = ip_hdr(skb)->daddr;
	unsigned int ret;

	ret = nf_nat_fn(ops, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    ip_hdr(skb)->daddr != daddr) {
		skb_dst_drop(skb);
	}
	return ret;
}

static unsigned int nf_nat_postrouting(const struct nf_hook_ops *ops,
				       struct sk_buff *skb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo __maybe_unused;
	const struct nf_conn *ct __maybe_unused;
	unsigned int ret;

	ret = nf_nat_fn(ops, skb, in, out, okfn);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.src.u3.ip !=
		    ct->tuplehash[!dir].tuple.dst.u3.ip ||
		    ct->tuplehash[dir].tuple.src.u.all !=
		    ct->tuplehash[!dir].tuple.dst.u.all)
			return nf_xfrm_me_harder(skb, AF_INET) == 0 ?
								ret : NF_DROP;
	}
#endif
	return ret;
}

static unsigned int nf_nat_output(const struct nf_hook_ops *ops,
				  struct sk_buff *skb,
				  const struct net_device *in,
				  const struct net_device *out,
				  int (*okfn)(struct sk_buff *))
{
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int ret;

	ret = nf_nat_fn(ops, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.dst.u3.ip !=
		    ct->tuplehash[!dir].tuple.src.u3.ip) {
			if (ip_route_me_harder(skb, RTN_UNSPEC))
				ret = NF_DROP;
		}
#ifdef CONFIG_XFRM
		else if (ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all)
			if (nf_xfrm_me_harder(skb, AF_INET))
				ret = NF_DROP;
#endif
	}
	return ret;
}

static struct nf_chain_type nft_chain_nat_ipv4 = {
	.family		= NFPROTO_IPV4,
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.fn		= {
		[NF_INET_PRE_ROUTING]	= nf_nat_prerouting,
		[NF_INET_POST_ROUTING]	= nf_nat_postrouting,
		[NF_INET_LOCAL_OUT]	= nf_nat_output,
		[NF_INET_LOCAL_IN]	= nf_nat_fn,
	},
	.me		= THIS_MODULE,
};

static int __init nft_chain_nat_init(void)
{
	int err;

	err = nft_register_chain_type(&nft_chain_nat_ipv4);
	if (err < 0)
		return err;

	return 0;
}

static void __exit nft_chain_nat_exit(void)
{
	nft_unregister_chain_type(&nft_chain_nat_ipv4);
}

module_init(nft_chain_nat_init);
module_exit(nft_chain_nat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_INET, "nat");

/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/ip.h>

struct nft_nat {
	enum nft_registers	sreg_addr_min:8;
	enum nft_registers	sreg_addr_max:8;
	enum nft_registers	sreg_proto_min:8;
	enum nft_registers	sreg_proto_max:8;
	enum nf_nat_manip_type	type;
};

static void nft_nat_eval(const struct nft_expr *expr,
			 struct nft_data data[NFT_REG_MAX + 1],
			 const struct nft_pktinfo *pkt)
{
	const struct nft_nat *priv = nft_expr_priv(expr);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(pkt->skb, &ctinfo);
	struct nf_nat_range range;

	memset(&range, 0, sizeof(range));
	if (priv->sreg_addr_min) {
		range.min_addr.ip = data[priv->sreg_addr_min].data[0];
		range.max_addr.ip = data[priv->sreg_addr_max].data[0];
		range.flags |= NF_NAT_RANGE_MAP_IPS;
	}

	if (priv->sreg_proto_min) {
		range.min_proto.all = data[priv->sreg_proto_min].data[0];
		range.max_proto.all = data[priv->sreg_proto_max].data[0];
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}

	data[NFT_REG_VERDICT].verdict =
		nf_nat_setup_info(ct, &range, priv->type);
}

static const struct nla_policy nft_nat_policy[NFTA_NAT_MAX + 1] = {
	[NFTA_NAT_ADDR_MIN]	= { .type = NLA_U32 },
	[NFTA_NAT_ADDR_MAX]	= { .type = NLA_U32 },
	[NFTA_NAT_PROTO_MIN]	= { .type = NLA_U32 },
	[NFTA_NAT_PROTO_MAX]	= { .type = NLA_U32 },
	[NFTA_NAT_TYPE]		= { .type = NLA_U32 },
};

static int nft_nat_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_nat *priv = nft_expr_priv(expr);
	int err;

	if (tb[NFTA_NAT_TYPE] == NULL)
		return -EINVAL;

	switch (ntohl(nla_get_be32(tb[NFTA_NAT_TYPE]))) {
	case NFT_NAT_SNAT:
		priv->type = NF_NAT_MANIP_SRC;
		break;
	case NFT_NAT_DNAT:
		priv->type = NF_NAT_MANIP_DST;
		break;
	default:
		return -EINVAL;
	}

	if (tb[NFTA_NAT_ADDR_MIN]) {
		priv->sreg_addr_min = ntohl(nla_get_be32(tb[NFTA_NAT_ADDR_MIN]));
		err = nft_validate_input_register(priv->sreg_addr_min);
		if (err < 0)
			return err;
	}

	if (tb[NFTA_NAT_ADDR_MAX]) {
		priv->sreg_addr_max = ntohl(nla_get_be32(tb[NFTA_NAT_ADDR_MAX]));
		err = nft_validate_input_register(priv->sreg_addr_max);
		if (err < 0)
			return err;
	} else
		priv->sreg_addr_max = priv->sreg_addr_min;

	if (tb[NFTA_NAT_PROTO_MIN]) {
		priv->sreg_proto_min = ntohl(nla_get_be32(tb[NFTA_NAT_PROTO_MIN]));
		err = nft_validate_input_register(priv->sreg_proto_min);
		if (err < 0)
			return err;
	}

	if (tb[NFTA_NAT_PROTO_MAX]) {
		priv->sreg_proto_max = ntohl(nla_get_be32(tb[NFTA_NAT_PROTO_MAX]));
		err = nft_validate_input_register(priv->sreg_proto_max);
		if (err < 0)
			return err;
	} else
		priv->sreg_proto_max = priv->sreg_proto_min;

	return 0;
}

static int nft_nat_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_nat *priv = nft_expr_priv(expr);

	switch (priv->type) {
	case NF_NAT_MANIP_SRC:
		if (nla_put_be32(skb, NFTA_NAT_TYPE, htonl(NFT_NAT_SNAT)))
			goto nla_put_failure;
		break;
	case NF_NAT_MANIP_DST:
		if (nla_put_be32(skb, NFTA_NAT_TYPE, htonl(NFT_NAT_DNAT)))
			goto nla_put_failure;
		break;
	}

	if (nla_put_be32(skb, NFTA_NAT_ADDR_MIN, htonl(priv->sreg_addr_min)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_NAT_ADDR_MAX, htonl(priv->sreg_addr_max)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_NAT_PROTO_MIN, htonl(priv->sreg_proto_min)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_NAT_PROTO_MAX, htonl(priv->sreg_proto_max)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_nat_type;
static const struct nft_expr_ops nft_nat_ops = {
	.type		= &nft_nat_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_nat)),
	.eval		= nft_nat_eval,
	.init		= nft_nat_init,
	.dump		= nft_nat_dump,
};

static struct nft_expr_type nft_nat_type __read_mostly = {
	.name		= "nat",
	.ops		= &nft_nat_ops,
	.policy		= nft_nat_policy,
	.maxattr	= NFTA_NAT_MAX,
	.owner		= THIS_MODULE,
};

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

		ret = nft_do_chain(ops, skb, in, out, okfn);
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

struct nf_chain_type nft_chain_nat_ipv4 = {
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

	err = nft_register_expr(&nft_nat_type);
	if (err < 0)
		goto err;

	return 0;

err:
	nft_unregister_chain_type(&nft_chain_nat_ipv4);
	return err;
}

static void __exit nft_chain_nat_exit(void)
{
	nft_unregister_expr(&nft_nat_type);
	nft_unregister_chain_type(&nft_chain_nat_ipv4);
}

module_init(nft_chain_nat_init);
module_exit(nft_chain_nat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_INET, "nat");
MODULE_ALIAS_NFT_EXPR("nat");

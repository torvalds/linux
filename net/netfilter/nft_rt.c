/*
 * Copyright (c) 2016 Anders K. Pedersen <akp@cohaesio.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/dst.h>
#include <net/ip6_route.h>
#include <net/route.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>

struct nft_rt {
	enum nft_rt_keys	key:8;
	enum nft_registers	dreg:8;
};

static u16 get_tcpmss(const struct nft_pktinfo *pkt, const struct dst_entry *skbdst)
{
	u32 minlen = sizeof(struct ipv6hdr), mtu = dst_mtu(skbdst);
	const struct sk_buff *skb = pkt->skb;
	struct dst_entry *dst = NULL;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		fl.u.ip4.daddr = ip_hdr(skb)->saddr;
		minlen = sizeof(struct iphdr) + sizeof(struct tcphdr);
		break;
	case NFPROTO_IPV6:
		fl.u.ip6.daddr = ipv6_hdr(skb)->saddr;
		minlen = sizeof(struct ipv6hdr) + sizeof(struct tcphdr);
		break;
	}

	nf_route(nft_net(pkt), &dst, &fl, false, nft_pf(pkt));
	if (dst) {
		mtu = min(mtu, dst_mtu(dst));
		dst_release(dst);
	}

	if (mtu <= minlen || mtu > 0xffff)
		return TCP_MSS_DEFAULT;

	return mtu - minlen;
}

static void nft_rt_get_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_rt *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	const struct dst_entry *dst;

	dst = skb_dst(skb);
	if (!dst)
		goto err;

	switch (priv->key) {
#ifdef CONFIG_IP_ROUTE_CLASSID
	case NFT_RT_CLASSID:
		*dest = dst->tclassid;
		break;
#endif
	case NFT_RT_NEXTHOP4:
		if (nft_pf(pkt) != NFPROTO_IPV4)
			goto err;

		*dest = (__force u32)rt_nexthop((const struct rtable *)dst,
						ip_hdr(skb)->daddr);
		break;
	case NFT_RT_NEXTHOP6:
		if (nft_pf(pkt) != NFPROTO_IPV6)
			goto err;

		memcpy(dest, rt6_nexthop((struct rt6_info *)dst,
					 &ipv6_hdr(skb)->daddr),
		       sizeof(struct in6_addr));
		break;
	case NFT_RT_TCPMSS:
		nft_reg_store16(dest, get_tcpmss(pkt, dst));
		break;
	default:
		WARN_ON(1);
		goto err;
	}
	return;

err:
	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_rt_policy[NFTA_RT_MAX + 1] = {
	[NFTA_RT_DREG]		= { .type = NLA_U32 },
	[NFTA_RT_KEY]		= { .type = NLA_U32 },
};

static int nft_rt_get_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_rt *priv = nft_expr_priv(expr);
	unsigned int len;

	if (tb[NFTA_RT_KEY] == NULL ||
	    tb[NFTA_RT_DREG] == NULL)
		return -EINVAL;

	priv->key = ntohl(nla_get_be32(tb[NFTA_RT_KEY]));
	switch (priv->key) {
#ifdef CONFIG_IP_ROUTE_CLASSID
	case NFT_RT_CLASSID:
#endif
	case NFT_RT_NEXTHOP4:
		len = sizeof(u32);
		break;
	case NFT_RT_NEXTHOP6:
		len = sizeof(struct in6_addr);
		break;
	case NFT_RT_TCPMSS:
		len = sizeof(u16);
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->dreg = nft_parse_register(tb[NFTA_RT_DREG]);
	return nft_validate_register_store(ctx, priv->dreg, NULL,
					   NFT_DATA_VALUE, len);
}

static int nft_rt_get_dump(struct sk_buff *skb,
			   const struct nft_expr *expr)
{
	const struct nft_rt *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_RT_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_RT_DREG, priv->dreg))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int nft_rt_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
			   const struct nft_data **data)
{
	const struct nft_rt *priv = nft_expr_priv(expr);
	unsigned int hooks;

	switch (priv->key) {
	case NFT_RT_NEXTHOP4:
	case NFT_RT_NEXTHOP6:
	case NFT_RT_CLASSID:
		return 0;
	case NFT_RT_TCPMSS:
		hooks = (1 << NF_INET_FORWARD) |
			(1 << NF_INET_LOCAL_OUT) |
			(1 << NF_INET_POST_ROUTING);
		break;
	default:
		return -EINVAL;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}

static struct nft_expr_type nft_rt_type;
static const struct nft_expr_ops nft_rt_get_ops = {
	.type		= &nft_rt_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_rt)),
	.eval		= nft_rt_get_eval,
	.init		= nft_rt_get_init,
	.dump		= nft_rt_get_dump,
	.validate	= nft_rt_validate,
};

static struct nft_expr_type nft_rt_type __read_mostly = {
	.name		= "rt",
	.ops		= &nft_rt_get_ops,
	.policy		= nft_rt_policy,
	.maxattr	= NFTA_RT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_rt_module_init(void)
{
	return nft_register_expr(&nft_rt_type);
}

static void __exit nft_rt_module_exit(void)
{
	nft_unregister_expr(&nft_rt_type);
}

module_init(nft_rt_module_init);
module_exit(nft_rt_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anders K. Pedersen <akp@cohaesio.com>");
MODULE_ALIAS_NFT_EXPR("rt");

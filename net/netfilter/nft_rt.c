// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Anders K. Pedersen <akp@cohaesio.com>
 */

#include <linux/kernel.h>
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
	u8			dreg;
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

void nft_rt_get_eval(const struct nft_expr *expr,
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
#ifdef CONFIG_XFRM
	case NFT_RT_XFRM:
		nft_reg_store8(dest, !!dst->xfrm);
		break;
#endif
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
#ifdef CONFIG_XFRM
	case NFT_RT_XFRM:
		len = sizeof(u8);
		break;
#endif
	default:
		return -EOPNOTSUPP;
	}

	return nft_parse_register_store(ctx, tb[NFTA_RT_DREG], &priv->dreg,
					NULL, NFT_DATA_VALUE, len);
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

	if (ctx->family != NFPROTO_IPV4 &&
	    ctx->family != NFPROTO_IPV6 &&
	    ctx->family != NFPROTO_INET)
		return -EOPNOTSUPP;

	switch (priv->key) {
	case NFT_RT_NEXTHOP4:
	case NFT_RT_NEXTHOP6:
	case NFT_RT_CLASSID:
	case NFT_RT_XFRM:
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

static const struct nft_expr_ops nft_rt_get_ops = {
	.type		= &nft_rt_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_rt)),
	.eval		= nft_rt_get_eval,
	.init		= nft_rt_get_init,
	.dump		= nft_rt_get_dump,
	.validate	= nft_rt_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

struct nft_expr_type nft_rt_type __read_mostly = {
	.name		= "rt",
	.ops		= &nft_rt_get_ops,
	.policy		= nft_rt_policy,
	.maxattr	= NFTA_RT_MAX,
	.owner		= THIS_MODULE,
};

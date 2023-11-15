// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Pablo Neira Ayuso <pablo@netfilter.org>
 */

#include <linux/kernel.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_meta.h>
#include <net/netfilter/nf_tables_offload.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/gre.h>
#include <net/geneve.h>
#include <net/ip.h>
#include <linux/icmpv6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

static DEFINE_PER_CPU(struct nft_inner_tun_ctx, nft_pcpu_tun_ctx);

/* Same layout as nft_expr but it embeds the private expression data area. */
struct __nft_expr {
	const struct nft_expr_ops	*ops;
	union {
		struct nft_payload	payload;
		struct nft_meta		meta;
	} __attribute__((aligned(__alignof__(u64))));
};

enum {
	NFT_INNER_EXPR_PAYLOAD,
	NFT_INNER_EXPR_META,
};

struct nft_inner {
	u8			flags;
	u8			hdrsize;
	u8			type;
	u8			expr_type;

	struct __nft_expr	expr;
};

static int nft_inner_parse_l2l3(const struct nft_inner *priv,
				const struct nft_pktinfo *pkt,
				struct nft_inner_tun_ctx *ctx, u32 off)
{
	__be16 llproto, outer_llproto;
	u32 nhoff, thoff;

	if (priv->flags & NFT_INNER_LL) {
		struct vlan_ethhdr *veth, _veth;
		struct ethhdr *eth, _eth;
		u32 hdrsize;

		eth = skb_header_pointer(pkt->skb, off, sizeof(_eth), &_eth);
		if (!eth)
			return -1;

		switch (eth->h_proto) {
		case htons(ETH_P_IP):
		case htons(ETH_P_IPV6):
			llproto = eth->h_proto;
			hdrsize = sizeof(_eth);
			break;
		case htons(ETH_P_8021Q):
			veth = skb_header_pointer(pkt->skb, off, sizeof(_veth), &_veth);
			if (!veth)
				return -1;

			outer_llproto = veth->h_vlan_encapsulated_proto;
			llproto = veth->h_vlan_proto;
			hdrsize = sizeof(_veth);
			break;
		default:
			return -1;
		}

		ctx->inner_lloff = off;
		ctx->flags |= NFT_PAYLOAD_CTX_INNER_LL;
		off += hdrsize;
	} else {
		struct iphdr *iph;
		u32 _version;

		iph = skb_header_pointer(pkt->skb, off, sizeof(_version), &_version);
		if (!iph)
			return -1;

		switch (iph->version) {
		case 4:
			llproto = htons(ETH_P_IP);
			break;
		case 6:
			llproto = htons(ETH_P_IPV6);
			break;
		default:
			return -1;
		}
	}

	ctx->llproto = llproto;
	if (llproto == htons(ETH_P_8021Q))
		llproto = outer_llproto;

	nhoff = off;

	switch (llproto) {
	case htons(ETH_P_IP): {
		struct iphdr *iph, _iph;

		iph = skb_header_pointer(pkt->skb, nhoff, sizeof(_iph), &_iph);
		if (!iph)
			return -1;

		if (iph->ihl < 5 || iph->version != 4)
			return -1;

		ctx->inner_nhoff = nhoff;
		ctx->flags |= NFT_PAYLOAD_CTX_INNER_NH;

		thoff = nhoff + (iph->ihl * 4);
		if ((ntohs(iph->frag_off) & IP_OFFSET) == 0) {
			ctx->flags |= NFT_PAYLOAD_CTX_INNER_TH;
			ctx->inner_thoff = thoff;
			ctx->l4proto = iph->protocol;
		}
		}
		break;
	case htons(ETH_P_IPV6): {
		struct ipv6hdr *ip6h, _ip6h;
		int fh_flags = IP6_FH_F_AUTH;
		unsigned short fragoff;
		int l4proto;

		ip6h = skb_header_pointer(pkt->skb, nhoff, sizeof(_ip6h), &_ip6h);
		if (!ip6h)
			return -1;

		if (ip6h->version != 6)
			return -1;

		ctx->inner_nhoff = nhoff;
		ctx->flags |= NFT_PAYLOAD_CTX_INNER_NH;

		thoff = nhoff;
		l4proto = ipv6_find_hdr(pkt->skb, &thoff, -1, &fragoff, &fh_flags);
		if (l4proto < 0 || thoff > U16_MAX)
			return -1;

		if (fragoff == 0) {
			thoff = nhoff + sizeof(_ip6h);
			ctx->flags |= NFT_PAYLOAD_CTX_INNER_TH;
			ctx->inner_thoff = thoff;
			ctx->l4proto = l4proto;
		}
		}
		break;
	default:
		return -1;
	}

	return 0;
}

static int nft_inner_parse_tunhdr(const struct nft_inner *priv,
				  const struct nft_pktinfo *pkt,
				  struct nft_inner_tun_ctx *ctx, u32 *off)
{
	if (pkt->tprot == IPPROTO_GRE) {
		ctx->inner_tunoff = pkt->thoff;
		ctx->flags |= NFT_PAYLOAD_CTX_INNER_TUN;
		return 0;
	}

	if (pkt->tprot != IPPROTO_UDP)
		return -1;

	ctx->inner_tunoff = *off;
	ctx->flags |= NFT_PAYLOAD_CTX_INNER_TUN;
	*off += priv->hdrsize;

	switch (priv->type) {
	case NFT_INNER_GENEVE: {
		struct genevehdr *gnvh, _gnvh;

		gnvh = skb_header_pointer(pkt->skb, pkt->inneroff,
					  sizeof(_gnvh), &_gnvh);
		if (!gnvh)
			return -1;

		*off += gnvh->opt_len * 4;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int nft_inner_parse(const struct nft_inner *priv,
			   struct nft_pktinfo *pkt,
			   struct nft_inner_tun_ctx *tun_ctx)
{
	struct nft_inner_tun_ctx ctx = {};
	u32 off = pkt->inneroff;

	if (priv->flags & NFT_INNER_HDRSIZE &&
	    nft_inner_parse_tunhdr(priv, pkt, &ctx, &off) < 0)
		return -1;

	if (priv->flags & (NFT_INNER_LL | NFT_INNER_NH)) {
		if (nft_inner_parse_l2l3(priv, pkt, &ctx, off) < 0)
			return -1;
	} else if (priv->flags & NFT_INNER_TH) {
		ctx.inner_thoff = off;
		ctx.flags |= NFT_PAYLOAD_CTX_INNER_TH;
	}

	*tun_ctx = ctx;
	tun_ctx->type = priv->type;
	pkt->flags |= NFT_PKTINFO_INNER_FULL;

	return 0;
}

static bool nft_inner_parse_needed(const struct nft_inner *priv,
				   const struct nft_pktinfo *pkt,
				   const struct nft_inner_tun_ctx *tun_ctx)
{
	if (!(pkt->flags & NFT_PKTINFO_INNER_FULL))
		return true;

	if (priv->type != tun_ctx->type)
		return true;

	return false;
}

static void nft_inner_eval(const struct nft_expr *expr, struct nft_regs *regs,
			   const struct nft_pktinfo *pkt)
{
	struct nft_inner_tun_ctx *tun_ctx = this_cpu_ptr(&nft_pcpu_tun_ctx);
	const struct nft_inner *priv = nft_expr_priv(expr);

	if (nft_payload_inner_offset(pkt) < 0)
		goto err;

	if (nft_inner_parse_needed(priv, pkt, tun_ctx) &&
	    nft_inner_parse(priv, (struct nft_pktinfo *)pkt, tun_ctx) < 0)
		goto err;

	switch (priv->expr_type) {
	case NFT_INNER_EXPR_PAYLOAD:
		nft_payload_inner_eval((struct nft_expr *)&priv->expr, regs, pkt, tun_ctx);
		break;
	case NFT_INNER_EXPR_META:
		nft_meta_inner_eval((struct nft_expr *)&priv->expr, regs, pkt, tun_ctx);
		break;
	default:
		WARN_ON_ONCE(1);
		goto err;
	}
	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_inner_policy[NFTA_INNER_MAX + 1] = {
	[NFTA_INNER_NUM]	= { .type = NLA_U32 },
	[NFTA_INNER_FLAGS]	= { .type = NLA_U32 },
	[NFTA_INNER_HDRSIZE]	= { .type = NLA_U32 },
	[NFTA_INNER_TYPE]	= { .type = NLA_U32 },
	[NFTA_INNER_EXPR]	= { .type = NLA_NESTED },
};

struct nft_expr_info {
	const struct nft_expr_ops	*ops;
	const struct nlattr		*attr;
	struct nlattr			*tb[NFT_EXPR_MAXATTR + 1];
};

static int nft_inner_init(const struct nft_ctx *ctx,
			  const struct nft_expr *expr,
			  const struct nlattr * const tb[])
{
	struct nft_inner *priv = nft_expr_priv(expr);
	u32 flags, hdrsize, type, num;
	struct nft_expr_info expr_info;
	int err;

	if (!tb[NFTA_INNER_FLAGS] ||
	    !tb[NFTA_INNER_NUM] ||
	    !tb[NFTA_INNER_HDRSIZE] ||
	    !tb[NFTA_INNER_TYPE] ||
	    !tb[NFTA_INNER_EXPR])
		return -EINVAL;

	flags = ntohl(nla_get_be32(tb[NFTA_INNER_FLAGS]));
	if (flags & ~NFT_INNER_MASK)
		return -EOPNOTSUPP;

	num = ntohl(nla_get_be32(tb[NFTA_INNER_NUM]));
	if (num != 0)
		return -EOPNOTSUPP;

	hdrsize = ntohl(nla_get_be32(tb[NFTA_INNER_HDRSIZE]));
	type = ntohl(nla_get_be32(tb[NFTA_INNER_TYPE]));

	if (type > U8_MAX)
		return -EINVAL;

	if (flags & NFT_INNER_HDRSIZE) {
		if (hdrsize == 0 || hdrsize > 64)
			return -EOPNOTSUPP;
	}

	priv->flags = flags;
	priv->hdrsize = hdrsize;
	priv->type = type;

	err = nft_expr_inner_parse(ctx, tb[NFTA_INNER_EXPR], &expr_info);
	if (err < 0)
		return err;

	priv->expr.ops = expr_info.ops;

	if (!strcmp(expr_info.ops->type->name, "payload"))
		priv->expr_type = NFT_INNER_EXPR_PAYLOAD;
	else if (!strcmp(expr_info.ops->type->name, "meta"))
		priv->expr_type = NFT_INNER_EXPR_META;
	else
		return -EINVAL;

	err = expr_info.ops->init(ctx, (struct nft_expr *)&priv->expr,
				  (const struct nlattr * const*)expr_info.tb);
	if (err < 0)
		return err;

	return 0;
}

static int nft_inner_dump(struct sk_buff *skb,
			  const struct nft_expr *expr, bool reset)
{
	const struct nft_inner *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_INNER_NUM, htonl(0)) ||
	    nla_put_be32(skb, NFTA_INNER_TYPE, htonl(priv->type)) ||
	    nla_put_be32(skb, NFTA_INNER_FLAGS, htonl(priv->flags)) ||
	    nla_put_be32(skb, NFTA_INNER_HDRSIZE, htonl(priv->hdrsize)))
		goto nla_put_failure;

	if (nft_expr_dump(skb, NFTA_INNER_EXPR,
			  (struct nft_expr *)&priv->expr, reset) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_inner_ops = {
	.type		= &nft_inner_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_inner)),
	.eval		= nft_inner_eval,
	.init		= nft_inner_init,
	.dump		= nft_inner_dump,
};

struct nft_expr_type nft_inner_type __read_mostly = {
	.name		= "inner",
	.ops		= &nft_inner_ops,
	.policy		= nft_inner_policy,
	.maxattr	= NFTA_INNER_MAX,
	.owner		= THIS_MODULE,
};

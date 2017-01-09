/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2016 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
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
/* For layer 4 checksum field offset. */
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmpv6.h>

/* add vlan header into the user buffer for if tag was removed by offloads */
static bool
nft_payload_copy_vlan(u32 *d, const struct sk_buff *skb, u8 offset, u8 len)
{
	int mac_off = skb_mac_header(skb) - skb->data;
	u8 vlan_len, *vlanh, *dst_u8 = (u8 *) d;
	struct vlan_ethhdr veth;

	vlanh = (u8 *) &veth;
	if (offset < ETH_HLEN) {
		u8 ethlen = min_t(u8, len, ETH_HLEN - offset);

		if (skb_copy_bits(skb, mac_off, &veth, ETH_HLEN))
			return false;

		veth.h_vlan_proto = skb->vlan_proto;

		memcpy(dst_u8, vlanh + offset, ethlen);

		len -= ethlen;
		if (len == 0)
			return true;

		dst_u8 += ethlen;
		offset = ETH_HLEN;
	} else if (offset >= VLAN_ETH_HLEN) {
		offset -= VLAN_HLEN;
		goto skip;
	}

	veth.h_vlan_TCI = htons(skb_vlan_tag_get(skb));
	veth.h_vlan_encapsulated_proto = skb->protocol;

	vlanh += offset;

	vlan_len = min_t(u8, len, VLAN_ETH_HLEN - offset);
	memcpy(dst_u8, vlanh, vlan_len);

	len -= vlan_len;
	if (!len)
		return true;

	dst_u8 += vlan_len;
 skip:
	return skb_copy_bits(skb, offset + mac_off, dst_u8, len) == 0;
}

static void nft_payload_eval(const struct nft_expr *expr,
			     struct nft_regs *regs,
			     const struct nft_pktinfo *pkt)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	int offset;

	dest[priv->len / NFT_REG32_SIZE] = 0;
	switch (priv->base) {
	case NFT_PAYLOAD_LL_HEADER:
		if (!skb_mac_header_was_set(skb))
			goto err;

		if (skb_vlan_tag_present(skb)) {
			if (!nft_payload_copy_vlan(dest, skb,
						   priv->offset, priv->len))
				goto err;
			return;
		}
		offset = skb_mac_header(skb) - skb->data;
		break;
	case NFT_PAYLOAD_NETWORK_HEADER:
		offset = skb_network_offset(skb);
		break;
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		if (!pkt->tprot_set)
			goto err;
		offset = pkt->xt.thoff;
		break;
	default:
		BUG();
	}
	offset += priv->offset;

	if (skb_copy_bits(skb, offset, dest, priv->len) < 0)
		goto err;
	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_payload_policy[NFTA_PAYLOAD_MAX + 1] = {
	[NFTA_PAYLOAD_SREG]		= { .type = NLA_U32 },
	[NFTA_PAYLOAD_DREG]		= { .type = NLA_U32 },
	[NFTA_PAYLOAD_BASE]		= { .type = NLA_U32 },
	[NFTA_PAYLOAD_OFFSET]		= { .type = NLA_U32 },
	[NFTA_PAYLOAD_LEN]		= { .type = NLA_U32 },
	[NFTA_PAYLOAD_CSUM_TYPE]	= { .type = NLA_U32 },
	[NFTA_PAYLOAD_CSUM_OFFSET]	= { .type = NLA_U32 },
};

static int nft_payload_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr * const tb[])
{
	struct nft_payload *priv = nft_expr_priv(expr);

	priv->base   = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	priv->offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len    = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));
	priv->dreg   = nft_parse_register(tb[NFTA_PAYLOAD_DREG]);

	return nft_validate_register_store(ctx, priv->dreg, NULL,
					   NFT_DATA_VALUE, priv->len);
}

static int nft_payload_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_payload *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_PAYLOAD_DREG, priv->dreg) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_BASE, htonl(priv->base)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_OFFSET, htonl(priv->offset)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_LEN, htonl(priv->len)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_payload_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
};

const struct nft_expr_ops nft_payload_fast_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
};

static inline void nft_csum_replace(__sum16 *sum, __wsum fsum, __wsum tsum)
{
	*sum = csum_fold(csum_add(csum_sub(~csum_unfold(*sum), fsum), tsum));
	if (*sum == 0)
		*sum = CSUM_MANGLED_0;
}

static bool nft_payload_udp_checksum(struct sk_buff *skb, unsigned int thoff)
{
	struct udphdr *uh, _uh;

	uh = skb_header_pointer(skb, thoff, sizeof(_uh), &_uh);
	if (!uh)
		return false;

	return uh->check;
}

static int nft_payload_l4csum_offset(const struct nft_pktinfo *pkt,
				     struct sk_buff *skb,
				     unsigned int *l4csum_offset)
{
	switch (pkt->tprot) {
	case IPPROTO_TCP:
		*l4csum_offset = offsetof(struct tcphdr, check);
		break;
	case IPPROTO_UDP:
		if (!nft_payload_udp_checksum(skb, pkt->xt.thoff))
			return -1;
		/* Fall through. */
	case IPPROTO_UDPLITE:
		*l4csum_offset = offsetof(struct udphdr, check);
		break;
	case IPPROTO_ICMPV6:
		*l4csum_offset = offsetof(struct icmp6hdr, icmp6_cksum);
		break;
	default:
		return -1;
	}

	*l4csum_offset += pkt->xt.thoff;
	return 0;
}

static int nft_payload_l4csum_update(const struct nft_pktinfo *pkt,
				     struct sk_buff *skb,
				     __wsum fsum, __wsum tsum)
{
	int l4csum_offset;
	__sum16 sum;

	/* If we cannot determine layer 4 checksum offset or this packet doesn't
	 * require layer 4 checksum recalculation, skip this packet.
	 */
	if (nft_payload_l4csum_offset(pkt, skb, &l4csum_offset) < 0)
		return 0;

	if (skb_copy_bits(skb, l4csum_offset, &sum, sizeof(sum)) < 0)
		return -1;

	/* Checksum mangling for an arbitrary amount of bytes, based on
	 * inet_proto_csum_replace*() functions.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		nft_csum_replace(&sum, fsum, tsum);
		if (skb->ip_summed == CHECKSUM_COMPLETE) {
			skb->csum = ~csum_add(csum_sub(~(skb->csum), fsum),
					      tsum);
		}
	} else {
		sum = ~csum_fold(csum_add(csum_sub(csum_unfold(sum), fsum),
					  tsum));
	}

	if (!skb_make_writable(skb, l4csum_offset + sizeof(sum)) ||
	    skb_store_bits(skb, l4csum_offset, &sum, sizeof(sum)) < 0)
		return -1;

	return 0;
}

static void nft_payload_set_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	const struct nft_payload_set *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	const u32 *src = &regs->data[priv->sreg];
	int offset, csum_offset;
	__wsum fsum, tsum;
	__sum16 sum;

	switch (priv->base) {
	case NFT_PAYLOAD_LL_HEADER:
		if (!skb_mac_header_was_set(skb))
			goto err;
		offset = skb_mac_header(skb) - skb->data;
		break;
	case NFT_PAYLOAD_NETWORK_HEADER:
		offset = skb_network_offset(skb);
		break;
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		if (!pkt->tprot_set)
			goto err;
		offset = pkt->xt.thoff;
		break;
	default:
		BUG();
	}

	csum_offset = offset + priv->csum_offset;
	offset += priv->offset;

	if (priv->csum_type == NFT_PAYLOAD_CSUM_INET &&
	    (priv->base != NFT_PAYLOAD_TRANSPORT_HEADER ||
	     skb->ip_summed != CHECKSUM_PARTIAL)) {
		if (skb_copy_bits(skb, csum_offset, &sum, sizeof(sum)) < 0)
			goto err;

		fsum = skb_checksum(skb, offset, priv->len, 0);
		tsum = csum_partial(src, priv->len, 0);
		nft_csum_replace(&sum, fsum, tsum);

		if (!skb_make_writable(skb, csum_offset + sizeof(sum)) ||
		    skb_store_bits(skb, csum_offset, &sum, sizeof(sum)) < 0)
			goto err;

		if (priv->csum_flags &&
		    nft_payload_l4csum_update(pkt, skb, fsum, tsum) < 0)
			goto err;
	}

	if (!skb_make_writable(skb, max(offset + priv->len, 0)) ||
	    skb_store_bits(skb, offset, src, priv->len) < 0)
		goto err;

	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static int nft_payload_set_init(const struct nft_ctx *ctx,
				const struct nft_expr *expr,
				const struct nlattr * const tb[])
{
	struct nft_payload_set *priv = nft_expr_priv(expr);

	priv->base        = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	priv->offset      = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len         = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));
	priv->sreg        = nft_parse_register(tb[NFTA_PAYLOAD_SREG]);

	if (tb[NFTA_PAYLOAD_CSUM_TYPE])
		priv->csum_type =
			ntohl(nla_get_be32(tb[NFTA_PAYLOAD_CSUM_TYPE]));
	if (tb[NFTA_PAYLOAD_CSUM_OFFSET])
		priv->csum_offset =
			ntohl(nla_get_be32(tb[NFTA_PAYLOAD_CSUM_OFFSET]));
	if (tb[NFTA_PAYLOAD_CSUM_FLAGS]) {
		u32 flags;

		flags = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_CSUM_FLAGS]));
		if (flags & ~NFT_PAYLOAD_L4CSUM_PSEUDOHDR)
			return -EINVAL;

		priv->csum_flags = flags;
	}

	switch (priv->csum_type) {
	case NFT_PAYLOAD_CSUM_NONE:
	case NFT_PAYLOAD_CSUM_INET:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return nft_validate_register_load(priv->sreg, priv->len);
}

static int nft_payload_set_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_payload_set *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_PAYLOAD_SREG, priv->sreg) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_BASE, htonl(priv->base)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_OFFSET, htonl(priv->offset)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_LEN, htonl(priv->len)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_CSUM_TYPE, htonl(priv->csum_type)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_CSUM_OFFSET,
			 htonl(priv->csum_offset)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_CSUM_FLAGS, htonl(priv->csum_flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_payload_set_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload_set)),
	.eval		= nft_payload_set_eval,
	.init		= nft_payload_set_init,
	.dump		= nft_payload_set_dump,
};

static const struct nft_expr_ops *
nft_payload_select_ops(const struct nft_ctx *ctx,
		       const struct nlattr * const tb[])
{
	enum nft_payload_bases base;
	unsigned int offset, len;

	if (tb[NFTA_PAYLOAD_BASE] == NULL ||
	    tb[NFTA_PAYLOAD_OFFSET] == NULL ||
	    tb[NFTA_PAYLOAD_LEN] == NULL)
		return ERR_PTR(-EINVAL);

	base = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	switch (base) {
	case NFT_PAYLOAD_LL_HEADER:
	case NFT_PAYLOAD_NETWORK_HEADER:
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		break;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (tb[NFTA_PAYLOAD_SREG] != NULL) {
		if (tb[NFTA_PAYLOAD_DREG] != NULL)
			return ERR_PTR(-EINVAL);
		return &nft_payload_set_ops;
	}

	if (tb[NFTA_PAYLOAD_DREG] == NULL)
		return ERR_PTR(-EINVAL);

	offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	len    = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));

	if (len <= 4 && is_power_of_2(len) && IS_ALIGNED(offset, len) &&
	    base != NFT_PAYLOAD_LL_HEADER)
		return &nft_payload_fast_ops;
	else
		return &nft_payload_ops;
}

struct nft_expr_type nft_payload_type __read_mostly = {
	.name		= "payload",
	.select_ops	= nft_payload_select_ops,
	.policy		= nft_payload_policy,
	.maxattr	= NFTA_PAYLOAD_MAX,
	.owner		= THIS_MODULE,
};

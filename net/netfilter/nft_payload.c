// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2016 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_tables_offload.h>
/* For layer 4 checksum field offset. */
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/gre.h>
#include <linux/icmpv6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/sctp/checksum.h>

static bool nft_payload_rebuild_vlan_hdr(const struct sk_buff *skb, int mac_off,
					 struct vlan_ethhdr *veth)
{
	if (skb_copy_bits(skb, mac_off, veth, ETH_HLEN))
		return false;

	veth->h_vlan_proto = skb->vlan_proto;
	veth->h_vlan_TCI = htons(skb_vlan_tag_get(skb));
	veth->h_vlan_encapsulated_proto = skb->protocol;

	return true;
}

/* add vlan header into the user buffer for if tag was removed by offloads */
static bool
nft_payload_copy_vlan(u32 *d, const struct sk_buff *skb, u8 offset, u8 len)
{
	int mac_off = skb_mac_header(skb) - skb->data;
	u8 *vlanh, *dst_u8 = (u8 *) d;
	struct vlan_ethhdr veth;
	u8 vlan_hlen = 0;

	if ((skb->protocol == htons(ETH_P_8021AD) ||
	     skb->protocol == htons(ETH_P_8021Q)) &&
	    offset >= VLAN_ETH_HLEN && offset < VLAN_ETH_HLEN + VLAN_HLEN)
		vlan_hlen += VLAN_HLEN;

	vlanh = (u8 *) &veth;
	if (offset < VLAN_ETH_HLEN + vlan_hlen) {
		u8 ethlen = len;

		if (vlan_hlen &&
		    skb_copy_bits(skb, mac_off, &veth, VLAN_ETH_HLEN) < 0)
			return false;
		else if (!nft_payload_rebuild_vlan_hdr(skb, mac_off, &veth))
			return false;

		if (offset + len > VLAN_ETH_HLEN + vlan_hlen)
			ethlen -= offset + len - VLAN_ETH_HLEN - vlan_hlen;

		memcpy(dst_u8, vlanh + offset - vlan_hlen, ethlen);

		len -= ethlen;
		if (len == 0)
			return true;

		dst_u8 += ethlen;
		offset = ETH_HLEN + vlan_hlen;
	} else {
		offset -= VLAN_HLEN + vlan_hlen;
	}

	return skb_copy_bits(skb, offset + mac_off, dst_u8, len) == 0;
}

static int __nft_payload_inner_offset(struct nft_pktinfo *pkt)
{
	unsigned int thoff = nft_thoff(pkt);

	if (!(pkt->flags & NFT_PKTINFO_L4PROTO) || pkt->fragoff)
		return -1;

	switch (pkt->tprot) {
	case IPPROTO_UDP:
		pkt->inneroff = thoff + sizeof(struct udphdr);
		break;
	case IPPROTO_TCP: {
		struct tcphdr *th, _tcph;

		th = skb_header_pointer(pkt->skb, thoff, sizeof(_tcph), &_tcph);
		if (!th)
			return -1;

		pkt->inneroff = thoff + __tcp_hdrlen(th);
		}
		break;
	case IPPROTO_GRE: {
		u32 offset = sizeof(struct gre_base_hdr);
		struct gre_base_hdr *gre, _gre;
		__be16 version;

		gre = skb_header_pointer(pkt->skb, thoff, sizeof(_gre), &_gre);
		if (!gre)
			return -1;

		version = gre->flags & GRE_VERSION;
		switch (version) {
		case GRE_VERSION_0:
			if (gre->flags & GRE_ROUTING)
				return -1;

			if (gre->flags & GRE_CSUM) {
				offset += sizeof_field(struct gre_full_hdr, csum) +
					  sizeof_field(struct gre_full_hdr, reserved1);
			}
			if (gre->flags & GRE_KEY)
				offset += sizeof_field(struct gre_full_hdr, key);

			if (gre->flags & GRE_SEQ)
				offset += sizeof_field(struct gre_full_hdr, seq);
			break;
		default:
			return -1;
		}

		pkt->inneroff = thoff + offset;
		}
		break;
	case IPPROTO_IPIP:
		pkt->inneroff = thoff;
		break;
	default:
		return -1;
	}

	pkt->flags |= NFT_PKTINFO_INNER;

	return 0;
}

int nft_payload_inner_offset(const struct nft_pktinfo *pkt)
{
	if (!(pkt->flags & NFT_PKTINFO_INNER) &&
	    __nft_payload_inner_offset((struct nft_pktinfo *)pkt) < 0)
		return -1;

	return pkt->inneroff;
}

void nft_payload_eval(const struct nft_expr *expr,
		      struct nft_regs *regs,
		      const struct nft_pktinfo *pkt)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	int offset;

	if (priv->len % NFT_REG32_SIZE)
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
		if (!(pkt->flags & NFT_PKTINFO_L4PROTO) || pkt->fragoff)
			goto err;
		offset = nft_thoff(pkt);
		break;
	case NFT_PAYLOAD_INNER_HEADER:
		offset = nft_payload_inner_offset(pkt);
		if (offset < 0)
			goto err;
		break;
	default:
		WARN_ON_ONCE(1);
		goto err;
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
	[NFTA_PAYLOAD_OFFSET]		= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_PAYLOAD_LEN]		= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_PAYLOAD_CSUM_TYPE]	= { .type = NLA_U32 },
	[NFTA_PAYLOAD_CSUM_OFFSET]	= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_PAYLOAD_CSUM_FLAGS]	= { .type = NLA_U32 },
};

static int nft_payload_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr * const tb[])
{
	struct nft_payload *priv = nft_expr_priv(expr);

	priv->base   = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	priv->offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len    = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));

	return nft_parse_register_store(ctx, tb[NFTA_PAYLOAD_DREG],
					&priv->dreg, NULL, NFT_DATA_VALUE,
					priv->len);
}

static int nft_payload_dump(struct sk_buff *skb,
			    const struct nft_expr *expr, bool reset)
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

static bool nft_payload_reduce(struct nft_regs_track *track,
			       const struct nft_expr *expr)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct nft_payload *payload;

	if (!nft_reg_track_cmp(track, expr, priv->dreg)) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	payload = nft_expr_priv(track->regs[priv->dreg].selector);
	if (priv->base != payload->base ||
	    priv->offset != payload->offset ||
	    priv->len != payload->len) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	if (!track->regs[priv->dreg].bitwise)
		return true;

	return nft_expr_reduce_bitwise(track, expr);
}

static bool nft_payload_offload_mask(struct nft_offload_reg *reg,
				     u32 priv_len, u32 field_len)
{
	unsigned int remainder, delta, k;
	struct nft_data mask = {};
	__be32 remainder_mask;

	if (priv_len == field_len) {
		memset(&reg->mask, 0xff, priv_len);
		return true;
	} else if (priv_len > field_len) {
		return false;
	}

	memset(&mask, 0xff, field_len);
	remainder = priv_len % sizeof(u32);
	if (remainder) {
		k = priv_len / sizeof(u32);
		delta = field_len - priv_len;
		remainder_mask = htonl(~((1 << (delta * BITS_PER_BYTE)) - 1));
		mask.data[k] = (__force u32)remainder_mask;
	}

	memcpy(&reg->mask, &mask, field_len);

	return true;
}

static int nft_payload_offload_ll(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_payload *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->offset) {
	case offsetof(struct ethhdr, h_source):
		if (!nft_payload_offload_mask(reg, priv->len, ETH_ALEN))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_ETH_ADDRS, eth_addrs,
				  src, ETH_ALEN, reg);
		break;
	case offsetof(struct ethhdr, h_dest):
		if (!nft_payload_offload_mask(reg, priv->len, ETH_ALEN))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_ETH_ADDRS, eth_addrs,
				  dst, ETH_ALEN, reg);
		break;
	case offsetof(struct ethhdr, h_proto):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_BASIC, basic,
				  n_proto, sizeof(__be16), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_NETWORK);
		break;
	case offsetof(struct vlan_ethhdr, h_vlan_TCI):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH_FLAGS(FLOW_DISSECTOR_KEY_VLAN, vlan,
					vlan_tci, sizeof(__be16), reg,
					NFT_OFFLOAD_F_NETWORK2HOST);
		break;
	case offsetof(struct vlan_ethhdr, h_vlan_encapsulated_proto):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_VLAN, vlan,
				  vlan_tpid, sizeof(__be16), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_NETWORK);
		break;
	case offsetof(struct vlan_ethhdr, h_vlan_TCI) + sizeof(struct vlan_hdr):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH_FLAGS(FLOW_DISSECTOR_KEY_CVLAN, cvlan,
					vlan_tci, sizeof(__be16), reg,
					NFT_OFFLOAD_F_NETWORK2HOST);
		break;
	case offsetof(struct vlan_ethhdr, h_vlan_encapsulated_proto) +
							sizeof(struct vlan_hdr):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_CVLAN, cvlan,
				  vlan_tpid, sizeof(__be16), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_NETWORK);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nft_payload_offload_ip(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_payload *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->offset) {
	case offsetof(struct iphdr, saddr):
		if (!nft_payload_offload_mask(reg, priv->len,
					      sizeof(struct in_addr)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4, src,
				  sizeof(struct in_addr), reg);
		nft_flow_rule_set_addr_type(flow, FLOW_DISSECTOR_KEY_IPV4_ADDRS);
		break;
	case offsetof(struct iphdr, daddr):
		if (!nft_payload_offload_mask(reg, priv->len,
					      sizeof(struct in_addr)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4, dst,
				  sizeof(struct in_addr), reg);
		nft_flow_rule_set_addr_type(flow, FLOW_DISSECTOR_KEY_IPV4_ADDRS);
		break;
	case offsetof(struct iphdr, protocol):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__u8)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_BASIC, basic, ip_proto,
				  sizeof(__u8), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_TRANSPORT);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nft_payload_offload_ip6(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_payload *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->offset) {
	case offsetof(struct ipv6hdr, saddr):
		if (!nft_payload_offload_mask(reg, priv->len,
					      sizeof(struct in6_addr)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6, src,
				  sizeof(struct in6_addr), reg);
		nft_flow_rule_set_addr_type(flow, FLOW_DISSECTOR_KEY_IPV6_ADDRS);
		break;
	case offsetof(struct ipv6hdr, daddr):
		if (!nft_payload_offload_mask(reg, priv->len,
					      sizeof(struct in6_addr)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6, dst,
				  sizeof(struct in6_addr), reg);
		nft_flow_rule_set_addr_type(flow, FLOW_DISSECTOR_KEY_IPV6_ADDRS);
		break;
	case offsetof(struct ipv6hdr, nexthdr):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__u8)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_BASIC, basic, ip_proto,
				  sizeof(__u8), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_TRANSPORT);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nft_payload_offload_nh(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_payload *priv)
{
	int err;

	switch (ctx->dep.l3num) {
	case htons(ETH_P_IP):
		err = nft_payload_offload_ip(ctx, flow, priv);
		break;
	case htons(ETH_P_IPV6):
		err = nft_payload_offload_ip6(ctx, flow, priv);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int nft_payload_offload_tcp(struct nft_offload_ctx *ctx,
				   struct nft_flow_rule *flow,
				   const struct nft_payload *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->offset) {
	case offsetof(struct tcphdr, source):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_PORTS, tp, src,
				  sizeof(__be16), reg);
		break;
	case offsetof(struct tcphdr, dest):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_PORTS, tp, dst,
				  sizeof(__be16), reg);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nft_payload_offload_udp(struct nft_offload_ctx *ctx,
				   struct nft_flow_rule *flow,
				   const struct nft_payload *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->offset) {
	case offsetof(struct udphdr, source):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_PORTS, tp, src,
				  sizeof(__be16), reg);
		break;
	case offsetof(struct udphdr, dest):
		if (!nft_payload_offload_mask(reg, priv->len, sizeof(__be16)))
			return -EOPNOTSUPP;

		NFT_OFFLOAD_MATCH(FLOW_DISSECTOR_KEY_PORTS, tp, dst,
				  sizeof(__be16), reg);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nft_payload_offload_th(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_payload *priv)
{
	int err;

	switch (ctx->dep.protonum) {
	case IPPROTO_TCP:
		err = nft_payload_offload_tcp(ctx, flow, priv);
		break;
	case IPPROTO_UDP:
		err = nft_payload_offload_udp(ctx, flow, priv);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int nft_payload_offload(struct nft_offload_ctx *ctx,
			       struct nft_flow_rule *flow,
			       const struct nft_expr *expr)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	int err;

	switch (priv->base) {
	case NFT_PAYLOAD_LL_HEADER:
		err = nft_payload_offload_ll(ctx, flow, priv);
		break;
	case NFT_PAYLOAD_NETWORK_HEADER:
		err = nft_payload_offload_nh(ctx, flow, priv);
		break;
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		err = nft_payload_offload_th(ctx, flow, priv);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static const struct nft_expr_ops nft_payload_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
	.reduce		= nft_payload_reduce,
	.offload	= nft_payload_offload,
};

const struct nft_expr_ops nft_payload_fast_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
	.reduce		= nft_payload_reduce,
	.offload	= nft_payload_offload,
};

void nft_payload_inner_eval(const struct nft_expr *expr, struct nft_regs *regs,
			    const struct nft_pktinfo *pkt,
			    struct nft_inner_tun_ctx *tun_ctx)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	int offset;

	if (priv->len % NFT_REG32_SIZE)
		dest[priv->len / NFT_REG32_SIZE] = 0;

	switch (priv->base) {
	case NFT_PAYLOAD_TUN_HEADER:
		if (!(tun_ctx->flags & NFT_PAYLOAD_CTX_INNER_TUN))
			goto err;

		offset = tun_ctx->inner_tunoff;
		break;
	case NFT_PAYLOAD_LL_HEADER:
		if (!(tun_ctx->flags & NFT_PAYLOAD_CTX_INNER_LL))
			goto err;

		offset = tun_ctx->inner_lloff;
		break;
	case NFT_PAYLOAD_NETWORK_HEADER:
		if (!(tun_ctx->flags & NFT_PAYLOAD_CTX_INNER_NH))
			goto err;

		offset = tun_ctx->inner_nhoff;
		break;
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		if (!(tun_ctx->flags & NFT_PAYLOAD_CTX_INNER_TH))
			goto err;

		offset = tun_ctx->inner_thoff;
		break;
	default:
		WARN_ON_ONCE(1);
		goto err;
	}
	offset += priv->offset;

	if (skb_copy_bits(skb, offset, dest, priv->len) < 0)
		goto err;

	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static int nft_payload_inner_init(const struct nft_ctx *ctx,
				  const struct nft_expr *expr,
				  const struct nlattr * const tb[])
{
	struct nft_payload *priv = nft_expr_priv(expr);
	u32 base;

	base   = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	switch (base) {
	case NFT_PAYLOAD_TUN_HEADER:
	case NFT_PAYLOAD_LL_HEADER:
	case NFT_PAYLOAD_NETWORK_HEADER:
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->base   = base;
	priv->offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len    = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));

	return nft_parse_register_store(ctx, tb[NFTA_PAYLOAD_DREG],
					&priv->dreg, NULL, NFT_DATA_VALUE,
					priv->len);
}

static const struct nft_expr_ops nft_payload_inner_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.init		= nft_payload_inner_init,
	.dump		= nft_payload_dump,
	/* direct call to nft_payload_inner_eval(). */
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

	return (__force bool)uh->check;
}

static int nft_payload_l4csum_offset(const struct nft_pktinfo *pkt,
				     struct sk_buff *skb,
				     unsigned int *l4csum_offset)
{
	if (pkt->fragoff)
		return -1;

	switch (pkt->tprot) {
	case IPPROTO_TCP:
		*l4csum_offset = offsetof(struct tcphdr, check);
		break;
	case IPPROTO_UDP:
		if (!nft_payload_udp_checksum(skb, nft_thoff(pkt)))
			return -1;
		fallthrough;
	case IPPROTO_UDPLITE:
		*l4csum_offset = offsetof(struct udphdr, check);
		break;
	case IPPROTO_ICMPV6:
		*l4csum_offset = offsetof(struct icmp6hdr, icmp6_cksum);
		break;
	default:
		return -1;
	}

	*l4csum_offset += nft_thoff(pkt);
	return 0;
}

static int nft_payload_csum_sctp(struct sk_buff *skb, int offset)
{
	struct sctphdr *sh;

	if (skb_ensure_writable(skb, offset + sizeof(*sh)))
		return -1;

	sh = (struct sctphdr *)(skb->data + offset);
	sh->checksum = sctp_compute_cksum(skb, offset);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
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

	if (skb_ensure_writable(skb, l4csum_offset + sizeof(sum)) ||
	    skb_store_bits(skb, l4csum_offset, &sum, sizeof(sum)) < 0)
		return -1;

	return 0;
}

static int nft_payload_csum_inet(struct sk_buff *skb, const u32 *src,
				 __wsum fsum, __wsum tsum, int csum_offset)
{
	__sum16 sum;

	if (skb_copy_bits(skb, csum_offset, &sum, sizeof(sum)) < 0)
		return -1;

	nft_csum_replace(&sum, fsum, tsum);
	if (skb_ensure_writable(skb, csum_offset + sizeof(sum)) ||
	    skb_store_bits(skb, csum_offset, &sum, sizeof(sum)) < 0)
		return -1;

	return 0;
}

struct nft_payload_set {
	enum nft_payload_bases	base:8;
	u8			offset;
	u8			len;
	u8			sreg;
	u8			csum_type;
	u8			csum_offset;
	u8			csum_flags;
};

static void nft_payload_set_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	const struct nft_payload_set *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	const u32 *src = &regs->data[priv->sreg];
	int offset, csum_offset;
	__wsum fsum, tsum;

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
		if (!(pkt->flags & NFT_PKTINFO_L4PROTO) || pkt->fragoff)
			goto err;
		offset = nft_thoff(pkt);
		break;
	case NFT_PAYLOAD_INNER_HEADER:
		offset = nft_payload_inner_offset(pkt);
		if (offset < 0)
			goto err;
		break;
	default:
		WARN_ON_ONCE(1);
		goto err;
	}

	csum_offset = offset + priv->csum_offset;
	offset += priv->offset;

	if ((priv->csum_type == NFT_PAYLOAD_CSUM_INET || priv->csum_flags) &&
	    ((priv->base != NFT_PAYLOAD_TRANSPORT_HEADER &&
	      priv->base != NFT_PAYLOAD_INNER_HEADER) ||
	     skb->ip_summed != CHECKSUM_PARTIAL)) {
		fsum = skb_checksum(skb, offset, priv->len, 0);
		tsum = csum_partial(src, priv->len, 0);

		if (priv->csum_type == NFT_PAYLOAD_CSUM_INET &&
		    nft_payload_csum_inet(skb, src, fsum, tsum, csum_offset))
			goto err;

		if (priv->csum_flags &&
		    nft_payload_l4csum_update(pkt, skb, fsum, tsum) < 0)
			goto err;
	}

	if (skb_ensure_writable(skb, max(offset + priv->len, 0)) ||
	    skb_store_bits(skb, offset, src, priv->len) < 0)
		goto err;

	if (priv->csum_type == NFT_PAYLOAD_CSUM_SCTP &&
	    pkt->tprot == IPPROTO_SCTP &&
	    skb->ip_summed != CHECKSUM_PARTIAL) {
		if (pkt->fragoff == 0 &&
		    nft_payload_csum_sctp(skb, nft_thoff(pkt)))
			goto err;
	}

	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static int nft_payload_set_init(const struct nft_ctx *ctx,
				const struct nft_expr *expr,
				const struct nlattr * const tb[])
{
	struct nft_payload_set *priv = nft_expr_priv(expr);
	u32 csum_offset, csum_type = NFT_PAYLOAD_CSUM_NONE;
	int err;

	priv->base        = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	priv->offset      = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len         = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));

	if (tb[NFTA_PAYLOAD_CSUM_TYPE])
		csum_type = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_CSUM_TYPE]));
	if (tb[NFTA_PAYLOAD_CSUM_OFFSET]) {
		err = nft_parse_u32_check(tb[NFTA_PAYLOAD_CSUM_OFFSET], U8_MAX,
					  &csum_offset);
		if (err < 0)
			return err;

		priv->csum_offset = csum_offset;
	}
	if (tb[NFTA_PAYLOAD_CSUM_FLAGS]) {
		u32 flags;

		flags = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_CSUM_FLAGS]));
		if (flags & ~NFT_PAYLOAD_L4CSUM_PSEUDOHDR)
			return -EINVAL;

		priv->csum_flags = flags;
	}

	switch (csum_type) {
	case NFT_PAYLOAD_CSUM_NONE:
	case NFT_PAYLOAD_CSUM_INET:
		break;
	case NFT_PAYLOAD_CSUM_SCTP:
		if (priv->base != NFT_PAYLOAD_TRANSPORT_HEADER)
			return -EINVAL;

		if (priv->csum_offset != offsetof(struct sctphdr, checksum))
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}
	priv->csum_type = csum_type;

	return nft_parse_register_load(tb[NFTA_PAYLOAD_SREG], &priv->sreg,
				       priv->len);
}

static int nft_payload_set_dump(struct sk_buff *skb,
				const struct nft_expr *expr, bool reset)
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

static bool nft_payload_set_reduce(struct nft_regs_track *track,
				   const struct nft_expr *expr)
{
	int i;

	for (i = 0; i < NFT_REG32_NUM; i++) {
		if (!track->regs[i].selector)
			continue;

		if (track->regs[i].selector->ops != &nft_payload_ops &&
		    track->regs[i].selector->ops != &nft_payload_fast_ops)
			continue;

		__nft_reg_track_cancel(track, i);
	}

	return false;
}

static const struct nft_expr_ops nft_payload_set_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload_set)),
	.eval		= nft_payload_set_eval,
	.init		= nft_payload_set_init,
	.dump		= nft_payload_set_dump,
	.reduce		= nft_payload_set_reduce,
};

static const struct nft_expr_ops *
nft_payload_select_ops(const struct nft_ctx *ctx,
		       const struct nlattr * const tb[])
{
	enum nft_payload_bases base;
	unsigned int offset, len;
	int err;

	if (tb[NFTA_PAYLOAD_BASE] == NULL ||
	    tb[NFTA_PAYLOAD_OFFSET] == NULL ||
	    tb[NFTA_PAYLOAD_LEN] == NULL)
		return ERR_PTR(-EINVAL);

	base = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	switch (base) {
	case NFT_PAYLOAD_LL_HEADER:
	case NFT_PAYLOAD_NETWORK_HEADER:
	case NFT_PAYLOAD_TRANSPORT_HEADER:
	case NFT_PAYLOAD_INNER_HEADER:
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

	err = nft_parse_u32_check(tb[NFTA_PAYLOAD_OFFSET], U8_MAX, &offset);
	if (err < 0)
		return ERR_PTR(err);

	err = nft_parse_u32_check(tb[NFTA_PAYLOAD_LEN], U8_MAX, &len);
	if (err < 0)
		return ERR_PTR(err);

	if (len <= 4 && is_power_of_2(len) && IS_ALIGNED(offset, len) &&
	    base != NFT_PAYLOAD_LL_HEADER && base != NFT_PAYLOAD_INNER_HEADER)
		return &nft_payload_fast_ops;
	else
		return &nft_payload_ops;
}

struct nft_expr_type nft_payload_type __read_mostly = {
	.name		= "payload",
	.select_ops	= nft_payload_select_ops,
	.inner_ops	= &nft_payload_inner_ops,
	.policy		= nft_payload_policy,
	.maxattr	= NFTA_PAYLOAD_MAX,
	.owner		= THIS_MODULE,
};

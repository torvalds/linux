/*
 * Copyright (c) 2014 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_reject.h>
#include <net/netfilter/ipv4/nf_reject.h>
#include <net/netfilter/ipv6/nf_reject.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv6.h>
#include "../br_private.h"

static void nft_reject_br_push_etherhdr(struct sk_buff *oldskb,
					struct sk_buff *nskb)
{
	struct ethhdr *eth;

	eth = skb_push(nskb, ETH_HLEN);
	skb_reset_mac_header(nskb);
	ether_addr_copy(eth->h_source, eth_hdr(oldskb)->h_dest);
	ether_addr_copy(eth->h_dest, eth_hdr(oldskb)->h_source);
	eth->h_proto = eth_hdr(oldskb)->h_proto;
	skb_pull(nskb, ETH_HLEN);
}

static int nft_bridge_iphdr_validate(struct sk_buff *skb)
{
	struct iphdr *iph;
	u32 len;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		return 0;

	iph = ip_hdr(skb);
	if (iph->ihl < 5 || iph->version != 4)
		return 0;

	len = ntohs(iph->tot_len);
	if (skb->len < len)
		return 0;
	else if (len < (iph->ihl*4))
		return 0;

	if (!pskb_may_pull(skb, iph->ihl*4))
		return 0;

	return 1;
}

/* We cannot use oldskb->dev, it can be either bridge device (NF_BRIDGE INPUT)
 * or the bridge port (NF_BRIDGE PREROUTING).
 */
static void nft_reject_br_send_v4_tcp_reset(struct net *net,
					    struct sk_buff *oldskb,
					    const struct net_device *dev,
					    int hook)
{
	struct sk_buff *nskb;
	struct iphdr *niph;
	const struct tcphdr *oth;
	struct tcphdr _oth;

	if (!nft_bridge_iphdr_validate(oldskb))
		return;

	oth = nf_reject_ip_tcphdr_get(oldskb, &_oth, hook);
	if (!oth)
		return;

	nskb = alloc_skb(sizeof(struct iphdr) + sizeof(struct tcphdr) +
			 LL_MAX_HEADER, GFP_ATOMIC);
	if (!nskb)
		return;

	skb_reserve(nskb, LL_MAX_HEADER);
	niph = nf_reject_iphdr_put(nskb, oldskb, IPPROTO_TCP,
				   net->ipv4.sysctl_ip_default_ttl);
	nf_reject_ip_tcphdr_put(nskb, oldskb, oth);
	niph->tot_len = htons(nskb->len);
	ip_send_check(niph);

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static void nft_reject_br_send_v4_unreach(struct net *net,
					  struct sk_buff *oldskb,
					  const struct net_device *dev,
					  int hook, u8 code)
{
	struct sk_buff *nskb;
	struct iphdr *niph;
	struct icmphdr *icmph;
	unsigned int len;
	__wsum csum;
	u8 proto;

	if (!nft_bridge_iphdr_validate(oldskb))
		return;

	/* IP header checks: fragment. */
	if (ip_hdr(oldskb)->frag_off & htons(IP_OFFSET))
		return;

	/* RFC says return as much as we can without exceeding 576 bytes. */
	len = min_t(unsigned int, 536, oldskb->len);

	if (!pskb_may_pull(oldskb, len))
		return;

	if (pskb_trim_rcsum(oldskb, ntohs(ip_hdr(oldskb)->tot_len)))
		return;

	proto = ip_hdr(oldskb)->protocol;

	if (!skb_csum_unnecessary(oldskb) &&
	    nf_reject_verify_csum(proto) &&
	    nf_ip_checksum(oldskb, hook, ip_hdrlen(oldskb), proto))
		return;

	nskb = alloc_skb(sizeof(struct iphdr) + sizeof(struct icmphdr) +
			 LL_MAX_HEADER + len, GFP_ATOMIC);
	if (!nskb)
		return;

	skb_reserve(nskb, LL_MAX_HEADER);
	niph = nf_reject_iphdr_put(nskb, oldskb, IPPROTO_ICMP,
				   net->ipv4.sysctl_ip_default_ttl);

	skb_reset_transport_header(nskb);
	icmph = skb_put_zero(nskb, sizeof(struct icmphdr));
	icmph->type     = ICMP_DEST_UNREACH;
	icmph->code	= code;

	skb_put_data(nskb, skb_network_header(oldskb), len);

	csum = csum_partial((void *)icmph, len + sizeof(struct icmphdr), 0);
	icmph->checksum = csum_fold(csum);

	niph->tot_len	= htons(nskb->len);
	ip_send_check(niph);

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static int nft_bridge_ip6hdr_validate(struct sk_buff *skb)
{
	struct ipv6hdr *hdr;
	u32 pkt_len;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		return 0;

	hdr = ipv6_hdr(skb);
	if (hdr->version != 6)
		return 0;

	pkt_len = ntohs(hdr->payload_len);
	if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
		return 0;

	return 1;
}

static void nft_reject_br_send_v6_tcp_reset(struct net *net,
					    struct sk_buff *oldskb,
					    const struct net_device *dev,
					    int hook)
{
	struct sk_buff *nskb;
	const struct tcphdr *oth;
	struct tcphdr _oth;
	unsigned int otcplen;
	struct ipv6hdr *nip6h;

	if (!nft_bridge_ip6hdr_validate(oldskb))
		return;

	oth = nf_reject_ip6_tcphdr_get(oldskb, &_oth, &otcplen, hook);
	if (!oth)
		return;

	nskb = alloc_skb(sizeof(struct ipv6hdr) + sizeof(struct tcphdr) +
			 LL_MAX_HEADER, GFP_ATOMIC);
	if (!nskb)
		return;

	skb_reserve(nskb, LL_MAX_HEADER);
	nip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_TCP,
				     net->ipv6.devconf_all->hop_limit);
	nf_reject_ip6_tcphdr_put(nskb, oldskb, oth, otcplen);
	nip6h->payload_len = htons(nskb->len - sizeof(struct ipv6hdr));

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static bool reject6_br_csum_ok(struct sk_buff *skb, int hook)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	int thoff;
	__be16 fo;
	u8 proto = ip6h->nexthdr;

	if (skb_csum_unnecessary(skb))
		return true;

	if (ip6h->payload_len &&
	    pskb_trim_rcsum(skb, ntohs(ip6h->payload_len) + sizeof(*ip6h)))
		return false;

	ip6h = ipv6_hdr(skb);
	thoff = ipv6_skip_exthdr(skb, ((u8*)(ip6h+1) - skb->data), &proto, &fo);
	if (thoff < 0 || thoff >= skb->len || (fo & htons(~0x7)) != 0)
		return false;

	if (!nf_reject_verify_csum(proto))
		return true;

	return nf_ip6_checksum(skb, hook, thoff, proto) == 0;
}

static void nft_reject_br_send_v6_unreach(struct net *net,
					  struct sk_buff *oldskb,
					  const struct net_device *dev,
					  int hook, u8 code)
{
	struct sk_buff *nskb;
	struct ipv6hdr *nip6h;
	struct icmp6hdr *icmp6h;
	unsigned int len;

	if (!nft_bridge_ip6hdr_validate(oldskb))
		return;

	/* Include "As much of invoking packet as possible without the ICMPv6
	 * packet exceeding the minimum IPv6 MTU" in the ICMP payload.
	 */
	len = min_t(unsigned int, 1220, oldskb->len);

	if (!pskb_may_pull(oldskb, len))
		return;

	if (!reject6_br_csum_ok(oldskb, hook))
		return;

	nskb = alloc_skb(sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) +
			 LL_MAX_HEADER + len, GFP_ATOMIC);
	if (!nskb)
		return;

	skb_reserve(nskb, LL_MAX_HEADER);
	nip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_ICMPV6,
				     net->ipv6.devconf_all->hop_limit);

	skb_reset_transport_header(nskb);
	icmp6h = skb_put_zero(nskb, sizeof(struct icmp6hdr));
	icmp6h->icmp6_type = ICMPV6_DEST_UNREACH;
	icmp6h->icmp6_code = code;

	skb_put_data(nskb, skb_network_header(oldskb), len);
	nip6h->payload_len = htons(nskb->len - sizeof(struct ipv6hdr));

	icmp6h->icmp6_cksum =
		csum_ipv6_magic(&nip6h->saddr, &nip6h->daddr,
				nskb->len - sizeof(struct ipv6hdr),
				IPPROTO_ICMPV6,
				csum_partial(icmp6h,
					     nskb->len - sizeof(struct ipv6hdr),
					     0));

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static void nft_reject_bridge_eval(const struct nft_expr *expr,
				   struct nft_regs *regs,
				   const struct nft_pktinfo *pkt)
{
	struct nft_reject *priv = nft_expr_priv(expr);
	const unsigned char *dest = eth_hdr(pkt->skb)->h_dest;

	if (is_broadcast_ether_addr(dest) ||
	    is_multicast_ether_addr(dest))
		goto out;

	switch (eth_hdr(pkt->skb)->h_proto) {
	case htons(ETH_P_IP):
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nft_reject_br_send_v4_unreach(nft_net(pkt), pkt->skb,
						      nft_in(pkt),
						      nft_hook(pkt),
						      priv->icmp_code);
			break;
		case NFT_REJECT_TCP_RST:
			nft_reject_br_send_v4_tcp_reset(nft_net(pkt), pkt->skb,
							nft_in(pkt),
							nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nft_reject_br_send_v4_unreach(nft_net(pkt), pkt->skb,
						      nft_in(pkt),
						      nft_hook(pkt),
						      nft_reject_icmp_code(priv->icmp_code));
			break;
		}
		break;
	case htons(ETH_P_IPV6):
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nft_reject_br_send_v6_unreach(nft_net(pkt), pkt->skb,
						      nft_in(pkt),
						      nft_hook(pkt),
						      priv->icmp_code);
			break;
		case NFT_REJECT_TCP_RST:
			nft_reject_br_send_v6_tcp_reset(nft_net(pkt), pkt->skb,
							nft_in(pkt),
							nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nft_reject_br_send_v6_unreach(nft_net(pkt), pkt->skb,
						      nft_in(pkt),
						      nft_hook(pkt),
						      nft_reject_icmpv6_code(priv->icmp_code));
			break;
		}
		break;
	default:
		/* No explicit way to reject this protocol, drop it. */
		break;
	}
out:
	regs->verdict.code = NF_DROP;
}

static int nft_reject_bridge_validate(const struct nft_ctx *ctx,
				      const struct nft_expr *expr,
				      const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_BR_PRE_ROUTING) |
						    (1 << NF_BR_LOCAL_IN));
}

static int nft_reject_bridge_init(const struct nft_ctx *ctx,
				  const struct nft_expr *expr,
				  const struct nlattr * const tb[])
{
	struct nft_reject *priv = nft_expr_priv(expr);
	int icmp_code;

	if (tb[NFTA_REJECT_TYPE] == NULL)
		return -EINVAL;

	priv->type = ntohl(nla_get_be32(tb[NFTA_REJECT_TYPE]));
	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
	case NFT_REJECT_ICMPX_UNREACH:
		if (tb[NFTA_REJECT_ICMP_CODE] == NULL)
			return -EINVAL;

		icmp_code = nla_get_u8(tb[NFTA_REJECT_ICMP_CODE]);
		if (priv->type == NFT_REJECT_ICMPX_UNREACH &&
		    icmp_code > NFT_REJECT_ICMPX_MAX)
			return -EINVAL;

		priv->icmp_code = icmp_code;
		break;
	case NFT_REJECT_TCP_RST:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int nft_reject_bridge_dump(struct sk_buff *skb,
				  const struct nft_expr *expr)
{
	const struct nft_reject *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_REJECT_TYPE, htonl(priv->type)))
		goto nla_put_failure;

	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
	case NFT_REJECT_ICMPX_UNREACH:
		if (nla_put_u8(skb, NFTA_REJECT_ICMP_CODE, priv->icmp_code))
			goto nla_put_failure;
		break;
	default:
		break;
	}

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_reject_bridge_type;
static const struct nft_expr_ops nft_reject_bridge_ops = {
	.type		= &nft_reject_bridge_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_bridge_eval,
	.init		= nft_reject_bridge_init,
	.dump		= nft_reject_bridge_dump,
	.validate	= nft_reject_bridge_validate,
};

static struct nft_expr_type nft_reject_bridge_type __read_mostly = {
	.family		= NFPROTO_BRIDGE,
	.name		= "reject",
	.ops		= &nft_reject_bridge_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_bridge_module_init(void)
{
	return nft_register_expr(&nft_reject_bridge_type);
}

static void __exit nft_reject_bridge_module_exit(void)
{
	nft_unregister_expr(&nft_reject_bridge_type);
}

module_init(nft_reject_bridge_module_init);
module_exit(nft_reject_bridge_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_AF_EXPR(AF_BRIDGE, "reject");

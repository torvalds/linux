// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Pablo Neira Ayuso <pablo@netfilter.org>
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

	if (skb_vlan_tag_present(oldskb)) {
		u16 vid = skb_vlan_tag_get(oldskb);

		__vlan_hwaccel_put_tag(nskb, oldskb->vlan_proto, vid);
	}
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

	nskb = nf_reject_skb_v4_tcp_reset(net, oldskb, NULL, hook);
	if (!nskb)
		return;

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static void nft_reject_br_send_v4_unreach(struct net *net,
					  struct sk_buff *oldskb,
					  const struct net_device *dev,
					  int hook, u8 code)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v4_unreach(net, oldskb, NULL, hook, code);
	if (!nskb)
		return;

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}

static void nft_reject_br_send_v6_tcp_reset(struct net *net,
					    struct sk_buff *oldskb,
					    const struct net_device *dev,
					    int hook)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v6_tcp_reset(net, oldskb, NULL, hook);
	if (!nskb)
		return;

	nft_reject_br_push_etherhdr(oldskb, nskb);

	br_forward(br_port_get_rcu(dev), nskb, false, true);
}


static void nft_reject_br_send_v6_unreach(struct net *net,
					  struct sk_buff *oldskb,
					  const struct net_device *dev,
					  int hook, u8 code)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v6_unreach(net, oldskb, NULL, hook, code);
	if (!nskb)
		return;

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
				      const struct nft_expr *expr)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_BR_PRE_ROUTING) |
						    (1 << NF_BR_LOCAL_IN));
}

static struct nft_expr_type nft_reject_bridge_type;
static const struct nft_expr_ops nft_reject_bridge_ops = {
	.type		= &nft_reject_bridge_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_bridge_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
	.validate	= nft_reject_bridge_validate,
	.reduce		= NFT_REDUCE_READONLY,
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
MODULE_DESCRIPTION("Reject packets from bridge via nftables");

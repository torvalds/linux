// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Laura Garcia Liebana <nevola@gmail.com>
 * Copyright (c) 2020 Jose M. Guisado <guigom@riseup.net>
 */

#include <linux/etherdevice.h>
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

static void nft_reject_queue_xmit(struct sk_buff *nskb, struct sk_buff *oldskb)
{
	dev_hard_header(nskb, nskb->dev, ntohs(oldskb->protocol),
			eth_hdr(oldskb)->h_source, eth_hdr(oldskb)->h_dest,
			nskb->len);
	dev_queue_xmit(nskb);
}

static void nft_reject_netdev_send_v4_tcp_reset(struct net *net,
						struct sk_buff *oldskb,
						const struct net_device *dev,
						int hook)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v4_tcp_reset(net, oldskb, dev, hook);
	if (!nskb)
		return;

	nft_reject_queue_xmit(nskb, oldskb);
}

static void nft_reject_netdev_send_v4_unreach(struct net *net,
					      struct sk_buff *oldskb,
					      const struct net_device *dev,
					      int hook, u8 code)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v4_unreach(net, oldskb, dev, hook, code);
	if (!nskb)
		return;

	nft_reject_queue_xmit(nskb, oldskb);
}

static void nft_reject_netdev_send_v6_tcp_reset(struct net *net,
						struct sk_buff *oldskb,
						const struct net_device *dev,
						int hook)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v6_tcp_reset(net, oldskb, dev, hook);
	if (!nskb)
		return;

	nft_reject_queue_xmit(nskb, oldskb);
}


static void nft_reject_netdev_send_v6_unreach(struct net *net,
					      struct sk_buff *oldskb,
					      const struct net_device *dev,
					      int hook, u8 code)
{
	struct sk_buff *nskb;

	nskb = nf_reject_skb_v6_unreach(net, oldskb, dev, hook, code);
	if (!nskb)
		return;

	nft_reject_queue_xmit(nskb, oldskb);
}

static void nft_reject_netdev_eval(const struct nft_expr *expr,
				   struct nft_regs *regs,
				   const struct nft_pktinfo *pkt)
{
	struct ethhdr *eth = eth_hdr(pkt->skb);
	struct nft_reject *priv = nft_expr_priv(expr);
	const unsigned char *dest = eth->h_dest;

	if (is_broadcast_ether_addr(dest) ||
	    is_multicast_ether_addr(dest))
		goto out;

	switch (eth->h_proto) {
	case htons(ETH_P_IP):
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nft_reject_netdev_send_v4_unreach(nft_net(pkt), pkt->skb,
							  nft_in(pkt),
							  nft_hook(pkt),
							  priv->icmp_code);
			break;
		case NFT_REJECT_TCP_RST:
			nft_reject_netdev_send_v4_tcp_reset(nft_net(pkt), pkt->skb,
							    nft_in(pkt),
							    nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nft_reject_netdev_send_v4_unreach(nft_net(pkt), pkt->skb,
							  nft_in(pkt),
							  nft_hook(pkt),
							  nft_reject_icmp_code(priv->icmp_code));
			break;
		}
		break;
	case htons(ETH_P_IPV6):
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nft_reject_netdev_send_v6_unreach(nft_net(pkt), pkt->skb,
							  nft_in(pkt),
							  nft_hook(pkt),
							  priv->icmp_code);
			break;
		case NFT_REJECT_TCP_RST:
			nft_reject_netdev_send_v6_tcp_reset(nft_net(pkt), pkt->skb,
							    nft_in(pkt),
							    nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nft_reject_netdev_send_v6_unreach(nft_net(pkt), pkt->skb,
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

static int nft_reject_netdev_validate(const struct nft_ctx *ctx,
				      const struct nft_expr *expr,
				      const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_NETDEV_INGRESS));
}

static struct nft_expr_type nft_reject_netdev_type;
static const struct nft_expr_ops nft_reject_netdev_ops = {
	.type		= &nft_reject_netdev_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_netdev_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
	.validate	= nft_reject_netdev_validate,
};

static struct nft_expr_type nft_reject_netdev_type __read_mostly = {
	.family		= NFPROTO_NETDEV,
	.name		= "reject",
	.ops		= &nft_reject_netdev_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_netdev_module_init(void)
{
	return nft_register_expr(&nft_reject_netdev_type);
}

static void __exit nft_reject_netdev_module_exit(void)
{
	nft_unregister_expr(&nft_reject_netdev_type);
}

module_init(nft_reject_netdev_module_init);
module_exit(nft_reject_netdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Laura Garcia Liebana <nevola@gmail.com>");
MODULE_AUTHOR("Jose M. Guisado <guigom@riseup.net>");
MODULE_DESCRIPTION("Reject packets from netdev via nftables");
MODULE_ALIAS_NFT_AF_EXPR(5, "reject");

/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter_bridge.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_bridge.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

int nft_bridge_iphdr_validate(struct sk_buff *skb)
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
EXPORT_SYMBOL_GPL(nft_bridge_iphdr_validate);

int nft_bridge_ip6hdr_validate(struct sk_buff *skb)
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
EXPORT_SYMBOL_GPL(nft_bridge_ip6hdr_validate);

static inline void nft_bridge_set_pktinfo_ipv4(struct nft_pktinfo *pkt,
					       const struct nf_hook_ops *ops,
					       struct sk_buff *skb,
					       const struct nf_hook_state *state)
{
	if (nft_bridge_iphdr_validate(skb))
		nft_set_pktinfo_ipv4(pkt, ops, skb, state);
	else
		nft_set_pktinfo(pkt, ops, skb, state);
}

static inline void nft_bridge_set_pktinfo_ipv6(struct nft_pktinfo *pkt,
					       const struct nf_hook_ops *ops,
					       struct sk_buff *skb,
					       const struct nf_hook_state *state)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (nft_bridge_ip6hdr_validate(skb) &&
	    nft_set_pktinfo_ipv6(pkt, ops, skb, state) == 0)
		return;
#endif
	nft_set_pktinfo(pkt, ops, skb, state);
}

static unsigned int
nft_do_chain_bridge(const struct nf_hook_ops *ops,
		    struct sk_buff *skb,
		    const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		nft_bridge_set_pktinfo_ipv4(&pkt, ops, skb, state);
		break;
	case htons(ETH_P_IPV6):
		nft_bridge_set_pktinfo_ipv6(&pkt, ops, skb, state);
		break;
	default:
		nft_set_pktinfo(&pkt, ops, skb, state);
		break;
	}

	return nft_do_chain(&pkt, ops);
}

static struct nft_af_info nft_af_bridge __read_mostly = {
	.family		= NFPROTO_BRIDGE,
	.nhooks		= NF_BR_NUMHOOKS,
	.owner		= THIS_MODULE,
	.nops		= 1,
	.hooks		= {
		[NF_BR_PRE_ROUTING]	= nft_do_chain_bridge,
		[NF_BR_LOCAL_IN]	= nft_do_chain_bridge,
		[NF_BR_FORWARD]		= nft_do_chain_bridge,
		[NF_BR_LOCAL_OUT]	= nft_do_chain_bridge,
		[NF_BR_POST_ROUTING]	= nft_do_chain_bridge,
	},
};

static int nf_tables_bridge_init_net(struct net *net)
{
	net->nft.bridge = kmalloc(sizeof(struct nft_af_info), GFP_KERNEL);
	if (net->nft.bridge == NULL)
		return -ENOMEM;

	memcpy(net->nft.bridge, &nft_af_bridge, sizeof(nft_af_bridge));

	if (nft_register_afinfo(net, net->nft.bridge) < 0)
		goto err;

	return 0;
err:
	kfree(net->nft.bridge);
	return -ENOMEM;
}

static void nf_tables_bridge_exit_net(struct net *net)
{
	nft_unregister_afinfo(net->nft.bridge);
	kfree(net->nft.bridge);
}

static struct pernet_operations nf_tables_bridge_net_ops = {
	.init	= nf_tables_bridge_init_net,
	.exit	= nf_tables_bridge_exit_net,
};

static const struct nf_chain_type filter_bridge = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_BRIDGE,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_LOCAL_IN) |
			  (1 << NF_BR_FORWARD) |
			  (1 << NF_BR_LOCAL_OUT) |
			  (1 << NF_BR_POST_ROUTING),
};

static int __init nf_tables_bridge_init(void)
{
	int ret;

	nft_register_chain_type(&filter_bridge);
	ret = register_pernet_subsys(&nf_tables_bridge_net_ops);
	if (ret < 0)
		nft_unregister_chain_type(&filter_bridge);

	return ret;
}

static void __exit nf_tables_bridge_exit(void)
{
	unregister_pernet_subsys(&nf_tables_bridge_net_ops);
	nft_unregister_chain_type(&filter_bridge);
}

module_init(nf_tables_bridge_init);
module_exit(nf_tables_bridge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(AF_BRIDGE);

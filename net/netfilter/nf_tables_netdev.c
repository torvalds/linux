/*
 * Copyright (c) 2015 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netfilter/nf_tables.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int
nft_do_chain_netdev(void *priv, struct sk_buff *skb,
		    const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt, skb);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt, skb);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt, skb);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nf_chain_type nft_filter_chain_netdev = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_NETDEV,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_NETDEV_INGRESS),
	.hooks		= {
		[NF_NETDEV_INGRESS]	= nft_do_chain_netdev,
	},
};

static void nft_netdev_event(unsigned long event, struct net_device *dev,
			     struct nft_ctx *ctx)
{
	struct nft_base_chain *basechain = nft_base_chain(ctx->chain);

	switch (event) {
	case NETDEV_UNREGISTER:
		if (strcmp(basechain->dev_name, dev->name) != 0)
			return;

		__nft_release_basechain(ctx);
		break;
	case NETDEV_CHANGENAME:
		if (dev->ifindex != basechain->ops.dev->ifindex)
			return;

		strncpy(basechain->dev_name, dev->name, IFNAMSIZ);
		break;
	}
}

static int nf_tables_netdev_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nft_table *table;
	struct nft_chain *chain, *nr;
	struct nft_ctx ctx = {
		.net	= dev_net(dev),
	};

	if (event != NETDEV_UNREGISTER &&
	    event != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_for_each_entry(table, &ctx.net->nft.tables, list) {
		if (table->family != NFPROTO_NETDEV)
			continue;

		ctx.family = table->family;
		ctx.table = table;
		list_for_each_entry_safe(chain, nr, &table->chains, list) {
			if (!nft_is_base_chain(chain))
				continue;

			ctx.chain = chain;
			nft_netdev_event(event, dev, &ctx);
		}
	}
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);

	return NOTIFY_DONE;
}

static struct notifier_block nf_tables_netdev_notifier = {
	.notifier_call	= nf_tables_netdev_event,
};

static int __init nf_tables_netdev_init(void)
{
	int ret;

	ret = nft_register_chain_type(&nft_filter_chain_netdev);
	if (ret)
		return ret;

	ret = register_netdevice_notifier(&nf_tables_netdev_notifier);
	if (ret)
		goto err_register_netdevice_notifier;

	return 0;

err_register_netdevice_notifier:
	nft_unregister_chain_type(&nft_filter_chain_netdev);

	return ret;
}

static void __exit nf_tables_netdev_exit(void)
{
	unregister_netdevice_notifier(&nf_tables_netdev_notifier);
	nft_unregister_chain_type(&nft_filter_chain_netdev);
}

module_init(nf_tables_netdev_init);
module_exit(nf_tables_netdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_CHAIN(5, "filter"); /* NFPROTO_NETDEV */

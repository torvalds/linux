/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
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

static struct nft_af_info nft_af_bridge __read_mostly = {
	.family		= NFPROTO_BRIDGE,
	.nhooks		= NF_BR_NUMHOOKS,
	.owner		= THIS_MODULE,
};

static int __init nf_tables_bridge_init(void)
{
	return nft_register_afinfo(&nft_af_bridge);
}

static void __exit nf_tables_bridge_exit(void)
{
	nft_unregister_afinfo(&nft_af_bridge);
}

module_init(nf_tables_bridge_init);
module_exit(nf_tables_bridge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(AF_BRIDGE);

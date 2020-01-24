// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ebt_redirect
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 */
#include <linux/module.h>
#include <net/sock.h>
#include "../br_private.h"
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_redirect.h>

static unsigned int
ebt_redirect_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ebt_redirect_info *info = par->targinfo;

	if (skb_ensure_writable(skb, ETH_ALEN))
		return EBT_DROP;

	if (xt_hooknum(par) != NF_BR_BROUTING)
		/* rcu_read_lock()ed by nf_hook_thresh */
		ether_addr_copy(eth_hdr(skb)->h_dest,
				br_port_get_rcu(xt_in(par))->br->dev->dev_addr);
	else
		ether_addr_copy(eth_hdr(skb)->h_dest, xt_in(par)->dev_addr);
	skb->pkt_type = PACKET_HOST;
	return info->target;
}

static int ebt_redirect_tg_check(const struct xt_tgchk_param *par)
{
	const struct ebt_redirect_info *info = par->targinfo;
	unsigned int hook_mask;

	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;

	hook_mask = par->hook_mask & ~(1 << NF_BR_NUMHOOKS);
	if ((strcmp(par->table, "nat") != 0 ||
	    hook_mask & ~(1 << NF_BR_PRE_ROUTING)) &&
	    (strcmp(par->table, "broute") != 0 ||
	    hook_mask & ~(1 << NF_BR_BROUTING)))
		return -EINVAL;
	if (ebt_invalid_target(info->target))
		return -EINVAL;
	return 0;
}

static struct xt_target ebt_redirect_tg_reg __read_mostly = {
	.name		= "redirect",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.hooks		= (1 << NF_BR_NUMHOOKS) | (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_BROUTING),
	.target		= ebt_redirect_tg,
	.checkentry	= ebt_redirect_tg_check,
	.targetsize	= sizeof(struct ebt_redirect_info),
	.me		= THIS_MODULE,
};

static int __init ebt_redirect_init(void)
{
	return xt_register_target(&ebt_redirect_tg_reg);
}

static void __exit ebt_redirect_fini(void)
{
	xt_unregister_target(&ebt_redirect_tg_reg);
}

module_init(ebt_redirect_init);
module_exit(ebt_redirect_fini);
MODULE_DESCRIPTION("Ebtables: Packet redirection to localhost");
MODULE_LICENSE("GPL");

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
ebt_redirect_tg(struct sk_buff *skb, const struct net_device *in,
		const struct net_device *out, unsigned int hooknr,
		const struct xt_target *target, const void *data)
{
	const struct ebt_redirect_info *info = data;

	if (!skb_make_writable(skb, 0))
		return EBT_DROP;

	if (hooknr != NF_BR_BROUTING)
		memcpy(eth_hdr(skb)->h_dest,
		       in->br_port->br->dev->dev_addr, ETH_ALEN);
	else
		memcpy(eth_hdr(skb)->h_dest, in->dev_addr, ETH_ALEN);
	skb->pkt_type = PACKET_HOST;
	return info->target;
}

static bool
ebt_redirect_tg_check(const char *tablename, const void *e,
		      const struct xt_target *target, void *data,
		      unsigned int hookmask)
{
	const struct ebt_redirect_info *info = data;

	if (BASE_CHAIN && info->target == EBT_RETURN)
		return false;
	CLEAR_BASE_CHAIN_BIT;
	if ( (strcmp(tablename, "nat") || hookmask & ~(1 << NF_BR_PRE_ROUTING)) &&
	     (strcmp(tablename, "broute") || hookmask & ~(1 << NF_BR_BROUTING)) )
		return false;
	if (INVALID_TARGET)
		return false;
	return true;
}

static struct xt_target ebt_redirect_tg_reg __read_mostly = {
	.name		= "redirect",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.target		= ebt_redirect_tg,
	.checkentry	= ebt_redirect_tg_check,
	.targetsize	= XT_ALIGN(sizeof(struct ebt_redirect_info)),
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

/*
 *  ebt_redirect
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 */

#include <linux/netfilter.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_redirect.h>
#include <linux/module.h>
#include <net/sock.h>
#include "../br_private.h"

static int ebt_target_redirect(struct sk_buff *skb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	const struct ebt_redirect_info *info = data;

	if (skb_make_writable(skb, 0))
		return NF_DROP;

	if (hooknr != NF_BR_BROUTING)
		memcpy(eth_hdr(skb)->h_dest,
		       in->br_port->br->dev->dev_addr, ETH_ALEN);
	else
		memcpy(eth_hdr(skb)->h_dest, in->dev_addr, ETH_ALEN);
	skb->pkt_type = PACKET_HOST;
	return info->target;
}

static int ebt_target_redirect_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	const struct ebt_redirect_info *info = data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_redirect_info)))
		return -EINVAL;
	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if ( (strcmp(tablename, "nat") || hookmask & ~(1 << NF_BR_PRE_ROUTING)) &&
	     (strcmp(tablename, "broute") || hookmask & ~(1 << NF_BR_BROUTING)) )
		return -EINVAL;
	if (INVALID_TARGET)
		return -EINVAL;
	return 0;
}

static struct ebt_target redirect_target __read_mostly = {
	.name		= EBT_REDIRECT_TARGET,
	.target		= ebt_target_redirect,
	.check		= ebt_target_redirect_check,
	.me		= THIS_MODULE,
};

static int __init ebt_redirect_init(void)
{
	return ebt_register_target(&redirect_target);
}

static void __exit ebt_redirect_fini(void)
{
	ebt_unregister_target(&redirect_target);
}

module_init(ebt_redirect_init);
module_exit(ebt_redirect_fini);
MODULE_DESCRIPTION("Ebtables: Packet redirection to localhost");
MODULE_LICENSE("GPL");

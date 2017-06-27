/*
 *  ebt_dnat
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  June, 2002
 *
 */
#include <linux/module.h>
#include <net/sock.h>
#include "../br_private.h"
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nat.h>

static unsigned int
ebt_dnat_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ebt_nat_info *info = par->targinfo;
	struct net_device *dev;

	if (!skb_make_writable(skb, 0))
		return EBT_DROP;

	ether_addr_copy(eth_hdr(skb)->h_dest, info->mac);

	if (is_multicast_ether_addr(info->mac)) {
		if (is_broadcast_ether_addr(info->mac))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	} else {
		if (xt_hooknum(par) != NF_BR_BROUTING)
			dev = br_port_get_rcu(xt_in(par))->br->dev;
		else
			dev = xt_in(par);

		if (ether_addr_equal(info->mac, dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
	}

	return info->target;
}

static int ebt_dnat_tg_check(const struct xt_tgchk_param *par)
{
	const struct ebt_nat_info *info = par->targinfo;
	unsigned int hook_mask;

	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;

	hook_mask = par->hook_mask & ~(1 << NF_BR_NUMHOOKS);
	if ((strcmp(par->table, "nat") != 0 ||
	    (hook_mask & ~((1 << NF_BR_PRE_ROUTING) |
	    (1 << NF_BR_LOCAL_OUT)))) &&
	    (strcmp(par->table, "broute") != 0 ||
	    hook_mask & ~(1 << NF_BR_BROUTING)))
		return -EINVAL;
	if (INVALID_TARGET)
		return -EINVAL;
	return 0;
}

static struct xt_target ebt_dnat_tg_reg __read_mostly = {
	.name		= "dnat",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.hooks		= (1 << NF_BR_NUMHOOKS) | (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_LOCAL_OUT) | (1 << NF_BR_BROUTING),
	.target		= ebt_dnat_tg,
	.checkentry	= ebt_dnat_tg_check,
	.targetsize	= sizeof(struct ebt_nat_info),
	.me		= THIS_MODULE,
};

static int __init ebt_dnat_init(void)
{
	return xt_register_target(&ebt_dnat_tg_reg);
}

static void __exit ebt_dnat_fini(void)
{
	xt_unregister_target(&ebt_dnat_tg_reg);
}

module_init(ebt_dnat_init);
module_exit(ebt_dnat_fini);
MODULE_DESCRIPTION("Ebtables: Destination MAC address translation");
MODULE_LICENSE("GPL");

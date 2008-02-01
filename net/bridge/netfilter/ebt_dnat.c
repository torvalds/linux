/*
 *  ebt_dnat
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  June, 2002
 *
 */

#include <linux/netfilter.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nat.h>
#include <linux/module.h>
#include <net/sock.h>

static int ebt_target_dnat(struct sk_buff *skb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	const struct ebt_nat_info *info = data;

	if (skb_make_writable(skb, 0))
		return NF_DROP;

	memcpy(eth_hdr(skb)->h_dest, info->mac, ETH_ALEN);
	return info->target;
}

static int ebt_target_dnat_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	const struct ebt_nat_info *info = data;

	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if ( (strcmp(tablename, "nat") ||
	   (hookmask & ~((1 << NF_BR_PRE_ROUTING) | (1 << NF_BR_LOCAL_OUT)))) &&
	   (strcmp(tablename, "broute") || hookmask & ~(1 << NF_BR_BROUTING)) )
		return -EINVAL;
	if (datalen != EBT_ALIGN(sizeof(struct ebt_nat_info)))
		return -EINVAL;
	if (INVALID_TARGET)
		return -EINVAL;
	return 0;
}

static struct ebt_target dnat __read_mostly = {
	.name		= EBT_DNAT_TARGET,
	.target		= ebt_target_dnat,
	.check		= ebt_target_dnat_check,
	.me		= THIS_MODULE,
};

static int __init ebt_dnat_init(void)
{
	return ebt_register_target(&dnat);
}

static void __exit ebt_dnat_fini(void)
{
	ebt_unregister_target(&dnat);
}

module_init(ebt_dnat_init);
module_exit(ebt_dnat_fini);
MODULE_DESCRIPTION("Ebtables: Destination MAC address translation");
MODULE_LICENSE("GPL");

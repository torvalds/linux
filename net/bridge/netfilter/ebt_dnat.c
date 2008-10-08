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
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nat.h>

static unsigned int
ebt_dnat_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct ebt_nat_info *info = par->targinfo;

	if (!skb_make_writable(skb, 0))
		return EBT_DROP;

	memcpy(eth_hdr(skb)->h_dest, info->mac, ETH_ALEN);
	return info->target;
}

static bool
ebt_dnat_tg_check(const char *tablename, const void *entry,
		  const struct xt_target *target, void *data,
		  unsigned int hookmask)
{
	const struct ebt_nat_info *info = data;

	if (BASE_CHAIN && info->target == EBT_RETURN)
		return false;
	CLEAR_BASE_CHAIN_BIT;
	if ( (strcmp(tablename, "nat") ||
	   (hookmask & ~((1 << NF_BR_PRE_ROUTING) | (1 << NF_BR_LOCAL_OUT)))) &&
	   (strcmp(tablename, "broute") || hookmask & ~(1 << NF_BR_BROUTING)) )
		return false;
	if (INVALID_TARGET)
		return false;
	return true;
}

static struct xt_target ebt_dnat_tg_reg __read_mostly = {
	.name		= "dnat",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.hooks		= (1 << NF_BR_NUMHOOKS) | (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_LOCAL_OUT) | (1 << NF_BR_BROUTING),
	.target		= ebt_dnat_tg,
	.checkentry	= ebt_dnat_tg_check,
	.targetsize	= XT_ALIGN(sizeof(struct ebt_nat_info)),
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

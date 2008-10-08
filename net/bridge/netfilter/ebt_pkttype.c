/*
 *  ebt_pkttype
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2003
 *
 */
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_pkttype.h>

static int ebt_filter_pkttype(const struct sk_buff *skb,
   const struct net_device *in,
   const struct net_device *out,
   const void *data,
   unsigned int datalen)
{
	const struct ebt_pkttype_info *info = data;

	return (skb->pkt_type != info->pkt_type) ^ info->invert;
}

static bool ebt_pkttype_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	const struct ebt_pkttype_info *info = data;

	if (info->invert != 0 && info->invert != 1)
		return false;
	/* Allow any pkt_type value */
	return true;
}

static struct ebt_match filter_pkttype __read_mostly = {
	.name		= EBT_PKTTYPE_MATCH,
	.match		= ebt_filter_pkttype,
	.check		= ebt_pkttype_check,
	.matchsize	= XT_ALIGN(sizeof(struct ebt_pkttype_info)),
	.me		= THIS_MODULE,
};

static int __init ebt_pkttype_init(void)
{
	return ebt_register_match(&filter_pkttype);
}

static void __exit ebt_pkttype_fini(void)
{
	ebt_unregister_match(&filter_pkttype);
}

module_init(ebt_pkttype_init);
module_exit(ebt_pkttype_fini);
MODULE_DESCRIPTION("Ebtables: Link layer packet type match");
MODULE_LICENSE("GPL");

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

static bool
ebt_pkttype_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ebt_pkttype_info *info = par->matchinfo;

	return (skb->pkt_type == info->pkt_type) ^ info->invert;
}

static bool ebt_pkttype_mt_check(const struct xt_mtchk_param *par)
{
	const struct ebt_pkttype_info *info = par->matchinfo;

	if (info->invert != 0 && info->invert != 1)
		return false;
	/* Allow any pkt_type value */
	return true;
}

static struct xt_match ebt_pkttype_mt_reg __read_mostly = {
	.name		= "pkttype",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_pkttype_mt,
	.checkentry	= ebt_pkttype_mt_check,
	.matchsize	= sizeof(struct ebt_pkttype_info),
	.me		= THIS_MODULE,
};

static int __init ebt_pkttype_init(void)
{
	return xt_register_match(&ebt_pkttype_mt_reg);
}

static void __exit ebt_pkttype_fini(void)
{
	xt_unregister_match(&ebt_pkttype_mt_reg);
}

module_init(ebt_pkttype_init);
module_exit(ebt_pkttype_fini);
MODULE_DESCRIPTION("Ebtables: Link layer packet type match");
MODULE_LICENSE("GPL");

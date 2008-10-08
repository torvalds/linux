/*
 *  ebt_mark_m
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  July, 2002
 *
 */
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_mark_m.h>

static bool
ebt_mark_mt(const struct sk_buff *skb, const struct net_device *in,
	    const struct net_device *out, const struct xt_match *match,
	    const void *data, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct ebt_mark_m_info *info = data;

	if (info->bitmask & EBT_MARK_OR)
		return !!(skb->mark & info->mask) ^ info->invert;
	return ((skb->mark & info->mask) == info->mark) ^ info->invert;
}

static bool
ebt_mark_mt_check(const char *table, const void *e,
		  const struct xt_match *match, void *data,
		  unsigned int hook_mask)
{
	const struct ebt_mark_m_info *info = data;

	if (info->bitmask & ~EBT_MARK_MASK)
		return false;
	if ((info->bitmask & EBT_MARK_OR) && (info->bitmask & EBT_MARK_AND))
		return false;
	if (!info->bitmask)
		return false;
	return true;
}

static struct xt_match ebt_mark_mt_reg __read_mostly = {
	.name		= "mark_m",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_mark_mt,
	.checkentry	= ebt_mark_mt_check,
	.matchsize	= XT_ALIGN(sizeof(struct ebt_mark_m_info)),
	.me		= THIS_MODULE,
};

static int __init ebt_mark_m_init(void)
{
	return xt_register_match(&ebt_mark_mt_reg);
}

static void __exit ebt_mark_m_fini(void)
{
	xt_unregister_match(&ebt_mark_mt_reg);
}

module_init(ebt_mark_m_init);
module_exit(ebt_mark_m_fini);
MODULE_DESCRIPTION("Ebtables: Packet mark match");
MODULE_LICENSE("GPL");

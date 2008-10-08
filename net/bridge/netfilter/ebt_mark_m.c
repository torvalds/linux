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

static int ebt_filter_mark(const struct sk_buff *skb,
   const struct net_device *in, const struct net_device *out, const void *data,
   unsigned int datalen)
{
	const struct ebt_mark_m_info *info = data;

	if (info->bitmask & EBT_MARK_OR)
		return !(!!(skb->mark & info->mask) ^ info->invert);
	return !(((skb->mark & info->mask) == info->mark) ^ info->invert);
}

static int ebt_mark_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	const struct ebt_mark_m_info *info = data;

	if (info->bitmask & ~EBT_MARK_MASK)
		return -EINVAL;
	if ((info->bitmask & EBT_MARK_OR) && (info->bitmask & EBT_MARK_AND))
		return -EINVAL;
	if (!info->bitmask)
		return -EINVAL;
	return 0;
}

static struct ebt_match filter_mark __read_mostly = {
	.name		= EBT_MARK_MATCH,
	.match		= ebt_filter_mark,
	.check		= ebt_mark_check,
	.matchsize	= XT_ALIGN(sizeof(struct ebt_mark_m_info)),
	.me		= THIS_MODULE,
};

static int __init ebt_mark_m_init(void)
{
	return ebt_register_match(&filter_mark);
}

static void __exit ebt_mark_m_fini(void)
{
	ebt_unregister_match(&filter_mark);
}

module_init(ebt_mark_m_init);
module_exit(ebt_mark_m_fini);
MODULE_DESCRIPTION("Ebtables: Packet mark match");
MODULE_LICENSE("GPL");

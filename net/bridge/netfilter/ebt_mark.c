/*
 *  ebt_mark
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  July, 2002
 *
 */

/* The mark target can be used in any chain,
 * I believe adding a mangle table just for marking is total overkill.
 * Marking a frame doesn't really change anything in the frame anyway.
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_mark_t.h>
#include <linux/module.h>

static int ebt_target_mark(struct sk_buff **pskb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_mark_t_info *info = (struct ebt_mark_t_info *)data;

	if ((*pskb)->nfmark != info->mark) {
		(*pskb)->nfmark = info->mark;
		(*pskb)->nfcache |= NFC_ALTERED;
	}
	return info->target;
}

static int ebt_target_mark_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_mark_t_info *info = (struct ebt_mark_t_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_mark_t_info)))
		return -EINVAL;
	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if (INVALID_TARGET)
		return -EINVAL;
	return 0;
}

static struct ebt_target mark_target =
{
	.name		= EBT_MARK_TARGET,
	.target		= ebt_target_mark,
	.check		= ebt_target_mark_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_target(&mark_target);
}

static void __exit fini(void)
{
	ebt_unregister_target(&mark_target);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");

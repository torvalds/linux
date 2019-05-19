// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_mark_t.h>

static unsigned int
ebt_mark_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ebt_mark_t_info *info = par->targinfo;
	int action = info->target & -16;

	if (action == MARK_SET_VALUE)
		skb->mark = info->mark;
	else if (action == MARK_OR_VALUE)
		skb->mark |= info->mark;
	else if (action == MARK_AND_VALUE)
		skb->mark &= info->mark;
	else
		skb->mark ^= info->mark;

	return info->target | ~EBT_VERDICT_BITS;
}

static int ebt_mark_tg_check(const struct xt_tgchk_param *par)
{
	const struct ebt_mark_t_info *info = par->targinfo;
	int tmp;

	tmp = info->target | ~EBT_VERDICT_BITS;
	if (BASE_CHAIN && tmp == EBT_RETURN)
		return -EINVAL;
	if (ebt_invalid_target(tmp))
		return -EINVAL;
	tmp = info->target & ~EBT_VERDICT_BITS;
	if (tmp != MARK_SET_VALUE && tmp != MARK_OR_VALUE &&
	    tmp != MARK_AND_VALUE && tmp != MARK_XOR_VALUE)
		return -EINVAL;
	return 0;
}
#ifdef CONFIG_COMPAT
struct compat_ebt_mark_t_info {
	compat_ulong_t mark;
	compat_uint_t target;
};

static void mark_tg_compat_from_user(void *dst, const void *src)
{
	const struct compat_ebt_mark_t_info *user = src;
	struct ebt_mark_t_info *kern = dst;

	kern->mark = user->mark;
	kern->target = user->target;
}

static int mark_tg_compat_to_user(void __user *dst, const void *src)
{
	struct compat_ebt_mark_t_info __user *user = dst;
	const struct ebt_mark_t_info *kern = src;

	if (put_user(kern->mark, &user->mark) ||
	    put_user(kern->target, &user->target))
		return -EFAULT;
	return 0;
}
#endif

static struct xt_target ebt_mark_tg_reg __read_mostly = {
	.name		= "mark",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.target		= ebt_mark_tg,
	.checkentry	= ebt_mark_tg_check,
	.targetsize	= sizeof(struct ebt_mark_t_info),
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(struct compat_ebt_mark_t_info),
	.compat_from_user = mark_tg_compat_from_user,
	.compat_to_user	= mark_tg_compat_to_user,
#endif
	.me		= THIS_MODULE,
};

static int __init ebt_mark_init(void)
{
	return xt_register_target(&ebt_mark_tg_reg);
}

static void __exit ebt_mark_fini(void)
{
	xt_unregister_target(&ebt_mark_tg_reg);
}

module_init(ebt_mark_init);
module_exit(ebt_mark_fini);
MODULE_DESCRIPTION("Ebtables: Packet mark modification");
MODULE_LICENSE("GPL");

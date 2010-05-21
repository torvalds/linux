/*
 *  ebt_limit
 *
 *	Authors:
 *	Tom Marshall <tommy@home.tig-grr.com>
 *
 *	Mostly copied from netfilter's ipt_limit.c, see that file for
 *	more explanation
 *
 *  September, 2003
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_limit.h>

static DEFINE_SPINLOCK(limit_lock);

#define MAX_CPJ (0xFFFFFFFF / (HZ*60*60*24))

#define _POW2_BELOW2(x) ((x)|((x)>>1))
#define _POW2_BELOW4(x) (_POW2_BELOW2(x)|_POW2_BELOW2((x)>>2))
#define _POW2_BELOW8(x) (_POW2_BELOW4(x)|_POW2_BELOW4((x)>>4))
#define _POW2_BELOW16(x) (_POW2_BELOW8(x)|_POW2_BELOW8((x)>>8))
#define _POW2_BELOW32(x) (_POW2_BELOW16(x)|_POW2_BELOW16((x)>>16))
#define POW2_BELOW32(x) ((_POW2_BELOW32(x)>>1) + 1)

#define CREDITS_PER_JIFFY POW2_BELOW32(MAX_CPJ)

static bool
ebt_limit_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct ebt_limit_info *info = (void *)par->matchinfo;
	unsigned long now = jiffies;

	spin_lock_bh(&limit_lock);
	info->credit += (now - xchg(&info->prev, now)) * CREDITS_PER_JIFFY;
	if (info->credit > info->credit_cap)
		info->credit = info->credit_cap;

	if (info->credit >= info->cost) {
		/* We're not limited. */
		info->credit -= info->cost;
		spin_unlock_bh(&limit_lock);
		return true;
	}

	spin_unlock_bh(&limit_lock);
	return false;
}

/* Precision saver. */
static u_int32_t
user2credits(u_int32_t user)
{
	/* If multiplying would overflow... */
	if (user > 0xFFFFFFFF / (HZ*CREDITS_PER_JIFFY))
		/* Divide first. */
		return (user / EBT_LIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / EBT_LIMIT_SCALE;
}

static int ebt_limit_mt_check(const struct xt_mtchk_param *par)
{
	struct ebt_limit_info *info = par->matchinfo;

	/* Check for overflow. */
	if (info->burst == 0 ||
	    user2credits(info->avg * info->burst) < user2credits(info->avg)) {
		pr_info("overflow, try lower: %u/%u\n",
			info->avg, info->burst);
		return -EINVAL;
	}

	/* User avg in seconds * EBT_LIMIT_SCALE: convert to jiffies * 128. */
	info->prev = jiffies;
	info->credit = user2credits(info->avg * info->burst);
	info->credit_cap = user2credits(info->avg * info->burst);
	info->cost = user2credits(info->avg);
	return 0;
}


#ifdef CONFIG_COMPAT
/*
 * no conversion function needed --
 * only avg/burst have meaningful values in userspace.
 */
struct ebt_compat_limit_info {
	compat_uint_t avg, burst;
	compat_ulong_t prev;
	compat_uint_t credit, credit_cap, cost;
};
#endif

static struct xt_match ebt_limit_mt_reg __read_mostly = {
	.name		= "limit",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_limit_mt,
	.checkentry	= ebt_limit_mt_check,
	.matchsize	= sizeof(struct ebt_limit_info),
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(struct ebt_compat_limit_info),
#endif
	.me		= THIS_MODULE,
};

static int __init ebt_limit_init(void)
{
	return xt_register_match(&ebt_limit_mt_reg);
}

static void __exit ebt_limit_fini(void)
{
	xt_unregister_match(&ebt_limit_mt_reg);
}

module_init(ebt_limit_init);
module_exit(ebt_limit_fini);
MODULE_DESCRIPTION("Ebtables: Rate-limit match");
MODULE_LICENSE("GPL");

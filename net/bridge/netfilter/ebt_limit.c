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

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_limit.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(limit_lock);

#define MAX_CPJ (0xFFFFFFFF / (HZ*60*60*24))

#define _POW2_BELOW2(x) ((x)|((x)>>1))
#define _POW2_BELOW4(x) (_POW2_BELOW2(x)|_POW2_BELOW2((x)>>2))
#define _POW2_BELOW8(x) (_POW2_BELOW4(x)|_POW2_BELOW4((x)>>4))
#define _POW2_BELOW16(x) (_POW2_BELOW8(x)|_POW2_BELOW8((x)>>8))
#define _POW2_BELOW32(x) (_POW2_BELOW16(x)|_POW2_BELOW16((x)>>16))
#define POW2_BELOW32(x) ((_POW2_BELOW32(x)>>1) + 1)

#define CREDITS_PER_JIFFY POW2_BELOW32(MAX_CPJ)

static int ebt_limit_match(const struct sk_buff *skb,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_limit_info *info = (struct ebt_limit_info *)data;
	unsigned long now = jiffies;

	spin_lock_bh(&limit_lock);
	info->credit += (now - xchg(&info->prev, now)) * CREDITS_PER_JIFFY;
	if (info->credit > info->credit_cap)
		info->credit = info->credit_cap;

	if (info->credit >= info->cost) {
		/* We're not limited. */
		info->credit -= info->cost;
		spin_unlock_bh(&limit_lock);
		return EBT_MATCH;
	}

	spin_unlock_bh(&limit_lock);
	return EBT_NOMATCH;
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

static int ebt_limit_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_limit_info *info = (struct ebt_limit_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_limit_info)))
		return -EINVAL;

	/* Check for overflow. */
	if (info->burst == 0 ||
	    user2credits(info->avg * info->burst) < user2credits(info->avg)) {
		printk("Overflow in ebt_limit, try lower: %u/%u\n",
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

static struct ebt_match ebt_limit_reg =
{
	.name		= EBT_LIMIT_MATCH,
	.match		= ebt_limit_match,
	.check		= ebt_limit_check,
	.me		= THIS_MODULE,
};

static int __init ebt_limit_init(void)
{
	return ebt_register_match(&ebt_limit_reg);
}

static void __exit ebt_limit_fini(void)
{
	ebt_unregister_match(&ebt_limit_reg);
}

module_init(ebt_limit_init);
module_exit(ebt_limit_fini);
MODULE_LICENSE("GPL");

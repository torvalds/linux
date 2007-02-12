/* Kernel module to control the rate
 *
 * 2 September 1999: Changed from the target RATE to the match
 *                   `limit', removed logging.  Did I mention that
 *                   Alexey is a fucking genius?
 *                   Rusty Russell (rusty@rustcorp.com.au).  */

/* (C) 1999 Jérôme de Vivie <devivie@info.enserb.u-bordeaux.fr>
 * (C) 1999 Hervé Eychenne <eychenne@info.enserb.u-bordeaux.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_limit.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Herve Eychenne <rv@wallfire.org>");
MODULE_DESCRIPTION("iptables rate limit match");
MODULE_ALIAS("ipt_limit");
MODULE_ALIAS("ip6t_limit");

/* The algorithm used is the Simple Token Bucket Filter (TBF)
 * see net/sched/sch_tbf.c in the linux source tree
 */

static DEFINE_SPINLOCK(limit_lock);

/* Rusty: This is my (non-mathematically-inclined) understanding of
   this algorithm.  The `average rate' in jiffies becomes your initial
   amount of credit `credit' and the most credit you can ever have
   `credit_cap'.  The `peak rate' becomes the cost of passing the
   test, `cost'.

   `prev' tracks the last packet hit: you gain one credit per jiffy.
   If you get credit balance more than this, the extra credit is
   discarded.  Every time the match passes, you lose `cost' credits;
   if you don't have that many, the test fails.

   See Alexey's formal explanation in net/sched/sch_tbf.c.

   To get the maxmum range, we multiply by this factor (ie. you get N
   credits per jiffy).  We want to allow a rate as low as 1 per day
   (slowest userspace tool allows), which means
   CREDITS_PER_JIFFY*HZ*60*60*24 < 2^32. ie. */
#define MAX_CPJ (0xFFFFFFFF / (HZ*60*60*24))

/* Repeated shift and or gives us all 1s, final shift and add 1 gives
 * us the power of 2 below the theoretical max, so GCC simply does a
 * shift. */
#define _POW2_BELOW2(x) ((x)|((x)>>1))
#define _POW2_BELOW4(x) (_POW2_BELOW2(x)|_POW2_BELOW2((x)>>2))
#define _POW2_BELOW8(x) (_POW2_BELOW4(x)|_POW2_BELOW4((x)>>4))
#define _POW2_BELOW16(x) (_POW2_BELOW8(x)|_POW2_BELOW8((x)>>8))
#define _POW2_BELOW32(x) (_POW2_BELOW16(x)|_POW2_BELOW16((x)>>16))
#define POW2_BELOW32(x) ((_POW2_BELOW32(x)>>1) + 1)

#define CREDITS_PER_JIFFY POW2_BELOW32(MAX_CPJ)

static int
ipt_limit_match(const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const struct xt_match *match,
		const void *matchinfo,
		int offset,
		unsigned int protoff,
		int *hotdrop)
{
	struct xt_rateinfo *r = ((struct xt_rateinfo *)matchinfo)->master;
	unsigned long now = jiffies;

	spin_lock_bh(&limit_lock);
	r->credit += (now - xchg(&r->prev, now)) * CREDITS_PER_JIFFY;
	if (r->credit > r->credit_cap)
		r->credit = r->credit_cap;

	if (r->credit >= r->cost) {
		/* We're not limited. */
		r->credit -= r->cost;
		spin_unlock_bh(&limit_lock);
		return 1;
	}

	spin_unlock_bh(&limit_lock);
	return 0;
}

/* Precision saver. */
static u_int32_t
user2credits(u_int32_t user)
{
	/* If multiplying would overflow... */
	if (user > 0xFFFFFFFF / (HZ*CREDITS_PER_JIFFY))
		/* Divide first. */
		return (user / XT_LIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / XT_LIMIT_SCALE;
}

static int
ipt_limit_checkentry(const char *tablename,
		     const void *inf,
		     const struct xt_match *match,
		     void *matchinfo,
		     unsigned int hook_mask)
{
	struct xt_rateinfo *r = matchinfo;

	/* Check for overflow. */
	if (r->burst == 0
	    || user2credits(r->avg * r->burst) < user2credits(r->avg)) {
		printk("Overflow in xt_limit, try lower: %u/%u\n",
		       r->avg, r->burst);
		return 0;
	}

	/* For SMP, we only want to use one set of counters. */
	r->master = r;
	if (r->cost == 0) {
		/* User avg in seconds * XT_LIMIT_SCALE: convert to jiffies *
		   128. */
		r->prev = jiffies;
		r->credit = user2credits(r->avg * r->burst);	 /* Credits full. */
		r->credit_cap = user2credits(r->avg * r->burst); /* Credits full. */
		r->cost = user2credits(r->avg);
	}
	return 1;
}

#ifdef CONFIG_COMPAT
struct compat_xt_rateinfo {
	u_int32_t avg;
	u_int32_t burst;

	compat_ulong_t prev;
	u_int32_t credit;
	u_int32_t credit_cap, cost;

	u_int32_t master;
};

/* To keep the full "prev" timestamp, the upper 32 bits are stored in the
 * master pointer, which does not need to be preserved. */
static void compat_from_user(void *dst, void *src)
{
	struct compat_xt_rateinfo *cm = src;
	struct xt_rateinfo m = {
		.avg		= cm->avg,
		.burst		= cm->burst,
		.prev		= cm->prev | (unsigned long)cm->master << 32,
		.credit		= cm->credit,
		.credit_cap	= cm->credit_cap,
		.cost		= cm->cost,
	};
	memcpy(dst, &m, sizeof(m));
}

static int compat_to_user(void __user *dst, void *src)
{
	struct xt_rateinfo *m = src;
	struct compat_xt_rateinfo cm = {
		.avg		= m->avg,
		.burst		= m->burst,
		.prev		= m->prev,
		.credit		= m->credit,
		.credit_cap	= m->credit_cap,
		.cost		= m->cost,
		.master		= m->prev >> 32,
	};
	return copy_to_user(dst, &cm, sizeof(cm)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_match xt_limit_match[] = {
	{
		.name		= "limit",
		.family		= AF_INET,
		.checkentry	= ipt_limit_checkentry,
		.match		= ipt_limit_match,
		.matchsize	= sizeof(struct xt_rateinfo),
#ifdef CONFIG_COMPAT
		.compatsize	= sizeof(struct compat_xt_rateinfo),
		.compat_from_user = compat_from_user,
		.compat_to_user	= compat_to_user,
#endif
		.me		= THIS_MODULE,
	},
	{
		.name		= "limit",
		.family		= AF_INET6,
		.checkentry	= ipt_limit_checkentry,
		.match		= ipt_limit_match,
		.matchsize	= sizeof(struct xt_rateinfo),
		.me		= THIS_MODULE,
	},
};

static int __init xt_limit_init(void)
{
	return xt_register_matches(xt_limit_match, ARRAY_SIZE(xt_limit_match));
}

static void __exit xt_limit_fini(void)
{
	xt_unregister_matches(xt_limit_match, ARRAY_SIZE(xt_limit_match));
}

module_init(xt_limit_init);
module_exit(xt_limit_fini);

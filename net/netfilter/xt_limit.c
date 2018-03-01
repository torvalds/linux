/* (C) 1999 Jérôme de Vivie <devivie@info.enserb.u-bordeaux.fr>
 * (C) 1999 Hervé Eychenne <eychenne@info.enserb.u-bordeaux.fr>
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_limit.h>

struct xt_limit_priv {
	spinlock_t lock;
	unsigned long prev;
	uint32_t credit;
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Herve Eychenne <rv@wallfire.org>");
MODULE_DESCRIPTION("Xtables: rate-limit match");
MODULE_ALIAS("ipt_limit");
MODULE_ALIAS("ip6t_limit");

/* The algorithm used is the Simple Token Bucket Filter (TBF)
 * see net/sched/sch_tbf.c in the linux source tree
 */

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

static bool
limit_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_rateinfo *r = par->matchinfo;
	struct xt_limit_priv *priv = r->master;
	unsigned long now = jiffies;

	spin_lock_bh(&priv->lock);
	priv->credit += (now - xchg(&priv->prev, now)) * CREDITS_PER_JIFFY;
	if (priv->credit > r->credit_cap)
		priv->credit = r->credit_cap;

	if (priv->credit >= r->cost) {
		/* We're not limited. */
		priv->credit -= r->cost;
		spin_unlock_bh(&priv->lock);
		return true;
	}

	spin_unlock_bh(&priv->lock);
	return false;
}

/* Precision saver. */
static u32 user2credits(u32 user)
{
	/* If multiplying would overflow... */
	if (user > 0xFFFFFFFF / (HZ*CREDITS_PER_JIFFY))
		/* Divide first. */
		return (user / XT_LIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / XT_LIMIT_SCALE;
}

static int limit_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_rateinfo *r = par->matchinfo;
	struct xt_limit_priv *priv;

	/* Check for overflow. */
	if (r->burst == 0
	    || user2credits(r->avg * r->burst) < user2credits(r->avg)) {
		pr_info("Overflow, try lower: %u/%u\n",
			r->avg, r->burst);
		return -ERANGE;
	}

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	/* For SMP, we only want to use one set of state. */
	r->master = priv;
	/* User avg in seconds * XT_LIMIT_SCALE: convert to jiffies *
	   128. */
	priv->prev = jiffies;
	priv->credit = user2credits(r->avg * r->burst); /* Credits full. */
	if (r->cost == 0) {
		r->credit_cap = priv->credit; /* Credits full. */
		r->cost = user2credits(r->avg);
	}
	spin_lock_init(&priv->lock);

	return 0;
}

static void limit_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_rateinfo *info = par->matchinfo;

	kfree(info->master);
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
static void limit_mt_compat_from_user(void *dst, const void *src)
{
	const struct compat_xt_rateinfo *cm = src;
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

static int limit_mt_compat_to_user(void __user *dst, const void *src)
{
	const struct xt_rateinfo *m = src;
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

static struct xt_match limit_mt_reg __read_mostly = {
	.name             = "limit",
	.revision         = 0,
	.family           = NFPROTO_UNSPEC,
	.match            = limit_mt,
	.checkentry       = limit_mt_check,
	.destroy          = limit_mt_destroy,
	.matchsize        = sizeof(struct xt_rateinfo),
#ifdef CONFIG_COMPAT
	.compatsize       = sizeof(struct compat_xt_rateinfo),
	.compat_from_user = limit_mt_compat_from_user,
	.compat_to_user   = limit_mt_compat_to_user,
#endif
	.usersize         = offsetof(struct xt_rateinfo, prev),
	.me               = THIS_MODULE,
};

static int __init limit_mt_init(void)
{
	return xt_register_match(&limit_mt_reg);
}

static void __exit limit_mt_exit(void)
{
	xt_unregister_match(&limit_mt_reg);
}

module_init(limit_mt_init);
module_exit(limit_mt_exit);

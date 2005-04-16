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

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_limit.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Herve Eychenne <rv@wallfire.org>");
MODULE_DESCRIPTION("rate limiting within ip6tables");

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

   To avoid underflow, we multiply by 128 (ie. you get 128 credits per
   jiffy).  Hence a cost of 2^32-1, means one pass per 32768 seconds
   at 1024HZ (or one every 9 hours).  A cost of 1 means 12800 passes
   per second at 100HZ.  */

#define CREDITS_PER_JIFFY 128

static int
ip6t_limit_match(const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const void *matchinfo,
		int offset,
		unsigned int protoff,
		int *hotdrop)
{
	struct ip6t_rateinfo *r = ((struct ip6t_rateinfo *)matchinfo)->master;
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
		return (user / IP6T_LIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / IP6T_LIMIT_SCALE;
}

static int
ip6t_limit_checkentry(const char *tablename,
		     const struct ip6t_ip6 *ip,
		     void *matchinfo,
		     unsigned int matchsize,
		     unsigned int hook_mask)
{
	struct ip6t_rateinfo *r = matchinfo;

	if (matchsize != IP6T_ALIGN(sizeof(struct ip6t_rateinfo)))
		return 0;

	/* Check for overflow. */
	if (r->burst == 0
	    || user2credits(r->avg * r->burst) < user2credits(r->avg)) {
		printk("Call rusty: overflow in ip6t_limit: %u/%u\n",
		       r->avg, r->burst);
		return 0;
	}

	/* User avg in seconds * IP6T_LIMIT_SCALE: convert to jiffies *
	   128. */
	r->prev = jiffies;
	r->credit = user2credits(r->avg * r->burst);	 /* Credits full. */
	r->credit_cap = user2credits(r->avg * r->burst); /* Credits full. */
	r->cost = user2credits(r->avg);

	/* For SMP, we only want to use one set of counters. */
	r->master = r;

	return 1;
}

static struct ip6t_match ip6t_limit_reg = {
	.name		= "limit",
	.match		= ip6t_limit_match,
	.checkentry	= ip6t_limit_checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	if (ip6t_register_match(&ip6t_limit_reg))
		return -EINVAL;
	return 0;
}

static void __exit fini(void)
{
	ip6t_unregister_match(&ip6t_limit_reg);
}

module_init(init);
module_exit(fini);

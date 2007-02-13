/*
 * netfilter module to enforce network quotas
 *
 * Sam Johnston <samj@samj.net>
 */
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_quota.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sam Johnston <samj@samj.net>");
MODULE_ALIAS("ipt_quota");
MODULE_ALIAS("ip6t_quota");

static DEFINE_SPINLOCK(quota_lock);

static int
match(const struct sk_buff *skb,
      const struct net_device *in, const struct net_device *out,
      const struct xt_match *match, const void *matchinfo,
      int offset, unsigned int protoff, int *hotdrop)
{
	struct xt_quota_info *q = ((struct xt_quota_info *)matchinfo)->master;
	int ret = q->flags & XT_QUOTA_INVERT ? 1 : 0;

	spin_lock_bh(&quota_lock);
	if (q->quota >= skb->len) {
		q->quota -= skb->len;
		ret ^= 1;
	} else {
		/* we do not allow even small packets from now on */
		q->quota = 0;
	}
	spin_unlock_bh(&quota_lock);

	return ret;
}

static int
checkentry(const char *tablename, const void *entry,
	   const struct xt_match *match, void *matchinfo,
	   unsigned int hook_mask)
{
	struct xt_quota_info *q = (struct xt_quota_info *)matchinfo;

	if (q->flags & ~XT_QUOTA_MASK)
		return 0;
	/* For SMP, we only want to use one set of counters. */
	q->master = q;
	return 1;
}

static struct xt_match xt_quota_match[] = {
	{
		.name		= "quota",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_quota_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "quota",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_quota_info),
		.me		= THIS_MODULE
	},
};

static int __init xt_quota_init(void)
{
	return xt_register_matches(xt_quota_match, ARRAY_SIZE(xt_quota_match));
}

static void __exit xt_quota_fini(void)
{
	xt_unregister_matches(xt_quota_match, ARRAY_SIZE(xt_quota_match));
}

module_init(xt_quota_init);
module_exit(xt_quota_fini);

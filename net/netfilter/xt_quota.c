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
	   unsigned int matchsize, unsigned int hook_mask)
{
	struct xt_quota_info *q = (struct xt_quota_info *)matchinfo;

	if (q->flags & ~XT_QUOTA_MASK)
		return 0;
	/* For SMP, we only want to use one set of counters. */
	q->master = q;
	return 1;
}

static struct xt_match quota_match = {
	.name		= "quota",
	.family		= AF_INET,
	.match		= match,
	.matchsize	= sizeof(struct xt_quota_info),
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static struct xt_match quota_match6 = {
	.name		= "quota",
	.family		= AF_INET6,
	.match		= match,
	.matchsize	= sizeof(struct xt_quota_info),
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static int __init xt_quota_init(void)
{
	int ret;

	ret = xt_register_match(&quota_match);
	if (ret)
		goto err1;
	ret = xt_register_match(&quota_match6);
	if (ret)
		goto err2;
	return ret;

err2:
	xt_unregister_match(&quota_match);
err1:
	return ret;
}

static void __exit xt_quota_fini(void)
{
	xt_unregister_match(&quota_match6);
	xt_unregister_match(&quota_match);
}

module_init(xt_quota_init);
module_exit(xt_quota_fini);

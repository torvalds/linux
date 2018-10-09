/*
 * netfilter module to enforce network quotas
 *
 * Sam Johnston <samj@samj.net>
 */
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_quota.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sam Johnston <samj@samj.net>");
MODULE_DESCRIPTION("Xtables: countdown quota match");
MODULE_ALIAS("ipt_quota");
MODULE_ALIAS("ip6t_quota");

static bool
quota_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct xt_quota_info *q = (void *)par->matchinfo;
	u64 current_count = atomic64_read(&q->counter);
	bool ret = q->flags & XT_QUOTA_INVERT;
	u64 old_count, new_count;

	do {
		if (current_count == 1)
			return ret;
		if (current_count <= skb->len) {
			atomic64_set(&q->counter, 1);
			return ret;
		}
		old_count = current_count;
		new_count = current_count - skb->len;
		current_count = atomic64_cmpxchg(&q->counter, old_count,
						 new_count);
	} while (current_count != old_count);
	return !ret;
}

static int quota_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_quota_info *q = par->matchinfo;

	BUILD_BUG_ON(sizeof(atomic64_t) != sizeof(__u64));

	if (q->flags & ~XT_QUOTA_MASK)
		return -EINVAL;
	if (atomic64_read(&q->counter) > q->quota + 1)
		return -ERANGE;

	if (atomic64_read(&q->counter) == 0)
		atomic64_set(&q->counter, q->quota + 1);
	return 0;
}

static struct xt_match quota_mt_reg __read_mostly = {
	.name       = "quota",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = quota_mt,
	.checkentry = quota_mt_check,
	.matchsize  = sizeof(struct xt_quota_info),
	.me         = THIS_MODULE,
};

static int __init quota_mt_init(void)
{
	return xt_register_match(&quota_mt_reg);
}

static void __exit quota_mt_exit(void)
{
	xt_unregister_match(&quota_mt_reg);
}

module_init(quota_mt_init);
module_exit(quota_mt_exit);

/*
 * netfilter module to enforce network quotas
 *
 * Sam Johnston <samj@samj.net>
 */
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_quota.h>

struct xt_quota_priv {
	uint64_t quota;
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sam Johnston <samj@samj.net>");
MODULE_DESCRIPTION("Xtables: countdown quota match");
MODULE_ALIAS("ipt_quota");
MODULE_ALIAS("ip6t_quota");

static DEFINE_SPINLOCK(quota_lock);

static bool
quota_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	struct xt_quota_info *q = (void *)par->matchinfo;
	struct xt_quota_priv *priv = q->master;
	bool ret = q->flags & XT_QUOTA_INVERT;

	spin_lock_bh(&quota_lock);
	if (priv->quota >= skb->len) {
		priv->quota -= skb->len;
		ret = !ret;
	} else {
		/* we do not allow even small packets from now on */
		priv->quota = 0;
	}
	/* Copy quota back to matchinfo so that iptables can display it */
	q->quota = priv->quota;
	spin_unlock_bh(&quota_lock);

	return ret;
}

static bool quota_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_quota_info *q = par->matchinfo;

	if (q->flags & ~XT_QUOTA_MASK)
		return false;

	q->master = kmalloc(sizeof(*q->master), GFP_KERNEL);
	if (q->master == NULL)
		return false;

	q->master->quota = q->quota;
	return true;
}

static void quota_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_quota_info *q = par->matchinfo;

	kfree(q->master);
}

static struct xt_match quota_mt_reg __read_mostly = {
	.name       = "quota",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = quota_mt,
	.checkentry = quota_mt_check,
	.destroy    = quota_mt_destroy,
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

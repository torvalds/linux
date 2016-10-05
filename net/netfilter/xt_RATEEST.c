/*
 * (C) 2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/gen_stats.h>
#include <linux/jhash.h>
#include <linux/rtnetlink.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/gen_stats.h>
#include <net/netlink.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_RATEEST.h>
#include <net/netfilter/xt_rateest.h>

static DEFINE_MUTEX(xt_rateest_mutex);

#define RATEEST_HSIZE	16
static struct hlist_head rateest_hash[RATEEST_HSIZE] __read_mostly;
static unsigned int jhash_rnd __read_mostly;

static unsigned int xt_rateest_hash(const char *name)
{
	return jhash(name, FIELD_SIZEOF(struct xt_rateest, name), jhash_rnd) &
	       (RATEEST_HSIZE - 1);
}

static void xt_rateest_hash_insert(struct xt_rateest *est)
{
	unsigned int h;

	h = xt_rateest_hash(est->name);
	hlist_add_head(&est->list, &rateest_hash[h]);
}

struct xt_rateest *xt_rateest_lookup(const char *name)
{
	struct xt_rateest *est;
	unsigned int h;

	h = xt_rateest_hash(name);
	mutex_lock(&xt_rateest_mutex);
	hlist_for_each_entry(est, &rateest_hash[h], list) {
		if (strcmp(est->name, name) == 0) {
			est->refcnt++;
			mutex_unlock(&xt_rateest_mutex);
			return est;
		}
	}
	mutex_unlock(&xt_rateest_mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(xt_rateest_lookup);

void xt_rateest_put(struct xt_rateest *est)
{
	mutex_lock(&xt_rateest_mutex);
	if (--est->refcnt == 0) {
		hlist_del(&est->list);
		gen_kill_estimator(&est->bstats, &est->rstats);
		/*
		 * gen_estimator est_timer() might access est->lock or bstats,
		 * wait a RCU grace period before freeing 'est'
		 */
		kfree_rcu(est, rcu);
	}
	mutex_unlock(&xt_rateest_mutex);
}
EXPORT_SYMBOL_GPL(xt_rateest_put);

static unsigned int
xt_rateest_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_rateest_target_info *info = par->targinfo;
	struct gnet_stats_basic_packed *stats = &info->est->bstats;

	spin_lock_bh(&info->est->lock);
	stats->bytes += skb->len;
	stats->packets++;
	spin_unlock_bh(&info->est->lock);

	return XT_CONTINUE;
}

static int xt_rateest_tg_checkentry(const struct xt_tgchk_param *par)
{
	struct xt_rateest_target_info *info = par->targinfo;
	struct xt_rateest *est;
	struct {
		struct nlattr		opt;
		struct gnet_estimator	est;
	} cfg;
	int ret;

	net_get_random_once(&jhash_rnd, sizeof(jhash_rnd));

	est = xt_rateest_lookup(info->name);
	if (est) {
		/*
		 * If estimator parameters are specified, they must match the
		 * existing estimator.
		 */
		if ((!info->interval && !info->ewma_log) ||
		    (info->interval != est->params.interval ||
		     info->ewma_log != est->params.ewma_log)) {
			xt_rateest_put(est);
			return -EINVAL;
		}
		info->est = est;
		return 0;
	}

	ret = -ENOMEM;
	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		goto err1;

	strlcpy(est->name, info->name, sizeof(est->name));
	spin_lock_init(&est->lock);
	est->refcnt		= 1;
	est->params.interval	= info->interval;
	est->params.ewma_log	= info->ewma_log;

	cfg.opt.nla_len		= nla_attr_size(sizeof(cfg.est));
	cfg.opt.nla_type	= TCA_STATS_RATE_EST;
	cfg.est.interval	= info->interval;
	cfg.est.ewma_log	= info->ewma_log;

	ret = gen_new_estimator(&est->bstats, NULL, &est->rstats,
				&est->lock, NULL, &cfg.opt);
	if (ret < 0)
		goto err2;

	info->est = est;
	xt_rateest_hash_insert(est);
	return 0;

err2:
	kfree(est);
err1:
	return ret;
}

static void xt_rateest_tg_destroy(const struct xt_tgdtor_param *par)
{
	struct xt_rateest_target_info *info = par->targinfo;

	xt_rateest_put(info->est);
}

static struct xt_target xt_rateest_tg_reg __read_mostly = {
	.name       = "RATEEST",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.target     = xt_rateest_tg,
	.checkentry = xt_rateest_tg_checkentry,
	.destroy    = xt_rateest_tg_destroy,
	.targetsize = sizeof(struct xt_rateest_target_info),
	.me         = THIS_MODULE,
};

static int __init xt_rateest_tg_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rateest_hash); i++)
		INIT_HLIST_HEAD(&rateest_hash[i]);

	return xt_register_target(&xt_rateest_tg_reg);
}

static void __exit xt_rateest_tg_fini(void)
{
	xt_unregister_target(&xt_rateest_tg_reg);
}


MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: packet rate estimator");
MODULE_ALIAS("ipt_RATEEST");
MODULE_ALIAS("ip6t_RATEEST");
module_init(xt_rateest_tg_init);
module_exit(xt_rateest_tg_fini);

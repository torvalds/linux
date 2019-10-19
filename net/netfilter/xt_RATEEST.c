// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 2007 Patrick McHardy <kaber@trash.net>
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
#include <net/netns/generic.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_RATEEST.h>
#include <net/netfilter/xt_rateest.h>

#define RATEEST_HSIZE	16

struct xt_rateest_net {
	struct mutex hash_lock;
	struct hlist_head hash[RATEEST_HSIZE];
};

static unsigned int xt_rateest_id;

static unsigned int jhash_rnd __read_mostly;

static unsigned int xt_rateest_hash(const char *name)
{
	return jhash(name, FIELD_SIZEOF(struct xt_rateest, name), jhash_rnd) &
	       (RATEEST_HSIZE - 1);
}

static void xt_rateest_hash_insert(struct xt_rateest_net *xn,
				   struct xt_rateest *est)
{
	unsigned int h;

	h = xt_rateest_hash(est->name);
	hlist_add_head(&est->list, &xn->hash[h]);
}

static struct xt_rateest *__xt_rateest_lookup(struct xt_rateest_net *xn,
					      const char *name)
{
	struct xt_rateest *est;
	unsigned int h;

	h = xt_rateest_hash(name);
	hlist_for_each_entry(est, &xn->hash[h], list) {
		if (strcmp(est->name, name) == 0) {
			est->refcnt++;
			return est;
		}
	}

	return NULL;
}

struct xt_rateest *xt_rateest_lookup(struct net *net, const char *name)
{
	struct xt_rateest_net *xn = net_generic(net, xt_rateest_id);
	struct xt_rateest *est;

	mutex_lock(&xn->hash_lock);
	est = __xt_rateest_lookup(xn, name);
	mutex_unlock(&xn->hash_lock);
	return est;
}
EXPORT_SYMBOL_GPL(xt_rateest_lookup);

void xt_rateest_put(struct net *net, struct xt_rateest *est)
{
	struct xt_rateest_net *xn = net_generic(net, xt_rateest_id);

	mutex_lock(&xn->hash_lock);
	if (--est->refcnt == 0) {
		hlist_del(&est->list);
		gen_kill_estimator(&est->rate_est);
		/*
		 * gen_estimator est_timer() might access est->lock or bstats,
		 * wait a RCU grace period before freeing 'est'
		 */
		kfree_rcu(est, rcu);
	}
	mutex_unlock(&xn->hash_lock);
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
	struct xt_rateest_net *xn = net_generic(par->net, xt_rateest_id);
	struct xt_rateest_target_info *info = par->targinfo;
	struct xt_rateest *est;
	struct {
		struct nlattr		opt;
		struct gnet_estimator	est;
	} cfg;
	int ret;

	net_get_random_once(&jhash_rnd, sizeof(jhash_rnd));

	mutex_lock(&xn->hash_lock);
	est = __xt_rateest_lookup(xn, info->name);
	if (est) {
		mutex_unlock(&xn->hash_lock);
		/*
		 * If estimator parameters are specified, they must match the
		 * existing estimator.
		 */
		if ((!info->interval && !info->ewma_log) ||
		    (info->interval != est->params.interval ||
		     info->ewma_log != est->params.ewma_log)) {
			xt_rateest_put(par->net, est);
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

	ret = gen_new_estimator(&est->bstats, NULL, &est->rate_est,
				&est->lock, NULL, &cfg.opt);
	if (ret < 0)
		goto err2;

	info->est = est;
	xt_rateest_hash_insert(xn, est);
	mutex_unlock(&xn->hash_lock);
	return 0;

err2:
	kfree(est);
err1:
	mutex_unlock(&xn->hash_lock);
	return ret;
}

static void xt_rateest_tg_destroy(const struct xt_tgdtor_param *par)
{
	struct xt_rateest_target_info *info = par->targinfo;

	xt_rateest_put(par->net, info->est);
}

static struct xt_target xt_rateest_tg_reg __read_mostly = {
	.name       = "RATEEST",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.target     = xt_rateest_tg,
	.checkentry = xt_rateest_tg_checkentry,
	.destroy    = xt_rateest_tg_destroy,
	.targetsize = sizeof(struct xt_rateest_target_info),
	.usersize   = offsetof(struct xt_rateest_target_info, est),
	.me         = THIS_MODULE,
};

static __net_init int xt_rateest_net_init(struct net *net)
{
	struct xt_rateest_net *xn = net_generic(net, xt_rateest_id);
	int i;

	mutex_init(&xn->hash_lock);
	for (i = 0; i < ARRAY_SIZE(xn->hash); i++)
		INIT_HLIST_HEAD(&xn->hash[i]);
	return 0;
}

static struct pernet_operations xt_rateest_net_ops = {
	.init = xt_rateest_net_init,
	.id   = &xt_rateest_id,
	.size = sizeof(struct xt_rateest_net),
};

static int __init xt_rateest_tg_init(void)
{
	int err = register_pernet_subsys(&xt_rateest_net_ops);

	if (err)
		return err;
	return xt_register_target(&xt_rateest_tg_reg);
}

static void __exit xt_rateest_tg_fini(void)
{
	xt_unregister_target(&xt_rateest_tg_reg);
	unregister_pernet_subsys(&xt_rateest_net_ops);
}


MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: packet rate estimator");
MODULE_ALIAS("ipt_RATEEST");
MODULE_ALIAS("ip6t_RATEEST");
module_init(xt_rateest_tg_init);
module_exit(xt_rateest_tg_fini);

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

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_rateest.h>
#include <net/netfilter/xt_rateest.h>


static bool
xt_rateest_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_rateest_match_info *info = par->matchinfo;
	struct gnet_stats_rate_est64 sample = {0};
	u_int32_t bps1, bps2, pps1, pps2;
	bool ret = true;

	gen_estimator_read(&info->est1->rate_est, &sample);

	if (info->flags & XT_RATEEST_MATCH_DELTA) {
		bps1 = info->bps1 >= sample.bps ? info->bps1 - sample.bps : 0;
		pps1 = info->pps1 >= sample.pps ? info->pps1 - sample.pps : 0;
	} else {
		bps1 = sample.bps;
		pps1 = sample.pps;
	}

	if (info->flags & XT_RATEEST_MATCH_ABS) {
		bps2 = info->bps2;
		pps2 = info->pps2;
	} else {
		gen_estimator_read(&info->est2->rate_est, &sample);

		if (info->flags & XT_RATEEST_MATCH_DELTA) {
			bps2 = info->bps2 >= sample.bps ? info->bps2 - sample.bps : 0;
			pps2 = info->pps2 >= sample.pps ? info->pps2 - sample.pps : 0;
		} else {
			bps2 = sample.bps;
			pps2 = sample.pps;
		}
	}

	switch (info->mode) {
	case XT_RATEEST_MATCH_LT:
		if (info->flags & XT_RATEEST_MATCH_BPS)
			ret &= bps1 < bps2;
		if (info->flags & XT_RATEEST_MATCH_PPS)
			ret &= pps1 < pps2;
		break;
	case XT_RATEEST_MATCH_GT:
		if (info->flags & XT_RATEEST_MATCH_BPS)
			ret &= bps1 > bps2;
		if (info->flags & XT_RATEEST_MATCH_PPS)
			ret &= pps1 > pps2;
		break;
	case XT_RATEEST_MATCH_EQ:
		if (info->flags & XT_RATEEST_MATCH_BPS)
			ret &= bps1 == bps2;
		if (info->flags & XT_RATEEST_MATCH_PPS)
			ret &= pps1 == pps2;
		break;
	}

	ret ^= info->flags & XT_RATEEST_MATCH_INVERT ? true : false;
	return ret;
}

static int xt_rateest_mt_checkentry(const struct xt_mtchk_param *par)
{
	struct xt_rateest_match_info *info = par->matchinfo;
	struct xt_rateest *est1, *est2;
	int ret = -EINVAL;

	if (hweight32(info->flags & (XT_RATEEST_MATCH_ABS |
				     XT_RATEEST_MATCH_REL)) != 1)
		goto err1;

	if (!(info->flags & (XT_RATEEST_MATCH_BPS | XT_RATEEST_MATCH_PPS)))
		goto err1;

	switch (info->mode) {
	case XT_RATEEST_MATCH_EQ:
	case XT_RATEEST_MATCH_LT:
	case XT_RATEEST_MATCH_GT:
		break;
	default:
		goto err1;
	}

	ret  = -ENOENT;
	est1 = xt_rateest_lookup(par->net, info->name1);
	if (!est1)
		goto err1;

	est2 = NULL;
	if (info->flags & XT_RATEEST_MATCH_REL) {
		est2 = xt_rateest_lookup(par->net, info->name2);
		if (!est2)
			goto err2;
	}

	info->est1 = est1;
	info->est2 = est2;
	return 0;

err2:
	xt_rateest_put(par->net, est1);
err1:
	return ret;
}

static void xt_rateest_mt_destroy(const struct xt_mtdtor_param *par)
{
	struct xt_rateest_match_info *info = par->matchinfo;

	xt_rateest_put(par->net, info->est1);
	if (info->est2)
		xt_rateest_put(par->net, info->est2);
}

static struct xt_match xt_rateest_mt_reg __read_mostly = {
	.name       = "rateest",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = xt_rateest_mt,
	.checkentry = xt_rateest_mt_checkentry,
	.destroy    = xt_rateest_mt_destroy,
	.matchsize  = sizeof(struct xt_rateest_match_info),
	.usersize   = offsetof(struct xt_rateest_match_info, est1),
	.me         = THIS_MODULE,
};

static int __init xt_rateest_mt_init(void)
{
	return xt_register_match(&xt_rateest_mt_reg);
}

static void __exit xt_rateest_mt_fini(void)
{
	xt_unregister_match(&xt_rateest_mt_reg);
}

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("xtables rate estimator match");
MODULE_ALIAS("ipt_rateest");
MODULE_ALIAS("ip6t_rateest");
module_init(xt_rateest_mt_init);
module_exit(xt_rateest_mt_fini);

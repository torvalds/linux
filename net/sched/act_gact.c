/*
 * net/sched/gact.c	Generic actions
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright 	Jamal Hadi Salim (2002-4)
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_gact.h>
#include <net/tc_act/tc_gact.h>

#define GACT_TAB_MASK	15

#ifdef CONFIG_GACT_PROB
static int gact_net_rand(struct tcf_gact *gact)
{
	if (!gact->tcfg_pval || prandom_u32() % gact->tcfg_pval)
		return gact->tcf_action;
	return gact->tcfg_paction;
}

static int gact_determ(struct tcf_gact *gact)
{
	if (!gact->tcfg_pval || gact->tcf_bstats.packets % gact->tcfg_pval)
		return gact->tcf_action;
	return gact->tcfg_paction;
}

typedef int (*g_rand)(struct tcf_gact *gact);
static g_rand gact_rand[MAX_RAND] = { NULL, gact_net_rand, gact_determ };
#endif /* CONFIG_GACT_PROB */

static const struct nla_policy gact_policy[TCA_GACT_MAX + 1] = {
	[TCA_GACT_PARMS]	= { .len = sizeof(struct tc_gact) },
	[TCA_GACT_PROB]		= { .len = sizeof(struct tc_gact_p) },
};

static int tcf_gact_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action *a,
			 int ovr, int bind)
{
	struct nlattr *tb[TCA_GACT_MAX + 1];
	struct tc_gact *parm;
	struct tcf_gact *gact;
	int ret = 0;
	int err;
#ifdef CONFIG_GACT_PROB
	struct tc_gact_p *p_parm = NULL;
#endif

	if (nla == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_GACT_MAX, nla, gact_policy);
	if (err < 0)
		return err;

	if (tb[TCA_GACT_PARMS] == NULL)
		return -EINVAL;
	parm = nla_data(tb[TCA_GACT_PARMS]);

#ifndef CONFIG_GACT_PROB
	if (tb[TCA_GACT_PROB] != NULL)
		return -EOPNOTSUPP;
#else
	if (tb[TCA_GACT_PROB]) {
		p_parm = nla_data(tb[TCA_GACT_PROB]);
		if (p_parm->ptype >= MAX_RAND)
			return -EINVAL;
	}
#endif

	if (!tcf_hash_check(parm->index, a, bind)) {
		ret = tcf_hash_create(parm->index, est, a, sizeof(*gact), bind);
		if (ret)
			return ret;
		ret = ACT_P_CREATED;
	} else {
		if (bind)/* dont override defaults */
			return 0;
		tcf_hash_release(a, bind);
		if (!ovr)
			return -EEXIST;
	}

	gact = to_gact(a);

	spin_lock_bh(&gact->tcf_lock);
	gact->tcf_action = parm->action;
#ifdef CONFIG_GACT_PROB
	if (p_parm) {
		gact->tcfg_paction = p_parm->paction;
		gact->tcfg_pval    = p_parm->pval;
		gact->tcfg_ptype   = p_parm->ptype;
	}
#endif
	spin_unlock_bh(&gact->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(a);
	return ret;
}

static int tcf_gact(struct sk_buff *skb, const struct tc_action *a,
		    struct tcf_result *res)
{
	struct tcf_gact *gact = a->priv;
	int action = TC_ACT_SHOT;

	spin_lock(&gact->tcf_lock);
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype)
		action = gact_rand[gact->tcfg_ptype](gact);
	else
		action = gact->tcf_action;
#else
	action = gact->tcf_action;
#endif
	gact->tcf_bstats.bytes += qdisc_pkt_len(skb);
	gact->tcf_bstats.packets++;
	if (action == TC_ACT_SHOT)
		gact->tcf_qstats.drops++;
	gact->tcf_tm.lastuse = jiffies;
	spin_unlock(&gact->tcf_lock);

	return action;
}

static int tcf_gact_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_gact *gact = a->priv;
	struct tc_gact opt = {
		.index   = gact->tcf_index,
		.refcnt  = gact->tcf_refcnt - ref,
		.bindcnt = gact->tcf_bindcnt - bind,
		.action  = gact->tcf_action,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_GACT_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype) {
		struct tc_gact_p p_opt = {
			.paction = gact->tcfg_paction,
			.pval    = gact->tcfg_pval,
			.ptype   = gact->tcfg_ptype,
		};

		if (nla_put(skb, TCA_GACT_PROB, sizeof(p_opt), &p_opt))
			goto nla_put_failure;
	}
#endif
	t.install = jiffies_to_clock_t(jiffies - gact->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - gact->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(gact->tcf_tm.expires);
	if (nla_put(skb, TCA_GACT_TM, sizeof(t), &t))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_gact_ops = {
	.kind		=	"gact",
	.type		=	TCA_ACT_GACT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_gact,
	.dump		=	tcf_gact_dump,
	.init		=	tcf_gact_init,
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Classifier actions");
MODULE_LICENSE("GPL");

static int __init gact_init_module(void)
{
#ifdef CONFIG_GACT_PROB
	pr_info("GACT probability on\n");
#else
	pr_info("GACT probability NOT on\n");
#endif
	return tcf_register_action(&act_gact_ops, GACT_TAB_MASK);
}

static void __exit gact_cleanup_module(void)
{
	tcf_unregister_action(&act_gact_ops);
}

module_init(gact_init_module);
module_exit(gact_cleanup_module);

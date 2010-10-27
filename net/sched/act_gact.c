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
static struct tcf_common *tcf_gact_ht[GACT_TAB_MASK + 1];
static u32 gact_idx_gen;
static DEFINE_RWLOCK(gact_lock);

static struct tcf_hashinfo gact_hash_info = {
	.htab	=	tcf_gact_ht,
	.hmask	=	GACT_TAB_MASK,
	.lock	=	&gact_lock,
};

#ifdef CONFIG_GACT_PROB
static int gact_net_rand(struct tcf_gact *gact)
{
	if (!gact->tcfg_pval || net_random() % gact->tcfg_pval)
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
static g_rand gact_rand[MAX_RAND]= { NULL, gact_net_rand, gact_determ };
#endif /* CONFIG_GACT_PROB */

static const struct nla_policy gact_policy[TCA_GACT_MAX + 1] = {
	[TCA_GACT_PARMS]	= { .len = sizeof(struct tc_gact) },
	[TCA_GACT_PROB]		= { .len = sizeof(struct tc_gact_p) },
};

static int tcf_gact_init(struct nlattr *nla, struct nlattr *est,
			 struct tc_action *a, int ovr, int bind)
{
	struct nlattr *tb[TCA_GACT_MAX + 1];
	struct tc_gact *parm;
	struct tcf_gact *gact;
	struct tcf_common *pc;
	int ret = 0;
	int err;

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
#endif

	pc = tcf_hash_check(parm->index, a, bind, &gact_hash_info);
	if (!pc) {
		pc = tcf_hash_create(parm->index, est, a, sizeof(*gact),
				     bind, &gact_idx_gen, &gact_hash_info);
		if (IS_ERR(pc))
		    return PTR_ERR(pc);
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_hash_release(pc, bind, &gact_hash_info);
			return -EEXIST;
		}
	}

	gact = to_gact(pc);

	spin_lock_bh(&gact->tcf_lock);
	gact->tcf_action = parm->action;
#ifdef CONFIG_GACT_PROB
	if (tb[TCA_GACT_PROB] != NULL) {
		struct tc_gact_p *p_parm = nla_data(tb[TCA_GACT_PROB]);
		gact->tcfg_paction = p_parm->paction;
		gact->tcfg_pval    = p_parm->pval;
		gact->tcfg_ptype   = p_parm->ptype;
	}
#endif
	spin_unlock_bh(&gact->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &gact_hash_info);
	return ret;
}

static int tcf_gact_cleanup(struct tc_action *a, int bind)
{
	struct tcf_gact *gact = a->priv;

	if (gact)
		return tcf_hash_release(&gact->common, bind, &gact_hash_info);
	return 0;
}

static int tcf_gact(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	struct tcf_gact *gact = a->priv;
	int action = TC_ACT_SHOT;

	spin_lock(&gact->tcf_lock);
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype && gact_rand[gact->tcfg_ptype] != NULL)
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

	NLA_PUT(skb, TCA_GACT_PARMS, sizeof(opt), &opt);
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype) {
		struct tc_gact_p p_opt = {
			.paction = gact->tcfg_paction,
			.pval    = gact->tcfg_pval,
			.ptype   = gact->tcfg_ptype,
		};

		NLA_PUT(skb, TCA_GACT_PROB, sizeof(p_opt), &p_opt);
	}
#endif
	t.install = jiffies_to_clock_t(jiffies - gact->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - gact->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(gact->tcf_tm.expires);
	NLA_PUT(skb, TCA_GACT_TM, sizeof(t), &t);
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_gact_ops = {
	.kind		=	"gact",
	.hinfo		=	&gact_hash_info,
	.type		=	TCA_ACT_GACT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_gact,
	.dump		=	tcf_gact_dump,
	.cleanup	=	tcf_gact_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_gact_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Classifier actions");
MODULE_LICENSE("GPL");

static int __init gact_init_module(void)
{
#ifdef CONFIG_GACT_PROB
	printk(KERN_INFO "GACT probability on\n");
#else
	printk(KERN_INFO "GACT probability NOT on\n");
#endif
	return tcf_register_action(&act_gact_ops);
}

static void __exit gact_cleanup_module(void)
{
	tcf_unregister_action(&act_gact_ops);
}

module_init(gact_init_module);
module_exit(gact_cleanup_module);

/*
 * net/sched/act_police.c	Input police filter
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 * 		J Hadi Salim (action changes)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/act_api.h>
#include <net/netlink.h>

struct tcf_police {
	struct tc_action	common;
	int			tcfp_result;
	u32			tcfp_ewma_rate;
	s64			tcfp_burst;
	u32			tcfp_mtu;
	s64			tcfp_toks;
	s64			tcfp_ptoks;
	s64			tcfp_mtu_ptoks;
	s64			tcfp_t_c;
	struct psched_ratecfg	rate;
	bool			rate_present;
	struct psched_ratecfg	peak;
	bool			peak_present;
};

#define to_police(pc) ((struct tcf_police *)pc)

/* old policer structure from before tc actions */
struct tc_police_compat {
	u32			index;
	int			action;
	u32			limit;
	u32			burst;
	u32			mtu;
	struct tc_ratespec	rate;
	struct tc_ratespec	peakrate;
};

/* Each policer is serialized by its individual spinlock */

static unsigned int police_net_id;
static struct tc_action_ops act_police_ops;

static int tcf_act_police_walker(struct net *net, struct sk_buff *skb,
				 struct netlink_callback *cb, int type,
				 const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, police_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static const struct nla_policy police_policy[TCA_POLICE_MAX + 1] = {
	[TCA_POLICE_RATE]	= { .len = TC_RTAB_SIZE },
	[TCA_POLICE_PEAKRATE]	= { .len = TC_RTAB_SIZE },
	[TCA_POLICE_AVRATE]	= { .type = NLA_U32 },
	[TCA_POLICE_RESULT]	= { .type = NLA_U32 },
};

static int tcf_act_police_init(struct net *net, struct nlattr *nla,
			       struct nlattr *est, struct tc_action **a,
			       int ovr, int bind)
{
	int ret = 0, err;
	struct nlattr *tb[TCA_POLICE_MAX + 1];
	struct tc_police *parm;
	struct tcf_police *police;
	struct qdisc_rate_table *R_tab = NULL, *P_tab = NULL;
	struct tc_action_net *tn = net_generic(net, police_net_id);
	bool exists = false;
	int size;

	if (nla == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_POLICE_MAX, nla, police_policy, NULL);
	if (err < 0)
		return err;

	if (tb[TCA_POLICE_TBF] == NULL)
		return -EINVAL;
	size = nla_len(tb[TCA_POLICE_TBF]);
	if (size != sizeof(*parm) && size != sizeof(struct tc_police_compat))
		return -EINVAL;

	parm = nla_data(tb[TCA_POLICE_TBF]);
	exists = tcf_idr_check(tn, parm->index, a, bind);
	if (exists && bind)
		return 0;

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, NULL, a,
				     &act_police_ops, bind, false);
		if (ret)
			return ret;
		ret = ACT_P_CREATED;
	} else {
		tcf_idr_release(*a, bind);
		if (!ovr)
			return -EEXIST;
	}

	police = to_police(*a);
	if (parm->rate.rate) {
		err = -ENOMEM;
		R_tab = qdisc_get_rtab(&parm->rate, tb[TCA_POLICE_RATE], NULL);
		if (R_tab == NULL)
			goto failure;

		if (parm->peakrate.rate) {
			P_tab = qdisc_get_rtab(&parm->peakrate,
					       tb[TCA_POLICE_PEAKRATE], NULL);
			if (P_tab == NULL)
				goto failure;
		}
	}

	if (est) {
		err = gen_replace_estimator(&police->tcf_bstats, NULL,
					    &police->tcf_rate_est,
					    &police->tcf_lock,
					    NULL, est);
		if (err)
			goto failure;
	} else if (tb[TCA_POLICE_AVRATE] &&
		   (ret == ACT_P_CREATED ||
		    !gen_estimator_active(&police->tcf_rate_est))) {
		err = -EINVAL;
		goto failure;
	}

	spin_lock_bh(&police->tcf_lock);
	/* No failure allowed after this point */
	police->tcfp_mtu = parm->mtu;
	if (police->tcfp_mtu == 0) {
		police->tcfp_mtu = ~0;
		if (R_tab)
			police->tcfp_mtu = 255 << R_tab->rate.cell_log;
	}
	if (R_tab) {
		police->rate_present = true;
		psched_ratecfg_precompute(&police->rate, &R_tab->rate, 0);
		qdisc_put_rtab(R_tab);
	} else {
		police->rate_present = false;
	}
	if (P_tab) {
		police->peak_present = true;
		psched_ratecfg_precompute(&police->peak, &P_tab->rate, 0);
		qdisc_put_rtab(P_tab);
	} else {
		police->peak_present = false;
	}

	if (tb[TCA_POLICE_RESULT])
		police->tcfp_result = nla_get_u32(tb[TCA_POLICE_RESULT]);
	police->tcfp_burst = PSCHED_TICKS2NS(parm->burst);
	police->tcfp_toks = police->tcfp_burst;
	if (police->peak_present) {
		police->tcfp_mtu_ptoks = (s64) psched_l2t_ns(&police->peak,
							     police->tcfp_mtu);
		police->tcfp_ptoks = police->tcfp_mtu_ptoks;
	}
	police->tcf_action = parm->action;

	if (tb[TCA_POLICE_AVRATE])
		police->tcfp_ewma_rate = nla_get_u32(tb[TCA_POLICE_AVRATE]);

	spin_unlock_bh(&police->tcf_lock);
	if (ret != ACT_P_CREATED)
		return ret;

	police->tcfp_t_c = ktime_get_ns();
	tcf_idr_insert(tn, *a);

	return ret;

failure:
	qdisc_put_rtab(P_tab);
	qdisc_put_rtab(R_tab);
	if (ret == ACT_P_CREATED)
		tcf_idr_release(*a, bind);
	return err;
}

static int tcf_act_police(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_police *police = to_police(a);
	s64 now;
	s64 toks;
	s64 ptoks = 0;

	spin_lock(&police->tcf_lock);

	bstats_update(&police->tcf_bstats, skb);
	tcf_lastuse_update(&police->tcf_tm);

	if (police->tcfp_ewma_rate) {
		struct gnet_stats_rate_est64 sample;

		if (!gen_estimator_read(&police->tcf_rate_est, &sample) ||
		    sample.bps >= police->tcfp_ewma_rate) {
			police->tcf_qstats.overlimits++;
			if (police->tcf_action == TC_ACT_SHOT)
				police->tcf_qstats.drops++;
			spin_unlock(&police->tcf_lock);
			return police->tcf_action;
		}
	}

	if (qdisc_pkt_len(skb) <= police->tcfp_mtu) {
		if (!police->rate_present) {
			spin_unlock(&police->tcf_lock);
			return police->tcfp_result;
		}

		now = ktime_get_ns();
		toks = min_t(s64, now - police->tcfp_t_c,
			     police->tcfp_burst);
		if (police->peak_present) {
			ptoks = toks + police->tcfp_ptoks;
			if (ptoks > police->tcfp_mtu_ptoks)
				ptoks = police->tcfp_mtu_ptoks;
			ptoks -= (s64) psched_l2t_ns(&police->peak,
						     qdisc_pkt_len(skb));
		}
		toks += police->tcfp_toks;
		if (toks > police->tcfp_burst)
			toks = police->tcfp_burst;
		toks -= (s64) psched_l2t_ns(&police->rate, qdisc_pkt_len(skb));
		if ((toks|ptoks) >= 0) {
			police->tcfp_t_c = now;
			police->tcfp_toks = toks;
			police->tcfp_ptoks = ptoks;
			if (police->tcfp_result == TC_ACT_SHOT)
				police->tcf_qstats.drops++;
			spin_unlock(&police->tcf_lock);
			return police->tcfp_result;
		}
	}

	police->tcf_qstats.overlimits++;
	if (police->tcf_action == TC_ACT_SHOT)
		police->tcf_qstats.drops++;
	spin_unlock(&police->tcf_lock);
	return police->tcf_action;
}

static int tcf_act_police_dump(struct sk_buff *skb, struct tc_action *a,
			       int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_police *police = to_police(a);
	struct tc_police opt = {
		.index = police->tcf_index,
		.action = police->tcf_action,
		.mtu = police->tcfp_mtu,
		.burst = PSCHED_NS2TICKS(police->tcfp_burst),
		.refcnt = police->tcf_refcnt - ref,
		.bindcnt = police->tcf_bindcnt - bind,
	};
	struct tcf_t t;

	if (police->rate_present)
		psched_ratecfg_getrate(&opt.rate, &police->rate);
	if (police->peak_present)
		psched_ratecfg_getrate(&opt.peakrate, &police->peak);
	if (nla_put(skb, TCA_POLICE_TBF, sizeof(opt), &opt))
		goto nla_put_failure;
	if (police->tcfp_result &&
	    nla_put_u32(skb, TCA_POLICE_RESULT, police->tcfp_result))
		goto nla_put_failure;
	if (police->tcfp_ewma_rate &&
	    nla_put_u32(skb, TCA_POLICE_AVRATE, police->tcfp_ewma_rate))
		goto nla_put_failure;

	t.install = jiffies_to_clock_t(jiffies - police->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - police->tcf_tm.lastuse);
	t.firstuse = jiffies_to_clock_t(jiffies - police->tcf_tm.firstuse);
	t.expires = jiffies_to_clock_t(police->tcf_tm.expires);
	if (nla_put_64bit(skb, TCA_POLICE_TM, sizeof(t), &t, TCA_POLICE_PAD))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_police_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, police_net_id);

	return tcf_idr_search(tn, a, index);
}

MODULE_AUTHOR("Alexey Kuznetsov");
MODULE_DESCRIPTION("Policing actions");
MODULE_LICENSE("GPL");

static struct tc_action_ops act_police_ops = {
	.kind		=	"police",
	.type		=	TCA_ID_POLICE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_act_police,
	.dump		=	tcf_act_police_dump,
	.init		=	tcf_act_police_init,
	.walk		=	tcf_act_police_walker,
	.lookup		=	tcf_police_search,
	.size		=	sizeof(struct tcf_police),
};

static __net_init int police_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, police_net_id);

	return tc_action_net_init(tn, &act_police_ops);
}

static void __net_exit police_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, police_net_id);
}

static struct pernet_operations police_net_ops = {
	.init = police_init_net,
	.exit_batch = police_exit_net,
	.id   = &police_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init police_init_module(void)
{
	return tcf_register_action(&act_police_ops, &police_net_ops);
}

static void __exit police_cleanup_module(void)
{
	tcf_unregister_action(&act_police_ops, &police_net_ops);
}

module_init(police_init_module);
module_exit(police_cleanup_module);

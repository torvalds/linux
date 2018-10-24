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

struct tcf_police_params {
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
	struct rcu_head rcu;
};

struct tcf_police {
	struct tc_action	common;
	struct tcf_police_params __rcu *params;
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

static int tcf_police_walker(struct net *net, struct sk_buff *skb,
				 struct netlink_callback *cb, int type,
				 const struct tc_action_ops *ops,
				 struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, police_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static const struct nla_policy police_policy[TCA_POLICE_MAX + 1] = {
	[TCA_POLICE_RATE]	= { .len = TC_RTAB_SIZE },
	[TCA_POLICE_PEAKRATE]	= { .len = TC_RTAB_SIZE },
	[TCA_POLICE_AVRATE]	= { .type = NLA_U32 },
	[TCA_POLICE_RESULT]	= { .type = NLA_U32 },
};

static int tcf_police_init(struct net *net, struct nlattr *nla,
			       struct nlattr *est, struct tc_action **a,
			       int ovr, int bind, bool rtnl_held,
			       struct netlink_ext_ack *extack)
{
	int ret = 0, err;
	struct nlattr *tb[TCA_POLICE_MAX + 1];
	struct tc_police *parm;
	struct tcf_police *police;
	struct qdisc_rate_table *R_tab = NULL, *P_tab = NULL;
	struct tc_action_net *tn = net_generic(net, police_net_id);
	struct tcf_police_params *new;
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
	err = tcf_idr_check_alloc(tn, &parm->index, a, bind);
	if (err < 0)
		return err;
	exists = err;
	if (exists && bind)
		return 0;

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, NULL, a,
				     &act_police_ops, bind, true);
		if (ret) {
			tcf_idr_cleanup(tn, parm->index);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
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
		err = gen_replace_estimator(&police->tcf_bstats,
					    police->common.cpu_bstats,
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

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (unlikely(!new)) {
		err = -ENOMEM;
		goto failure;
	}

	/* No failure allowed after this point */
	new->tcfp_mtu = parm->mtu;
	if (!new->tcfp_mtu) {
		new->tcfp_mtu = ~0;
		if (R_tab)
			new->tcfp_mtu = 255 << R_tab->rate.cell_log;
	}
	if (R_tab) {
		new->rate_present = true;
		psched_ratecfg_precompute(&new->rate, &R_tab->rate, 0);
		qdisc_put_rtab(R_tab);
	} else {
		new->rate_present = false;
	}
	if (P_tab) {
		new->peak_present = true;
		psched_ratecfg_precompute(&new->peak, &P_tab->rate, 0);
		qdisc_put_rtab(P_tab);
	} else {
		new->peak_present = false;
	}

	new->tcfp_burst = PSCHED_TICKS2NS(parm->burst);
	new->tcfp_toks = new->tcfp_burst;
	if (new->peak_present) {
		new->tcfp_mtu_ptoks = (s64)psched_l2t_ns(&new->peak,
							 new->tcfp_mtu);
		new->tcfp_ptoks = new->tcfp_mtu_ptoks;
	}

	if (tb[TCA_POLICE_AVRATE])
		new->tcfp_ewma_rate = nla_get_u32(tb[TCA_POLICE_AVRATE]);

	if (tb[TCA_POLICE_RESULT]) {
		new->tcfp_result = nla_get_u32(tb[TCA_POLICE_RESULT]);
		if (TC_ACT_EXT_CMP(new->tcfp_result, TC_ACT_GOTO_CHAIN)) {
			NL_SET_ERR_MSG(extack,
				       "goto chain not allowed on fallback");
			err = -EINVAL;
			goto failure;
		}
	}

	spin_lock_bh(&police->tcf_lock);
	new->tcfp_t_c = ktime_get_ns();
	police->tcf_action = parm->action;
	rcu_swap_protected(police->params,
			   new,
			   lockdep_is_held(&police->tcf_lock));
	spin_unlock_bh(&police->tcf_lock);

	if (new)
		kfree_rcu(new, rcu);

	if (ret == ACT_P_CREATED)
		tcf_idr_insert(tn, *a);
	return ret;

failure:
	qdisc_put_rtab(P_tab);
	qdisc_put_rtab(R_tab);
	tcf_idr_release(*a, bind);
	return err;
}

static int tcf_police_act(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_police *police = to_police(a);
	struct tcf_police_params *p;
	s64 now, toks, ptoks = 0;
	int ret;

	tcf_lastuse_update(&police->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(police->common.cpu_bstats), skb);

	ret = READ_ONCE(police->tcf_action);
	p = rcu_dereference_bh(police->params);

	if (p->tcfp_ewma_rate) {
		struct gnet_stats_rate_est64 sample;

		if (!gen_estimator_read(&police->tcf_rate_est, &sample) ||
		    sample.bps >= p->tcfp_ewma_rate)
			goto inc_overlimits;
	}

	if (qdisc_pkt_len(skb) <= p->tcfp_mtu) {
		if (!p->rate_present) {
			ret = p->tcfp_result;
			goto end;
		}

		now = ktime_get_ns();
		toks = min_t(s64, now - p->tcfp_t_c, p->tcfp_burst);
		if (p->peak_present) {
			ptoks = toks + p->tcfp_ptoks;
			if (ptoks > p->tcfp_mtu_ptoks)
				ptoks = p->tcfp_mtu_ptoks;
			ptoks -= (s64)psched_l2t_ns(&p->peak,
						    qdisc_pkt_len(skb));
		}
		toks += p->tcfp_toks;
		if (toks > p->tcfp_burst)
			toks = p->tcfp_burst;
		toks -= (s64)psched_l2t_ns(&p->rate, qdisc_pkt_len(skb));
		if ((toks|ptoks) >= 0) {
			p->tcfp_t_c = now;
			p->tcfp_toks = toks;
			p->tcfp_ptoks = ptoks;
			ret = p->tcfp_result;
			goto inc_drops;
		}
	}

inc_overlimits:
	qstats_overlimit_inc(this_cpu_ptr(police->common.cpu_qstats));
inc_drops:
	if (ret == TC_ACT_SHOT)
		qstats_drop_inc(this_cpu_ptr(police->common.cpu_qstats));
end:
	return ret;
}

static void tcf_police_cleanup(struct tc_action *a)
{
	struct tcf_police *police = to_police(a);
	struct tcf_police_params *p;

	p = rcu_dereference_protected(police->params, 1);
	if (p)
		kfree_rcu(p, rcu);
}

static int tcf_police_dump(struct sk_buff *skb, struct tc_action *a,
			       int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_police *police = to_police(a);
	struct tcf_police_params *p;
	struct tc_police opt = {
		.index = police->tcf_index,
		.refcnt = refcount_read(&police->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&police->tcf_bindcnt) - bind,
	};
	struct tcf_t t;

	spin_lock_bh(&police->tcf_lock);
	opt.action = police->tcf_action;
	p = rcu_dereference_protected(police->params,
				      lockdep_is_held(&police->tcf_lock));
	opt.mtu = p->tcfp_mtu;
	opt.burst = PSCHED_NS2TICKS(p->tcfp_burst);
	if (p->rate_present)
		psched_ratecfg_getrate(&opt.rate, &p->rate);
	if (p->peak_present)
		psched_ratecfg_getrate(&opt.peakrate, &p->peak);
	if (nla_put(skb, TCA_POLICE_TBF, sizeof(opt), &opt))
		goto nla_put_failure;
	if (p->tcfp_result &&
	    nla_put_u32(skb, TCA_POLICE_RESULT, p->tcfp_result))
		goto nla_put_failure;
	if (p->tcfp_ewma_rate &&
	    nla_put_u32(skb, TCA_POLICE_AVRATE, p->tcfp_ewma_rate))
		goto nla_put_failure;

	t.install = jiffies_to_clock_t(jiffies - police->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - police->tcf_tm.lastuse);
	t.firstuse = jiffies_to_clock_t(jiffies - police->tcf_tm.firstuse);
	t.expires = jiffies_to_clock_t(police->tcf_tm.expires);
	if (nla_put_64bit(skb, TCA_POLICE_TM, sizeof(t), &t, TCA_POLICE_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&police->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&police->tcf_lock);
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
	.act		=	tcf_police_act,
	.dump		=	tcf_police_dump,
	.init		=	tcf_police_init,
	.walk		=	tcf_police_walker,
	.lookup		=	tcf_police_search,
	.cleanup	=	tcf_police_cleanup,
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

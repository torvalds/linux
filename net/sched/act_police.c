/*
 * net/sched/police.c	Input police filter.
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
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <net/act_api.h>
#include <net/netlink.h>

#define L2T(p,L)   ((p)->tcfp_R_tab->data[(L)>>(p)->tcfp_R_tab->rate.cell_log])
#define L2T_P(p,L) ((p)->tcfp_P_tab->data[(L)>>(p)->tcfp_P_tab->rate.cell_log])

#define POL_TAB_MASK     15
static struct tcf_common *tcf_police_ht[POL_TAB_MASK + 1];
static u32 police_idx_gen;
static DEFINE_RWLOCK(police_lock);

static struct tcf_hashinfo police_hash_info = {
	.htab	=	tcf_police_ht,
	.hmask	=	POL_TAB_MASK,
	.lock	=	&police_lock,
};

/* old policer structure from before tc actions */
struct tc_police_compat
{
	u32			index;
	int			action;
	u32			limit;
	u32			burst;
	u32			mtu;
	struct tc_ratespec	rate;
	struct tc_ratespec	peakrate;
};

/* Each policer is serialized by its individual spinlock */

static int tcf_act_police_walker(struct sk_buff *skb, struct netlink_callback *cb,
			      int type, struct tc_action *a)
{
	struct tcf_common *p;
	int err = 0, index = -1, i = 0, s_i = 0, n_i = 0;
	struct rtattr *r;

	read_lock(&police_lock);

	s_i = cb->args[0];

	for (i = 0; i < (POL_TAB_MASK + 1); i++) {
		p = tcf_police_ht[tcf_hash(i, POL_TAB_MASK)];

		for (; p; p = p->tcfc_next) {
			index++;
			if (index < s_i)
				continue;
			a->priv = p;
			a->order = index;
			r = (struct rtattr *)skb_tail_pointer(skb);
			RTA_PUT(skb, a->order, 0, NULL);
			if (type == RTM_DELACTION)
				err = tcf_action_dump_1(skb, a, 0, 1);
			else
				err = tcf_action_dump_1(skb, a, 0, 0);
			if (err < 0) {
				index--;
				nlmsg_trim(skb, r);
				goto done;
			}
			r->rta_len = skb_tail_pointer(skb) - (u8 *)r;
			n_i++;
		}
	}
done:
	read_unlock(&police_lock);
	if (n_i)
		cb->args[0] += n_i;
	return n_i;

rtattr_failure:
	nlmsg_trim(skb, r);
	goto done;
}

static void tcf_police_destroy(struct tcf_police *p)
{
	unsigned int h = tcf_hash(p->tcf_index, POL_TAB_MASK);
	struct tcf_common **p1p;

	for (p1p = &tcf_police_ht[h]; *p1p; p1p = &(*p1p)->tcfc_next) {
		if (*p1p == &p->common) {
			write_lock_bh(&police_lock);
			*p1p = p->tcf_next;
			write_unlock_bh(&police_lock);
			gen_kill_estimator(&p->tcf_bstats,
					   &p->tcf_rate_est);
			if (p->tcfp_R_tab)
				qdisc_put_rtab(p->tcfp_R_tab);
			if (p->tcfp_P_tab)
				qdisc_put_rtab(p->tcfp_P_tab);
			kfree(p);
			return;
		}
	}
	BUG_TRAP(0);
}

static int tcf_act_police_locate(struct rtattr *rta, struct rtattr *est,
				 struct tc_action *a, int ovr, int bind)
{
	unsigned h;
	int ret = 0, err;
	struct rtattr *tb[TCA_POLICE_MAX];
	struct tc_police *parm;
	struct tcf_police *police;
	struct qdisc_rate_table *R_tab = NULL, *P_tab = NULL;
	int size;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_POLICE_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_POLICE_TBF-1] == NULL)
		return -EINVAL;
	size = RTA_PAYLOAD(tb[TCA_POLICE_TBF-1]);
	if (size != sizeof(*parm) && size != sizeof(struct tc_police_compat))
		return -EINVAL;
	parm = RTA_DATA(tb[TCA_POLICE_TBF-1]);

	if (tb[TCA_POLICE_RESULT-1] != NULL &&
	    RTA_PAYLOAD(tb[TCA_POLICE_RESULT-1]) != sizeof(u32))
		return -EINVAL;
	if (tb[TCA_POLICE_RESULT-1] != NULL &&
	    RTA_PAYLOAD(tb[TCA_POLICE_RESULT-1]) != sizeof(u32))
		return -EINVAL;

	if (parm->index) {
		struct tcf_common *pc;

		pc = tcf_hash_lookup(parm->index, &police_hash_info);
		if (pc != NULL) {
			a->priv = pc;
			police = to_police(pc);
			if (bind) {
				police->tcf_bindcnt += 1;
				police->tcf_refcnt += 1;
			}
			if (ovr)
				goto override;
			return ret;
		}
	}

	police = kzalloc(sizeof(*police), GFP_KERNEL);
	if (police == NULL)
		return -ENOMEM;
	ret = ACT_P_CREATED;
	police->tcf_refcnt = 1;
	spin_lock_init(&police->tcf_lock);
	if (bind)
		police->tcf_bindcnt = 1;
override:
	if (parm->rate.rate) {
		err = -ENOMEM;
		R_tab = qdisc_get_rtab(&parm->rate, tb[TCA_POLICE_RATE-1]);
		if (R_tab == NULL)
			goto failure;
		if (parm->peakrate.rate) {
			P_tab = qdisc_get_rtab(&parm->peakrate,
					       tb[TCA_POLICE_PEAKRATE-1]);
			if (P_tab == NULL) {
				qdisc_put_rtab(R_tab);
				goto failure;
			}
		}
	}
	/* No failure allowed after this point */
	spin_lock_bh(&police->tcf_lock);
	if (R_tab != NULL) {
		qdisc_put_rtab(police->tcfp_R_tab);
		police->tcfp_R_tab = R_tab;
	}
	if (P_tab != NULL) {
		qdisc_put_rtab(police->tcfp_P_tab);
		police->tcfp_P_tab = P_tab;
	}

	if (tb[TCA_POLICE_RESULT-1])
		police->tcfp_result = *(u32*)RTA_DATA(tb[TCA_POLICE_RESULT-1]);
	police->tcfp_toks = police->tcfp_burst = parm->burst;
	police->tcfp_mtu = parm->mtu;
	if (police->tcfp_mtu == 0) {
		police->tcfp_mtu = ~0;
		if (police->tcfp_R_tab)
			police->tcfp_mtu = 255<<police->tcfp_R_tab->rate.cell_log;
	}
	if (police->tcfp_P_tab)
		police->tcfp_ptoks = L2T_P(police, police->tcfp_mtu);
	police->tcf_action = parm->action;

	if (tb[TCA_POLICE_AVRATE-1])
		police->tcfp_ewma_rate =
			*(u32*)RTA_DATA(tb[TCA_POLICE_AVRATE-1]);
	if (est)
		gen_replace_estimator(&police->tcf_bstats,
				      &police->tcf_rate_est,
				      &police->tcf_lock, est);

	spin_unlock_bh(&police->tcf_lock);
	if (ret != ACT_P_CREATED)
		return ret;

	police->tcfp_t_c = psched_get_time();
	police->tcf_index = parm->index ? parm->index :
		tcf_hash_new_index(&police_idx_gen, &police_hash_info);
	h = tcf_hash(police->tcf_index, POL_TAB_MASK);
	write_lock_bh(&police_lock);
	police->tcf_next = tcf_police_ht[h];
	tcf_police_ht[h] = &police->common;
	write_unlock_bh(&police_lock);

	a->priv = police;
	return ret;

failure:
	if (ret == ACT_P_CREATED)
		kfree(police);
	return err;
}

static int tcf_act_police_cleanup(struct tc_action *a, int bind)
{
	struct tcf_police *p = a->priv;
	int ret = 0;

	if (p != NULL) {
		if (bind)
			p->tcf_bindcnt--;

		p->tcf_refcnt--;
		if (p->tcf_refcnt <= 0 && !p->tcf_bindcnt) {
			tcf_police_destroy(p);
			ret = 1;
		}
	}
	return ret;
}

static int tcf_act_police(struct sk_buff *skb, struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_police *police = a->priv;
	psched_time_t now;
	long toks;
	long ptoks = 0;

	spin_lock(&police->tcf_lock);

	police->tcf_bstats.bytes += skb->len;
	police->tcf_bstats.packets++;

	if (police->tcfp_ewma_rate &&
	    police->tcf_rate_est.bps >= police->tcfp_ewma_rate) {
		police->tcf_qstats.overlimits++;
		spin_unlock(&police->tcf_lock);
		return police->tcf_action;
	}

	if (skb->len <= police->tcfp_mtu) {
		if (police->tcfp_R_tab == NULL) {
			spin_unlock(&police->tcf_lock);
			return police->tcfp_result;
		}

		now = psched_get_time();
		toks = psched_tdiff_bounded(now, police->tcfp_t_c,
					    police->tcfp_burst);
		if (police->tcfp_P_tab) {
			ptoks = toks + police->tcfp_ptoks;
			if (ptoks > (long)L2T_P(police, police->tcfp_mtu))
				ptoks = (long)L2T_P(police, police->tcfp_mtu);
			ptoks -= L2T_P(police, skb->len);
		}
		toks += police->tcfp_toks;
		if (toks > (long)police->tcfp_burst)
			toks = police->tcfp_burst;
		toks -= L2T(police, skb->len);
		if ((toks|ptoks) >= 0) {
			police->tcfp_t_c = now;
			police->tcfp_toks = toks;
			police->tcfp_ptoks = ptoks;
			spin_unlock(&police->tcf_lock);
			return police->tcfp_result;
		}
	}

	police->tcf_qstats.overlimits++;
	spin_unlock(&police->tcf_lock);
	return police->tcf_action;
}

static int
tcf_act_police_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_police *police = a->priv;
	struct tc_police opt;

	opt.index = police->tcf_index;
	opt.action = police->tcf_action;
	opt.mtu = police->tcfp_mtu;
	opt.burst = police->tcfp_burst;
	opt.refcnt = police->tcf_refcnt - ref;
	opt.bindcnt = police->tcf_bindcnt - bind;
	if (police->tcfp_R_tab)
		opt.rate = police->tcfp_R_tab->rate;
	else
		memset(&opt.rate, 0, sizeof(opt.rate));
	if (police->tcfp_P_tab)
		opt.peakrate = police->tcfp_P_tab->rate;
	else
		memset(&opt.peakrate, 0, sizeof(opt.peakrate));
	RTA_PUT(skb, TCA_POLICE_TBF, sizeof(opt), &opt);
	if (police->tcfp_result)
		RTA_PUT(skb, TCA_POLICE_RESULT, sizeof(int),
			&police->tcfp_result);
	if (police->tcfp_ewma_rate)
		RTA_PUT(skb, TCA_POLICE_AVRATE, 4, &police->tcfp_ewma_rate);
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

MODULE_AUTHOR("Alexey Kuznetsov");
MODULE_DESCRIPTION("Policing actions");
MODULE_LICENSE("GPL");

static struct tc_action_ops act_police_ops = {
	.kind		=	"police",
	.hinfo		=	&police_hash_info,
	.type		=	TCA_ID_POLICE,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_act_police,
	.dump		=	tcf_act_police_dump,
	.cleanup	=	tcf_act_police_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_act_police_locate,
	.walk		=	tcf_act_police_walker
};

static int __init
police_init_module(void)
{
	return tcf_register_action(&act_police_ops);
}

static void __exit
police_cleanup_module(void)
{
	tcf_unregister_action(&act_police_ops);
}

module_init(police_init_module);
module_exit(police_cleanup_module);

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

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <net/sock.h>
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

#ifdef CONFIG_NET_CLS_ACT
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
#endif

void tcf_police_destroy(struct tcf_police *p)
{
	unsigned int h = tcf_hash(p->tcf_index, POL_TAB_MASK);
	struct tcf_common **p1p;

	for (p1p = &tcf_police_ht[h]; *p1p; p1p = &(*p1p)->tcfc_next) {
		if (*p1p == &p->common) {
			write_lock_bh(&police_lock);
			*p1p = p->tcf_next;
			write_unlock_bh(&police_lock);
#ifdef CONFIG_NET_ESTIMATOR
			gen_kill_estimator(&p->tcf_bstats,
					   &p->tcf_rate_est);
#endif
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

#ifdef CONFIG_NET_CLS_ACT
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
	police->tcf_stats_lock = &police->tcf_lock;
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

#ifdef CONFIG_NET_ESTIMATOR
	if (tb[TCA_POLICE_AVRATE-1])
		police->tcfp_ewma_rate =
			*(u32*)RTA_DATA(tb[TCA_POLICE_AVRATE-1]);
	if (est)
		gen_replace_estimator(&police->tcf_bstats,
				      &police->tcf_rate_est,
				      police->tcf_stats_lock, est);
#endif

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

	if (p != NULL)
		return tcf_police_release(p, bind);
	return 0;
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

#ifdef CONFIG_NET_ESTIMATOR
	if (police->tcfp_ewma_rate &&
	    police->tcf_rate_est.bps >= police->tcfp_ewma_rate) {
		police->tcf_qstats.overlimits++;
		spin_unlock(&police->tcf_lock);
		return police->tcf_action;
	}
#endif

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
#ifdef CONFIG_NET_ESTIMATOR
	if (police->tcfp_ewma_rate)
		RTA_PUT(skb, TCA_POLICE_AVRATE, 4, &police->tcfp_ewma_rate);
#endif
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

#else /* CONFIG_NET_CLS_ACT */

static struct tcf_common *tcf_police_lookup(u32 index)
{
	struct tcf_hashinfo *hinfo = &police_hash_info;
	struct tcf_common *p;

	read_lock(hinfo->lock);
	for (p = hinfo->htab[tcf_hash(index, hinfo->hmask)]; p;
	     p = p->tcfc_next) {
		if (p->tcfc_index == index)
			break;
	}
	read_unlock(hinfo->lock);

	return p;
}

static u32 tcf_police_new_index(void)
{
	u32 *idx_gen = &police_idx_gen;
	u32 val = *idx_gen;

	do {
		if (++val == 0)
			val = 1;
	} while (tcf_police_lookup(val));

	return (*idx_gen = val);
}

struct tcf_police *tcf_police_locate(struct rtattr *rta, struct rtattr *est)
{
	unsigned int h;
	struct tcf_police *police;
	struct rtattr *tb[TCA_POLICE_MAX];
	struct tc_police *parm;
	int size;

	if (rtattr_parse_nested(tb, TCA_POLICE_MAX, rta) < 0)
		return NULL;

	if (tb[TCA_POLICE_TBF-1] == NULL)
		return NULL;
	size = RTA_PAYLOAD(tb[TCA_POLICE_TBF-1]);
	if (size != sizeof(*parm) && size != sizeof(struct tc_police_compat))
		return NULL;

	parm = RTA_DATA(tb[TCA_POLICE_TBF-1]);

	if (parm->index) {
		struct tcf_common *pc;

		pc = tcf_police_lookup(parm->index);
		if (pc) {
			police = to_police(pc);
			police->tcf_refcnt++;
			return police;
		}
	}
	police = kzalloc(sizeof(*police), GFP_KERNEL);
	if (unlikely(!police))
		return NULL;

	police->tcf_refcnt = 1;
	spin_lock_init(&police->tcf_lock);
	police->tcf_stats_lock = &police->tcf_lock;
	if (parm->rate.rate) {
		police->tcfp_R_tab =
			qdisc_get_rtab(&parm->rate, tb[TCA_POLICE_RATE-1]);
		if (police->tcfp_R_tab == NULL)
			goto failure;
		if (parm->peakrate.rate) {
			police->tcfp_P_tab =
				qdisc_get_rtab(&parm->peakrate,
					       tb[TCA_POLICE_PEAKRATE-1]);
			if (police->tcfp_P_tab == NULL)
				goto failure;
		}
	}
	if (tb[TCA_POLICE_RESULT-1]) {
		if (RTA_PAYLOAD(tb[TCA_POLICE_RESULT-1]) != sizeof(u32))
			goto failure;
		police->tcfp_result = *(u32*)RTA_DATA(tb[TCA_POLICE_RESULT-1]);
	}
#ifdef CONFIG_NET_ESTIMATOR
	if (tb[TCA_POLICE_AVRATE-1]) {
		if (RTA_PAYLOAD(tb[TCA_POLICE_AVRATE-1]) != sizeof(u32))
			goto failure;
		police->tcfp_ewma_rate =
			*(u32*)RTA_DATA(tb[TCA_POLICE_AVRATE-1]);
	}
#endif
	police->tcfp_toks = police->tcfp_burst = parm->burst;
	police->tcfp_mtu = parm->mtu;
	if (police->tcfp_mtu == 0) {
		police->tcfp_mtu = ~0;
		if (police->tcfp_R_tab)
			police->tcfp_mtu = 255<<police->tcfp_R_tab->rate.cell_log;
	}
	if (police->tcfp_P_tab)
		police->tcfp_ptoks = L2T_P(police, police->tcfp_mtu);
	police->tcfp_t_c = psched_get_time();
	police->tcf_index = parm->index ? parm->index :
		tcf_police_new_index();
	police->tcf_action = parm->action;
#ifdef CONFIG_NET_ESTIMATOR
	if (est)
		gen_new_estimator(&police->tcf_bstats, &police->tcf_rate_est,
				  police->tcf_stats_lock, est);
#endif
	h = tcf_hash(police->tcf_index, POL_TAB_MASK);
	write_lock_bh(&police_lock);
	police->tcf_next = tcf_police_ht[h];
	tcf_police_ht[h] = &police->common;
	write_unlock_bh(&police_lock);
	return police;

failure:
	if (police->tcfp_R_tab)
		qdisc_put_rtab(police->tcfp_R_tab);
	kfree(police);
	return NULL;
}

int tcf_police(struct sk_buff *skb, struct tcf_police *police)
{
	psched_time_t now;
	long toks;
	long ptoks = 0;

	spin_lock(&police->tcf_lock);

	police->tcf_bstats.bytes += skb->len;
	police->tcf_bstats.packets++;

#ifdef CONFIG_NET_ESTIMATOR
	if (police->tcfp_ewma_rate &&
	    police->tcf_rate_est.bps >= police->tcfp_ewma_rate) {
		police->tcf_qstats.overlimits++;
		spin_unlock(&police->tcf_lock);
		return police->tcf_action;
	}
#endif
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
EXPORT_SYMBOL(tcf_police);

int tcf_police_dump(struct sk_buff *skb, struct tcf_police *police)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_police opt;

	opt.index = police->tcf_index;
	opt.action = police->tcf_action;
	opt.mtu = police->tcfp_mtu;
	opt.burst = police->tcfp_burst;
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
#ifdef CONFIG_NET_ESTIMATOR
	if (police->tcfp_ewma_rate)
		RTA_PUT(skb, TCA_POLICE_AVRATE, 4, &police->tcfp_ewma_rate);
#endif
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

int tcf_police_dump_stats(struct sk_buff *skb, struct tcf_police *police)
{
	struct gnet_dump d;

	if (gnet_stats_start_copy_compat(skb, TCA_STATS2, TCA_STATS,
					 TCA_XSTATS, police->tcf_stats_lock,
					 &d) < 0)
		goto errout;

	if (gnet_stats_copy_basic(&d, &police->tcf_bstats) < 0 ||
#ifdef CONFIG_NET_ESTIMATOR
	    gnet_stats_copy_rate_est(&d, &police->tcf_rate_est) < 0 ||
#endif
	    gnet_stats_copy_queue(&d, &police->tcf_qstats) < 0)
		goto errout;

	if (gnet_stats_finish_copy(&d) < 0)
		goto errout;

	return 0;

errout:
	return -1;
}

#endif /* CONFIG_NET_CLS_ACT */

/*
 * net/sched/sch_gred.c	Generic Random Early Detection queue.
 *
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:    J Hadi Salim (hadi@cyberus.ca) 1998-2002
 *
 *             991129: -  Bug fix with grio mode
 *		       - a better sing. AvgQ mode with Grio(WRED)
 *		       - A finer grained VQ dequeue based on sugestion
 *		         from Ren Liu
 *		       - More error checks
 *
 *  For all the glorious comments look at include/net/red.h
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>
#include <net/red.h>

#define GRED_DEF_PRIO (MAX_DPs / 2)
#define GRED_VQ_MASK (MAX_DPs - 1)

struct gred_sched_data;
struct gred_sched;

struct gred_sched_data {
	u32		limit;		/* HARD maximal queue length	*/
	u32		DP;		/* the drop parameters */
	u32		bytesin;	/* bytes seen on virtualQ so far*/
	u32		packetsin;	/* packets seen on virtualQ so far*/
	u32		backlog;	/* bytes on the virtualQ */
	u8		prio;		/* the prio of this vq */

	struct red_parms parms;
	struct red_vars  vars;
	struct red_stats stats;
};

enum {
	GRED_WRED_MODE = 1,
	GRED_RIO_MODE,
};

struct gred_sched {
	struct gred_sched_data *tab[MAX_DPs];
	unsigned long	flags;
	u32		red_flags;
	u32 		DPs;
	u32 		def;
	struct red_vars wred_set;
};

static inline int gred_wred_mode(struct gred_sched *table)
{
	return test_bit(GRED_WRED_MODE, &table->flags);
}

static inline void gred_enable_wred_mode(struct gred_sched *table)
{
	__set_bit(GRED_WRED_MODE, &table->flags);
}

static inline void gred_disable_wred_mode(struct gred_sched *table)
{
	__clear_bit(GRED_WRED_MODE, &table->flags);
}

static inline int gred_rio_mode(struct gred_sched *table)
{
	return test_bit(GRED_RIO_MODE, &table->flags);
}

static inline void gred_enable_rio_mode(struct gred_sched *table)
{
	__set_bit(GRED_RIO_MODE, &table->flags);
}

static inline void gred_disable_rio_mode(struct gred_sched *table)
{
	__clear_bit(GRED_RIO_MODE, &table->flags);
}

static inline int gred_wred_mode_check(struct Qdisc *sch)
{
	struct gred_sched *table = qdisc_priv(sch);
	int i;

	/* Really ugly O(n^2) but shouldn't be necessary too frequent. */
	for (i = 0; i < table->DPs; i++) {
		struct gred_sched_data *q = table->tab[i];
		int n;

		if (q == NULL)
			continue;

		for (n = 0; n < table->DPs; n++)
			if (table->tab[n] && table->tab[n] != q &&
			    table->tab[n]->prio == q->prio)
				return 1;
	}

	return 0;
}

static inline unsigned int gred_backlog(struct gred_sched *table,
					struct gred_sched_data *q,
					struct Qdisc *sch)
{
	if (gred_wred_mode(table))
		return sch->qstats.backlog;
	else
		return q->backlog;
}

static inline u16 tc_index_to_dp(struct sk_buff *skb)
{
	return skb->tc_index & GRED_VQ_MASK;
}

static inline void gred_load_wred_set(const struct gred_sched *table,
				      struct gred_sched_data *q)
{
	q->vars.qavg = table->wred_set.qavg;
	q->vars.qidlestart = table->wred_set.qidlestart;
}

static inline void gred_store_wred_set(struct gred_sched *table,
				       struct gred_sched_data *q)
{
	table->wred_set.qavg = q->vars.qavg;
}

static inline int gred_use_ecn(struct gred_sched *t)
{
	return t->red_flags & TC_RED_ECN;
}

static inline int gred_use_harddrop(struct gred_sched *t)
{
	return t->red_flags & TC_RED_HARDDROP;
}

static int gred_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct gred_sched_data *q = NULL;
	struct gred_sched *t = qdisc_priv(sch);
	unsigned long qavg = 0;
	u16 dp = tc_index_to_dp(skb);

	if (dp >= t->DPs || (q = t->tab[dp]) == NULL) {
		dp = t->def;

		q = t->tab[dp];
		if (!q) {
			/* Pass through packets not assigned to a DP
			 * if no default DP has been configured. This
			 * allows for DP flows to be left untouched.
			 */
			if (skb_queue_len(&sch->q) < qdisc_dev(sch)->tx_queue_len)
				return qdisc_enqueue_tail(skb, sch);
			else
				goto drop;
		}

		/* fix tc_index? --could be controversial but needed for
		   requeueing */
		skb->tc_index = (skb->tc_index & ~GRED_VQ_MASK) | dp;
	}

	/* sum up all the qaves of prios <= to ours to get the new qave */
	if (!gred_wred_mode(t) && gred_rio_mode(t)) {
		int i;

		for (i = 0; i < t->DPs; i++) {
			if (t->tab[i] && t->tab[i]->prio < q->prio &&
			    !red_is_idling(&t->tab[i]->vars))
				qavg += t->tab[i]->vars.qavg;
		}

	}

	q->packetsin++;
	q->bytesin += qdisc_pkt_len(skb);

	if (gred_wred_mode(t))
		gred_load_wred_set(t, q);

	q->vars.qavg = red_calc_qavg(&q->parms,
				     &q->vars,
				     gred_backlog(t, q, sch));

	if (red_is_idling(&q->vars))
		red_end_of_idle_period(&q->vars);

	if (gred_wred_mode(t))
		gred_store_wred_set(t, q);

	switch (red_action(&q->parms, &q->vars, q->vars.qavg + qavg)) {
	case RED_DONT_MARK:
		break;

	case RED_PROB_MARK:
		sch->qstats.overlimits++;
		if (!gred_use_ecn(t) || !INET_ECN_set_ce(skb)) {
			q->stats.prob_drop++;
			goto congestion_drop;
		}

		q->stats.prob_mark++;
		break;

	case RED_HARD_MARK:
		sch->qstats.overlimits++;
		if (gred_use_harddrop(t) || !gred_use_ecn(t) ||
		    !INET_ECN_set_ce(skb)) {
			q->stats.forced_drop++;
			goto congestion_drop;
		}
		q->stats.forced_mark++;
		break;
	}

	if (q->backlog + qdisc_pkt_len(skb) <= q->limit) {
		q->backlog += qdisc_pkt_len(skb);
		return qdisc_enqueue_tail(skb, sch);
	}

	q->stats.pdrop++;
drop:
	return qdisc_drop(skb, sch);

congestion_drop:
	qdisc_drop(skb, sch);
	return NET_XMIT_CN;
}

static struct sk_buff *gred_dequeue(struct Qdisc *sch)
{
	struct sk_buff *skb;
	struct gred_sched *t = qdisc_priv(sch);

	skb = qdisc_dequeue_head(sch);

	if (skb) {
		struct gred_sched_data *q;
		u16 dp = tc_index_to_dp(skb);

		if (dp >= t->DPs || (q = t->tab[dp]) == NULL) {
			if (net_ratelimit())
				pr_warning("GRED: Unable to relocate VQ 0x%x "
					   "after dequeue, screwing up "
					   "backlog.\n", tc_index_to_dp(skb));
		} else {
			q->backlog -= qdisc_pkt_len(skb);

			if (!q->backlog && !gred_wred_mode(t))
				red_start_of_idle_period(&q->vars);
		}

		return skb;
	}

	if (gred_wred_mode(t) && !red_is_idling(&t->wred_set))
		red_start_of_idle_period(&t->wred_set);

	return NULL;
}

static unsigned int gred_drop(struct Qdisc *sch)
{
	struct sk_buff *skb;
	struct gred_sched *t = qdisc_priv(sch);

	skb = qdisc_dequeue_tail(sch);
	if (skb) {
		unsigned int len = qdisc_pkt_len(skb);
		struct gred_sched_data *q;
		u16 dp = tc_index_to_dp(skb);

		if (dp >= t->DPs || (q = t->tab[dp]) == NULL) {
			if (net_ratelimit())
				pr_warning("GRED: Unable to relocate VQ 0x%x "
					   "while dropping, screwing up "
					   "backlog.\n", tc_index_to_dp(skb));
		} else {
			q->backlog -= len;
			q->stats.other++;

			if (!q->backlog && !gred_wred_mode(t))
				red_start_of_idle_period(&q->vars);
		}

		qdisc_drop(skb, sch);
		return len;
	}

	if (gred_wred_mode(t) && !red_is_idling(&t->wred_set))
		red_start_of_idle_period(&t->wred_set);

	return 0;

}

static void gred_reset(struct Qdisc *sch)
{
	int i;
	struct gred_sched *t = qdisc_priv(sch);

	qdisc_reset_queue(sch);

	for (i = 0; i < t->DPs; i++) {
		struct gred_sched_data *q = t->tab[i];

		if (!q)
			continue;

		red_restart(&q->vars);
		q->backlog = 0;
	}
}

static inline void gred_destroy_vq(struct gred_sched_data *q)
{
	kfree(q);
}

static inline int gred_change_table_def(struct Qdisc *sch, struct nlattr *dps)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_sopt *sopt;
	int i;

	if (dps == NULL)
		return -EINVAL;

	sopt = nla_data(dps);

	if (sopt->DPs > MAX_DPs || sopt->DPs == 0 || sopt->def_DP >= sopt->DPs)
		return -EINVAL;

	sch_tree_lock(sch);
	table->DPs = sopt->DPs;
	table->def = sopt->def_DP;
	table->red_flags = sopt->flags;

	/*
	 * Every entry point to GRED is synchronized with the above code
	 * and the DP is checked against DPs, i.e. shadowed VQs can no
	 * longer be found so we can unlock right here.
	 */
	sch_tree_unlock(sch);

	if (sopt->grio) {
		gred_enable_rio_mode(table);
		gred_disable_wred_mode(table);
		if (gred_wred_mode_check(sch))
			gred_enable_wred_mode(table);
	} else {
		gred_disable_rio_mode(table);
		gred_disable_wred_mode(table);
	}

	for (i = table->DPs; i < MAX_DPs; i++) {
		if (table->tab[i]) {
			pr_warning("GRED: Warning: Destroying "
				   "shadowed VQ 0x%x\n", i);
			gred_destroy_vq(table->tab[i]);
			table->tab[i] = NULL;
		}
	}

	return 0;
}

static inline int gred_change_vq(struct Qdisc *sch, int dp,
				 struct tc_gred_qopt *ctl, int prio,
				 u8 *stab, u32 max_P,
				 struct gred_sched_data **prealloc)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct gred_sched_data *q = table->tab[dp];

	if (!q) {
		table->tab[dp] = q = *prealloc;
		*prealloc = NULL;
		if (!q)
			return -ENOMEM;
	}

	q->DP = dp;
	q->prio = prio;
	q->limit = ctl->limit;

	if (q->backlog == 0)
		red_end_of_idle_period(&q->vars);

	red_set_parms(&q->parms,
		      ctl->qth_min, ctl->qth_max, ctl->Wlog, ctl->Plog,
		      ctl->Scell_log, stab, max_P);
	red_set_vars(&q->vars);
	return 0;
}

static const struct nla_policy gred_policy[TCA_GRED_MAX + 1] = {
	[TCA_GRED_PARMS]	= { .len = sizeof(struct tc_gred_qopt) },
	[TCA_GRED_STAB]		= { .len = 256 },
	[TCA_GRED_DPS]		= { .len = sizeof(struct tc_gred_sopt) },
	[TCA_GRED_MAX_P]	= { .type = NLA_U32 },
};

static int gred_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_qopt *ctl;
	struct nlattr *tb[TCA_GRED_MAX + 1];
	int err, prio = GRED_DEF_PRIO;
	u8 *stab;
	u32 max_P;
	struct gred_sched_data *prealloc;

	if (opt == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_GRED_MAX, opt, gred_policy);
	if (err < 0)
		return err;

	if (tb[TCA_GRED_PARMS] == NULL && tb[TCA_GRED_STAB] == NULL)
		return gred_change_table_def(sch, opt);

	if (tb[TCA_GRED_PARMS] == NULL ||
	    tb[TCA_GRED_STAB] == NULL)
		return -EINVAL;

	max_P = tb[TCA_GRED_MAX_P] ? nla_get_u32(tb[TCA_GRED_MAX_P]) : 0;

	err = -EINVAL;
	ctl = nla_data(tb[TCA_GRED_PARMS]);
	stab = nla_data(tb[TCA_GRED_STAB]);

	if (ctl->DP >= table->DPs)
		goto errout;

	if (gred_rio_mode(table)) {
		if (ctl->prio == 0) {
			int def_prio = GRED_DEF_PRIO;

			if (table->tab[table->def])
				def_prio = table->tab[table->def]->prio;

			printk(KERN_DEBUG "GRED: DP %u does not have a prio "
			       "setting default to %d\n", ctl->DP, def_prio);

			prio = def_prio;
		} else
			prio = ctl->prio;
	}

	prealloc = kzalloc(sizeof(*prealloc), GFP_KERNEL);
	sch_tree_lock(sch);

	err = gred_change_vq(sch, ctl->DP, ctl, prio, stab, max_P, &prealloc);
	if (err < 0)
		goto errout_locked;

	if (gred_rio_mode(table)) {
		gred_disable_wred_mode(table);
		if (gred_wred_mode_check(sch))
			gred_enable_wred_mode(table);
	}

	err = 0;

errout_locked:
	sch_tree_unlock(sch);
	kfree(prealloc);
errout:
	return err;
}

static int gred_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct nlattr *tb[TCA_GRED_MAX + 1];
	int err;

	if (opt == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_GRED_MAX, opt, gred_policy);
	if (err < 0)
		return err;

	if (tb[TCA_GRED_PARMS] || tb[TCA_GRED_STAB])
		return -EINVAL;

	return gred_change_table_def(sch, tb[TCA_GRED_DPS]);
}

static int gred_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct nlattr *parms, *opts = NULL;
	int i;
	u32 max_p[MAX_DPs];
	struct tc_gred_sopt sopt = {
		.DPs	= table->DPs,
		.def_DP	= table->def,
		.grio	= gred_rio_mode(table),
		.flags	= table->red_flags,
	};

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;
	NLA_PUT(skb, TCA_GRED_DPS, sizeof(sopt), &sopt);

	for (i = 0; i < MAX_DPs; i++) {
		struct gred_sched_data *q = table->tab[i];

		max_p[i] = q ? q->parms.max_P : 0;
	}
	NLA_PUT(skb, TCA_GRED_MAX_P, sizeof(max_p), max_p);

	parms = nla_nest_start(skb, TCA_GRED_PARMS);
	if (parms == NULL)
		goto nla_put_failure;

	for (i = 0; i < MAX_DPs; i++) {
		struct gred_sched_data *q = table->tab[i];
		struct tc_gred_qopt opt;

		memset(&opt, 0, sizeof(opt));

		if (!q) {
			/* hack -- fix at some point with proper message
			   This is how we indicate to tc that there is no VQ
			   at this DP */

			opt.DP = MAX_DPs + i;
			goto append_opt;
		}

		opt.limit	= q->limit;
		opt.DP		= q->DP;
		opt.backlog	= q->backlog;
		opt.prio	= q->prio;
		opt.qth_min	= q->parms.qth_min >> q->parms.Wlog;
		opt.qth_max	= q->parms.qth_max >> q->parms.Wlog;
		opt.Wlog	= q->parms.Wlog;
		opt.Plog	= q->parms.Plog;
		opt.Scell_log	= q->parms.Scell_log;
		opt.other	= q->stats.other;
		opt.early	= q->stats.prob_drop;
		opt.forced	= q->stats.forced_drop;
		opt.pdrop	= q->stats.pdrop;
		opt.packets	= q->packetsin;
		opt.bytesin	= q->bytesin;

		if (gred_wred_mode(table))
			gred_load_wred_set(table, q);

		opt.qave = red_calc_qavg(&q->parms, &q->vars, q->vars.qavg);

append_opt:
		if (nla_append(skb, sizeof(opt), &opt) < 0)
			goto nla_put_failure;
	}

	nla_nest_end(skb, parms);

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static void gred_destroy(struct Qdisc *sch)
{
	struct gred_sched *table = qdisc_priv(sch);
	int i;

	for (i = 0; i < table->DPs; i++) {
		if (table->tab[i])
			gred_destroy_vq(table->tab[i]);
	}
}

static struct Qdisc_ops gred_qdisc_ops __read_mostly = {
	.id		=	"gred",
	.priv_size	=	sizeof(struct gred_sched),
	.enqueue	=	gred_enqueue,
	.dequeue	=	gred_dequeue,
	.peek		=	qdisc_peek_head,
	.drop		=	gred_drop,
	.init		=	gred_init,
	.reset		=	gred_reset,
	.destroy	=	gred_destroy,
	.change		=	gred_change,
	.dump		=	gred_dump,
	.owner		=	THIS_MODULE,
};

static int __init gred_module_init(void)
{
	return register_qdisc(&gred_qdisc_ops);
}

static void __exit gred_module_exit(void)
{
	unregister_qdisc(&gred_qdisc_ops);
}

module_init(gred_module_init)
module_exit(gred_module_exit)

MODULE_LICENSE("GPL");

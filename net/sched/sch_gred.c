// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_gred.c	Generic Random Early Detection queue.
 *
 * Authors:    J Hadi Salim (hadi@cyberus.ca) 1998-2002
 *
 *             991129: -  Bug fix with grio mode
 *		       - a better sing. AvgQ mode with Grio(WRED)
 *		       - A finer grained VQ dequeue based on suggestion
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
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/red.h>

#define GRED_DEF_PRIO (MAX_DPs / 2)
#define GRED_VQ_MASK (MAX_DPs - 1)

#define GRED_VQ_RED_FLAGS	(TC_RED_ECN | TC_RED_HARDDROP)

struct gred_sched_data;
struct gred_sched;

struct gred_sched_data {
	u32		limit;		/* HARD maximal queue length	*/
	u32		DP;		/* the drop parameters */
	u32		red_flags;	/* virtualQ version of red_flags */
	u64		bytesin;	/* bytes seen on virtualQ so far*/
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
	struct tc_gred_qopt_offload *opt;
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

		for (n = i + 1; n < table->DPs; n++)
			if (table->tab[n] && table->tab[n]->prio == q->prio)
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
	table->wred_set.qidlestart = q->vars.qidlestart;
}

static int gred_use_ecn(struct gred_sched_data *q)
{
	return q->red_flags & TC_RED_ECN;
}

static int gred_use_harddrop(struct gred_sched_data *q)
{
	return q->red_flags & TC_RED_HARDDROP;
}

static bool gred_per_vq_red_flags_used(struct gred_sched *table)
{
	unsigned int i;

	/* Local per-vq flags couldn't have been set unless global are 0 */
	if (table->red_flags)
		return false;
	for (i = 0; i < MAX_DPs; i++)
		if (table->tab[i] && table->tab[i]->red_flags)
			return true;
	return false;
}

static int gred_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			struct sk_buff **to_free)
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
			if (likely(sch->qstats.backlog + qdisc_pkt_len(skb) <=
					sch->limit))
				return qdisc_enqueue_tail(skb, sch);
			else
				goto drop;
		}

		/* fix tc_index? --could be controversial but needed for
		   requeueing */
		skb->tc_index = (skb->tc_index & ~GRED_VQ_MASK) | dp;
	}

	/* sum up all the qaves of prios < ours to get the new qave */
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
		qdisc_qstats_overlimit(sch);
		if (!gred_use_ecn(q) || !INET_ECN_set_ce(skb)) {
			q->stats.prob_drop++;
			goto congestion_drop;
		}

		q->stats.prob_mark++;
		break;

	case RED_HARD_MARK:
		qdisc_qstats_overlimit(sch);
		if (gred_use_harddrop(q) || !gred_use_ecn(q) ||
		    !INET_ECN_set_ce(skb)) {
			q->stats.forced_drop++;
			goto congestion_drop;
		}
		q->stats.forced_mark++;
		break;
	}

	if (gred_backlog(t, q, sch) + qdisc_pkt_len(skb) <= q->limit) {
		q->backlog += qdisc_pkt_len(skb);
		return qdisc_enqueue_tail(skb, sch);
	}

	q->stats.pdrop++;
drop:
	return qdisc_drop_reason(skb, sch, to_free, SKB_DROP_REASON_QDISC_OVERLIMIT);

congestion_drop:
	qdisc_drop_reason(skb, sch, to_free, SKB_DROP_REASON_QDISC_CONGESTED);
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
			net_warn_ratelimited("GRED: Unable to relocate VQ 0x%x after dequeue, screwing up backlog\n",
					     tc_index_to_dp(skb));
		} else {
			q->backlog -= qdisc_pkt_len(skb);

			if (gred_wred_mode(t)) {
				if (!sch->qstats.backlog)
					red_start_of_idle_period(&t->wred_set);
			} else {
				if (!q->backlog)
					red_start_of_idle_period(&q->vars);
			}
		}

		return skb;
	}

	return NULL;
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

static void gred_offload(struct Qdisc *sch, enum tc_gred_command command)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct tc_gred_qopt_offload *opt = table->opt;

	if (!tc_can_offload(dev) || !dev->netdev_ops->ndo_setup_tc)
		return;

	memset(opt, 0, sizeof(*opt));
	opt->command = command;
	opt->handle = sch->handle;
	opt->parent = sch->parent;

	if (command == TC_GRED_REPLACE) {
		unsigned int i;

		opt->set.grio_on = gred_rio_mode(table);
		opt->set.wred_on = gred_wred_mode(table);
		opt->set.dp_cnt = table->DPs;
		opt->set.dp_def = table->def;

		for (i = 0; i < table->DPs; i++) {
			struct gred_sched_data *q = table->tab[i];

			if (!q)
				continue;
			opt->set.tab[i].present = true;
			opt->set.tab[i].limit = q->limit;
			opt->set.tab[i].prio = q->prio;
			opt->set.tab[i].min = q->parms.qth_min >> q->parms.Wlog;
			opt->set.tab[i].max = q->parms.qth_max >> q->parms.Wlog;
			opt->set.tab[i].is_ecn = gred_use_ecn(q);
			opt->set.tab[i].is_harddrop = gred_use_harddrop(q);
			opt->set.tab[i].probability = q->parms.max_P;
			opt->set.tab[i].backlog = &q->backlog;
		}
		opt->set.qstats = &sch->qstats;
	}

	dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_GRED, opt);
}

static int gred_offload_dump_stats(struct Qdisc *sch)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_qopt_offload *hw_stats;
	u64 bytes = 0, packets = 0;
	unsigned int i;
	int ret;

	hw_stats = kzalloc(sizeof(*hw_stats), GFP_KERNEL);
	if (!hw_stats)
		return -ENOMEM;

	hw_stats->command = TC_GRED_STATS;
	hw_stats->handle = sch->handle;
	hw_stats->parent = sch->parent;

	for (i = 0; i < MAX_DPs; i++) {
		gnet_stats_basic_sync_init(&hw_stats->stats.bstats[i]);
		if (table->tab[i])
			hw_stats->stats.xstats[i] = &table->tab[i]->stats;
	}

	ret = qdisc_offload_dump_helper(sch, TC_SETUP_QDISC_GRED, hw_stats);
	/* Even if driver returns failure adjust the stats - in case offload
	 * ended but driver still wants to adjust the values.
	 */
	sch_tree_lock(sch);
	for (i = 0; i < MAX_DPs; i++) {
		if (!table->tab[i])
			continue;
		table->tab[i]->packetsin += u64_stats_read(&hw_stats->stats.bstats[i].packets);
		table->tab[i]->bytesin += u64_stats_read(&hw_stats->stats.bstats[i].bytes);
		table->tab[i]->backlog += hw_stats->stats.qstats[i].backlog;

		bytes += u64_stats_read(&hw_stats->stats.bstats[i].bytes);
		packets += u64_stats_read(&hw_stats->stats.bstats[i].packets);
		sch->qstats.qlen += hw_stats->stats.qstats[i].qlen;
		sch->qstats.backlog += hw_stats->stats.qstats[i].backlog;
		sch->qstats.drops += hw_stats->stats.qstats[i].drops;
		sch->qstats.requeues += hw_stats->stats.qstats[i].requeues;
		sch->qstats.overlimits += hw_stats->stats.qstats[i].overlimits;
	}
	_bstats_update(&sch->bstats, bytes, packets);
	sch_tree_unlock(sch);

	kfree(hw_stats);
	return ret;
}

static inline void gred_destroy_vq(struct gred_sched_data *q)
{
	kfree(q);
}

static int gred_change_table_def(struct Qdisc *sch, struct nlattr *dps,
				 struct netlink_ext_ack *extack)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_sopt *sopt;
	bool red_flags_changed;
	int i;

	if (!dps)
		return -EINVAL;

	sopt = nla_data(dps);

	if (sopt->DPs > MAX_DPs) {
		NL_SET_ERR_MSG_MOD(extack, "number of virtual queues too high");
		return -EINVAL;
	}
	if (sopt->DPs == 0) {
		NL_SET_ERR_MSG_MOD(extack,
				   "number of virtual queues can't be 0");
		return -EINVAL;
	}
	if (sopt->def_DP >= sopt->DPs) {
		NL_SET_ERR_MSG_MOD(extack, "default virtual queue above virtual queue count");
		return -EINVAL;
	}
	if (sopt->flags && gred_per_vq_red_flags_used(table)) {
		NL_SET_ERR_MSG_MOD(extack, "can't set per-Qdisc RED flags when per-virtual queue flags are used");
		return -EINVAL;
	}

	sch_tree_lock(sch);
	table->DPs = sopt->DPs;
	table->def = sopt->def_DP;
	red_flags_changed = table->red_flags != sopt->flags;
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

	if (red_flags_changed)
		for (i = 0; i < table->DPs; i++)
			if (table->tab[i])
				table->tab[i]->red_flags =
					table->red_flags & GRED_VQ_RED_FLAGS;

	for (i = table->DPs; i < MAX_DPs; i++) {
		if (table->tab[i]) {
			pr_warn("GRED: Warning: Destroying shadowed VQ 0x%x\n",
				i);
			gred_destroy_vq(table->tab[i]);
			table->tab[i] = NULL;
		}
	}

	gred_offload(sch, TC_GRED_REPLACE);
	return 0;
}

static inline int gred_change_vq(struct Qdisc *sch, int dp,
				 struct tc_gred_qopt *ctl, int prio,
				 u8 *stab, u32 max_P,
				 struct gred_sched_data **prealloc,
				 struct netlink_ext_ack *extack)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct gred_sched_data *q = table->tab[dp];

	if (!red_check_params(ctl->qth_min, ctl->qth_max, ctl->Wlog, ctl->Scell_log, stab)) {
		NL_SET_ERR_MSG_MOD(extack, "invalid RED parameters");
		return -EINVAL;
	}

	if (!q) {
		table->tab[dp] = q = *prealloc;
		*prealloc = NULL;
		if (!q)
			return -ENOMEM;
		q->red_flags = table->red_flags & GRED_VQ_RED_FLAGS;
	}

	q->DP = dp;
	q->prio = prio;
	if (ctl->limit > sch->limit)
		q->limit = sch->limit;
	else
		q->limit = ctl->limit;

	if (q->backlog == 0)
		red_end_of_idle_period(&q->vars);

	red_set_parms(&q->parms,
		      ctl->qth_min, ctl->qth_max, ctl->Wlog, ctl->Plog,
		      ctl->Scell_log, stab, max_P);
	red_set_vars(&q->vars);
	return 0;
}

static const struct nla_policy gred_vq_policy[TCA_GRED_VQ_MAX + 1] = {
	[TCA_GRED_VQ_DP]	= { .type = NLA_U32 },
	[TCA_GRED_VQ_FLAGS]	= { .type = NLA_U32 },
};

static const struct nla_policy gred_vqe_policy[TCA_GRED_VQ_ENTRY_MAX + 1] = {
	[TCA_GRED_VQ_ENTRY]	= { .type = NLA_NESTED },
};

static const struct nla_policy gred_policy[TCA_GRED_MAX + 1] = {
	[TCA_GRED_PARMS]	= { .len = sizeof(struct tc_gred_qopt) },
	[TCA_GRED_STAB]		= { .len = 256 },
	[TCA_GRED_DPS]		= { .len = sizeof(struct tc_gred_sopt) },
	[TCA_GRED_MAX_P]	= { .type = NLA_U32 },
	[TCA_GRED_LIMIT]	= { .type = NLA_U32 },
	[TCA_GRED_VQ_LIST]	= { .type = NLA_NESTED },
};

static void gred_vq_apply(struct gred_sched *table, const struct nlattr *entry)
{
	struct nlattr *tb[TCA_GRED_VQ_MAX + 1];
	u32 dp;

	nla_parse_nested_deprecated(tb, TCA_GRED_VQ_MAX, entry,
				    gred_vq_policy, NULL);

	dp = nla_get_u32(tb[TCA_GRED_VQ_DP]);

	if (tb[TCA_GRED_VQ_FLAGS])
		table->tab[dp]->red_flags = nla_get_u32(tb[TCA_GRED_VQ_FLAGS]);
}

static void gred_vqs_apply(struct gred_sched *table, struct nlattr *vqs)
{
	const struct nlattr *attr;
	int rem;

	nla_for_each_nested(attr, vqs, rem) {
		switch (nla_type(attr)) {
		case TCA_GRED_VQ_ENTRY:
			gred_vq_apply(table, attr);
			break;
		}
	}
}

static int gred_vq_validate(struct gred_sched *table, u32 cdp,
			    const struct nlattr *entry,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_GRED_VQ_MAX + 1];
	int err;
	u32 dp;

	err = nla_parse_nested_deprecated(tb, TCA_GRED_VQ_MAX, entry,
					  gred_vq_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_GRED_VQ_DP]) {
		NL_SET_ERR_MSG_MOD(extack, "Virtual queue with no index specified");
		return -EINVAL;
	}
	dp = nla_get_u32(tb[TCA_GRED_VQ_DP]);
	if (dp >= table->DPs) {
		NL_SET_ERR_MSG_MOD(extack, "Virtual queue with index out of bounds");
		return -EINVAL;
	}
	if (dp != cdp && !table->tab[dp]) {
		NL_SET_ERR_MSG_MOD(extack, "Virtual queue not yet instantiated");
		return -EINVAL;
	}

	if (tb[TCA_GRED_VQ_FLAGS]) {
		u32 red_flags = nla_get_u32(tb[TCA_GRED_VQ_FLAGS]);

		if (table->red_flags && table->red_flags != red_flags) {
			NL_SET_ERR_MSG_MOD(extack, "can't change per-virtual queue RED flags when per-Qdisc flags are used");
			return -EINVAL;
		}
		if (red_flags & ~GRED_VQ_RED_FLAGS) {
			NL_SET_ERR_MSG_MOD(extack,
					   "invalid RED flags specified");
			return -EINVAL;
		}
	}

	return 0;
}

static int gred_vqs_validate(struct gred_sched *table, u32 cdp,
			     struct nlattr *vqs, struct netlink_ext_ack *extack)
{
	const struct nlattr *attr;
	int rem, err;

	err = nla_validate_nested_deprecated(vqs, TCA_GRED_VQ_ENTRY_MAX,
					     gred_vqe_policy, extack);
	if (err < 0)
		return err;

	nla_for_each_nested(attr, vqs, rem) {
		switch (nla_type(attr)) {
		case TCA_GRED_VQ_ENTRY:
			err = gred_vq_validate(table, cdp, attr, extack);
			if (err)
				return err;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "GRED_VQ_LIST can contain only entry attributes");
			return -EINVAL;
		}
	}

	if (rem > 0) {
		NL_SET_ERR_MSG_MOD(extack, "Trailing data after parsing virtual queue list");
		return -EINVAL;
	}

	return 0;
}

static int gred_change(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_qopt *ctl;
	struct nlattr *tb[TCA_GRED_MAX + 1];
	int err, prio = GRED_DEF_PRIO;
	u8 *stab;
	u32 max_P;
	struct gred_sched_data *prealloc;

	err = nla_parse_nested_deprecated(tb, TCA_GRED_MAX, opt, gred_policy,
					  extack);
	if (err < 0)
		return err;

	if (tb[TCA_GRED_PARMS] == NULL && tb[TCA_GRED_STAB] == NULL) {
		if (tb[TCA_GRED_LIMIT] != NULL)
			sch->limit = nla_get_u32(tb[TCA_GRED_LIMIT]);
		return gred_change_table_def(sch, tb[TCA_GRED_DPS], extack);
	}

	if (tb[TCA_GRED_PARMS] == NULL ||
	    tb[TCA_GRED_STAB] == NULL ||
	    tb[TCA_GRED_LIMIT] != NULL) {
		NL_SET_ERR_MSG_MOD(extack, "can't configure Qdisc and virtual queue at the same time");
		return -EINVAL;
	}

	max_P = nla_get_u32_default(tb[TCA_GRED_MAX_P], 0);

	ctl = nla_data(tb[TCA_GRED_PARMS]);
	stab = nla_data(tb[TCA_GRED_STAB]);

	if (ctl->DP >= table->DPs) {
		NL_SET_ERR_MSG_MOD(extack, "virtual queue index above virtual queue count");
		return -EINVAL;
	}

	if (tb[TCA_GRED_VQ_LIST]) {
		err = gred_vqs_validate(table, ctl->DP, tb[TCA_GRED_VQ_LIST],
					extack);
		if (err)
			return err;
	}

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

	err = gred_change_vq(sch, ctl->DP, ctl, prio, stab, max_P, &prealloc,
			     extack);
	if (err < 0)
		goto err_unlock_free;

	if (tb[TCA_GRED_VQ_LIST])
		gred_vqs_apply(table, tb[TCA_GRED_VQ_LIST]);

	if (gred_rio_mode(table)) {
		gred_disable_wred_mode(table);
		if (gred_wred_mode_check(sch))
			gred_enable_wred_mode(table);
	}

	sch_tree_unlock(sch);
	kfree(prealloc);

	gred_offload(sch, TC_GRED_REPLACE);
	return 0;

err_unlock_free:
	sch_tree_unlock(sch);
	kfree(prealloc);
	return err;
}

static int gred_init(struct Qdisc *sch, struct nlattr *opt,
		     struct netlink_ext_ack *extack)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct nlattr *tb[TCA_GRED_MAX + 1];
	int err;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, TCA_GRED_MAX, opt, gred_policy,
					  extack);
	if (err < 0)
		return err;

	if (tb[TCA_GRED_PARMS] || tb[TCA_GRED_STAB]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "virtual queue configuration can't be specified at initialization time");
		return -EINVAL;
	}

	if (tb[TCA_GRED_LIMIT])
		sch->limit = nla_get_u32(tb[TCA_GRED_LIMIT]);
	else
		sch->limit = qdisc_dev(sch)->tx_queue_len
		             * psched_mtu(qdisc_dev(sch));

	if (qdisc_dev(sch)->netdev_ops->ndo_setup_tc) {
		table->opt = kzalloc(sizeof(*table->opt), GFP_KERNEL);
		if (!table->opt)
			return -ENOMEM;
	}

	return gred_change_table_def(sch, tb[TCA_GRED_DPS], extack);
}

static int gred_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct nlattr *parms, *vqs, *opts = NULL;
	int i;
	u32 max_p[MAX_DPs];
	struct tc_gred_sopt sopt = {
		.DPs	= table->DPs,
		.def_DP	= table->def,
		.grio	= gred_rio_mode(table),
		.flags	= table->red_flags,
	};

	if (gred_offload_dump_stats(sch))
		goto nla_put_failure;

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;
	if (nla_put(skb, TCA_GRED_DPS, sizeof(sopt), &sopt))
		goto nla_put_failure;

	for (i = 0; i < MAX_DPs; i++) {
		struct gred_sched_data *q = table->tab[i];

		max_p[i] = q ? q->parms.max_P : 0;
	}
	if (nla_put(skb, TCA_GRED_MAX_P, sizeof(max_p), max_p))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_GRED_LIMIT, sch->limit))
		goto nla_put_failure;

	/* Old style all-in-one dump of VQs */
	parms = nla_nest_start_noflag(skb, TCA_GRED_PARMS);
	if (parms == NULL)
		goto nla_put_failure;

	for (i = 0; i < MAX_DPs; i++) {
		struct gred_sched_data *q = table->tab[i];
		struct tc_gred_qopt opt;
		unsigned long qavg;

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
		opt.backlog	= gred_backlog(table, q, sch);
		opt.prio	= q->prio;
		opt.qth_min	= q->parms.qth_min >> q->parms.Wlog;
		opt.qth_max	= q->parms.qth_max >> q->parms.Wlog;
		opt.Wlog	= q->parms.Wlog;
		opt.Plog	= q->parms.Plog;
		opt.Scell_log	= q->parms.Scell_log;
		opt.early	= q->stats.prob_drop;
		opt.forced	= q->stats.forced_drop;
		opt.pdrop	= q->stats.pdrop;
		opt.packets	= q->packetsin;
		opt.bytesin	= q->bytesin;

		if (gred_wred_mode(table))
			gred_load_wred_set(table, q);

		qavg = red_calc_qavg(&q->parms, &q->vars,
				     q->vars.qavg >> q->parms.Wlog);
		opt.qave = qavg >> q->parms.Wlog;

append_opt:
		if (nla_append(skb, sizeof(opt), &opt) < 0)
			goto nla_put_failure;
	}

	nla_nest_end(skb, parms);

	/* Dump the VQs again, in more structured way */
	vqs = nla_nest_start_noflag(skb, TCA_GRED_VQ_LIST);
	if (!vqs)
		goto nla_put_failure;

	for (i = 0; i < MAX_DPs; i++) {
		struct gred_sched_data *q = table->tab[i];
		struct nlattr *vq;

		if (!q)
			continue;

		vq = nla_nest_start_noflag(skb, TCA_GRED_VQ_ENTRY);
		if (!vq)
			goto nla_put_failure;

		if (nla_put_u32(skb, TCA_GRED_VQ_DP, q->DP))
			goto nla_put_failure;

		if (nla_put_u32(skb, TCA_GRED_VQ_FLAGS, q->red_flags))
			goto nla_put_failure;

		/* Stats */
		if (nla_put_u64_64bit(skb, TCA_GRED_VQ_STAT_BYTES, q->bytesin,
				      TCA_GRED_VQ_PAD))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_PACKETS, q->packetsin))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_BACKLOG,
				gred_backlog(table, q, sch)))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_PROB_DROP,
				q->stats.prob_drop))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_PROB_MARK,
				q->stats.prob_mark))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_FORCED_DROP,
				q->stats.forced_drop))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_FORCED_MARK,
				q->stats.forced_mark))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_GRED_VQ_STAT_PDROP, q->stats.pdrop))
			goto nla_put_failure;

		nla_nest_end(skb, vq);
	}
	nla_nest_end(skb, vqs);

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static void gred_destroy(struct Qdisc *sch)
{
	struct gred_sched *table = qdisc_priv(sch);
	int i;

	for (i = 0; i < table->DPs; i++)
		gred_destroy_vq(table->tab[i]);

	gred_offload(sch, TC_GRED_DESTROY);
	kfree(table->opt);
}

static struct Qdisc_ops gred_qdisc_ops __read_mostly = {
	.id		=	"gred",
	.priv_size	=	sizeof(struct gred_sched),
	.enqueue	=	gred_enqueue,
	.dequeue	=	gred_dequeue,
	.peek		=	qdisc_peek_head,
	.init		=	gred_init,
	.reset		=	gred_reset,
	.destroy	=	gred_destroy,
	.change		=	gred_change,
	.dump		=	gred_dump,
	.owner		=	THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("gred");

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
MODULE_DESCRIPTION("Generic Random Early Detection qdisc");

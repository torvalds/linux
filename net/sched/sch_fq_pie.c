// SPDX-License-Identifier: GPL-2.0-only
/* Flow Queue PIE discipline
 *
 * Copyright (C) 2019 Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 * Copyright (C) 2019 Sachin D. Patil <sdp.sachin@gmail.com>
 * Copyright (C) 2019 V. Saicharan <vsaicharan1998@gmail.com>
 * Copyright (C) 2019 Mohit Bhasi <mohitbhasi1998@gmail.com>
 * Copyright (C) 2019 Leslie Monis <lesliemonis@gmail.com>
 * Copyright (C) 2019 Gautam Ramakrishnan <gautamramk@gmail.com>
 */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/vmalloc.h>
#include <net/pkt_cls.h>
#include <net/pie.h>

/* Flow Queue PIE
 *
 * Principles:
 *   - Packets are classified on flows.
 *   - This is a Stochastic model (as we use a hash, several flows might
 *                                 be hashed to the same slot)
 *   - Each flow has a PIE managed queue.
 *   - Flows are linked onto two (Round Robin) lists,
 *     so that new flows have priority on old ones.
 *   - For a given flow, packets are not reordered.
 *   - Drops during enqueue only.
 *   - ECN capability is off by default.
 *   - ECN threshold (if ECN is enabled) is at 10% by default.
 *   - Uses timestamps to calculate queue delay by default.
 */

/**
 * struct fq_pie_flow - contains data for each flow
 * @vars:	pie vars associated with the flow
 * @deficit:	number of remaining byte credits
 * @backlog:	size of data in the flow
 * @qlen:	number of packets in the flow
 * @flowchain:	flowchain for the flow
 * @head:	first packet in the flow
 * @tail:	last packet in the flow
 */
struct fq_pie_flow {
	struct pie_vars vars;
	s32 deficit;
	u32 backlog;
	u32 qlen;
	struct list_head flowchain;
	struct sk_buff *head;
	struct sk_buff *tail;
};

struct fq_pie_sched_data {
	struct tcf_proto __rcu *filter_list; /* optional external classifier */
	struct tcf_block *block;
	struct fq_pie_flow *flows;
	struct Qdisc *sch;
	struct list_head old_flows;
	struct list_head new_flows;
	struct pie_params p_params;
	u32 ecn_prob;
	u32 flows_cnt;
	u32 flows_cursor;
	u32 quantum;
	u32 memory_limit;
	u32 new_flow_count;
	u32 memory_usage;
	u32 overmemory;
	struct pie_stats stats;
	struct timer_list adapt_timer;
};

static unsigned int fq_pie_hash(const struct fq_pie_sched_data *q,
				struct sk_buff *skb)
{
	return reciprocal_scale(skb_get_hash(skb), q->flows_cnt);
}

static unsigned int fq_pie_classify(struct sk_buff *skb, struct Qdisc *sch,
				    int *qerr)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct tcf_proto *filter;
	struct tcf_result res;
	int result;

	if (TC_H_MAJ(skb->priority) == sch->handle &&
	    TC_H_MIN(skb->priority) > 0 &&
	    TC_H_MIN(skb->priority) <= q->flows_cnt)
		return TC_H_MIN(skb->priority);

	filter = rcu_dereference_bh(q->filter_list);
	if (!filter)
		return fq_pie_hash(q, skb) + 1;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tcf_classify(skb, NULL, filter, &res, false);
	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_STOLEN:
		case TC_ACT_QUEUED:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
		case TC_ACT_SHOT:
			return 0;
		}
#endif
		if (TC_H_MIN(res.classid) <= q->flows_cnt)
			return TC_H_MIN(res.classid);
	}
	return 0;
}

/* add skb to flow queue (tail add) */
static inline void flow_queue_add(struct fq_pie_flow *flow,
				  struct sk_buff *skb)
{
	if (!flow->head)
		flow->head = skb;
	else
		flow->tail->next = skb;
	flow->tail = skb;
	skb->next = NULL;
}

static int fq_pie_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch,
				struct sk_buff **to_free)
{
	enum skb_drop_reason reason = SKB_DROP_REASON_QDISC_OVERLIMIT;
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct fq_pie_flow *sel_flow;
	int ret;
	u8 memory_limited = false;
	u8 enqueue = false;
	u32 pkt_len;
	u32 idx;

	/* Classifies packet into corresponding flow */
	idx = fq_pie_classify(skb, sch, &ret);
	if (idx == 0) {
		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return ret;
	}
	idx--;

	sel_flow = &q->flows[idx];
	/* Checks whether adding a new packet would exceed memory limit */
	get_pie_cb(skb)->mem_usage = skb->truesize;
	memory_limited = q->memory_usage > q->memory_limit + skb->truesize;

	/* Checks if the qdisc is full */
	if (unlikely(qdisc_qlen(sch) >= sch->limit)) {
		q->stats.overlimit++;
		goto out;
	} else if (unlikely(memory_limited)) {
		q->overmemory++;
	}

	reason = SKB_DROP_REASON_QDISC_CONGESTED;

	if (!pie_drop_early(sch, &q->p_params, &sel_flow->vars,
			    sel_flow->backlog, skb->len)) {
		enqueue = true;
	} else if (q->p_params.ecn &&
		   sel_flow->vars.prob <= (MAX_PROB / 100) * q->ecn_prob &&
		   INET_ECN_set_ce(skb)) {
		/* If packet is ecn capable, mark it if drop probability
		 * is lower than the parameter ecn_prob, else drop it.
		 */
		q->stats.ecn_mark++;
		enqueue = true;
	}
	if (enqueue) {
		/* Set enqueue time only when dq_rate_estimator is disabled. */
		if (!q->p_params.dq_rate_estimator)
			pie_set_enqueue_time(skb);

		pkt_len = qdisc_pkt_len(skb);
		q->stats.packets_in++;
		q->memory_usage += skb->truesize;
		sch->qstats.backlog += pkt_len;
		sch->q.qlen++;
		flow_queue_add(sel_flow, skb);
		if (list_empty(&sel_flow->flowchain)) {
			list_add_tail(&sel_flow->flowchain, &q->new_flows);
			q->new_flow_count++;
			sel_flow->deficit = q->quantum;
			sel_flow->qlen = 0;
			sel_flow->backlog = 0;
		}
		sel_flow->qlen++;
		sel_flow->backlog += pkt_len;
		return NET_XMIT_SUCCESS;
	}
out:
	q->stats.dropped++;
	sel_flow->vars.accu_prob = 0;
	qdisc_drop_reason(skb, sch, to_free, reason);
	return NET_XMIT_CN;
}

static const struct netlink_range_validation fq_pie_q_range = {
	.min = 1,
	.max = 1 << 20,
};

static const struct nla_policy fq_pie_policy[TCA_FQ_PIE_MAX + 1] = {
	[TCA_FQ_PIE_LIMIT]		= {.type = NLA_U32},
	[TCA_FQ_PIE_FLOWS]		= {.type = NLA_U32},
	[TCA_FQ_PIE_TARGET]		= {.type = NLA_U32},
	[TCA_FQ_PIE_TUPDATE]		= {.type = NLA_U32},
	[TCA_FQ_PIE_ALPHA]		= {.type = NLA_U32},
	[TCA_FQ_PIE_BETA]		= {.type = NLA_U32},
	[TCA_FQ_PIE_QUANTUM]		=
			NLA_POLICY_FULL_RANGE(NLA_U32, &fq_pie_q_range),
	[TCA_FQ_PIE_MEMORY_LIMIT]	= {.type = NLA_U32},
	[TCA_FQ_PIE_ECN_PROB]		= {.type = NLA_U32},
	[TCA_FQ_PIE_ECN]		= {.type = NLA_U32},
	[TCA_FQ_PIE_BYTEMODE]		= {.type = NLA_U32},
	[TCA_FQ_PIE_DQ_RATE_ESTIMATOR]	= {.type = NLA_U32},
};

static inline struct sk_buff *dequeue_head(struct fq_pie_flow *flow)
{
	struct sk_buff *skb = flow->head;

	flow->head = skb->next;
	skb->next = NULL;
	return skb;
}

static struct sk_buff *fq_pie_qdisc_dequeue(struct Qdisc *sch)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = NULL;
	struct fq_pie_flow *flow;
	struct list_head *head;
	u32 pkt_len;

begin:
	head = &q->new_flows;
	if (list_empty(head)) {
		head = &q->old_flows;
		if (list_empty(head))
			return NULL;
	}

	flow = list_first_entry(head, struct fq_pie_flow, flowchain);
	/* Flow has exhausted all its credits */
	if (flow->deficit <= 0) {
		flow->deficit += q->quantum;
		list_move_tail(&flow->flowchain, &q->old_flows);
		goto begin;
	}

	if (flow->head) {
		skb = dequeue_head(flow);
		pkt_len = qdisc_pkt_len(skb);
		sch->qstats.backlog -= pkt_len;
		sch->q.qlen--;
		qdisc_bstats_update(sch, skb);
	}

	if (!skb) {
		/* force a pass through old_flows to prevent starvation */
		if (head == &q->new_flows && !list_empty(&q->old_flows))
			list_move_tail(&flow->flowchain, &q->old_flows);
		else
			list_del_init(&flow->flowchain);
		goto begin;
	}

	flow->qlen--;
	flow->deficit -= pkt_len;
	flow->backlog -= pkt_len;
	q->memory_usage -= get_pie_cb(skb)->mem_usage;
	pie_process_dequeue(skb, &q->p_params, &flow->vars, flow->backlog);
	return skb;
}

static int fq_pie_change(struct Qdisc *sch, struct nlattr *opt,
			 struct netlink_ext_ack *extack)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_FQ_PIE_MAX + 1];
	unsigned int len_dropped = 0;
	unsigned int num_dropped = 0;
	int err;

	err = nla_parse_nested(tb, TCA_FQ_PIE_MAX, opt, fq_pie_policy, extack);
	if (err < 0)
		return err;

	sch_tree_lock(sch);
	if (tb[TCA_FQ_PIE_LIMIT]) {
		u32 limit = nla_get_u32(tb[TCA_FQ_PIE_LIMIT]);

		WRITE_ONCE(q->p_params.limit, limit);
		WRITE_ONCE(sch->limit, limit);
	}
	if (tb[TCA_FQ_PIE_FLOWS]) {
		if (q->flows) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Number of flows cannot be changed");
			goto flow_error;
		}
		q->flows_cnt = nla_get_u32(tb[TCA_FQ_PIE_FLOWS]);
		if (!q->flows_cnt || q->flows_cnt > 65536) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Number of flows must range in [1..65536]");
			goto flow_error;
		}
	}

	/* convert from microseconds to pschedtime */
	if (tb[TCA_FQ_PIE_TARGET]) {
		/* target is in us */
		u32 target = nla_get_u32(tb[TCA_FQ_PIE_TARGET]);

		/* convert to pschedtime */
		WRITE_ONCE(q->p_params.target,
			   PSCHED_NS2TICKS((u64)target * NSEC_PER_USEC));
	}

	/* tupdate is in jiffies */
	if (tb[TCA_FQ_PIE_TUPDATE])
		WRITE_ONCE(q->p_params.tupdate,
			usecs_to_jiffies(nla_get_u32(tb[TCA_FQ_PIE_TUPDATE])));

	if (tb[TCA_FQ_PIE_ALPHA])
		WRITE_ONCE(q->p_params.alpha,
			   nla_get_u32(tb[TCA_FQ_PIE_ALPHA]));

	if (tb[TCA_FQ_PIE_BETA])
		WRITE_ONCE(q->p_params.beta,
			   nla_get_u32(tb[TCA_FQ_PIE_BETA]));

	if (tb[TCA_FQ_PIE_QUANTUM])
		WRITE_ONCE(q->quantum, nla_get_u32(tb[TCA_FQ_PIE_QUANTUM]));

	if (tb[TCA_FQ_PIE_MEMORY_LIMIT])
		WRITE_ONCE(q->memory_limit,
			   nla_get_u32(tb[TCA_FQ_PIE_MEMORY_LIMIT]));

	if (tb[TCA_FQ_PIE_ECN_PROB])
		WRITE_ONCE(q->ecn_prob,
			   nla_get_u32(tb[TCA_FQ_PIE_ECN_PROB]));

	if (tb[TCA_FQ_PIE_ECN])
		WRITE_ONCE(q->p_params.ecn,
			   nla_get_u32(tb[TCA_FQ_PIE_ECN]));

	if (tb[TCA_FQ_PIE_BYTEMODE])
		WRITE_ONCE(q->p_params.bytemode,
			   nla_get_u32(tb[TCA_FQ_PIE_BYTEMODE]));

	if (tb[TCA_FQ_PIE_DQ_RATE_ESTIMATOR])
		WRITE_ONCE(q->p_params.dq_rate_estimator,
			   nla_get_u32(tb[TCA_FQ_PIE_DQ_RATE_ESTIMATOR]));

	/* Drop excess packets if new limit is lower */
	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = fq_pie_qdisc_dequeue(sch);

		len_dropped += qdisc_pkt_len(skb);
		num_dropped += 1;
		rtnl_kfree_skbs(skb, skb);
	}
	qdisc_tree_reduce_backlog(sch, num_dropped, len_dropped);

	sch_tree_unlock(sch);
	return 0;

flow_error:
	sch_tree_unlock(sch);
	return -EINVAL;
}

static void fq_pie_timer(struct timer_list *t)
{
	struct fq_pie_sched_data *q = from_timer(q, t, adapt_timer);
	unsigned long next, tupdate;
	struct Qdisc *sch = q->sch;
	spinlock_t *root_lock; /* to lock qdisc for probability calculations */
	int max_cnt, i;

	rcu_read_lock();
	root_lock = qdisc_lock(qdisc_root_sleeping(sch));
	spin_lock(root_lock);

	/* Limit this expensive loop to 2048 flows per round. */
	max_cnt = min_t(int, q->flows_cnt - q->flows_cursor, 2048);
	for (i = 0; i < max_cnt; i++) {
		pie_calculate_probability(&q->p_params,
					  &q->flows[q->flows_cursor].vars,
					  q->flows[q->flows_cursor].backlog);
		q->flows_cursor++;
	}

	tupdate = q->p_params.tupdate;
	next = 0;
	if (q->flows_cursor >= q->flows_cnt) {
		q->flows_cursor = 0;
		next = tupdate;
	}
	if (tupdate)
		mod_timer(&q->adapt_timer, jiffies + next);
	spin_unlock(root_lock);
	rcu_read_unlock();
}

static int fq_pie_init(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	int err;
	u32 idx;

	pie_params_init(&q->p_params);
	sch->limit = 10 * 1024;
	q->p_params.limit = sch->limit;
	q->quantum = psched_mtu(qdisc_dev(sch));
	q->sch = sch;
	q->ecn_prob = 10;
	q->flows_cnt = 1024;
	q->memory_limit = SZ_32M;

	INIT_LIST_HEAD(&q->new_flows);
	INIT_LIST_HEAD(&q->old_flows);
	timer_setup(&q->adapt_timer, fq_pie_timer, 0);

	if (opt) {
		err = fq_pie_change(sch, opt, extack);

		if (err)
			return err;
	}

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		goto init_failure;

	q->flows = kvcalloc(q->flows_cnt, sizeof(struct fq_pie_flow),
			    GFP_KERNEL);
	if (!q->flows) {
		err = -ENOMEM;
		goto init_failure;
	}
	for (idx = 0; idx < q->flows_cnt; idx++) {
		struct fq_pie_flow *flow = q->flows + idx;

		INIT_LIST_HEAD(&flow->flowchain);
		pie_vars_init(&flow->vars);
	}

	mod_timer(&q->adapt_timer, jiffies + HZ / 2);

	return 0;

init_failure:
	q->flows_cnt = 0;

	return err;
}

static int fq_pie_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (!opts)
		return -EMSGSIZE;

	/* convert target from pschedtime to us */
	if (nla_put_u32(skb, TCA_FQ_PIE_LIMIT, READ_ONCE(sch->limit)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_FLOWS, READ_ONCE(q->flows_cnt)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_TARGET,
			((u32)PSCHED_TICKS2NS(READ_ONCE(q->p_params.target))) /
			NSEC_PER_USEC) ||
	    nla_put_u32(skb, TCA_FQ_PIE_TUPDATE,
			jiffies_to_usecs(READ_ONCE(q->p_params.tupdate))) ||
	    nla_put_u32(skb, TCA_FQ_PIE_ALPHA, READ_ONCE(q->p_params.alpha)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_BETA, READ_ONCE(q->p_params.beta)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_QUANTUM, READ_ONCE(q->quantum)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_MEMORY_LIMIT,
			READ_ONCE(q->memory_limit)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_ECN_PROB, READ_ONCE(q->ecn_prob)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_ECN, READ_ONCE(q->p_params.ecn)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_BYTEMODE, READ_ONCE(q->p_params.bytemode)) ||
	    nla_put_u32(skb, TCA_FQ_PIE_DQ_RATE_ESTIMATOR,
			READ_ONCE(q->p_params.dq_rate_estimator)))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static int fq_pie_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	struct tc_fq_pie_xstats st = {
		.packets_in	= q->stats.packets_in,
		.overlimit	= q->stats.overlimit,
		.overmemory	= q->overmemory,
		.dropped	= q->stats.dropped,
		.ecn_mark	= q->stats.ecn_mark,
		.new_flow_count = q->new_flow_count,
		.memory_usage   = q->memory_usage,
	};
	struct list_head *pos;

	sch_tree_lock(sch);
	list_for_each(pos, &q->new_flows)
		st.new_flows_len++;

	list_for_each(pos, &q->old_flows)
		st.old_flows_len++;
	sch_tree_unlock(sch);

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static void fq_pie_reset(struct Qdisc *sch)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);
	u32 idx;

	INIT_LIST_HEAD(&q->new_flows);
	INIT_LIST_HEAD(&q->old_flows);
	for (idx = 0; idx < q->flows_cnt; idx++) {
		struct fq_pie_flow *flow = q->flows + idx;

		/* Removes all packets from flow */
		rtnl_kfree_skbs(flow->head, flow->tail);
		flow->head = NULL;

		INIT_LIST_HEAD(&flow->flowchain);
		pie_vars_init(&flow->vars);
	}
}

static void fq_pie_destroy(struct Qdisc *sch)
{
	struct fq_pie_sched_data *q = qdisc_priv(sch);

	tcf_block_put(q->block);
	q->p_params.tupdate = 0;
	del_timer_sync(&q->adapt_timer);
	kvfree(q->flows);
}

static struct Qdisc_ops fq_pie_qdisc_ops __read_mostly = {
	.id		= "fq_pie",
	.priv_size	= sizeof(struct fq_pie_sched_data),
	.enqueue	= fq_pie_qdisc_enqueue,
	.dequeue	= fq_pie_qdisc_dequeue,
	.peek		= qdisc_peek_dequeued,
	.init		= fq_pie_init,
	.destroy	= fq_pie_destroy,
	.reset		= fq_pie_reset,
	.change		= fq_pie_change,
	.dump		= fq_pie_dump,
	.dump_stats	= fq_pie_dump_stats,
	.owner		= THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("fq_pie");

static int __init fq_pie_module_init(void)
{
	return register_qdisc(&fq_pie_qdisc_ops);
}

static void __exit fq_pie_module_exit(void)
{
	unregister_qdisc(&fq_pie_qdisc_ops);
}

module_init(fq_pie_module_init);
module_exit(fq_pie_module_exit);

MODULE_DESCRIPTION("Flow Queue Proportional Integral controller Enhanced (FQ-PIE)");
MODULE_AUTHOR("Mohit P. Tahiliani");
MODULE_LICENSE("GPL");

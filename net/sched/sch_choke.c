// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/sched/sch_choke.c	CHOKE scheduler
 *
 * Copyright (c) 2011 Stephen Hemminger <shemminger@vyatta.com>
 * Copyright (c) 2011 Eric Dumazet <eric.dumazet@gmail.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/inet_ecn.h>
#include <net/red.h>
#include <net/flow_dissector.h>

/*
   CHOKe stateless AQM for fair bandwidth allocation
   =================================================

   CHOKe (CHOose and Keep for responsive flows, CHOose and Kill for
   unresponsive flows) is a variant of RED that penalizes misbehaving flows but
   maintains no flow state. The difference from RED is an additional step
   during the enqueuing process. If average queue size is over the
   low threshold (qmin), a packet is chosen at random from the queue.
   If both the new and chosen packet are from the same flow, both
   are dropped. Unlike RED, CHOKe is not really a "classful" qdisc because it
   needs to access packets in queue randomly. It has a minimal class
   interface to allow overriding the builtin flow classifier with
   filters.

   Source:
   R. Pan, B. Prabhakar, and K. Psounis, "CHOKe, A Stateless
   Active Queue Management Scheme for Approximating Fair Bandwidth Allocation",
   IEEE INFOCOM, 2000.

   A. Tang, J. Wang, S. Low, "Understanding CHOKe: Throughput and Spatial
   Characteristics", IEEE/ACM Transactions on Networking, 2004

 */

/* Upper bound on size of sk_buff table (packets) */
#define CHOKE_MAX_QUEUE	(128*1024 - 1)

struct choke_sched_data {
/* Parameters */
	u32		 limit;
	unsigned char	 flags;

	struct red_parms parms;

/* Variables */
	struct red_vars  vars;
	struct {
		u32	prob_drop;	/* Early probability drops */
		u32	prob_mark;	/* Early probability marks */
		u32	forced_drop;	/* Forced drops, qavg > max_thresh */
		u32	forced_mark;	/* Forced marks, qavg > max_thresh */
		u32	pdrop;          /* Drops due to queue limits */
		u32	other;          /* Drops due to drop() calls */
		u32	matched;	/* Drops to flow match */
	} stats;

	unsigned int	 head;
	unsigned int	 tail;

	unsigned int	 tab_mask; /* size - 1 */

	struct sk_buff **tab;
};

/* number of elements in queue including holes */
static unsigned int choke_len(const struct choke_sched_data *q)
{
	return (q->tail - q->head) & q->tab_mask;
}

/* Is ECN parameter configured */
static int use_ecn(const struct choke_sched_data *q)
{
	return q->flags & TC_RED_ECN;
}

/* Should packets over max just be dropped (versus marked) */
static int use_harddrop(const struct choke_sched_data *q)
{
	return q->flags & TC_RED_HARDDROP;
}

/* Move head pointer forward to skip over holes */
static void choke_zap_head_holes(struct choke_sched_data *q)
{
	do {
		q->head = (q->head + 1) & q->tab_mask;
		if (q->head == q->tail)
			break;
	} while (q->tab[q->head] == NULL);
}

/* Move tail pointer backwards to reuse holes */
static void choke_zap_tail_holes(struct choke_sched_data *q)
{
	do {
		q->tail = (q->tail - 1) & q->tab_mask;
		if (q->head == q->tail)
			break;
	} while (q->tab[q->tail] == NULL);
}

/* Drop packet from queue array by creating a "hole" */
static void choke_drop_by_idx(struct Qdisc *sch, unsigned int idx,
			      struct sk_buff **to_free)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = q->tab[idx];

	q->tab[idx] = NULL;

	if (idx == q->head)
		choke_zap_head_holes(q);
	if (idx == q->tail)
		choke_zap_tail_holes(q);

	qdisc_qstats_backlog_dec(sch, skb);
	qdisc_tree_reduce_backlog(sch, 1, qdisc_pkt_len(skb));
	qdisc_drop(skb, sch, to_free);
	--sch->q.qlen;
}

struct choke_skb_cb {
	u16			classid;
	u8			keys_valid;
	struct			flow_keys_digest keys;
};

static inline struct choke_skb_cb *choke_skb_cb(const struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct choke_skb_cb));
	return (struct choke_skb_cb *)qdisc_skb_cb(skb)->data;
}

static inline void choke_set_classid(struct sk_buff *skb, u16 classid)
{
	choke_skb_cb(skb)->classid = classid;
}

/*
 * Compare flow of two packets
 *  Returns true only if source and destination address and port match.
 *          false for special cases
 */
static bool choke_match_flow(struct sk_buff *skb1,
			     struct sk_buff *skb2)
{
	struct flow_keys temp;

	if (skb1->protocol != skb2->protocol)
		return false;

	if (!choke_skb_cb(skb1)->keys_valid) {
		choke_skb_cb(skb1)->keys_valid = 1;
		skb_flow_dissect_flow_keys(skb1, &temp, 0);
		make_flow_keys_digest(&choke_skb_cb(skb1)->keys, &temp);
	}

	if (!choke_skb_cb(skb2)->keys_valid) {
		choke_skb_cb(skb2)->keys_valid = 1;
		skb_flow_dissect_flow_keys(skb2, &temp, 0);
		make_flow_keys_digest(&choke_skb_cb(skb2)->keys, &temp);
	}

	return !memcmp(&choke_skb_cb(skb1)->keys,
		       &choke_skb_cb(skb2)->keys,
		       sizeof(choke_skb_cb(skb1)->keys));
}

/*
 * Select a packet at random from queue
 * HACK: since queue can have holes from previous deletion; retry several
 *   times to find a random skb but then just give up and return the head
 * Will return NULL if queue is empty (q->head == q->tail)
 */
static struct sk_buff *choke_peek_random(const struct choke_sched_data *q,
					 unsigned int *pidx)
{
	struct sk_buff *skb;
	int retrys = 3;

	do {
		*pidx = (q->head + prandom_u32_max(choke_len(q))) & q->tab_mask;
		skb = q->tab[*pidx];
		if (skb)
			return skb;
	} while (--retrys > 0);

	return q->tab[*pidx = q->head];
}

/*
 * Compare new packet with random packet in queue
 * returns true if matched and sets *pidx
 */
static bool choke_match_random(const struct choke_sched_data *q,
			       struct sk_buff *nskb,
			       unsigned int *pidx)
{
	struct sk_buff *oskb;

	if (q->head == q->tail)
		return false;

	oskb = choke_peek_random(q, pidx);
	return choke_match_flow(oskb, nskb);
}

static int choke_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			 struct sk_buff **to_free)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	const struct red_parms *p = &q->parms;

	choke_skb_cb(skb)->keys_valid = 0;
	/* Compute average queue usage (see RED) */
	q->vars.qavg = red_calc_qavg(p, &q->vars, sch->q.qlen);
	if (red_is_idling(&q->vars))
		red_end_of_idle_period(&q->vars);

	/* Is queue small? */
	if (q->vars.qavg <= p->qth_min)
		q->vars.qcount = -1;
	else {
		unsigned int idx;

		/* Draw a packet at random from queue and compare flow */
		if (choke_match_random(q, skb, &idx)) {
			q->stats.matched++;
			choke_drop_by_idx(sch, idx, to_free);
			goto congestion_drop;
		}

		/* Queue is large, always mark/drop */
		if (q->vars.qavg > p->qth_max) {
			q->vars.qcount = -1;

			qdisc_qstats_overlimit(sch);
			if (use_harddrop(q) || !use_ecn(q) ||
			    !INET_ECN_set_ce(skb)) {
				q->stats.forced_drop++;
				goto congestion_drop;
			}

			q->stats.forced_mark++;
		} else if (++q->vars.qcount) {
			if (red_mark_probability(p, &q->vars, q->vars.qavg)) {
				q->vars.qcount = 0;
				q->vars.qR = red_random(p);

				qdisc_qstats_overlimit(sch);
				if (!use_ecn(q) || !INET_ECN_set_ce(skb)) {
					q->stats.prob_drop++;
					goto congestion_drop;
				}

				q->stats.prob_mark++;
			}
		} else
			q->vars.qR = red_random(p);
	}

	/* Admit new packet */
	if (sch->q.qlen < q->limit) {
		q->tab[q->tail] = skb;
		q->tail = (q->tail + 1) & q->tab_mask;
		++sch->q.qlen;
		qdisc_qstats_backlog_inc(sch, skb);
		return NET_XMIT_SUCCESS;
	}

	q->stats.pdrop++;
	return qdisc_drop(skb, sch, to_free);

congestion_drop:
	qdisc_drop(skb, sch, to_free);
	return NET_XMIT_CN;
}

static struct sk_buff *choke_dequeue(struct Qdisc *sch)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	if (q->head == q->tail) {
		if (!red_is_idling(&q->vars))
			red_start_of_idle_period(&q->vars);
		return NULL;
	}

	skb = q->tab[q->head];
	q->tab[q->head] = NULL;
	choke_zap_head_holes(q);
	--sch->q.qlen;
	qdisc_qstats_backlog_dec(sch, skb);
	qdisc_bstats_update(sch, skb);

	return skb;
}

static void choke_reset(struct Qdisc *sch)
{
	struct choke_sched_data *q = qdisc_priv(sch);

	while (q->head != q->tail) {
		struct sk_buff *skb = q->tab[q->head];

		q->head = (q->head + 1) & q->tab_mask;
		if (!skb)
			continue;
		rtnl_qdisc_drop(skb, sch);
	}

	sch->q.qlen = 0;
	sch->qstats.backlog = 0;
	memset(q->tab, 0, (q->tab_mask + 1) * sizeof(struct sk_buff *));
	q->head = q->tail = 0;
	red_restart(&q->vars);
}

static const struct nla_policy choke_policy[TCA_CHOKE_MAX + 1] = {
	[TCA_CHOKE_PARMS]	= { .len = sizeof(struct tc_red_qopt) },
	[TCA_CHOKE_STAB]	= { .len = RED_STAB_SIZE },
	[TCA_CHOKE_MAX_P]	= { .type = NLA_U32 },
};


static void choke_free(void *addr)
{
	kvfree(addr);
}

static int choke_change(struct Qdisc *sch, struct nlattr *opt,
			struct netlink_ext_ack *extack)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_CHOKE_MAX + 1];
	const struct tc_red_qopt *ctl;
	int err;
	struct sk_buff **old = NULL;
	unsigned int mask;
	u32 max_P;

	if (opt == NULL)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, TCA_CHOKE_MAX, opt,
					  choke_policy, NULL);
	if (err < 0)
		return err;

	if (tb[TCA_CHOKE_PARMS] == NULL ||
	    tb[TCA_CHOKE_STAB] == NULL)
		return -EINVAL;

	max_P = tb[TCA_CHOKE_MAX_P] ? nla_get_u32(tb[TCA_CHOKE_MAX_P]) : 0;

	ctl = nla_data(tb[TCA_CHOKE_PARMS]);

	if (!red_check_params(ctl->qth_min, ctl->qth_max, ctl->Wlog))
		return -EINVAL;

	if (ctl->limit > CHOKE_MAX_QUEUE)
		return -EINVAL;

	mask = roundup_pow_of_two(ctl->limit + 1) - 1;
	if (mask != q->tab_mask) {
		struct sk_buff **ntab;

		ntab = kvmalloc_array((mask + 1), sizeof(struct sk_buff *), GFP_KERNEL | __GFP_ZERO);
		if (!ntab)
			return -ENOMEM;

		sch_tree_lock(sch);
		old = q->tab;
		if (old) {
			unsigned int oqlen = sch->q.qlen, tail = 0;
			unsigned dropped = 0;

			while (q->head != q->tail) {
				struct sk_buff *skb = q->tab[q->head];

				q->head = (q->head + 1) & q->tab_mask;
				if (!skb)
					continue;
				if (tail < mask) {
					ntab[tail++] = skb;
					continue;
				}
				dropped += qdisc_pkt_len(skb);
				qdisc_qstats_backlog_dec(sch, skb);
				--sch->q.qlen;
				rtnl_qdisc_drop(skb, sch);
			}
			qdisc_tree_reduce_backlog(sch, oqlen - sch->q.qlen, dropped);
			q->head = 0;
			q->tail = tail;
		}

		q->tab_mask = mask;
		q->tab = ntab;
	} else
		sch_tree_lock(sch);

	q->flags = ctl->flags;
	q->limit = ctl->limit;

	red_set_parms(&q->parms, ctl->qth_min, ctl->qth_max, ctl->Wlog,
		      ctl->Plog, ctl->Scell_log,
		      nla_data(tb[TCA_CHOKE_STAB]),
		      max_P);
	red_set_vars(&q->vars);

	if (q->head == q->tail)
		red_end_of_idle_period(&q->vars);

	sch_tree_unlock(sch);
	choke_free(old);
	return 0;
}

static int choke_init(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	return choke_change(sch, opt, extack);
}

static int choke_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts = NULL;
	struct tc_red_qopt opt = {
		.limit		= q->limit,
		.flags		= q->flags,
		.qth_min	= q->parms.qth_min >> q->parms.Wlog,
		.qth_max	= q->parms.qth_max >> q->parms.Wlog,
		.Wlog		= q->parms.Wlog,
		.Plog		= q->parms.Plog,
		.Scell_log	= q->parms.Scell_log,
	};

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	if (nla_put(skb, TCA_CHOKE_PARMS, sizeof(opt), &opt) ||
	    nla_put_u32(skb, TCA_CHOKE_MAX_P, q->parms.max_P))
		goto nla_put_failure;
	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static int choke_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct choke_sched_data *q = qdisc_priv(sch);
	struct tc_choke_xstats st = {
		.early	= q->stats.prob_drop + q->stats.forced_drop,
		.marked	= q->stats.prob_mark + q->stats.forced_mark,
		.pdrop	= q->stats.pdrop,
		.other	= q->stats.other,
		.matched = q->stats.matched,
	};

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static void choke_destroy(struct Qdisc *sch)
{
	struct choke_sched_data *q = qdisc_priv(sch);

	choke_free(q->tab);
}

static struct sk_buff *choke_peek_head(struct Qdisc *sch)
{
	struct choke_sched_data *q = qdisc_priv(sch);

	return (q->head != q->tail) ? q->tab[q->head] : NULL;
}

static struct Qdisc_ops choke_qdisc_ops __read_mostly = {
	.id		=	"choke",
	.priv_size	=	sizeof(struct choke_sched_data),

	.enqueue	=	choke_enqueue,
	.dequeue	=	choke_dequeue,
	.peek		=	choke_peek_head,
	.init		=	choke_init,
	.destroy	=	choke_destroy,
	.reset		=	choke_reset,
	.change		=	choke_change,
	.dump		=	choke_dump,
	.dump_stats	=	choke_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init choke_module_init(void)
{
	return register_qdisc(&choke_qdisc_ops);
}

static void __exit choke_module_exit(void)
{
	unregister_qdisc(&choke_qdisc_ops);
}

module_init(choke_module_init)
module_exit(choke_module_exit)

MODULE_LICENSE("GPL");

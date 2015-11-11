/*
 * Codel - The Controlled-Delay Active Queue Management algorithm
 *
 *  Copyright (C) 2011-2012 Kathleen Nichols <nichols@pollere.com>
 *  Copyright (C) 2011-2012 Van Jacobson <van@pollere.net>
 *
 *  Implemented on linux by :
 *  Copyright (C) 2012 Michael D. Taht <dave.taht@bufferbloat.net>
 *  Copyright (C) 2012,2015 Eric Dumazet <edumazet@google.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/prefetch.h>
#include <net/pkt_sched.h>
#include <net/codel.h>


#define DEFAULT_CODEL_LIMIT 1000

struct codel_sched_data {
	struct codel_params	params;
	struct codel_vars	vars;
	struct codel_stats	stats;
	u32			drop_overlimit;
};

/* This is the specific function called from codel_dequeue()
 * to dequeue a packet from queue. Note: backlog is handled in
 * codel, we dont need to reduce it here.
 */
static struct sk_buff *dequeue(struct codel_vars *vars, struct Qdisc *sch)
{
	struct sk_buff *skb = __skb_dequeue(&sch->q);

	prefetch(&skb->end); /* we'll need skb_shinfo() */
	return skb;
}

static struct sk_buff *codel_qdisc_dequeue(struct Qdisc *sch)
{
	struct codel_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	skb = codel_dequeue(sch, &q->params, &q->vars, &q->stats, dequeue);

	/* We cant call qdisc_tree_decrease_qlen() if our qlen is 0,
	 * or HTB crashes. Defer it for next round.
	 */
	if (q->stats.drop_count && sch->q.qlen) {
		qdisc_tree_decrease_qlen(sch, q->stats.drop_count);
		q->stats.drop_count = 0;
	}
	if (skb)
		qdisc_bstats_update(sch, skb);
	return skb;
}

static int codel_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct codel_sched_data *q;

	if (likely(qdisc_qlen(sch) < sch->limit)) {
		codel_set_enqueue_time(skb);
		return qdisc_enqueue_tail(skb, sch);
	}
	q = qdisc_priv(sch);
	q->drop_overlimit++;
	return qdisc_drop(skb, sch);
}

static const struct nla_policy codel_policy[TCA_CODEL_MAX + 1] = {
	[TCA_CODEL_TARGET]	= { .type = NLA_U32 },
	[TCA_CODEL_LIMIT]	= { .type = NLA_U32 },
	[TCA_CODEL_INTERVAL]	= { .type = NLA_U32 },
	[TCA_CODEL_ECN]		= { .type = NLA_U32 },
	[TCA_CODEL_CE_THRESHOLD]= { .type = NLA_U32 },
};

static int codel_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct codel_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_CODEL_MAX + 1];
	unsigned int qlen;
	int err;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_CODEL_MAX, opt, codel_policy);
	if (err < 0)
		return err;

	sch_tree_lock(sch);

	if (tb[TCA_CODEL_TARGET]) {
		u32 target = nla_get_u32(tb[TCA_CODEL_TARGET]);

		q->params.target = ((u64)target * NSEC_PER_USEC) >> CODEL_SHIFT;
	}

	if (tb[TCA_CODEL_CE_THRESHOLD]) {
		u64 val = nla_get_u32(tb[TCA_CODEL_CE_THRESHOLD]);

		q->params.ce_threshold = (val * NSEC_PER_USEC) >> CODEL_SHIFT;
	}

	if (tb[TCA_CODEL_INTERVAL]) {
		u32 interval = nla_get_u32(tb[TCA_CODEL_INTERVAL]);

		q->params.interval = ((u64)interval * NSEC_PER_USEC) >> CODEL_SHIFT;
	}

	if (tb[TCA_CODEL_LIMIT])
		sch->limit = nla_get_u32(tb[TCA_CODEL_LIMIT]);

	if (tb[TCA_CODEL_ECN])
		q->params.ecn = !!nla_get_u32(tb[TCA_CODEL_ECN]);

	qlen = sch->q.qlen;
	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = __skb_dequeue(&sch->q);

		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_drop(skb, sch);
	}
	qdisc_tree_decrease_qlen(sch, qlen - sch->q.qlen);

	sch_tree_unlock(sch);
	return 0;
}

static int codel_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct codel_sched_data *q = qdisc_priv(sch);

	sch->limit = DEFAULT_CODEL_LIMIT;

	codel_params_init(&q->params, sch);
	codel_vars_init(&q->vars);
	codel_stats_init(&q->stats);

	if (opt) {
		int err = codel_change(sch, opt);

		if (err)
			return err;
	}

	if (sch->limit >= 1)
		sch->flags |= TCQ_F_CAN_BYPASS;
	else
		sch->flags &= ~TCQ_F_CAN_BYPASS;

	return 0;
}

static int codel_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct codel_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CODEL_TARGET,
			codel_time_to_us(q->params.target)) ||
	    nla_put_u32(skb, TCA_CODEL_LIMIT,
			sch->limit) ||
	    nla_put_u32(skb, TCA_CODEL_INTERVAL,
			codel_time_to_us(q->params.interval)) ||
	    nla_put_u32(skb, TCA_CODEL_ECN,
			q->params.ecn))
		goto nla_put_failure;
	if (q->params.ce_threshold != CODEL_DISABLED_THRESHOLD &&
	    nla_put_u32(skb, TCA_CODEL_CE_THRESHOLD,
			codel_time_to_us(q->params.ce_threshold)))
		goto nla_put_failure;
	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -1;
}

static int codel_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	const struct codel_sched_data *q = qdisc_priv(sch);
	struct tc_codel_xstats st = {
		.maxpacket	= q->stats.maxpacket,
		.count		= q->vars.count,
		.lastcount	= q->vars.lastcount,
		.drop_overlimit = q->drop_overlimit,
		.ldelay		= codel_time_to_us(q->vars.ldelay),
		.dropping	= q->vars.dropping,
		.ecn_mark	= q->stats.ecn_mark,
		.ce_mark	= q->stats.ce_mark,
	};

	if (q->vars.dropping) {
		codel_tdiff_t delta = q->vars.drop_next - codel_get_time();

		if (delta >= 0)
			st.drop_next = codel_time_to_us(delta);
		else
			st.drop_next = -codel_time_to_us(-delta);
	}

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static void codel_reset(struct Qdisc *sch)
{
	struct codel_sched_data *q = qdisc_priv(sch);

	qdisc_reset_queue(sch);
	codel_vars_init(&q->vars);
}

static struct Qdisc_ops codel_qdisc_ops __read_mostly = {
	.id		=	"codel",
	.priv_size	=	sizeof(struct codel_sched_data),

	.enqueue	=	codel_qdisc_enqueue,
	.dequeue	=	codel_qdisc_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	codel_init,
	.reset		=	codel_reset,
	.change 	=	codel_change,
	.dump		=	codel_dump,
	.dump_stats	=	codel_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init codel_module_init(void)
{
	return register_qdisc(&codel_qdisc_ops);
}

static void __exit codel_module_exit(void)
{
	unregister_qdisc(&codel_qdisc_ops);
}

module_init(codel_module_init)
module_exit(codel_module_exit)

MODULE_DESCRIPTION("Controlled Delay queue discipline");
MODULE_AUTHOR("Dave Taht");
MODULE_AUTHOR("Eric Dumazet");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * net/sched/sch_tbf.c	Token Bucket Filter queue.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *		Dmitry Torokhov <dtor@mail.ru> - allow attaching inner qdiscs -
 *						 original idea by Martin Devera
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>


/*	Simple Token Bucket Filter.
	=======================================

	SOURCE.
	-------

	None.

	Description.
	------------

	A data flow obeys TBF with rate R and depth B, if for any
	time interval t_i...t_f the number of transmitted bits
	does not exceed B + R*(t_f-t_i).

	Packetized version of this definition:
	The sequence of packets of sizes s_i served at moments t_i
	obeys TBF, if for any i<=k:

	s_i+....+s_k <= B + R*(t_k - t_i)

	Algorithm.
	----------

	Let N(t_i) be B/R initially and N(t) grow continuously with time as:

	N(t+delta) = min{B/R, N(t) + delta}

	If the first packet in queue has length S, it may be
	transmitted only at the time t_* when S/R <= N(t_*),
	and in this case N(t) jumps:

	N(t_* + 0) = N(t_* - 0) - S/R.



	Actually, QoS requires two TBF to be applied to a data stream.
	One of them controls steady state burst size, another
	one with rate P (peak rate) and depth M (equal to link MTU)
	limits bursts at a smaller time scale.

	It is easy to see that P>R, and B>M. If P is infinity, this double
	TBF is equivalent to a single one.

	When TBF works in reshaping mode, latency is estimated as:

	lat = max ((L-B)/R, (L-M)/P)


	NOTES.
	------

	If TBF throttles, it starts a watchdog timer, which will wake it up
	when it is ready to transmit.
	Note that the minimal timer resolution is 1/HZ.
	If no new packets arrive during this period,
	or if the device is not awaken by EOI for some previous packet,
	TBF can stop its activity for 1/HZ.


	This means, that with depth B, the maximal rate is

	R_crit = B*HZ

	F.e. for 10Mbit ethernet and HZ=100 the minimal allowed B is ~10Kbytes.

	Note that the peak rate TBF is much more tough: with MTU 1500
	P_crit = 150Kbytes/sec. So, if you need greater peak
	rates, use alpha with HZ=1000 :-)

	With classful TBF, limit is just kept for backwards compatibility.
	It is passed to the default bfifo qdisc - if the inner qdisc is
	changed the limit is not effective anymore.
*/

struct tbf_sched_data {
/* Parameters */
	u32		limit;		/* Maximal length of backlog: bytes */
	u32		max_size;
	s64		buffer;		/* Token bucket depth/rate: MUST BE >= MTU/B */
	s64		mtu;
	struct psched_ratecfg rate;
	struct psched_ratecfg peak;

/* Variables */
	s64	tokens;			/* Current number of B tokens */
	s64	ptokens;		/* Current number of P tokens */
	s64	t_c;			/* Time check-point */
	struct Qdisc	*qdisc;		/* Inner qdisc, default - bfifo queue */
	struct qdisc_watchdog watchdog;	/* Watchdog timer */
};


/* Time to Length, convert time in ns to length in bytes
 * to determinate how many bytes can be sent in given time.
 */
static u64 psched_ns_t2l(const struct psched_ratecfg *r,
			 u64 time_in_ns)
{
	/* The formula is :
	 * len = (time_in_ns * r->rate_bytes_ps) / NSEC_PER_SEC
	 */
	u64 len = time_in_ns * r->rate_bytes_ps;

	do_div(len, NSEC_PER_SEC);

	if (unlikely(r->linklayer == TC_LINKLAYER_ATM)) {
		do_div(len, 53);
		len = len * 48;
	}

	if (len > r->overhead)
		len -= r->overhead;
	else
		len = 0;

	return len;
}

/*
 * Return length of individual segments of a gso packet,
 * including all headers (MAC, IP, TCP/UDP)
 */
static unsigned int skb_gso_mac_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) - skb_mac_header(skb);
	return hdr_len + skb_gso_transport_seglen(skb);
}

/* GSO packet is too big, segment it so that tbf can transmit
 * each segment in time
 */
static int tbf_segment(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	struct tbf_sched_data *q = qdisc_priv(sch);
	struct sk_buff *segs, *nskb;
	netdev_features_t features = netif_skb_features(skb);
	unsigned int len = 0, prev_len = qdisc_pkt_len(skb);
	int ret, nb;

	segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);

	if (IS_ERR_OR_NULL(segs))
		return qdisc_drop(skb, sch, to_free);

	nb = 0;
	while (segs) {
		nskb = segs->next;
		segs->next = NULL;
		qdisc_skb_cb(segs)->pkt_len = segs->len;
		len += segs->len;
		ret = qdisc_enqueue(segs, q->qdisc, to_free);
		if (ret != NET_XMIT_SUCCESS) {
			if (net_xmit_drop_count(ret))
				qdisc_qstats_drop(sch);
		} else {
			nb++;
		}
		segs = nskb;
	}
	sch->q.qlen += nb;
	if (nb > 1)
		qdisc_tree_reduce_backlog(sch, 1 - nb, prev_len - len);
	consume_skb(skb);
	return nb > 0 ? NET_XMIT_SUCCESS : NET_XMIT_DROP;
}

static int tbf_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	struct tbf_sched_data *q = qdisc_priv(sch);
	int ret;

	if (qdisc_pkt_len(skb) > q->max_size) {
		if (skb_is_gso(skb) && skb_gso_mac_seglen(skb) <= q->max_size)
			return tbf_segment(skb, sch, to_free);
		return qdisc_drop(skb, sch, to_free);
	}
	ret = qdisc_enqueue(skb, q->qdisc, to_free);
	if (ret != NET_XMIT_SUCCESS) {
		if (net_xmit_drop_count(ret))
			qdisc_qstats_drop(sch);
		return ret;
	}

	qdisc_qstats_backlog_inc(sch, skb);
	sch->q.qlen++;
	return NET_XMIT_SUCCESS;
}

static bool tbf_peak_present(const struct tbf_sched_data *q)
{
	return q->peak.rate_bytes_ps;
}

static struct sk_buff *tbf_dequeue(struct Qdisc *sch)
{
	struct tbf_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	skb = q->qdisc->ops->peek(q->qdisc);

	if (skb) {
		s64 now;
		s64 toks;
		s64 ptoks = 0;
		unsigned int len = qdisc_pkt_len(skb);

		now = ktime_get_ns();
		toks = min_t(s64, now - q->t_c, q->buffer);

		if (tbf_peak_present(q)) {
			ptoks = toks + q->ptokens;
			if (ptoks > q->mtu)
				ptoks = q->mtu;
			ptoks -= (s64) psched_l2t_ns(&q->peak, len);
		}
		toks += q->tokens;
		if (toks > q->buffer)
			toks = q->buffer;
		toks -= (s64) psched_l2t_ns(&q->rate, len);

		if ((toks|ptoks) >= 0) {
			skb = qdisc_dequeue_peeked(q->qdisc);
			if (unlikely(!skb))
				return NULL;

			q->t_c = now;
			q->tokens = toks;
			q->ptokens = ptoks;
			qdisc_qstats_backlog_dec(sch, skb);
			sch->q.qlen--;
			qdisc_bstats_update(sch, skb);
			return skb;
		}

		qdisc_watchdog_schedule_ns(&q->watchdog,
					   now + max_t(long, -toks, -ptoks));

		/* Maybe we have a shorter packet in the queue,
		   which can be sent now. It sounds cool,
		   but, however, this is wrong in principle.
		   We MUST NOT reorder packets under these circumstances.

		   Really, if we split the flow into independent
		   subflows, it would be a very good solution.
		   This is the main idea of all FQ algorithms
		   (cf. CSZ, HPFQ, HFSC)
		 */

		qdisc_qstats_overlimit(sch);
	}
	return NULL;
}

static void tbf_reset(struct Qdisc *sch)
{
	struct tbf_sched_data *q = qdisc_priv(sch);

	qdisc_reset(q->qdisc);
	sch->qstats.backlog = 0;
	sch->q.qlen = 0;
	q->t_c = ktime_get_ns();
	q->tokens = q->buffer;
	q->ptokens = q->mtu;
	qdisc_watchdog_cancel(&q->watchdog);
}

static const struct nla_policy tbf_policy[TCA_TBF_MAX + 1] = {
	[TCA_TBF_PARMS]	= { .len = sizeof(struct tc_tbf_qopt) },
	[TCA_TBF_RTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_TBF_PTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_TBF_RATE64]	= { .type = NLA_U64 },
	[TCA_TBF_PRATE64]	= { .type = NLA_U64 },
	[TCA_TBF_BURST] = { .type = NLA_U32 },
	[TCA_TBF_PBURST] = { .type = NLA_U32 },
};

static int tbf_change(struct Qdisc *sch, struct nlattr *opt)
{
	int err;
	struct tbf_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_TBF_MAX + 1];
	struct tc_tbf_qopt *qopt;
	struct Qdisc *child = NULL;
	struct psched_ratecfg rate;
	struct psched_ratecfg peak;
	u64 max_size;
	s64 buffer, mtu;
	u64 rate64 = 0, prate64 = 0;

	err = nla_parse_nested(tb, TCA_TBF_MAX, opt, tbf_policy, NULL);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (tb[TCA_TBF_PARMS] == NULL)
		goto done;

	qopt = nla_data(tb[TCA_TBF_PARMS]);
	if (qopt->rate.linklayer == TC_LINKLAYER_UNAWARE)
		qdisc_put_rtab(qdisc_get_rtab(&qopt->rate,
					      tb[TCA_TBF_RTAB]));

	if (qopt->peakrate.linklayer == TC_LINKLAYER_UNAWARE)
			qdisc_put_rtab(qdisc_get_rtab(&qopt->peakrate,
						      tb[TCA_TBF_PTAB]));

	buffer = min_t(u64, PSCHED_TICKS2NS(qopt->buffer), ~0U);
	mtu = min_t(u64, PSCHED_TICKS2NS(qopt->mtu), ~0U);

	if (tb[TCA_TBF_RATE64])
		rate64 = nla_get_u64(tb[TCA_TBF_RATE64]);
	psched_ratecfg_precompute(&rate, &qopt->rate, rate64);

	if (tb[TCA_TBF_BURST]) {
		max_size = nla_get_u32(tb[TCA_TBF_BURST]);
		buffer = psched_l2t_ns(&rate, max_size);
	} else {
		max_size = min_t(u64, psched_ns_t2l(&rate, buffer), ~0U);
	}

	if (qopt->peakrate.rate) {
		if (tb[TCA_TBF_PRATE64])
			prate64 = nla_get_u64(tb[TCA_TBF_PRATE64]);
		psched_ratecfg_precompute(&peak, &qopt->peakrate, prate64);
		if (peak.rate_bytes_ps <= rate.rate_bytes_ps) {
			pr_warn_ratelimited("sch_tbf: peakrate %llu is lower than or equals to rate %llu !\n",
					peak.rate_bytes_ps, rate.rate_bytes_ps);
			err = -EINVAL;
			goto done;
		}

		if (tb[TCA_TBF_PBURST]) {
			u32 pburst = nla_get_u32(tb[TCA_TBF_PBURST]);
			max_size = min_t(u32, max_size, pburst);
			mtu = psched_l2t_ns(&peak, pburst);
		} else {
			max_size = min_t(u64, max_size, psched_ns_t2l(&peak, mtu));
		}
	} else {
		memset(&peak, 0, sizeof(peak));
	}

	if (max_size < psched_mtu(qdisc_dev(sch)))
		pr_warn_ratelimited("sch_tbf: burst %llu is lower than device %s mtu (%u) !\n",
				    max_size, qdisc_dev(sch)->name,
				    psched_mtu(qdisc_dev(sch)));

	if (!max_size) {
		err = -EINVAL;
		goto done;
	}

	if (q->qdisc != &noop_qdisc) {
		err = fifo_set_limit(q->qdisc, qopt->limit);
		if (err)
			goto done;
	} else if (qopt->limit > 0) {
		child = fifo_create_dflt(sch, &bfifo_qdisc_ops, qopt->limit);
		if (IS_ERR(child)) {
			err = PTR_ERR(child);
			goto done;
		}
	}

	sch_tree_lock(sch);
	if (child) {
		qdisc_tree_reduce_backlog(q->qdisc, q->qdisc->q.qlen,
					  q->qdisc->qstats.backlog);
		qdisc_destroy(q->qdisc);
		q->qdisc = child;
		if (child != &noop_qdisc)
			qdisc_hash_add(child, true);
	}
	q->limit = qopt->limit;
	if (tb[TCA_TBF_PBURST])
		q->mtu = mtu;
	else
		q->mtu = PSCHED_TICKS2NS(qopt->mtu);
	q->max_size = max_size;
	if (tb[TCA_TBF_BURST])
		q->buffer = buffer;
	else
		q->buffer = PSCHED_TICKS2NS(qopt->buffer);
	q->tokens = q->buffer;
	q->ptokens = q->mtu;

	memcpy(&q->rate, &rate, sizeof(struct psched_ratecfg));
	memcpy(&q->peak, &peak, sizeof(struct psched_ratecfg));

	sch_tree_unlock(sch);
	err = 0;
done:
	return err;
}

static int tbf_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct tbf_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_init(&q->watchdog, sch);
	q->qdisc = &noop_qdisc;

	if (opt == NULL)
		return -EINVAL;

	q->t_c = ktime_get_ns();

	return tbf_change(sch, opt);
}

static void tbf_destroy(struct Qdisc *sch)
{
	struct tbf_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	qdisc_destroy(q->qdisc);
}

static int tbf_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct tbf_sched_data *q = qdisc_priv(sch);
	struct nlattr *nest;
	struct tc_tbf_qopt opt;

	sch->qstats.backlog = q->qdisc->qstats.backlog;
	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	opt.limit = q->limit;
	psched_ratecfg_getrate(&opt.rate, &q->rate);
	if (tbf_peak_present(q))
		psched_ratecfg_getrate(&opt.peakrate, &q->peak);
	else
		memset(&opt.peakrate, 0, sizeof(opt.peakrate));
	opt.mtu = PSCHED_NS2TICKS(q->mtu);
	opt.buffer = PSCHED_NS2TICKS(q->buffer);
	if (nla_put(skb, TCA_TBF_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;
	if (q->rate.rate_bytes_ps >= (1ULL << 32) &&
	    nla_put_u64_64bit(skb, TCA_TBF_RATE64, q->rate.rate_bytes_ps,
			      TCA_TBF_PAD))
		goto nla_put_failure;
	if (tbf_peak_present(q) &&
	    q->peak.rate_bytes_ps >= (1ULL << 32) &&
	    nla_put_u64_64bit(skb, TCA_TBF_PRATE64, q->peak.rate_bytes_ps,
			      TCA_TBF_PAD))
		goto nla_put_failure;

	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int tbf_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct tbf_sched_data *q = qdisc_priv(sch);

	tcm->tcm_handle |= TC_H_MIN(1);
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int tbf_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old)
{
	struct tbf_sched_data *q = qdisc_priv(sch);

	if (new == NULL)
		new = &noop_qdisc;

	*old = qdisc_replace(sch, new, &q->qdisc);
	return 0;
}

static struct Qdisc *tbf_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct tbf_sched_data *q = qdisc_priv(sch);
	return q->qdisc;
}

static unsigned long tbf_get(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void tbf_put(struct Qdisc *sch, unsigned long arg)
{
}

static void tbf_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	if (!walker->stop) {
		if (walker->count >= walker->skip)
			if (walker->fn(sch, 1, walker) < 0) {
				walker->stop = 1;
				return;
			}
		walker->count++;
	}
}

static const struct Qdisc_class_ops tbf_class_ops = {
	.graft		=	tbf_graft,
	.leaf		=	tbf_leaf,
	.get		=	tbf_get,
	.put		=	tbf_put,
	.walk		=	tbf_walk,
	.dump		=	tbf_dump_class,
};

static struct Qdisc_ops tbf_qdisc_ops __read_mostly = {
	.next		=	NULL,
	.cl_ops		=	&tbf_class_ops,
	.id		=	"tbf",
	.priv_size	=	sizeof(struct tbf_sched_data),
	.enqueue	=	tbf_enqueue,
	.dequeue	=	tbf_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	tbf_init,
	.reset		=	tbf_reset,
	.destroy	=	tbf_destroy,
	.change		=	tbf_change,
	.dump		=	tbf_dump,
	.owner		=	THIS_MODULE,
};

static int __init tbf_module_init(void)
{
	return register_qdisc(&tbf_qdisc_ops);
}

static void __exit tbf_module_exit(void)
{
	unregister_qdisc(&tbf_qdisc_ops);
}
module_init(tbf_module_init)
module_exit(tbf_module_exit)
MODULE_LICENSE("GPL");

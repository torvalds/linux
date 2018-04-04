/*
 * net/sched/sch_cbs.c	Credit Based Shaper
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Vinicius Costa Gomes <vinicius.gomes@intel.com>
 *
 */

/* Credit Based Shaper (CBS)
 * =========================
 *
 * This is a simple rate-limiting shaper aimed at TSN applications on
 * systems with known traffic workloads.
 *
 * Its algorithm is defined by the IEEE 802.1Q-2014 Specification,
 * Section 8.6.8.2, and explained in more detail in the Annex L of the
 * same specification.
 *
 * There are four tunables to be considered:
 *
 *	'idleslope': Idleslope is the rate of credits that is
 *	accumulated (in kilobits per second) when there is at least
 *	one packet waiting for transmission. Packets are transmitted
 *	when the current value of credits is equal or greater than
 *	zero. When there is no packet to be transmitted the amount of
 *	credits is set to zero. This is the main tunable of the CBS
 *	algorithm.
 *
 *	'sendslope':
 *	Sendslope is the rate of credits that is depleted (it should be a
 *	negative number of kilobits per second) when a transmission is
 *	ocurring. It can be calculated as follows, (IEEE 802.1Q-2014 Section
 *	8.6.8.2 item g):
 *
 *	sendslope = idleslope - port_transmit_rate
 *
 *	'hicredit': Hicredit defines the maximum amount of credits (in
 *	bytes) that can be accumulated. Hicredit depends on the
 *	characteristics of interfering traffic,
 *	'max_interference_size' is the maximum size of any burst of
 *	traffic that can delay the transmission of a frame that is
 *	available for transmission for this traffic class, (IEEE
 *	802.1Q-2014 Annex L, Equation L-3):
 *
 *	hicredit = max_interference_size * (idleslope / port_transmit_rate)
 *
 *	'locredit': Locredit is the minimum amount of credits that can
 *	be reached. It is a function of the traffic flowing through
 *	this qdisc (IEEE 802.1Q-2014 Annex L, Equation L-2):
 *
 *	locredit = max_frame_size * (sendslope / port_transmit_rate)
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

#define BYTES_PER_KBIT (1000LL / 8)

struct cbs_sched_data {
	bool offload;
	int queue;
	s64 port_rate; /* in bytes/s */
	s64 last; /* timestamp in ns */
	s64 credits; /* in bytes */
	s32 locredit; /* in bytes */
	s32 hicredit; /* in bytes */
	s64 sendslope; /* in bytes/s */
	s64 idleslope; /* in bytes/s */
	struct qdisc_watchdog watchdog;
	int (*enqueue)(struct sk_buff *skb, struct Qdisc *sch);
	struct sk_buff *(*dequeue)(struct Qdisc *sch);
};

static int cbs_enqueue_offload(struct sk_buff *skb, struct Qdisc *sch)
{
	return qdisc_enqueue_tail(skb, sch);
}

static int cbs_enqueue_soft(struct sk_buff *skb, struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	if (sch->q.qlen == 0 && q->credits > 0) {
		/* We need to stop accumulating credits when there's
		 * no enqueued packets and q->credits is positive.
		 */
		q->credits = 0;
		q->last = ktime_get_ns();
	}

	return qdisc_enqueue_tail(skb, sch);
}

static int cbs_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	return q->enqueue(skb, sch);
}

/* timediff is in ns, slope is in bytes/s */
static s64 timediff_to_credits(s64 timediff, s64 slope)
{
	return div64_s64(timediff * slope, NSEC_PER_SEC);
}

static s64 delay_from_credits(s64 credits, s64 slope)
{
	if (unlikely(slope == 0))
		return S64_MAX;

	return div64_s64(-credits * NSEC_PER_SEC, slope);
}

static s64 credits_from_len(unsigned int len, s64 slope, s64 port_rate)
{
	if (unlikely(port_rate == 0))
		return S64_MAX;

	return div64_s64(len * slope, port_rate);
}

static struct sk_buff *cbs_dequeue_soft(struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	s64 now = ktime_get_ns();
	struct sk_buff *skb;
	s64 credits;
	int len;

	if (q->credits < 0) {
		credits = timediff_to_credits(now - q->last, q->idleslope);

		credits = q->credits + credits;
		q->credits = min_t(s64, credits, q->hicredit);

		if (q->credits < 0) {
			s64 delay;

			delay = delay_from_credits(q->credits, q->idleslope);
			qdisc_watchdog_schedule_ns(&q->watchdog, now + delay);

			q->last = now;

			return NULL;
		}
	}

	skb = qdisc_dequeue_head(sch);
	if (!skb)
		return NULL;

	len = qdisc_pkt_len(skb);

	/* As sendslope is a negative number, this will decrease the
	 * amount of q->credits.
	 */
	credits = credits_from_len(len, q->sendslope, q->port_rate);
	credits += q->credits;

	q->credits = max_t(s64, credits, q->locredit);
	q->last = now;

	return skb;
}

static struct sk_buff *cbs_dequeue_offload(struct Qdisc *sch)
{
	return qdisc_dequeue_head(sch);
}

static struct sk_buff *cbs_dequeue(struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	return q->dequeue(sch);
}

static const struct nla_policy cbs_policy[TCA_CBS_MAX + 1] = {
	[TCA_CBS_PARMS]	= { .len = sizeof(struct tc_cbs_qopt) },
};

static void cbs_disable_offload(struct net_device *dev,
				struct cbs_sched_data *q)
{
	struct tc_cbs_qopt_offload cbs = { };
	const struct net_device_ops *ops;
	int err;

	if (!q->offload)
		return;

	q->enqueue = cbs_enqueue_soft;
	q->dequeue = cbs_dequeue_soft;

	ops = dev->netdev_ops;
	if (!ops->ndo_setup_tc)
		return;

	cbs.queue = q->queue;
	cbs.enable = 0;

	err = ops->ndo_setup_tc(dev, TC_SETUP_QDISC_CBS, &cbs);
	if (err < 0)
		pr_warn("Couldn't disable CBS offload for queue %d\n",
			cbs.queue);
}

static int cbs_enable_offload(struct net_device *dev, struct cbs_sched_data *q,
			      const struct tc_cbs_qopt *opt,
			      struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	struct tc_cbs_qopt_offload cbs = { };
	int err;

	if (!ops->ndo_setup_tc) {
		NL_SET_ERR_MSG(extack, "Specified device does not support cbs offload");
		return -EOPNOTSUPP;
	}

	cbs.queue = q->queue;

	cbs.enable = 1;
	cbs.hicredit = opt->hicredit;
	cbs.locredit = opt->locredit;
	cbs.idleslope = opt->idleslope;
	cbs.sendslope = opt->sendslope;

	err = ops->ndo_setup_tc(dev, TC_SETUP_QDISC_CBS, &cbs);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Specified device failed to setup cbs hardware offload");
		return err;
	}

	q->enqueue = cbs_enqueue_offload;
	q->dequeue = cbs_dequeue_offload;

	return 0;
}

static int cbs_change(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct nlattr *tb[TCA_CBS_MAX + 1];
	struct tc_cbs_qopt *qopt;
	int err;

	err = nla_parse_nested(tb, TCA_CBS_MAX, opt, cbs_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_CBS_PARMS]) {
		NL_SET_ERR_MSG(extack, "Missing CBS parameter which are mandatory");
		return -EINVAL;
	}

	qopt = nla_data(tb[TCA_CBS_PARMS]);

	if (!qopt->offload) {
		struct ethtool_link_ksettings ecmd;
		s64 link_speed;

		if (!__ethtool_get_link_ksettings(dev, &ecmd))
			link_speed = ecmd.base.speed;
		else
			link_speed = SPEED_1000;

		q->port_rate = link_speed * 1000 * BYTES_PER_KBIT;

		cbs_disable_offload(dev, q);
	} else {
		err = cbs_enable_offload(dev, q, qopt, extack);
		if (err < 0)
			return err;
	}

	/* Everything went OK, save the parameters used. */
	q->hicredit = qopt->hicredit;
	q->locredit = qopt->locredit;
	q->idleslope = qopt->idleslope * BYTES_PER_KBIT;
	q->sendslope = qopt->sendslope * BYTES_PER_KBIT;
	q->offload = qopt->offload;

	return 0;
}

static int cbs_init(struct Qdisc *sch, struct nlattr *opt,
		    struct netlink_ext_ack *extack)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);

	if (!opt) {
		NL_SET_ERR_MSG(extack, "Missing CBS qdisc options  which are mandatory");
		return -EINVAL;
	}

	q->queue = sch->dev_queue - netdev_get_tx_queue(dev, 0);

	q->enqueue = cbs_enqueue_soft;
	q->dequeue = cbs_dequeue_soft;

	qdisc_watchdog_init(&q->watchdog, sch);

	return cbs_change(sch, opt, extack);
}

static void cbs_destroy(struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);

	qdisc_watchdog_cancel(&q->watchdog);

	cbs_disable_offload(dev, q);
}

static int cbs_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct tc_cbs_qopt opt = { };
	struct nlattr *nest;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	opt.hicredit = q->hicredit;
	opt.locredit = q->locredit;
	opt.sendslope = div64_s64(q->sendslope, BYTES_PER_KBIT);
	opt.idleslope = div64_s64(q->idleslope, BYTES_PER_KBIT);
	opt.offload = q->offload;

	if (nla_put(skb, TCA_CBS_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct Qdisc_ops cbs_qdisc_ops __read_mostly = {
	.id		=	"cbs",
	.priv_size	=	sizeof(struct cbs_sched_data),
	.enqueue	=	cbs_enqueue,
	.dequeue	=	cbs_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	cbs_init,
	.reset		=	qdisc_reset_queue,
	.destroy	=	cbs_destroy,
	.change		=	cbs_change,
	.dump		=	cbs_dump,
	.owner		=	THIS_MODULE,
};

static int __init cbs_module_init(void)
{
	return register_qdisc(&cbs_qdisc_ops);
}

static void __exit cbs_module_exit(void)
{
	unregister_qdisc(&cbs_qdisc_ops);
}
module_init(cbs_module_init)
module_exit(cbs_module_exit)
MODULE_LICENSE("GPL");

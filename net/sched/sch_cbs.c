// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_cbs.c	Credit Based Shaper
 *
 * Authors:	Vinicius Costa Gomes <vinicius.gomes@intel.com>
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

#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netevent.h>
#include <net/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>

static LIST_HEAD(cbs_list);
static DEFINE_SPINLOCK(cbs_list_lock);

#define BYTES_PER_KBIT (1000LL / 8)

struct cbs_sched_data {
	bool offload;
	int queue;
	atomic64_t port_rate; /* in bytes/s */
	s64 last; /* timestamp in ns */
	s64 credits; /* in bytes */
	s32 locredit; /* in bytes */
	s32 hicredit; /* in bytes */
	s64 sendslope; /* in bytes/s */
	s64 idleslope; /* in bytes/s */
	struct qdisc_watchdog watchdog;
	int (*enqueue)(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free);
	struct sk_buff *(*dequeue)(struct Qdisc *sch);
	struct Qdisc *qdisc;
	struct list_head cbs_list;
};

static int cbs_child_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			     struct Qdisc *child,
			     struct sk_buff **to_free)
{
	unsigned int len = qdisc_pkt_len(skb);
	int err;

	err = child->ops->enqueue(skb, child, to_free);
	if (err != NET_XMIT_SUCCESS)
		return err;

	sch->qstats.backlog += len;
	sch->q.qlen++;

	return NET_XMIT_SUCCESS;
}

static int cbs_enqueue_offload(struct sk_buff *skb, struct Qdisc *sch,
			       struct sk_buff **to_free)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc = q->qdisc;

	return cbs_child_enqueue(skb, sch, qdisc, to_free);
}

static int cbs_enqueue_soft(struct sk_buff *skb, struct Qdisc *sch,
			    struct sk_buff **to_free)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc = q->qdisc;

	if (sch->q.qlen == 0 && q->credits > 0) {
		/* We need to stop accumulating credits when there's
		 * no enqueued packets and q->credits is positive.
		 */
		q->credits = 0;
		q->last = ktime_get_ns();
	}

	return cbs_child_enqueue(skb, sch, qdisc, to_free);
}

static int cbs_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	return q->enqueue(skb, sch, to_free);
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

static struct sk_buff *cbs_child_dequeue(struct Qdisc *sch, struct Qdisc *child)
{
	struct sk_buff *skb;

	skb = child->ops->dequeue(child);
	if (!skb)
		return NULL;

	qdisc_qstats_backlog_dec(sch, skb);
	qdisc_bstats_update(sch, skb);
	sch->q.qlen--;

	return skb;
}

static struct sk_buff *cbs_dequeue_soft(struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc = q->qdisc;
	s64 now = ktime_get_ns();
	struct sk_buff *skb;
	s64 credits;
	int len;

	/* The previous packet is still being sent */
	if (now < q->last) {
		qdisc_watchdog_schedule_ns(&q->watchdog, q->last);
		return NULL;
	}
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
	skb = cbs_child_dequeue(sch, qdisc);
	if (!skb)
		return NULL;

	len = qdisc_pkt_len(skb);

	/* As sendslope is a negative number, this will decrease the
	 * amount of q->credits.
	 */
	credits = credits_from_len(len, q->sendslope,
				   atomic64_read(&q->port_rate));
	credits += q->credits;

	q->credits = max_t(s64, credits, q->locredit);
	/* Estimate of the transmission of the last byte of the packet in ns */
	if (unlikely(atomic64_read(&q->port_rate) == 0))
		q->last = now;
	else
		q->last = now + div64_s64(len * NSEC_PER_SEC,
					  atomic64_read(&q->port_rate));

	return skb;
}

static struct sk_buff *cbs_dequeue_offload(struct Qdisc *sch)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc = q->qdisc;

	return cbs_child_dequeue(sch, qdisc);
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

static void cbs_set_port_rate(struct net_device *dev, struct cbs_sched_data *q)
{
	struct ethtool_link_ksettings ecmd;
	int speed = SPEED_10;
	int port_rate;
	int err;

	err = __ethtool_get_link_ksettings(dev, &ecmd);
	if (err < 0)
		goto skip;

	if (ecmd.base.speed && ecmd.base.speed != SPEED_UNKNOWN)
		speed = ecmd.base.speed;

skip:
	port_rate = speed * 1000 * BYTES_PER_KBIT;

	atomic64_set(&q->port_rate, port_rate);
	netdev_dbg(dev, "cbs: set %s's port_rate to: %lld, linkspeed: %d\n",
		   dev->name, (long long)atomic64_read(&q->port_rate),
		   ecmd.base.speed);
}

static int cbs_dev_notifier(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct cbs_sched_data *q;
	struct net_device *qdev;
	bool found = false;

	ASSERT_RTNL();

	if (event != NETDEV_UP && event != NETDEV_CHANGE)
		return NOTIFY_DONE;

	spin_lock(&cbs_list_lock);
	list_for_each_entry(q, &cbs_list, cbs_list) {
		qdev = qdisc_dev(q->qdisc);
		if (qdev == dev) {
			found = true;
			break;
		}
	}
	spin_unlock(&cbs_list_lock);

	if (found)
		cbs_set_port_rate(dev, q);

	return NOTIFY_DONE;
}

static int cbs_change(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct nlattr *tb[TCA_CBS_MAX + 1];
	struct tc_cbs_qopt *qopt;
	int err;

	err = nla_parse_nested_deprecated(tb, TCA_CBS_MAX, opt, cbs_policy,
					  extack);
	if (err < 0)
		return err;

	if (!tb[TCA_CBS_PARMS]) {
		NL_SET_ERR_MSG(extack, "Missing CBS parameter which are mandatory");
		return -EINVAL;
	}

	qopt = nla_data(tb[TCA_CBS_PARMS]);

	if (!qopt->offload) {
		cbs_set_port_rate(dev, q);
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

	q->qdisc = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
				     sch->handle, extack);
	if (!q->qdisc)
		return -ENOMEM;

	spin_lock(&cbs_list_lock);
	list_add(&q->cbs_list, &cbs_list);
	spin_unlock(&cbs_list_lock);

	qdisc_hash_add(q->qdisc, false);

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

	/* Nothing to do if we couldn't create the underlying qdisc */
	if (!q->qdisc)
		return;

	qdisc_watchdog_cancel(&q->watchdog);
	cbs_disable_offload(dev, q);

	spin_lock(&cbs_list_lock);
	list_del(&q->cbs_list);
	spin_unlock(&cbs_list_lock);

	qdisc_put(q->qdisc);
}

static int cbs_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct cbs_sched_data *q = qdisc_priv(sch);
	struct tc_cbs_qopt opt = { };
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
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

static int cbs_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	if (cl != 1 || !q->qdisc)	/* only one class */
		return -ENOENT;

	tcm->tcm_handle |= TC_H_MIN(1);
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int cbs_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	if (!new) {
		new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					sch->handle, NULL);
		if (!new)
			new = &noop_qdisc;
	}

	*old = qdisc_replace(sch, new, &q->qdisc);
	return 0;
}

static struct Qdisc *cbs_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct cbs_sched_data *q = qdisc_priv(sch);

	return q->qdisc;
}

static unsigned long cbs_find(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void cbs_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	if (!walker->stop) {
		tc_qdisc_stats_dump(sch, 1, walker);
	}
}

static const struct Qdisc_class_ops cbs_class_ops = {
	.graft		=	cbs_graft,
	.leaf		=	cbs_leaf,
	.find		=	cbs_find,
	.walk		=	cbs_walk,
	.dump		=	cbs_dump_class,
};

static struct Qdisc_ops cbs_qdisc_ops __read_mostly = {
	.id		=	"cbs",
	.cl_ops		=	&cbs_class_ops,
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

static struct notifier_block cbs_device_notifier = {
	.notifier_call = cbs_dev_notifier,
};

static int __init cbs_module_init(void)
{
	int err;

	err = register_netdevice_notifier(&cbs_device_notifier);
	if (err)
		return err;

	err = register_qdisc(&cbs_qdisc_ops);
	if (err)
		unregister_netdevice_notifier(&cbs_device_notifier);

	return err;
}

static void __exit cbs_module_exit(void)
{
	unregister_qdisc(&cbs_qdisc_ops);
	unregister_netdevice_notifier(&cbs_device_notifier);
}
module_init(cbs_module_init)
module_exit(cbs_module_exit)
MODULE_LICENSE("GPL");

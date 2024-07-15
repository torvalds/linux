// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/sched/sch_mqprio.c
 *
 * Copyright (c) 2010 John Fastabend <john.r.fastabend@intel.com>
 */

#include <linux/ethtool_netlink.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/pkt_cls.h>

#include "sch_mqprio_lib.h"

struct mqprio_sched {
	struct Qdisc		**qdiscs;
	u16 mode;
	u16 shaper;
	int hw_offload;
	u32 flags;
	u64 min_rate[TC_QOPT_MAX_QUEUE];
	u64 max_rate[TC_QOPT_MAX_QUEUE];
	u32 fp[TC_QOPT_MAX_QUEUE];
};

static int mqprio_enable_offload(struct Qdisc *sch,
				 const struct tc_mqprio_qopt *qopt,
				 struct netlink_ext_ack *extack)
{
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct tc_mqprio_qopt_offload mqprio = {
		.qopt = *qopt,
		.extack = extack,
	};
	int err, i;

	switch (priv->mode) {
	case TC_MQPRIO_MODE_DCB:
		if (priv->shaper != TC_MQPRIO_SHAPER_DCB)
			return -EINVAL;
		break;
	case TC_MQPRIO_MODE_CHANNEL:
		mqprio.flags = priv->flags;
		if (priv->flags & TC_MQPRIO_F_MODE)
			mqprio.mode = priv->mode;
		if (priv->flags & TC_MQPRIO_F_SHAPER)
			mqprio.shaper = priv->shaper;
		if (priv->flags & TC_MQPRIO_F_MIN_RATE)
			for (i = 0; i < mqprio.qopt.num_tc; i++)
				mqprio.min_rate[i] = priv->min_rate[i];
		if (priv->flags & TC_MQPRIO_F_MAX_RATE)
			for (i = 0; i < mqprio.qopt.num_tc; i++)
				mqprio.max_rate[i] = priv->max_rate[i];
		break;
	default:
		return -EINVAL;
	}

	mqprio_fp_to_offload(priv->fp, &mqprio);

	err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_MQPRIO,
					    &mqprio);
	if (err)
		return err;

	priv->hw_offload = mqprio.qopt.hw;

	return 0;
}

static void mqprio_disable_offload(struct Qdisc *sch)
{
	struct tc_mqprio_qopt_offload mqprio = { { 0 } };
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);

	switch (priv->mode) {
	case TC_MQPRIO_MODE_DCB:
	case TC_MQPRIO_MODE_CHANNEL:
		dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_MQPRIO,
					      &mqprio);
		break;
	}
}

static void mqprio_destroy(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mqprio_sched *priv = qdisc_priv(sch);
	unsigned int ntx;

	if (priv->qdiscs) {
		for (ntx = 0;
		     ntx < dev->num_tx_queues && priv->qdiscs[ntx];
		     ntx++)
			qdisc_put(priv->qdiscs[ntx]);
		kfree(priv->qdiscs);
	}

	if (priv->hw_offload && dev->netdev_ops->ndo_setup_tc)
		mqprio_disable_offload(sch);
	else
		netdev_set_num_tc(dev, 0);
}

static int mqprio_parse_opt(struct net_device *dev, struct tc_mqprio_qopt *qopt,
			    const struct tc_mqprio_caps *caps,
			    struct netlink_ext_ack *extack)
{
	int err;

	/* Limit qopt->hw to maximum supported offload value.  Drivers have
	 * the option of overriding this later if they don't support the a
	 * given offload type.
	 */
	if (qopt->hw > TC_MQPRIO_HW_OFFLOAD_MAX)
		qopt->hw = TC_MQPRIO_HW_OFFLOAD_MAX;

	/* If hardware offload is requested, we will leave 3 options to the
	 * device driver:
	 * - populate the queue counts itself (and ignore what was requested)
	 * - validate the provided queue counts by itself (and apply them)
	 * - request queue count validation here (and apply them)
	 */
	err = mqprio_validate_qopt(dev, qopt,
				   !qopt->hw || caps->validate_queue_counts,
				   false, extack);
	if (err)
		return err;

	/* If ndo_setup_tc is not present then hardware doesn't support offload
	 * and we should return an error.
	 */
	if (qopt->hw && !dev->netdev_ops->ndo_setup_tc) {
		NL_SET_ERR_MSG(extack,
			       "Device does not support hardware offload");
		return -EINVAL;
	}

	return 0;
}

static const struct
nla_policy mqprio_tc_entry_policy[TCA_MQPRIO_TC_ENTRY_MAX + 1] = {
	[TCA_MQPRIO_TC_ENTRY_INDEX]	= NLA_POLICY_MAX(NLA_U32,
							 TC_QOPT_MAX_QUEUE),
	[TCA_MQPRIO_TC_ENTRY_FP]	= NLA_POLICY_RANGE(NLA_U32,
							   TC_FP_EXPRESS,
							   TC_FP_PREEMPTIBLE),
};

static const struct nla_policy mqprio_policy[TCA_MQPRIO_MAX + 1] = {
	[TCA_MQPRIO_MODE]	= { .len = sizeof(u16) },
	[TCA_MQPRIO_SHAPER]	= { .len = sizeof(u16) },
	[TCA_MQPRIO_MIN_RATE64]	= { .type = NLA_NESTED },
	[TCA_MQPRIO_MAX_RATE64]	= { .type = NLA_NESTED },
	[TCA_MQPRIO_TC_ENTRY]	= { .type = NLA_NESTED },
};

static int mqprio_parse_tc_entry(u32 fp[TC_QOPT_MAX_QUEUE],
				 struct nlattr *opt,
				 unsigned long *seen_tcs,
				 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_MQPRIO_TC_ENTRY_MAX + 1];
	int err, tc;

	err = nla_parse_nested(tb, TCA_MQPRIO_TC_ENTRY_MAX, opt,
			       mqprio_tc_entry_policy, extack);
	if (err < 0)
		return err;

	if (NL_REQ_ATTR_CHECK(extack, opt, tb, TCA_MQPRIO_TC_ENTRY_INDEX)) {
		NL_SET_ERR_MSG(extack, "TC entry index missing");
		return -EINVAL;
	}

	tc = nla_get_u32(tb[TCA_MQPRIO_TC_ENTRY_INDEX]);
	if (*seen_tcs & BIT(tc)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[TCA_MQPRIO_TC_ENTRY_INDEX],
				    "Duplicate tc entry");
		return -EINVAL;
	}

	*seen_tcs |= BIT(tc);

	if (tb[TCA_MQPRIO_TC_ENTRY_FP])
		fp[tc] = nla_get_u32(tb[TCA_MQPRIO_TC_ENTRY_FP]);

	return 0;
}

static int mqprio_parse_tc_entries(struct Qdisc *sch, struct nlattr *nlattr_opt,
				   int nlattr_opt_len,
				   struct netlink_ext_ack *extack)
{
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	bool have_preemption = false;
	unsigned long seen_tcs = 0;
	u32 fp[TC_QOPT_MAX_QUEUE];
	struct nlattr *n;
	int tc, rem;
	int err = 0;

	for (tc = 0; tc < TC_QOPT_MAX_QUEUE; tc++)
		fp[tc] = priv->fp[tc];

	nla_for_each_attr(n, nlattr_opt, nlattr_opt_len, rem) {
		if (nla_type(n) != TCA_MQPRIO_TC_ENTRY)
			continue;

		err = mqprio_parse_tc_entry(fp, n, &seen_tcs, extack);
		if (err)
			goto out;
	}

	for (tc = 0; tc < TC_QOPT_MAX_QUEUE; tc++) {
		priv->fp[tc] = fp[tc];
		if (fp[tc] == TC_FP_PREEMPTIBLE)
			have_preemption = true;
	}

	if (have_preemption && !ethtool_dev_mm_supported(dev)) {
		NL_SET_ERR_MSG(extack, "Device does not support preemption");
		return -EOPNOTSUPP;
	}
out:
	return err;
}

/* Parse the other netlink attributes that represent the payload of
 * TCA_OPTIONS, which are appended right after struct tc_mqprio_qopt.
 */
static int mqprio_parse_nlattr(struct Qdisc *sch, struct tc_mqprio_qopt *qopt,
			       struct nlattr *opt,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *nlattr_opt = nla_data(opt) + NLA_ALIGN(sizeof(*qopt));
	int nlattr_opt_len = nla_len(opt) - NLA_ALIGN(sizeof(*qopt));
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct nlattr *tb[TCA_MQPRIO_MAX + 1] = {};
	struct nlattr *attr;
	int i, rem, err;

	if (nlattr_opt_len >= nla_attr_size(0)) {
		err = nla_parse_deprecated(tb, TCA_MQPRIO_MAX, nlattr_opt,
					   nlattr_opt_len, mqprio_policy,
					   NULL);
		if (err < 0)
			return err;
	}

	if (!qopt->hw) {
		NL_SET_ERR_MSG(extack,
			       "mqprio TCA_OPTIONS can only contain netlink attributes in hardware mode");
		return -EINVAL;
	}

	if (tb[TCA_MQPRIO_MODE]) {
		priv->flags |= TC_MQPRIO_F_MODE;
		priv->mode = nla_get_u16(tb[TCA_MQPRIO_MODE]);
	}

	if (tb[TCA_MQPRIO_SHAPER]) {
		priv->flags |= TC_MQPRIO_F_SHAPER;
		priv->shaper = nla_get_u16(tb[TCA_MQPRIO_SHAPER]);
	}

	if (tb[TCA_MQPRIO_MIN_RATE64]) {
		if (priv->shaper != TC_MQPRIO_SHAPER_BW_RATE) {
			NL_SET_ERR_MSG_ATTR(extack, tb[TCA_MQPRIO_MIN_RATE64],
					    "min_rate accepted only when shaper is in bw_rlimit mode");
			return -EINVAL;
		}
		i = 0;
		nla_for_each_nested(attr, tb[TCA_MQPRIO_MIN_RATE64],
				    rem) {
			if (nla_type(attr) != TCA_MQPRIO_MIN_RATE64) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "Attribute type expected to be TCA_MQPRIO_MIN_RATE64");
				return -EINVAL;
			}

			if (nla_len(attr) != sizeof(u64)) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "Attribute TCA_MQPRIO_MIN_RATE64 expected to have 8 bytes length");
				return -EINVAL;
			}

			if (i >= qopt->num_tc)
				break;
			priv->min_rate[i] = nla_get_u64(attr);
			i++;
		}
		priv->flags |= TC_MQPRIO_F_MIN_RATE;
	}

	if (tb[TCA_MQPRIO_MAX_RATE64]) {
		if (priv->shaper != TC_MQPRIO_SHAPER_BW_RATE) {
			NL_SET_ERR_MSG_ATTR(extack, tb[TCA_MQPRIO_MAX_RATE64],
					    "max_rate accepted only when shaper is in bw_rlimit mode");
			return -EINVAL;
		}
		i = 0;
		nla_for_each_nested(attr, tb[TCA_MQPRIO_MAX_RATE64],
				    rem) {
			if (nla_type(attr) != TCA_MQPRIO_MAX_RATE64) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "Attribute type expected to be TCA_MQPRIO_MAX_RATE64");
				return -EINVAL;
			}

			if (nla_len(attr) != sizeof(u64)) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "Attribute TCA_MQPRIO_MAX_RATE64 expected to have 8 bytes length");
				return -EINVAL;
			}

			if (i >= qopt->num_tc)
				break;
			priv->max_rate[i] = nla_get_u64(attr);
			i++;
		}
		priv->flags |= TC_MQPRIO_F_MAX_RATE;
	}

	if (tb[TCA_MQPRIO_TC_ENTRY]) {
		err = mqprio_parse_tc_entries(sch, nlattr_opt, nlattr_opt_len,
					      extack);
		if (err)
			return err;
	}

	return 0;
}

static int mqprio_init(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct netdev_queue *dev_queue;
	struct Qdisc *qdisc;
	int i, err = -EOPNOTSUPP;
	struct tc_mqprio_qopt *qopt = NULL;
	struct tc_mqprio_caps caps;
	int len, tc;

	BUILD_BUG_ON(TC_MAX_QUEUE != TC_QOPT_MAX_QUEUE);
	BUILD_BUG_ON(TC_BITMASK != TC_QOPT_BITMASK);

	if (sch->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	if (!netif_is_multiqueue(dev))
		return -EOPNOTSUPP;

	/* make certain can allocate enough classids to handle queues */
	if (dev->num_tx_queues >= TC_H_MIN_PRIORITY)
		return -ENOMEM;

	if (!opt || nla_len(opt) < sizeof(*qopt))
		return -EINVAL;

	for (tc = 0; tc < TC_QOPT_MAX_QUEUE; tc++)
		priv->fp[tc] = TC_FP_EXPRESS;

	qdisc_offload_query_caps(dev, TC_SETUP_QDISC_MQPRIO,
				 &caps, sizeof(caps));

	qopt = nla_data(opt);
	if (mqprio_parse_opt(dev, qopt, &caps, extack))
		return -EINVAL;

	len = nla_len(opt) - NLA_ALIGN(sizeof(*qopt));
	if (len > 0) {
		err = mqprio_parse_nlattr(sch, qopt, opt, extack);
		if (err)
			return err;
	}

	/* pre-allocate qdisc, attachment can't fail */
	priv->qdiscs = kcalloc(dev->num_tx_queues, sizeof(priv->qdiscs[0]),
			       GFP_KERNEL);
	if (!priv->qdiscs)
		return -ENOMEM;

	for (i = 0; i < dev->num_tx_queues; i++) {
		dev_queue = netdev_get_tx_queue(dev, i);
		qdisc = qdisc_create_dflt(dev_queue,
					  get_default_qdisc_ops(dev, i),
					  TC_H_MAKE(TC_H_MAJ(sch->handle),
						    TC_H_MIN(i + 1)), extack);
		if (!qdisc)
			return -ENOMEM;

		priv->qdiscs[i] = qdisc;
		qdisc->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	}

	/* If the mqprio options indicate that hardware should own
	 * the queue mapping then run ndo_setup_tc otherwise use the
	 * supplied and verified mapping
	 */
	if (qopt->hw) {
		err = mqprio_enable_offload(sch, qopt, extack);
		if (err)
			return err;
	} else {
		netdev_set_num_tc(dev, qopt->num_tc);
		for (i = 0; i < qopt->num_tc; i++)
			netdev_set_tc_queue(dev, i,
					    qopt->count[i], qopt->offset[i]);
	}

	/* Always use supplied priority mappings */
	for (i = 0; i < TC_BITMASK + 1; i++)
		netdev_set_prio_tc_map(dev, i, qopt->prio_tc_map[i]);

	sch->flags |= TCQ_F_MQROOT;
	return 0;
}

static void mqprio_attach(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct Qdisc *qdisc, *old;
	unsigned int ntx;

	/* Attach underlying qdisc */
	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = priv->qdiscs[ntx];
		old = dev_graft_qdisc(qdisc->dev_queue, qdisc);
		if (old)
			qdisc_put(old);
		if (ntx < dev->real_num_tx_queues)
			qdisc_hash_add(qdisc, false);
	}
	kfree(priv->qdiscs);
	priv->qdiscs = NULL;
}

static struct netdev_queue *mqprio_queue_get(struct Qdisc *sch,
					     unsigned long cl)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx = cl - 1;

	if (ntx >= dev->num_tx_queues)
		return NULL;
	return netdev_get_tx_queue(dev, ntx);
}

static int mqprio_graft(struct Qdisc *sch, unsigned long cl, struct Qdisc *new,
			struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct net_device *dev = qdisc_dev(sch);
	struct netdev_queue *dev_queue = mqprio_queue_get(sch, cl);

	if (!dev_queue)
		return -EINVAL;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	*old = dev_graft_qdisc(dev_queue, new);

	if (new)
		new->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;

	if (dev->flags & IFF_UP)
		dev_activate(dev);

	return 0;
}

static int dump_rates(struct mqprio_sched *priv,
		      struct tc_mqprio_qopt *opt, struct sk_buff *skb)
{
	struct nlattr *nest;
	int i;

	if (priv->flags & TC_MQPRIO_F_MIN_RATE) {
		nest = nla_nest_start_noflag(skb, TCA_MQPRIO_MIN_RATE64);
		if (!nest)
			goto nla_put_failure;

		for (i = 0; i < opt->num_tc; i++) {
			if (nla_put(skb, TCA_MQPRIO_MIN_RATE64,
				    sizeof(priv->min_rate[i]),
				    &priv->min_rate[i]))
				goto nla_put_failure;
		}
		nla_nest_end(skb, nest);
	}

	if (priv->flags & TC_MQPRIO_F_MAX_RATE) {
		nest = nla_nest_start_noflag(skb, TCA_MQPRIO_MAX_RATE64);
		if (!nest)
			goto nla_put_failure;

		for (i = 0; i < opt->num_tc; i++) {
			if (nla_put(skb, TCA_MQPRIO_MAX_RATE64,
				    sizeof(priv->max_rate[i]),
				    &priv->max_rate[i]))
				goto nla_put_failure;
		}
		nla_nest_end(skb, nest);
	}
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int mqprio_dump_tc_entries(struct mqprio_sched *priv,
				  struct sk_buff *skb)
{
	struct nlattr *n;
	int tc;

	for (tc = 0; tc < TC_QOPT_MAX_QUEUE; tc++) {
		n = nla_nest_start(skb, TCA_MQPRIO_TC_ENTRY);
		if (!n)
			return -EMSGSIZE;

		if (nla_put_u32(skb, TCA_MQPRIO_TC_ENTRY_INDEX, tc))
			goto nla_put_failure;

		if (nla_put_u32(skb, TCA_MQPRIO_TC_ENTRY_FP, priv->fp[tc]))
			goto nla_put_failure;

		nla_nest_end(skb, n);
	}

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, n);
	return -EMSGSIZE;
}

static int mqprio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mqprio_sched *priv = qdisc_priv(sch);
	struct nlattr *nla = (struct nlattr *)skb_tail_pointer(skb);
	struct tc_mqprio_qopt opt = { 0 };
	struct Qdisc *qdisc;
	unsigned int ntx;

	sch->q.qlen = 0;
	gnet_stats_basic_sync_init(&sch->bstats);
	memset(&sch->qstats, 0, sizeof(sch->qstats));

	/* MQ supports lockless qdiscs. However, statistics accounting needs
	 * to account for all, none, or a mix of locked and unlocked child
	 * qdiscs. Percpu stats are added to counters in-band and locking
	 * qdisc totals are added at end.
	 */
	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = rtnl_dereference(netdev_get_tx_queue(dev, ntx)->qdisc_sleeping);
		spin_lock_bh(qdisc_lock(qdisc));

		gnet_stats_add_basic(&sch->bstats, qdisc->cpu_bstats,
				     &qdisc->bstats, false);
		gnet_stats_add_queue(&sch->qstats, qdisc->cpu_qstats,
				     &qdisc->qstats);
		sch->q.qlen += qdisc_qlen(qdisc);

		spin_unlock_bh(qdisc_lock(qdisc));
	}

	mqprio_qopt_reconstruct(dev, &opt);
	opt.hw = priv->hw_offload;

	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;

	if ((priv->flags & TC_MQPRIO_F_MODE) &&
	    nla_put_u16(skb, TCA_MQPRIO_MODE, priv->mode))
		goto nla_put_failure;

	if ((priv->flags & TC_MQPRIO_F_SHAPER) &&
	    nla_put_u16(skb, TCA_MQPRIO_SHAPER, priv->shaper))
		goto nla_put_failure;

	if ((priv->flags & TC_MQPRIO_F_MIN_RATE ||
	     priv->flags & TC_MQPRIO_F_MAX_RATE) &&
	    (dump_rates(priv, &opt, skb) != 0))
		goto nla_put_failure;

	if (mqprio_dump_tc_entries(priv, skb))
		goto nla_put_failure;

	return nla_nest_end(skb, nla);
nla_put_failure:
	nlmsg_trim(skb, nla);
	return -1;
}

static struct Qdisc *mqprio_leaf(struct Qdisc *sch, unsigned long cl)
{
	struct netdev_queue *dev_queue = mqprio_queue_get(sch, cl);

	if (!dev_queue)
		return NULL;

	return rtnl_dereference(dev_queue->qdisc_sleeping);
}

static unsigned long mqprio_find(struct Qdisc *sch, u32 classid)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned int ntx = TC_H_MIN(classid);

	/* There are essentially two regions here that have valid classid
	 * values. The first region will have a classid value of 1 through
	 * num_tx_queues. All of these are backed by actual Qdiscs.
	 */
	if (ntx < TC_H_MIN_PRIORITY)
		return (ntx <= dev->num_tx_queues) ? ntx : 0;

	/* The second region represents the hardware traffic classes. These
	 * are represented by classid values of TC_H_MIN_PRIORITY through
	 * TC_H_MIN_PRIORITY + netdev_get_num_tc - 1
	 */
	return ((ntx - TC_H_MIN_PRIORITY) < netdev_get_num_tc(dev)) ? ntx : 0;
}

static int mqprio_dump_class(struct Qdisc *sch, unsigned long cl,
			 struct sk_buff *skb, struct tcmsg *tcm)
{
	if (cl < TC_H_MIN_PRIORITY) {
		struct netdev_queue *dev_queue = mqprio_queue_get(sch, cl);
		struct net_device *dev = qdisc_dev(sch);
		int tc = netdev_txq_to_tc(dev, cl - 1);

		tcm->tcm_parent = (tc < 0) ? 0 :
			TC_H_MAKE(TC_H_MAJ(sch->handle),
				  TC_H_MIN(tc + TC_H_MIN_PRIORITY));
		tcm->tcm_info = rtnl_dereference(dev_queue->qdisc_sleeping)->handle;
	} else {
		tcm->tcm_parent = TC_H_ROOT;
		tcm->tcm_info = 0;
	}
	tcm->tcm_handle |= TC_H_MIN(cl);
	return 0;
}

static int mqprio_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				   struct gnet_dump *d)
	__releases(d->lock)
	__acquires(d->lock)
{
	if (cl >= TC_H_MIN_PRIORITY) {
		int i;
		__u32 qlen;
		struct gnet_stats_queue qstats = {0};
		struct gnet_stats_basic_sync bstats;
		struct net_device *dev = qdisc_dev(sch);
		struct netdev_tc_txq tc = dev->tc_to_txq[cl & TC_BITMASK];

		gnet_stats_basic_sync_init(&bstats);
		/* Drop lock here it will be reclaimed before touching
		 * statistics this is required because the d->lock we
		 * hold here is the look on dev_queue->qdisc_sleeping
		 * also acquired below.
		 */
		if (d->lock)
			spin_unlock_bh(d->lock);

		for (i = tc.offset; i < tc.offset + tc.count; i++) {
			struct netdev_queue *q = netdev_get_tx_queue(dev, i);
			struct Qdisc *qdisc = rtnl_dereference(q->qdisc);

			spin_lock_bh(qdisc_lock(qdisc));

			gnet_stats_add_basic(&bstats, qdisc->cpu_bstats,
					     &qdisc->bstats, false);
			gnet_stats_add_queue(&qstats, qdisc->cpu_qstats,
					     &qdisc->qstats);
			sch->q.qlen += qdisc_qlen(qdisc);

			spin_unlock_bh(qdisc_lock(qdisc));
		}
		qlen = qdisc_qlen(sch) + qstats.qlen;

		/* Reclaim root sleeping lock before completing stats */
		if (d->lock)
			spin_lock_bh(d->lock);
		if (gnet_stats_copy_basic(d, NULL, &bstats, false) < 0 ||
		    gnet_stats_copy_queue(d, NULL, &qstats, qlen) < 0)
			return -1;
	} else {
		struct netdev_queue *dev_queue = mqprio_queue_get(sch, cl);

		sch = rtnl_dereference(dev_queue->qdisc_sleeping);
		if (gnet_stats_copy_basic(d, sch->cpu_bstats,
					  &sch->bstats, true) < 0 ||
		    qdisc_qstats_copy(d, sch) < 0)
			return -1;
	}
	return 0;
}

static void mqprio_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx;

	if (arg->stop)
		return;

	/* Walk hierarchy with a virtual class per tc */
	arg->count = arg->skip;
	for (ntx = arg->skip; ntx < netdev_get_num_tc(dev); ntx++) {
		if (!tc_qdisc_stats_dump(sch, ntx + TC_H_MIN_PRIORITY, arg))
			return;
	}

	/* Pad the values and skip over unused traffic classes */
	if (ntx < TC_MAX_QUEUE) {
		arg->count = TC_MAX_QUEUE;
		ntx = TC_MAX_QUEUE;
	}

	/* Reset offset, sort out remaining per-queue qdiscs */
	for (ntx -= TC_MAX_QUEUE; ntx < dev->num_tx_queues; ntx++) {
		if (arg->fn(sch, ntx + 1, arg) < 0) {
			arg->stop = 1;
			return;
		}
		arg->count++;
	}
}

static struct netdev_queue *mqprio_select_queue(struct Qdisc *sch,
						struct tcmsg *tcm)
{
	return mqprio_queue_get(sch, TC_H_MIN(tcm->tcm_parent));
}

static const struct Qdisc_class_ops mqprio_class_ops = {
	.graft		= mqprio_graft,
	.leaf		= mqprio_leaf,
	.find		= mqprio_find,
	.walk		= mqprio_walk,
	.dump		= mqprio_dump_class,
	.dump_stats	= mqprio_dump_class_stats,
	.select_queue	= mqprio_select_queue,
};

static struct Qdisc_ops mqprio_qdisc_ops __read_mostly = {
	.cl_ops		= &mqprio_class_ops,
	.id		= "mqprio",
	.priv_size	= sizeof(struct mqprio_sched),
	.init		= mqprio_init,
	.destroy	= mqprio_destroy,
	.attach		= mqprio_attach,
	.change_real_num_tx = mq_change_real_num_tx,
	.dump		= mqprio_dump,
	.owner		= THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("mqprio");

static int __init mqprio_module_init(void)
{
	return register_qdisc(&mqprio_qdisc_ops);
}

static void __exit mqprio_module_exit(void)
{
	unregister_qdisc(&mqprio_qdisc_ops);
}

module_init(mqprio_module_init);
module_exit(mqprio_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Classful multiqueue prio qdisc");

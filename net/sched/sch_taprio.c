// SPDX-License-Identifier: GPL-2.0

/* net/sched/sch_taprio.c	 Time Aware Priority Scheduler
 *
 * Authors:	Vinicius Costa Gomes <vinicius.gomes@intel.com>
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/sch_generic.h>

#define TAPRIO_ALL_GATES_OPEN -1

struct sched_entry {
	struct list_head list;

	/* The instant that this entry "closes" and the next one
	 * should open, the qdisc will make some effort so that no
	 * packet leaves after this time.
	 */
	ktime_t close_time;
	atomic_t budget;
	int index;
	u32 gate_mask;
	u32 interval;
	u8 command;
};

struct taprio_sched {
	struct Qdisc **qdiscs;
	struct Qdisc *root;
	s64 base_time;
	int clockid;
	int picos_per_byte; /* Using picoseconds because for 10Gbps+
			     * speeds it's sub-nanoseconds per byte
			     */
	size_t num_entries;

	/* Protects the update side of the RCU protected current_entry */
	spinlock_t current_entry_lock;
	struct sched_entry __rcu *current_entry;
	struct list_head entries;
	ktime_t (*get_time)(void);
	struct hrtimer advance_timer;
};

static int taprio_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			  struct sk_buff **to_free)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct Qdisc *child;
	int queue;

	queue = skb_get_queue_mapping(skb);

	child = q->qdiscs[queue];
	if (unlikely(!child))
		return qdisc_drop(skb, sch, to_free);

	qdisc_qstats_backlog_inc(sch, skb);
	sch->q.qlen++;

	return qdisc_enqueue(skb, child, to_free);
}

static struct sk_buff *taprio_peek(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sched_entry *entry;
	struct sk_buff *skb;
	u32 gate_mask;
	int i;

	rcu_read_lock();
	entry = rcu_dereference(q->current_entry);
	gate_mask = entry ? entry->gate_mask : -1;
	rcu_read_unlock();

	if (!gate_mask)
		return NULL;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct Qdisc *child = q->qdiscs[i];
		int prio;
		u8 tc;

		if (unlikely(!child))
			continue;

		skb = child->ops->peek(child);
		if (!skb)
			continue;

		prio = skb->priority;
		tc = netdev_get_prio_tc_map(dev, prio);

		if (!(gate_mask & BIT(tc)))
			return NULL;

		return skb;
	}

	return NULL;
}

static inline int length_to_duration(struct taprio_sched *q, int len)
{
	return (len * q->picos_per_byte) / 1000;
}

static struct sk_buff *taprio_dequeue(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sched_entry *entry;
	struct sk_buff *skb;
	u32 gate_mask;
	int i;

	rcu_read_lock();
	entry = rcu_dereference(q->current_entry);
	/* if there's no entry, it means that the schedule didn't
	 * start yet, so force all gates to be open, this is in
	 * accordance to IEEE 802.1Qbv-2015 Section 8.6.9.4.5
	 * "AdminGateSates"
	 */
	gate_mask = entry ? entry->gate_mask : TAPRIO_ALL_GATES_OPEN;
	rcu_read_unlock();

	if (!gate_mask)
		return NULL;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct Qdisc *child = q->qdiscs[i];
		ktime_t guard;
		int prio;
		int len;
		u8 tc;

		if (unlikely(!child))
			continue;

		skb = child->ops->peek(child);
		if (!skb)
			continue;

		prio = skb->priority;
		tc = netdev_get_prio_tc_map(dev, prio);

		if (!(gate_mask & BIT(tc)))
			continue;

		len = qdisc_pkt_len(skb);
		guard = ktime_add_ns(q->get_time(),
				     length_to_duration(q, len));

		/* In the case that there's no gate entry, there's no
		 * guard band ...
		 */
		if (gate_mask != TAPRIO_ALL_GATES_OPEN &&
		    ktime_after(guard, entry->close_time))
			return NULL;

		/* ... and no budget. */
		if (gate_mask != TAPRIO_ALL_GATES_OPEN &&
		    atomic_sub_return(len, &entry->budget) < 0)
			return NULL;

		skb = child->ops->dequeue(child);
		if (unlikely(!skb))
			return NULL;

		qdisc_bstats_update(sch, skb);
		qdisc_qstats_backlog_dec(sch, skb);
		sch->q.qlen--;

		return skb;
	}

	return NULL;
}

static bool should_restart_cycle(const struct taprio_sched *q,
				 const struct sched_entry *entry)
{
	WARN_ON(!entry);

	return list_is_last(&entry->list, &q->entries);
}

static enum hrtimer_restart advance_sched(struct hrtimer *timer)
{
	struct taprio_sched *q = container_of(timer, struct taprio_sched,
					      advance_timer);
	struct sched_entry *entry, *next;
	struct Qdisc *sch = q->root;
	ktime_t close_time;

	spin_lock(&q->current_entry_lock);
	entry = rcu_dereference_protected(q->current_entry,
					  lockdep_is_held(&q->current_entry_lock));

	/* This is the case that it's the first time that the schedule
	 * runs, so it only happens once per schedule. The first entry
	 * is pre-calculated during the schedule initialization.
	 */
	if (unlikely(!entry)) {
		next = list_first_entry(&q->entries, struct sched_entry,
					list);
		close_time = next->close_time;
		goto first_run;
	}

	if (should_restart_cycle(q, entry))
		next = list_first_entry(&q->entries, struct sched_entry,
					list);
	else
		next = list_next_entry(entry, list);

	close_time = ktime_add_ns(entry->close_time, next->interval);

	next->close_time = close_time;
	atomic_set(&next->budget,
		   (next->interval * 1000) / q->picos_per_byte);

first_run:
	rcu_assign_pointer(q->current_entry, next);
	spin_unlock(&q->current_entry_lock);

	hrtimer_set_expires(&q->advance_timer, close_time);

	rcu_read_lock();
	__netif_schedule(sch);
	rcu_read_unlock();

	return HRTIMER_RESTART;
}

static const struct nla_policy entry_policy[TCA_TAPRIO_SCHED_ENTRY_MAX + 1] = {
	[TCA_TAPRIO_SCHED_ENTRY_INDEX]	   = { .type = NLA_U32 },
	[TCA_TAPRIO_SCHED_ENTRY_CMD]	   = { .type = NLA_U8 },
	[TCA_TAPRIO_SCHED_ENTRY_GATE_MASK] = { .type = NLA_U32 },
	[TCA_TAPRIO_SCHED_ENTRY_INTERVAL]  = { .type = NLA_U32 },
};

static const struct nla_policy entry_list_policy[TCA_TAPRIO_SCHED_MAX + 1] = {
	[TCA_TAPRIO_SCHED_ENTRY] = { .type = NLA_NESTED },
};

static const struct nla_policy taprio_policy[TCA_TAPRIO_ATTR_MAX + 1] = {
	[TCA_TAPRIO_ATTR_PRIOMAP]	       = {
		.len = sizeof(struct tc_mqprio_qopt)
	},
	[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST]     = { .type = NLA_NESTED },
	[TCA_TAPRIO_ATTR_SCHED_BASE_TIME]      = { .type = NLA_S64 },
	[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY]   = { .type = NLA_NESTED },
	[TCA_TAPRIO_ATTR_SCHED_CLOCKID]        = { .type = NLA_S32 },
};

static int fill_sched_entry(struct nlattr **tb, struct sched_entry *entry,
			    struct netlink_ext_ack *extack)
{
	u32 interval = 0;

	if (tb[TCA_TAPRIO_SCHED_ENTRY_CMD])
		entry->command = nla_get_u8(
			tb[TCA_TAPRIO_SCHED_ENTRY_CMD]);

	if (tb[TCA_TAPRIO_SCHED_ENTRY_GATE_MASK])
		entry->gate_mask = nla_get_u32(
			tb[TCA_TAPRIO_SCHED_ENTRY_GATE_MASK]);

	if (tb[TCA_TAPRIO_SCHED_ENTRY_INTERVAL])
		interval = nla_get_u32(
			tb[TCA_TAPRIO_SCHED_ENTRY_INTERVAL]);

	if (interval == 0) {
		NL_SET_ERR_MSG(extack, "Invalid interval for schedule entry");
		return -EINVAL;
	}

	entry->interval = interval;

	return 0;
}

static int parse_sched_entry(struct nlattr *n, struct sched_entry *entry,
			     int index, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_TAPRIO_SCHED_ENTRY_MAX + 1] = { };
	int err;

	err = nla_parse_nested(tb, TCA_TAPRIO_SCHED_ENTRY_MAX, n,
			       entry_policy, NULL);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Could not parse nested entry");
		return -EINVAL;
	}

	entry->index = index;

	return fill_sched_entry(tb, entry, extack);
}

/* Returns the number of entries in case of success */
static int parse_sched_single_entry(struct nlattr *n,
				    struct taprio_sched *q,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *tb_entry[TCA_TAPRIO_SCHED_ENTRY_MAX + 1] = { };
	struct nlattr *tb_list[TCA_TAPRIO_SCHED_MAX + 1] = { };
	struct sched_entry *entry;
	bool found = false;
	u32 index;
	int err;

	err = nla_parse_nested(tb_list, TCA_TAPRIO_SCHED_MAX,
			       n, entry_list_policy, NULL);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Could not parse nested entry");
		return -EINVAL;
	}

	if (!tb_list[TCA_TAPRIO_SCHED_ENTRY]) {
		NL_SET_ERR_MSG(extack, "Single-entry must include an entry");
		return -EINVAL;
	}

	err = nla_parse_nested(tb_entry, TCA_TAPRIO_SCHED_ENTRY_MAX,
			       tb_list[TCA_TAPRIO_SCHED_ENTRY],
			       entry_policy, NULL);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Could not parse nested entry");
		return -EINVAL;
	}

	if (!tb_entry[TCA_TAPRIO_SCHED_ENTRY_INDEX]) {
		NL_SET_ERR_MSG(extack, "Entry must specify an index\n");
		return -EINVAL;
	}

	index = nla_get_u32(tb_entry[TCA_TAPRIO_SCHED_ENTRY_INDEX]);
	if (index >= q->num_entries) {
		NL_SET_ERR_MSG(extack, "Index for single entry exceeds number of entries in schedule");
		return -EINVAL;
	}

	list_for_each_entry(entry, &q->entries, list) {
		if (entry->index == index) {
			found = true;
			break;
		}
	}

	if (!found) {
		NL_SET_ERR_MSG(extack, "Could not find entry");
		return -ENOENT;
	}

	err = fill_sched_entry(tb_entry, entry, extack);
	if (err < 0)
		return err;

	return q->num_entries;
}

static int parse_sched_list(struct nlattr *list,
			    struct taprio_sched *q,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *n;
	int err, rem;
	int i = 0;

	if (!list)
		return -EINVAL;

	nla_for_each_nested(n, list, rem) {
		struct sched_entry *entry;

		if (nla_type(n) != TCA_TAPRIO_SCHED_ENTRY) {
			NL_SET_ERR_MSG(extack, "Attribute is not of type 'entry'");
			continue;
		}

		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			NL_SET_ERR_MSG(extack, "Not enough memory for entry");
			return -ENOMEM;
		}

		err = parse_sched_entry(n, entry, i, extack);
		if (err < 0) {
			kfree(entry);
			return err;
		}

		list_add_tail(&entry->list, &q->entries);
		i++;
	}

	q->num_entries = i;

	return i;
}

/* Returns the number of entries in case of success */
static int parse_taprio_opt(struct nlattr **tb, struct taprio_sched *q,
			    struct netlink_ext_ack *extack)
{
	int err = 0;
	int clockid;

	if (tb[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST] &&
	    tb[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY])
		return -EINVAL;

	if (tb[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY] && q->num_entries == 0)
		return -EINVAL;

	if (q->clockid == -1 && !tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID])
		return -EINVAL;

	if (tb[TCA_TAPRIO_ATTR_SCHED_BASE_TIME])
		q->base_time = nla_get_s64(
			tb[TCA_TAPRIO_ATTR_SCHED_BASE_TIME]);

	if (tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID]) {
		clockid = nla_get_s32(tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID]);

		/* We only support static clockids and we don't allow
		 * for it to be modified after the first init.
		 */
		if (clockid < 0 || (q->clockid != -1 && q->clockid != clockid))
			return -EINVAL;

		q->clockid = clockid;
	}

	if (tb[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST])
		err = parse_sched_list(
			tb[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST], q, extack);
	else if (tb[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY])
		err = parse_sched_single_entry(
			tb[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY], q, extack);

	/* parse_sched_* return the number of entries in the schedule,
	 * a schedule with zero entries is an error.
	 */
	if (err == 0) {
		NL_SET_ERR_MSG(extack, "The schedule should contain at least one entry");
		return -EINVAL;
	}

	return err;
}

static int taprio_parse_mqprio_opt(struct net_device *dev,
				   struct tc_mqprio_qopt *qopt,
				   struct netlink_ext_ack *extack)
{
	int i, j;

	if (!qopt) {
		NL_SET_ERR_MSG(extack, "'mqprio' configuration is necessary");
		return -EINVAL;
	}

	/* Verify num_tc is not out of max range */
	if (qopt->num_tc > TC_MAX_QUEUE) {
		NL_SET_ERR_MSG(extack, "Number of traffic classes is outside valid range");
		return -EINVAL;
	}

	/* taprio imposes that traffic classes map 1:n to tx queues */
	if (qopt->num_tc > dev->num_tx_queues) {
		NL_SET_ERR_MSG(extack, "Number of traffic classes is greater than number of HW queues");
		return -EINVAL;
	}

	/* Verify priority mapping uses valid tcs */
	for (i = 0; i < TC_BITMASK + 1; i++) {
		if (qopt->prio_tc_map[i] >= qopt->num_tc) {
			NL_SET_ERR_MSG(extack, "Invalid traffic class in priority to traffic class mapping");
			return -EINVAL;
		}
	}

	for (i = 0; i < qopt->num_tc; i++) {
		unsigned int last = qopt->offset[i] + qopt->count[i];

		/* Verify the queue count is in tx range being equal to the
		 * real_num_tx_queues indicates the last queue is in use.
		 */
		if (qopt->offset[i] >= dev->num_tx_queues ||
		    !qopt->count[i] ||
		    last > dev->real_num_tx_queues) {
			NL_SET_ERR_MSG(extack, "Invalid queue in traffic class to queue mapping");
			return -EINVAL;
		}

		/* Verify that the offset and counts do not overlap */
		for (j = i + 1; j < qopt->num_tc; j++) {
			if (last > qopt->offset[j]) {
				NL_SET_ERR_MSG(extack, "Detected overlap in the traffic class to queue mapping");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static ktime_t taprio_get_start_time(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct sched_entry *entry;
	ktime_t now, base, cycle;
	s64 n;

	base = ns_to_ktime(q->base_time);
	cycle = 0;

	/* Calculate the cycle_time, by summing all the intervals.
	 */
	list_for_each_entry(entry, &q->entries, list)
		cycle = ktime_add_ns(cycle, entry->interval);

	if (!cycle)
		return base;

	now = q->get_time();

	if (ktime_after(base, now))
		return base;

	/* Schedule the start time for the beginning of the next
	 * cycle.
	 */
	n = div64_s64(ktime_sub_ns(now, base), cycle);

	return ktime_add_ns(base, (n + 1) * cycle);
}

static void taprio_start_sched(struct Qdisc *sch, ktime_t start)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct sched_entry *first;
	unsigned long flags;

	spin_lock_irqsave(&q->current_entry_lock, flags);

	first = list_first_entry(&q->entries, struct sched_entry,
				 list);

	first->close_time = ktime_add_ns(start, first->interval);
	atomic_set(&first->budget,
		   (first->interval * 1000) / q->picos_per_byte);
	rcu_assign_pointer(q->current_entry, NULL);

	spin_unlock_irqrestore(&q->current_entry_lock, flags);

	hrtimer_start(&q->advance_timer, start, HRTIMER_MODE_ABS);
}

static int taprio_change(struct Qdisc *sch, struct nlattr *opt,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_TAPRIO_ATTR_MAX + 1] = { };
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct tc_mqprio_qopt *mqprio = NULL;
	struct ethtool_link_ksettings ecmd;
	int i, err, size;
	s64 link_speed;
	ktime_t start;

	err = nla_parse_nested(tb, TCA_TAPRIO_ATTR_MAX, opt,
			       taprio_policy, extack);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (tb[TCA_TAPRIO_ATTR_PRIOMAP])
		mqprio = nla_data(tb[TCA_TAPRIO_ATTR_PRIOMAP]);

	err = taprio_parse_mqprio_opt(dev, mqprio, extack);
	if (err < 0)
		return err;

	/* A schedule with less than one entry is an error */
	size = parse_taprio_opt(tb, q, extack);
	if (size < 0)
		return size;

	hrtimer_init(&q->advance_timer, q->clockid, HRTIMER_MODE_ABS);
	q->advance_timer.function = advance_sched;

	switch (q->clockid) {
	case CLOCK_REALTIME:
		q->get_time = ktime_get_real;
		break;
	case CLOCK_MONOTONIC:
		q->get_time = ktime_get;
		break;
	case CLOCK_BOOTTIME:
		q->get_time = ktime_get_boottime;
		break;
	case CLOCK_TAI:
		q->get_time = ktime_get_clocktai;
		break;
	default:
		return -ENOTSUPP;
	}

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *dev_queue;
		struct Qdisc *qdisc;

		dev_queue = netdev_get_tx_queue(dev, i);
		qdisc = qdisc_create_dflt(dev_queue,
					  &pfifo_qdisc_ops,
					  TC_H_MAKE(TC_H_MAJ(sch->handle),
						    TC_H_MIN(i + 1)),
					  extack);
		if (!qdisc)
			return -ENOMEM;

		if (i < dev->real_num_tx_queues)
			qdisc_hash_add(qdisc, false);

		q->qdiscs[i] = qdisc;
	}

	if (mqprio) {
		netdev_set_num_tc(dev, mqprio->num_tc);
		for (i = 0; i < mqprio->num_tc; i++)
			netdev_set_tc_queue(dev, i,
					    mqprio->count[i],
					    mqprio->offset[i]);

		/* Always use supplied priority mappings */
		for (i = 0; i < TC_BITMASK + 1; i++)
			netdev_set_prio_tc_map(dev, i,
					       mqprio->prio_tc_map[i]);
	}

	if (!__ethtool_get_link_ksettings(dev, &ecmd))
		link_speed = ecmd.base.speed;
	else
		link_speed = SPEED_1000;

	q->picos_per_byte = div64_s64(NSEC_PER_SEC * 1000LL * 8,
				      link_speed * 1000 * 1000);

	start = taprio_get_start_time(sch);
	if (!start)
		return 0;

	taprio_start_sched(sch, start);

	return 0;
}

static void taprio_destroy(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sched_entry *entry, *n;
	unsigned int i;

	hrtimer_cancel(&q->advance_timer);

	if (q->qdiscs) {
		for (i = 0; i < dev->num_tx_queues && q->qdiscs[i]; i++)
			qdisc_put(q->qdiscs[i]);

		kfree(q->qdiscs);
	}
	q->qdiscs = NULL;

	netdev_set_num_tc(dev, 0);

	list_for_each_entry_safe(entry, n, &q->entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}

static int taprio_init(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);

	INIT_LIST_HEAD(&q->entries);
	spin_lock_init(&q->current_entry_lock);

	/* We may overwrite the configuration later */
	hrtimer_init(&q->advance_timer, CLOCK_TAI, HRTIMER_MODE_ABS);

	q->root = sch;

	/* We only support static clockids. Use an invalid value as default
	 * and get the valid one on taprio_change().
	 */
	q->clockid = -1;

	if (sch->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	if (!netif_is_multiqueue(dev))
		return -EOPNOTSUPP;

	/* pre-allocate qdisc, attachment can't fail */
	q->qdiscs = kcalloc(dev->num_tx_queues,
			    sizeof(q->qdiscs[0]),
			    GFP_KERNEL);

	if (!q->qdiscs)
		return -ENOMEM;

	if (!opt)
		return -EINVAL;

	return taprio_change(sch, opt, extack);
}

static struct netdev_queue *taprio_queue_get(struct Qdisc *sch,
					     unsigned long cl)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx = cl - 1;

	if (ntx >= dev->num_tx_queues)
		return NULL;

	return netdev_get_tx_queue(dev, ntx);
}

static int taprio_graft(struct Qdisc *sch, unsigned long cl,
			struct Qdisc *new, struct Qdisc **old,
			struct netlink_ext_ack *extack)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct netdev_queue *dev_queue = taprio_queue_get(sch, cl);

	if (!dev_queue)
		return -EINVAL;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	*old = q->qdiscs[cl - 1];
	q->qdiscs[cl - 1] = new;

	if (new)
		new->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;

	if (dev->flags & IFF_UP)
		dev_activate(dev);

	return 0;
}

static int dump_entry(struct sk_buff *msg,
		      const struct sched_entry *entry)
{
	struct nlattr *item;

	item = nla_nest_start(msg, TCA_TAPRIO_SCHED_ENTRY);
	if (!item)
		return -ENOSPC;

	if (nla_put_u32(msg, TCA_TAPRIO_SCHED_ENTRY_INDEX, entry->index))
		goto nla_put_failure;

	if (nla_put_u8(msg, TCA_TAPRIO_SCHED_ENTRY_CMD, entry->command))
		goto nla_put_failure;

	if (nla_put_u32(msg, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK,
			entry->gate_mask))
		goto nla_put_failure;

	if (nla_put_u32(msg, TCA_TAPRIO_SCHED_ENTRY_INTERVAL,
			entry->interval))
		goto nla_put_failure;

	return nla_nest_end(msg, item);

nla_put_failure:
	nla_nest_cancel(msg, item);
	return -1;
}

static int taprio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct tc_mqprio_qopt opt = { 0 };
	struct nlattr *nest, *entry_list;
	struct sched_entry *entry;
	unsigned int i;

	opt.num_tc = netdev_get_num_tc(dev);
	memcpy(opt.prio_tc_map, dev->prio_tc_map, sizeof(opt.prio_tc_map));

	for (i = 0; i < netdev_get_num_tc(dev); i++) {
		opt.count[i] = dev->tc_to_txq[i].count;
		opt.offset[i] = dev->tc_to_txq[i].offset;
	}

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		return -ENOSPC;

	if (nla_put(skb, TCA_TAPRIO_ATTR_PRIOMAP, sizeof(opt), &opt))
		goto options_error;

	if (nla_put_s64(skb, TCA_TAPRIO_ATTR_SCHED_BASE_TIME,
			q->base_time, TCA_TAPRIO_PAD))
		goto options_error;

	if (nla_put_s32(skb, TCA_TAPRIO_ATTR_SCHED_CLOCKID, q->clockid))
		goto options_error;

	entry_list = nla_nest_start(skb, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST);
	if (!entry_list)
		goto options_error;

	list_for_each_entry(entry, &q->entries, list) {
		if (dump_entry(skb, entry) < 0)
			goto options_error;
	}

	nla_nest_end(skb, entry_list);

	return nla_nest_end(skb, nest);

options_error:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct Qdisc *taprio_leaf(struct Qdisc *sch, unsigned long cl)
{
	struct netdev_queue *dev_queue = taprio_queue_get(sch, cl);

	if (!dev_queue)
		return NULL;

	return dev_queue->qdisc_sleeping;
}

static unsigned long taprio_find(struct Qdisc *sch, u32 classid)
{
	unsigned int ntx = TC_H_MIN(classid);

	if (!taprio_queue_get(sch, ntx))
		return 0;
	return ntx;
}

static int taprio_dump_class(struct Qdisc *sch, unsigned long cl,
			     struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netdev_queue *dev_queue = taprio_queue_get(sch, cl);

	tcm->tcm_parent = TC_H_ROOT;
	tcm->tcm_handle |= TC_H_MIN(cl);
	tcm->tcm_info = dev_queue->qdisc_sleeping->handle;

	return 0;
}

static int taprio_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				   struct gnet_dump *d)
	__releases(d->lock)
	__acquires(d->lock)
{
	struct netdev_queue *dev_queue = taprio_queue_get(sch, cl);

	sch = dev_queue->qdisc_sleeping;
	if (gnet_stats_copy_basic(&sch->running, d, NULL, &sch->bstats) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &sch->qstats, sch->q.qlen) < 0)
		return -1;
	return 0;
}

static void taprio_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx;

	if (arg->stop)
		return;

	arg->count = arg->skip;
	for (ntx = arg->skip; ntx < dev->num_tx_queues; ntx++) {
		if (arg->fn(sch, ntx + 1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static struct netdev_queue *taprio_select_queue(struct Qdisc *sch,
						struct tcmsg *tcm)
{
	return taprio_queue_get(sch, TC_H_MIN(tcm->tcm_parent));
}

static const struct Qdisc_class_ops taprio_class_ops = {
	.graft		= taprio_graft,
	.leaf		= taprio_leaf,
	.find		= taprio_find,
	.walk		= taprio_walk,
	.dump		= taprio_dump_class,
	.dump_stats	= taprio_dump_class_stats,
	.select_queue	= taprio_select_queue,
};

static struct Qdisc_ops taprio_qdisc_ops __read_mostly = {
	.cl_ops		= &taprio_class_ops,
	.id		= "taprio",
	.priv_size	= sizeof(struct taprio_sched),
	.init		= taprio_init,
	.destroy	= taprio_destroy,
	.peek		= taprio_peek,
	.dequeue	= taprio_dequeue,
	.enqueue	= taprio_enqueue,
	.dump		= taprio_dump,
	.owner		= THIS_MODULE,
};

static int __init taprio_module_init(void)
{
	return register_qdisc(&taprio_qdisc_ops);
}

static void __exit taprio_module_exit(void)
{
	unregister_qdisc(&taprio_qdisc_ops);
}

module_init(taprio_module_init);
module_exit(taprio_module_exit);
MODULE_LICENSE("GPL");

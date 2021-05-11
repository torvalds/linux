// SPDX-License-Identifier: GPL-2.0

/* net/sched/sch_taprio.c	 Time Aware Priority Scheduler
 *
 * Authors:	Vinicius Costa Gomes <vinicius.gomes@intel.com>
 *
 */

#include <linux/ethtool.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/sch_generic.h>
#include <net/sock.h>
#include <net/tcp.h>

static LIST_HEAD(taprio_list);
static DEFINE_SPINLOCK(taprio_list_lock);

#define TAPRIO_ALL_GATES_OPEN -1

#define TXTIME_ASSIST_IS_ENABLED(flags) ((flags) & TCA_TAPRIO_ATTR_FLAG_TXTIME_ASSIST)
#define FULL_OFFLOAD_IS_ENABLED(flags) ((flags) & TCA_TAPRIO_ATTR_FLAG_FULL_OFFLOAD)
#define TAPRIO_FLAGS_INVALID U32_MAX

struct sched_entry {
	struct list_head list;

	/* The instant that this entry "closes" and the next one
	 * should open, the qdisc will make some effort so that no
	 * packet leaves after this time.
	 */
	ktime_t close_time;
	ktime_t next_txtime;
	atomic_t budget;
	int index;
	u32 gate_mask;
	u32 interval;
	u8 command;
};

struct sched_gate_list {
	struct rcu_head rcu;
	struct list_head entries;
	size_t num_entries;
	ktime_t cycle_close_time;
	s64 cycle_time;
	s64 cycle_time_extension;
	s64 base_time;
};

struct taprio_sched {
	struct Qdisc **qdiscs;
	struct Qdisc *root;
	u32 flags;
	enum tk_offsets tk_offset;
	int clockid;
	atomic64_t picos_per_byte; /* Using picoseconds because for 10Gbps+
				    * speeds it's sub-nanoseconds per byte
				    */

	/* Protects the update side of the RCU protected current_entry */
	spinlock_t current_entry_lock;
	struct sched_entry __rcu *current_entry;
	struct sched_gate_list __rcu *oper_sched;
	struct sched_gate_list __rcu *admin_sched;
	struct hrtimer advance_timer;
	struct list_head taprio_list;
	struct sk_buff *(*dequeue)(struct Qdisc *sch);
	struct sk_buff *(*peek)(struct Qdisc *sch);
	u32 txtime_delay;
};

struct __tc_taprio_qopt_offload {
	refcount_t users;
	struct tc_taprio_qopt_offload offload;
};

static ktime_t sched_base_time(const struct sched_gate_list *sched)
{
	if (!sched)
		return KTIME_MAX;

	return ns_to_ktime(sched->base_time);
}

static ktime_t taprio_get_time(struct taprio_sched *q)
{
	ktime_t mono = ktime_get();

	switch (q->tk_offset) {
	case TK_OFFS_MAX:
		return mono;
	default:
		return ktime_mono_to_any(mono, q->tk_offset);
	}

	return KTIME_MAX;
}

static void taprio_free_sched_cb(struct rcu_head *head)
{
	struct sched_gate_list *sched = container_of(head, struct sched_gate_list, rcu);
	struct sched_entry *entry, *n;

	list_for_each_entry_safe(entry, n, &sched->entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	kfree(sched);
}

static void switch_schedules(struct taprio_sched *q,
			     struct sched_gate_list **admin,
			     struct sched_gate_list **oper)
{
	rcu_assign_pointer(q->oper_sched, *admin);
	rcu_assign_pointer(q->admin_sched, NULL);

	if (*oper)
		call_rcu(&(*oper)->rcu, taprio_free_sched_cb);

	*oper = *admin;
	*admin = NULL;
}

/* Get how much time has been already elapsed in the current cycle. */
static s32 get_cycle_time_elapsed(struct sched_gate_list *sched, ktime_t time)
{
	ktime_t time_since_sched_start;
	s32 time_elapsed;

	time_since_sched_start = ktime_sub(time, sched->base_time);
	div_s64_rem(time_since_sched_start, sched->cycle_time, &time_elapsed);

	return time_elapsed;
}

static ktime_t get_interval_end_time(struct sched_gate_list *sched,
				     struct sched_gate_list *admin,
				     struct sched_entry *entry,
				     ktime_t intv_start)
{
	s32 cycle_elapsed = get_cycle_time_elapsed(sched, intv_start);
	ktime_t intv_end, cycle_ext_end, cycle_end;

	cycle_end = ktime_add_ns(intv_start, sched->cycle_time - cycle_elapsed);
	intv_end = ktime_add_ns(intv_start, entry->interval);
	cycle_ext_end = ktime_add(cycle_end, sched->cycle_time_extension);

	if (ktime_before(intv_end, cycle_end))
		return intv_end;
	else if (admin && admin != sched &&
		 ktime_after(admin->base_time, cycle_end) &&
		 ktime_before(admin->base_time, cycle_ext_end))
		return admin->base_time;
	else
		return cycle_end;
}

static int length_to_duration(struct taprio_sched *q, int len)
{
	return div_u64(len * atomic64_read(&q->picos_per_byte), 1000);
}

/* Returns the entry corresponding to next available interval. If
 * validate_interval is set, it only validates whether the timestamp occurs
 * when the gate corresponding to the skb's traffic class is open.
 */
static struct sched_entry *find_entry_to_transmit(struct sk_buff *skb,
						  struct Qdisc *sch,
						  struct sched_gate_list *sched,
						  struct sched_gate_list *admin,
						  ktime_t time,
						  ktime_t *interval_start,
						  ktime_t *interval_end,
						  bool validate_interval)
{
	ktime_t curr_intv_start, curr_intv_end, cycle_end, packet_transmit_time;
	ktime_t earliest_txtime = KTIME_MAX, txtime, cycle, transmit_end_time;
	struct sched_entry *entry = NULL, *entry_found = NULL;
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	bool entry_available = false;
	s32 cycle_elapsed;
	int tc, n;

	tc = netdev_get_prio_tc_map(dev, skb->priority);
	packet_transmit_time = length_to_duration(q, qdisc_pkt_len(skb));

	*interval_start = 0;
	*interval_end = 0;

	if (!sched)
		return NULL;

	cycle = sched->cycle_time;
	cycle_elapsed = get_cycle_time_elapsed(sched, time);
	curr_intv_end = ktime_sub_ns(time, cycle_elapsed);
	cycle_end = ktime_add_ns(curr_intv_end, cycle);

	list_for_each_entry(entry, &sched->entries, list) {
		curr_intv_start = curr_intv_end;
		curr_intv_end = get_interval_end_time(sched, admin, entry,
						      curr_intv_start);

		if (ktime_after(curr_intv_start, cycle_end))
			break;

		if (!(entry->gate_mask & BIT(tc)) ||
		    packet_transmit_time > entry->interval)
			continue;

		txtime = entry->next_txtime;

		if (ktime_before(txtime, time) || validate_interval) {
			transmit_end_time = ktime_add_ns(time, packet_transmit_time);
			if ((ktime_before(curr_intv_start, time) &&
			     ktime_before(transmit_end_time, curr_intv_end)) ||
			    (ktime_after(curr_intv_start, time) && !validate_interval)) {
				entry_found = entry;
				*interval_start = curr_intv_start;
				*interval_end = curr_intv_end;
				break;
			} else if (!entry_available && !validate_interval) {
				/* Here, we are just trying to find out the
				 * first available interval in the next cycle.
				 */
				entry_available = true;
				entry_found = entry;
				*interval_start = ktime_add_ns(curr_intv_start, cycle);
				*interval_end = ktime_add_ns(curr_intv_end, cycle);
			}
		} else if (ktime_before(txtime, earliest_txtime) &&
			   !entry_available) {
			earliest_txtime = txtime;
			entry_found = entry;
			n = div_s64(ktime_sub(txtime, curr_intv_start), cycle);
			*interval_start = ktime_add(curr_intv_start, n * cycle);
			*interval_end = ktime_add(curr_intv_end, n * cycle);
		}
	}

	return entry_found;
}

static bool is_valid_interval(struct sk_buff *skb, struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct sched_gate_list *sched, *admin;
	ktime_t interval_start, interval_end;
	struct sched_entry *entry;

	rcu_read_lock();
	sched = rcu_dereference(q->oper_sched);
	admin = rcu_dereference(q->admin_sched);

	entry = find_entry_to_transmit(skb, sch, sched, admin, skb->tstamp,
				       &interval_start, &interval_end, true);
	rcu_read_unlock();

	return entry;
}

static bool taprio_flags_valid(u32 flags)
{
	/* Make sure no other flag bits are set. */
	if (flags & ~(TCA_TAPRIO_ATTR_FLAG_TXTIME_ASSIST |
		      TCA_TAPRIO_ATTR_FLAG_FULL_OFFLOAD))
		return false;
	/* txtime-assist and full offload are mutually exclusive */
	if ((flags & TCA_TAPRIO_ATTR_FLAG_TXTIME_ASSIST) &&
	    (flags & TCA_TAPRIO_ATTR_FLAG_FULL_OFFLOAD))
		return false;
	return true;
}

/* This returns the tstamp value set by TCP in terms of the set clock. */
static ktime_t get_tcp_tstamp(struct taprio_sched *q, struct sk_buff *skb)
{
	unsigned int offset = skb_network_offset(skb);
	const struct ipv6hdr *ipv6h;
	const struct iphdr *iph;
	struct ipv6hdr _ipv6h;

	ipv6h = skb_header_pointer(skb, offset, sizeof(_ipv6h), &_ipv6h);
	if (!ipv6h)
		return 0;

	if (ipv6h->version == 4) {
		iph = (struct iphdr *)ipv6h;
		offset += iph->ihl * 4;

		/* special-case 6in4 tunnelling, as that is a common way to get
		 * v6 connectivity in the home
		 */
		if (iph->protocol == IPPROTO_IPV6) {
			ipv6h = skb_header_pointer(skb, offset,
						   sizeof(_ipv6h), &_ipv6h);

			if (!ipv6h || ipv6h->nexthdr != IPPROTO_TCP)
				return 0;
		} else if (iph->protocol != IPPROTO_TCP) {
			return 0;
		}
	} else if (ipv6h->version == 6 && ipv6h->nexthdr != IPPROTO_TCP) {
		return 0;
	}

	return ktime_mono_to_any(skb->skb_mstamp_ns, q->tk_offset);
}

/* There are a few scenarios where we will have to modify the txtime from
 * what is read from next_txtime in sched_entry. They are:
 * 1. If txtime is in the past,
 *    a. The gate for the traffic class is currently open and packet can be
 *       transmitted before it closes, schedule the packet right away.
 *    b. If the gate corresponding to the traffic class is going to open later
 *       in the cycle, set the txtime of packet to the interval start.
 * 2. If txtime is in the future, there are packets corresponding to the
 *    current traffic class waiting to be transmitted. So, the following
 *    possibilities exist:
 *    a. We can transmit the packet before the window containing the txtime
 *       closes.
 *    b. The window might close before the transmission can be completed
 *       successfully. So, schedule the packet in the next open window.
 */
static long get_packet_txtime(struct sk_buff *skb, struct Qdisc *sch)
{
	ktime_t transmit_end_time, interval_end, interval_start, tcp_tstamp;
	struct taprio_sched *q = qdisc_priv(sch);
	struct sched_gate_list *sched, *admin;
	ktime_t minimum_time, now, txtime;
	int len, packet_transmit_time;
	struct sched_entry *entry;
	bool sched_changed;

	now = taprio_get_time(q);
	minimum_time = ktime_add_ns(now, q->txtime_delay);

	tcp_tstamp = get_tcp_tstamp(q, skb);
	minimum_time = max_t(ktime_t, minimum_time, tcp_tstamp);

	rcu_read_lock();
	admin = rcu_dereference(q->admin_sched);
	sched = rcu_dereference(q->oper_sched);
	if (admin && ktime_after(minimum_time, admin->base_time))
		switch_schedules(q, &admin, &sched);

	/* Until the schedule starts, all the queues are open */
	if (!sched || ktime_before(minimum_time, sched->base_time)) {
		txtime = minimum_time;
		goto done;
	}

	len = qdisc_pkt_len(skb);
	packet_transmit_time = length_to_duration(q, len);

	do {
		sched_changed = false;

		entry = find_entry_to_transmit(skb, sch, sched, admin,
					       minimum_time,
					       &interval_start, &interval_end,
					       false);
		if (!entry) {
			txtime = 0;
			goto done;
		}

		txtime = entry->next_txtime;
		txtime = max_t(ktime_t, txtime, minimum_time);
		txtime = max_t(ktime_t, txtime, interval_start);

		if (admin && admin != sched &&
		    ktime_after(txtime, admin->base_time)) {
			sched = admin;
			sched_changed = true;
			continue;
		}

		transmit_end_time = ktime_add(txtime, packet_transmit_time);
		minimum_time = transmit_end_time;

		/* Update the txtime of current entry to the next time it's
		 * interval starts.
		 */
		if (ktime_after(transmit_end_time, interval_end))
			entry->next_txtime = ktime_add(interval_start, sched->cycle_time);
	} while (sched_changed || ktime_after(transmit_end_time, interval_end));

	entry->next_txtime = transmit_end_time;

done:
	rcu_read_unlock();
	return txtime;
}

static int taprio_enqueue_one(struct sk_buff *skb, struct Qdisc *sch,
			      struct Qdisc *child, struct sk_buff **to_free)
{
	struct taprio_sched *q = qdisc_priv(sch);

	if (skb->sk && sock_flag(skb->sk, SOCK_TXTIME)) {
		if (!is_valid_interval(skb, sch))
			return qdisc_drop(skb, sch, to_free);
	} else if (TXTIME_ASSIST_IS_ENABLED(q->flags)) {
		skb->tstamp = get_packet_txtime(skb, sch);
		if (!skb->tstamp)
			return qdisc_drop(skb, sch, to_free);
	}

	qdisc_qstats_backlog_inc(sch, skb);
	sch->q.qlen++;

	return qdisc_enqueue(skb, child, to_free);
}

static int taprio_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			  struct sk_buff **to_free)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct Qdisc *child;
	int queue;

	if (unlikely(FULL_OFFLOAD_IS_ENABLED(q->flags))) {
		WARN_ONCE(1, "Trying to enqueue skb into the root of a taprio qdisc configured with full offload\n");
		return qdisc_drop(skb, sch, to_free);
	}

	queue = skb_get_queue_mapping(skb);

	child = q->qdiscs[queue];
	if (unlikely(!child))
		return qdisc_drop(skb, sch, to_free);

	/* Large packets might not be transmitted when the transmission duration
	 * exceeds any configured interval. Therefore, segment the skb into
	 * smaller chunks. Skip it for the full offload case, as the driver
	 * and/or the hardware is expected to handle this.
	 */
	if (skb_is_gso(skb) && !FULL_OFFLOAD_IS_ENABLED(q->flags)) {
		unsigned int slen = 0, numsegs = 0, len = qdisc_pkt_len(skb);
		netdev_features_t features = netif_skb_features(skb);
		struct sk_buff *segs, *nskb;
		int ret;

		segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);
		if (IS_ERR_OR_NULL(segs))
			return qdisc_drop(skb, sch, to_free);

		skb_list_walk_safe(segs, segs, nskb) {
			skb_mark_not_on_list(segs);
			qdisc_skb_cb(segs)->pkt_len = segs->len;
			slen += segs->len;

			ret = taprio_enqueue_one(segs, sch, child, to_free);
			if (ret != NET_XMIT_SUCCESS) {
				if (net_xmit_drop_count(ret))
					qdisc_qstats_drop(sch);
			} else {
				numsegs++;
			}
		}

		if (numsegs > 1)
			qdisc_tree_reduce_backlog(sch, 1 - numsegs, len - slen);
		consume_skb(skb);

		return numsegs > 0 ? NET_XMIT_SUCCESS : NET_XMIT_DROP;
	}

	return taprio_enqueue_one(skb, sch, child, to_free);
}

static struct sk_buff *taprio_peek_soft(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sched_entry *entry;
	struct sk_buff *skb;
	u32 gate_mask;
	int i;

	rcu_read_lock();
	entry = rcu_dereference(q->current_entry);
	gate_mask = entry ? entry->gate_mask : TAPRIO_ALL_GATES_OPEN;
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

		if (TXTIME_ASSIST_IS_ENABLED(q->flags))
			return skb;

		prio = skb->priority;
		tc = netdev_get_prio_tc_map(dev, prio);

		if (!(gate_mask & BIT(tc)))
			continue;

		return skb;
	}

	return NULL;
}

static struct sk_buff *taprio_peek_offload(struct Qdisc *sch)
{
	WARN_ONCE(1, "Trying to peek into the root of a taprio qdisc configured with full offload\n");

	return NULL;
}

static struct sk_buff *taprio_peek(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);

	return q->peek(sch);
}

static void taprio_set_budget(struct taprio_sched *q, struct sched_entry *entry)
{
	atomic_set(&entry->budget,
		   div64_u64((u64)entry->interval * 1000,
			     atomic64_read(&q->picos_per_byte)));
}

static struct sk_buff *taprio_dequeue_soft(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sk_buff *skb = NULL;
	struct sched_entry *entry;
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

	if (!gate_mask)
		goto done;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct Qdisc *child = q->qdiscs[i];
		ktime_t guard;
		int prio;
		int len;
		u8 tc;

		if (unlikely(!child))
			continue;

		if (TXTIME_ASSIST_IS_ENABLED(q->flags)) {
			skb = child->ops->dequeue(child);
			if (!skb)
				continue;
			goto skb_found;
		}

		skb = child->ops->peek(child);
		if (!skb)
			continue;

		prio = skb->priority;
		tc = netdev_get_prio_tc_map(dev, prio);

		if (!(gate_mask & BIT(tc))) {
			skb = NULL;
			continue;
		}

		len = qdisc_pkt_len(skb);
		guard = ktime_add_ns(taprio_get_time(q),
				     length_to_duration(q, len));

		/* In the case that there's no gate entry, there's no
		 * guard band ...
		 */
		if (gate_mask != TAPRIO_ALL_GATES_OPEN &&
		    ktime_after(guard, entry->close_time)) {
			skb = NULL;
			continue;
		}

		/* ... and no budget. */
		if (gate_mask != TAPRIO_ALL_GATES_OPEN &&
		    atomic_sub_return(len, &entry->budget) < 0) {
			skb = NULL;
			continue;
		}

		skb = child->ops->dequeue(child);
		if (unlikely(!skb))
			goto done;

skb_found:
		qdisc_bstats_update(sch, skb);
		qdisc_qstats_backlog_dec(sch, skb);
		sch->q.qlen--;

		goto done;
	}

done:
	rcu_read_unlock();

	return skb;
}

static struct sk_buff *taprio_dequeue_offload(struct Qdisc *sch)
{
	WARN_ONCE(1, "Trying to dequeue from the root of a taprio qdisc configured with full offload\n");

	return NULL;
}

static struct sk_buff *taprio_dequeue(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);

	return q->dequeue(sch);
}

static bool should_restart_cycle(const struct sched_gate_list *oper,
				 const struct sched_entry *entry)
{
	if (list_is_last(&entry->list, &oper->entries))
		return true;

	if (ktime_compare(entry->close_time, oper->cycle_close_time) == 0)
		return true;

	return false;
}

static bool should_change_schedules(const struct sched_gate_list *admin,
				    const struct sched_gate_list *oper,
				    ktime_t close_time)
{
	ktime_t next_base_time, extension_time;

	if (!admin)
		return false;

	next_base_time = sched_base_time(admin);

	/* This is the simple case, the close_time would fall after
	 * the next schedule base_time.
	 */
	if (ktime_compare(next_base_time, close_time) <= 0)
		return true;

	/* This is the cycle_time_extension case, if the close_time
	 * plus the amount that can be extended would fall after the
	 * next schedule base_time, we can extend the current schedule
	 * for that amount.
	 */
	extension_time = ktime_add_ns(close_time, oper->cycle_time_extension);

	/* FIXME: the IEEE 802.1Q-2018 Specification isn't clear about
	 * how precisely the extension should be made. So after
	 * conformance testing, this logic may change.
	 */
	if (ktime_compare(next_base_time, extension_time) <= 0)
		return true;

	return false;
}

static enum hrtimer_restart advance_sched(struct hrtimer *timer)
{
	struct taprio_sched *q = container_of(timer, struct taprio_sched,
					      advance_timer);
	struct sched_gate_list *oper, *admin;
	struct sched_entry *entry, *next;
	struct Qdisc *sch = q->root;
	ktime_t close_time;

	spin_lock(&q->current_entry_lock);
	entry = rcu_dereference_protected(q->current_entry,
					  lockdep_is_held(&q->current_entry_lock));
	oper = rcu_dereference_protected(q->oper_sched,
					 lockdep_is_held(&q->current_entry_lock));
	admin = rcu_dereference_protected(q->admin_sched,
					  lockdep_is_held(&q->current_entry_lock));

	if (!oper)
		switch_schedules(q, &admin, &oper);

	/* This can happen in two cases: 1. this is the very first run
	 * of this function (i.e. we weren't running any schedule
	 * previously); 2. The previous schedule just ended. The first
	 * entry of all schedules are pre-calculated during the
	 * schedule initialization.
	 */
	if (unlikely(!entry || entry->close_time == oper->base_time)) {
		next = list_first_entry(&oper->entries, struct sched_entry,
					list);
		close_time = next->close_time;
		goto first_run;
	}

	if (should_restart_cycle(oper, entry)) {
		next = list_first_entry(&oper->entries, struct sched_entry,
					list);
		oper->cycle_close_time = ktime_add_ns(oper->cycle_close_time,
						      oper->cycle_time);
	} else {
		next = list_next_entry(entry, list);
	}

	close_time = ktime_add_ns(entry->close_time, next->interval);
	close_time = min_t(ktime_t, close_time, oper->cycle_close_time);

	if (should_change_schedules(admin, oper, close_time)) {
		/* Set things so the next time this runs, the new
		 * schedule runs.
		 */
		close_time = sched_base_time(admin);
		switch_schedules(q, &admin, &oper);
	}

	next->close_time = close_time;
	taprio_set_budget(q, next);

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

static const struct nla_policy taprio_policy[TCA_TAPRIO_ATTR_MAX + 1] = {
	[TCA_TAPRIO_ATTR_PRIOMAP]	       = {
		.len = sizeof(struct tc_mqprio_qopt)
	},
	[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST]           = { .type = NLA_NESTED },
	[TCA_TAPRIO_ATTR_SCHED_BASE_TIME]            = { .type = NLA_S64 },
	[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY]         = { .type = NLA_NESTED },
	[TCA_TAPRIO_ATTR_SCHED_CLOCKID]              = { .type = NLA_S32 },
	[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME]           = { .type = NLA_S64 },
	[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION] = { .type = NLA_S64 },
	[TCA_TAPRIO_ATTR_FLAGS]                      = { .type = NLA_U32 },
	[TCA_TAPRIO_ATTR_TXTIME_DELAY]		     = { .type = NLA_U32 },
};

static int fill_sched_entry(struct taprio_sched *q, struct nlattr **tb,
			    struct sched_entry *entry,
			    struct netlink_ext_ack *extack)
{
	int min_duration = length_to_duration(q, ETH_ZLEN);
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

	/* The interval should allow at least the minimum ethernet
	 * frame to go out.
	 */
	if (interval < min_duration) {
		NL_SET_ERR_MSG(extack, "Invalid interval for schedule entry");
		return -EINVAL;
	}

	entry->interval = interval;

	return 0;
}

static int parse_sched_entry(struct taprio_sched *q, struct nlattr *n,
			     struct sched_entry *entry, int index,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_TAPRIO_SCHED_ENTRY_MAX + 1] = { };
	int err;

	err = nla_parse_nested_deprecated(tb, TCA_TAPRIO_SCHED_ENTRY_MAX, n,
					  entry_policy, NULL);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Could not parse nested entry");
		return -EINVAL;
	}

	entry->index = index;

	return fill_sched_entry(q, tb, entry, extack);
}

static int parse_sched_list(struct taprio_sched *q, struct nlattr *list,
			    struct sched_gate_list *sched,
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

		err = parse_sched_entry(q, n, entry, i, extack);
		if (err < 0) {
			kfree(entry);
			return err;
		}

		list_add_tail(&entry->list, &sched->entries);
		i++;
	}

	sched->num_entries = i;

	return i;
}

static int parse_taprio_schedule(struct taprio_sched *q, struct nlattr **tb,
				 struct sched_gate_list *new,
				 struct netlink_ext_ack *extack)
{
	int err = 0;

	if (tb[TCA_TAPRIO_ATTR_SCHED_SINGLE_ENTRY]) {
		NL_SET_ERR_MSG(extack, "Adding a single entry is not supported");
		return -ENOTSUPP;
	}

	if (tb[TCA_TAPRIO_ATTR_SCHED_BASE_TIME])
		new->base_time = nla_get_s64(tb[TCA_TAPRIO_ATTR_SCHED_BASE_TIME]);

	if (tb[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION])
		new->cycle_time_extension = nla_get_s64(tb[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION]);

	if (tb[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME])
		new->cycle_time = nla_get_s64(tb[TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME]);

	if (tb[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST])
		err = parse_sched_list(q, tb[TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST],
				       new, extack);
	if (err < 0)
		return err;

	if (!new->cycle_time) {
		struct sched_entry *entry;
		ktime_t cycle = 0;

		list_for_each_entry(entry, &new->entries, list)
			cycle = ktime_add_ns(cycle, entry->interval);

		if (!cycle) {
			NL_SET_ERR_MSG(extack, "'cycle_time' can never be 0");
			return -EINVAL;
		}

		new->cycle_time = cycle;
	}

	return 0;
}

static int taprio_parse_mqprio_opt(struct net_device *dev,
				   struct tc_mqprio_qopt *qopt,
				   struct netlink_ext_ack *extack,
				   u32 taprio_flags)
{
	int i, j;

	if (!qopt && !dev->num_tc) {
		NL_SET_ERR_MSG(extack, "'mqprio' configuration is necessary");
		return -EINVAL;
	}

	/* If num_tc is already set, it means that the user already
	 * configured the mqprio part
	 */
	if (dev->num_tc)
		return 0;

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
	for (i = 0; i <= TC_BITMASK; i++) {
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

		if (TXTIME_ASSIST_IS_ENABLED(taprio_flags))
			continue;

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

static int taprio_get_start_time(struct Qdisc *sch,
				 struct sched_gate_list *sched,
				 ktime_t *start)
{
	struct taprio_sched *q = qdisc_priv(sch);
	ktime_t now, base, cycle;
	s64 n;

	base = sched_base_time(sched);
	now = taprio_get_time(q);

	if (ktime_after(base, now)) {
		*start = base;
		return 0;
	}

	cycle = sched->cycle_time;

	/* The qdisc is expected to have at least one sched_entry.  Moreover,
	 * any entry must have 'interval' > 0. Thus if the cycle time is zero,
	 * something went really wrong. In that case, we should warn about this
	 * inconsistent state and return error.
	 */
	if (WARN_ON(!cycle))
		return -EFAULT;

	/* Schedule the start time for the beginning of the next
	 * cycle.
	 */
	n = div64_s64(ktime_sub_ns(now, base), cycle);
	*start = ktime_add_ns(base, (n + 1) * cycle);
	return 0;
}

static void setup_first_close_time(struct taprio_sched *q,
				   struct sched_gate_list *sched, ktime_t base)
{
	struct sched_entry *first;
	ktime_t cycle;

	first = list_first_entry(&sched->entries,
				 struct sched_entry, list);

	cycle = sched->cycle_time;

	/* FIXME: find a better place to do this */
	sched->cycle_close_time = ktime_add_ns(base, cycle);

	first->close_time = ktime_add_ns(base, first->interval);
	taprio_set_budget(q, first);
	rcu_assign_pointer(q->current_entry, NULL);
}

static void taprio_start_sched(struct Qdisc *sch,
			       ktime_t start, struct sched_gate_list *new)
{
	struct taprio_sched *q = qdisc_priv(sch);
	ktime_t expires;

	if (FULL_OFFLOAD_IS_ENABLED(q->flags))
		return;

	expires = hrtimer_get_expires(&q->advance_timer);
	if (expires == 0)
		expires = KTIME_MAX;

	/* If the new schedule starts before the next expiration, we
	 * reprogram it to the earliest one, so we change the admin
	 * schedule to the operational one at the right time.
	 */
	start = min_t(ktime_t, start, expires);

	hrtimer_start(&q->advance_timer, start, HRTIMER_MODE_ABS);
}

static void taprio_set_picos_per_byte(struct net_device *dev,
				      struct taprio_sched *q)
{
	struct ethtool_link_ksettings ecmd;
	int speed = SPEED_10;
	int picos_per_byte;
	int err;

	err = __ethtool_get_link_ksettings(dev, &ecmd);
	if (err < 0)
		goto skip;

	if (ecmd.base.speed && ecmd.base.speed != SPEED_UNKNOWN)
		speed = ecmd.base.speed;

skip:
	picos_per_byte = (USEC_PER_SEC * 8) / speed;

	atomic64_set(&q->picos_per_byte, picos_per_byte);
	netdev_dbg(dev, "taprio: set %s's picos_per_byte to: %lld, linkspeed: %d\n",
		   dev->name, (long long)atomic64_read(&q->picos_per_byte),
		   ecmd.base.speed);
}

static int taprio_dev_notifier(struct notifier_block *nb, unsigned long event,
			       void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net_device *qdev;
	struct taprio_sched *q;
	bool found = false;

	ASSERT_RTNL();

	if (event != NETDEV_UP && event != NETDEV_CHANGE)
		return NOTIFY_DONE;

	spin_lock(&taprio_list_lock);
	list_for_each_entry(q, &taprio_list, taprio_list) {
		qdev = qdisc_dev(q->root);
		if (qdev == dev) {
			found = true;
			break;
		}
	}
	spin_unlock(&taprio_list_lock);

	if (found)
		taprio_set_picos_per_byte(dev, q);

	return NOTIFY_DONE;
}

static void setup_txtime(struct taprio_sched *q,
			 struct sched_gate_list *sched, ktime_t base)
{
	struct sched_entry *entry;
	u32 interval = 0;

	list_for_each_entry(entry, &sched->entries, list) {
		entry->next_txtime = ktime_add_ns(base, interval);
		interval += entry->interval;
	}
}

static struct tc_taprio_qopt_offload *taprio_offload_alloc(int num_entries)
{
	struct __tc_taprio_qopt_offload *__offload;

	__offload = kzalloc(struct_size(__offload, offload.entries, num_entries),
			    GFP_KERNEL);
	if (!__offload)
		return NULL;

	refcount_set(&__offload->users, 1);

	return &__offload->offload;
}

struct tc_taprio_qopt_offload *taprio_offload_get(struct tc_taprio_qopt_offload
						  *offload)
{
	struct __tc_taprio_qopt_offload *__offload;

	__offload = container_of(offload, struct __tc_taprio_qopt_offload,
				 offload);

	refcount_inc(&__offload->users);

	return offload;
}
EXPORT_SYMBOL_GPL(taprio_offload_get);

void taprio_offload_free(struct tc_taprio_qopt_offload *offload)
{
	struct __tc_taprio_qopt_offload *__offload;

	__offload = container_of(offload, struct __tc_taprio_qopt_offload,
				 offload);

	if (!refcount_dec_and_test(&__offload->users))
		return;

	kfree(__offload);
}
EXPORT_SYMBOL_GPL(taprio_offload_free);

/* The function will only serve to keep the pointers to the "oper" and "admin"
 * schedules valid in relation to their base times, so when calling dump() the
 * users looks at the right schedules.
 * When using full offload, the admin configuration is promoted to oper at the
 * base_time in the PHC time domain.  But because the system time is not
 * necessarily in sync with that, we can't just trigger a hrtimer to call
 * switch_schedules at the right hardware time.
 * At the moment we call this by hand right away from taprio, but in the future
 * it will be useful to create a mechanism for drivers to notify taprio of the
 * offload state (PENDING, ACTIVE, INACTIVE) so it can be visible in dump().
 * This is left as TODO.
 */
static void taprio_offload_config_changed(struct taprio_sched *q)
{
	struct sched_gate_list *oper, *admin;

	spin_lock(&q->current_entry_lock);

	oper = rcu_dereference_protected(q->oper_sched,
					 lockdep_is_held(&q->current_entry_lock));
	admin = rcu_dereference_protected(q->admin_sched,
					  lockdep_is_held(&q->current_entry_lock));

	switch_schedules(q, &admin, &oper);

	spin_unlock(&q->current_entry_lock);
}

static u32 tc_map_to_queue_mask(struct net_device *dev, u32 tc_mask)
{
	u32 i, queue_mask = 0;

	for (i = 0; i < dev->num_tc; i++) {
		u32 offset, count;

		if (!(tc_mask & BIT(i)))
			continue;

		offset = dev->tc_to_txq[i].offset;
		count = dev->tc_to_txq[i].count;

		queue_mask |= GENMASK(offset + count - 1, offset);
	}

	return queue_mask;
}

static void taprio_sched_to_offload(struct net_device *dev,
				    struct sched_gate_list *sched,
				    struct tc_taprio_qopt_offload *offload)
{
	struct sched_entry *entry;
	int i = 0;

	offload->base_time = sched->base_time;
	offload->cycle_time = sched->cycle_time;
	offload->cycle_time_extension = sched->cycle_time_extension;

	list_for_each_entry(entry, &sched->entries, list) {
		struct tc_taprio_sched_entry *e = &offload->entries[i];

		e->command = entry->command;
		e->interval = entry->interval;
		e->gate_mask = tc_map_to_queue_mask(dev, entry->gate_mask);

		i++;
	}

	offload->num_entries = i;
}

static int taprio_enable_offload(struct net_device *dev,
				 struct taprio_sched *q,
				 struct sched_gate_list *sched,
				 struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	struct tc_taprio_qopt_offload *offload;
	int err = 0;

	if (!ops->ndo_setup_tc) {
		NL_SET_ERR_MSG(extack,
			       "Device does not support taprio offload");
		return -EOPNOTSUPP;
	}

	offload = taprio_offload_alloc(sched->num_entries);
	if (!offload) {
		NL_SET_ERR_MSG(extack,
			       "Not enough memory for enabling offload mode");
		return -ENOMEM;
	}
	offload->enable = 1;
	taprio_sched_to_offload(dev, sched, offload);

	err = ops->ndo_setup_tc(dev, TC_SETUP_QDISC_TAPRIO, offload);
	if (err < 0) {
		NL_SET_ERR_MSG(extack,
			       "Device failed to setup taprio offload");
		goto done;
	}

done:
	taprio_offload_free(offload);

	return err;
}

static int taprio_disable_offload(struct net_device *dev,
				  struct taprio_sched *q,
				  struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	struct tc_taprio_qopt_offload *offload;
	int err;

	if (!FULL_OFFLOAD_IS_ENABLED(q->flags))
		return 0;

	if (!ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	offload = taprio_offload_alloc(0);
	if (!offload) {
		NL_SET_ERR_MSG(extack,
			       "Not enough memory to disable offload mode");
		return -ENOMEM;
	}
	offload->enable = 0;

	err = ops->ndo_setup_tc(dev, TC_SETUP_QDISC_TAPRIO, offload);
	if (err < 0) {
		NL_SET_ERR_MSG(extack,
			       "Device failed to disable offload");
		goto out;
	}

out:
	taprio_offload_free(offload);

	return err;
}

/* If full offload is enabled, the only possible clockid is the net device's
 * PHC. For that reason, specifying a clockid through netlink is incorrect.
 * For txtime-assist, it is implicitly assumed that the device's PHC is kept
 * in sync with the specified clockid via a user space daemon such as phc2sys.
 * For both software taprio and txtime-assist, the clockid is used for the
 * hrtimer that advances the schedule and hence mandatory.
 */
static int taprio_parse_clockid(struct Qdisc *sch, struct nlattr **tb,
				struct netlink_ext_ack *extack)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	int err = -EINVAL;

	if (FULL_OFFLOAD_IS_ENABLED(q->flags)) {
		const struct ethtool_ops *ops = dev->ethtool_ops;
		struct ethtool_ts_info info = {
			.cmd = ETHTOOL_GET_TS_INFO,
			.phc_index = -1,
		};

		if (tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID]) {
			NL_SET_ERR_MSG(extack,
				       "The 'clockid' cannot be specified for full offload");
			goto out;
		}

		if (ops && ops->get_ts_info)
			err = ops->get_ts_info(dev, &info);

		if (err || info.phc_index < 0) {
			NL_SET_ERR_MSG(extack,
				       "Device does not have a PTP clock");
			err = -ENOTSUPP;
			goto out;
		}
	} else if (tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID]) {
		int clockid = nla_get_s32(tb[TCA_TAPRIO_ATTR_SCHED_CLOCKID]);

		/* We only support static clockids and we don't allow
		 * for it to be modified after the first init.
		 */
		if (clockid < 0 ||
		    (q->clockid != -1 && q->clockid != clockid)) {
			NL_SET_ERR_MSG(extack,
				       "Changing the 'clockid' of a running schedule is not supported");
			err = -ENOTSUPP;
			goto out;
		}

		switch (clockid) {
		case CLOCK_REALTIME:
			q->tk_offset = TK_OFFS_REAL;
			break;
		case CLOCK_MONOTONIC:
			q->tk_offset = TK_OFFS_MAX;
			break;
		case CLOCK_BOOTTIME:
			q->tk_offset = TK_OFFS_BOOT;
			break;
		case CLOCK_TAI:
			q->tk_offset = TK_OFFS_TAI;
			break;
		default:
			NL_SET_ERR_MSG(extack, "Invalid 'clockid'");
			err = -EINVAL;
			goto out;
		}

		q->clockid = clockid;
	} else {
		NL_SET_ERR_MSG(extack, "Specifying a 'clockid' is mandatory");
		goto out;
	}

	/* Everything went ok, return success. */
	err = 0;

out:
	return err;
}

static int taprio_mqprio_cmp(const struct net_device *dev,
			     const struct tc_mqprio_qopt *mqprio)
{
	int i;

	if (!mqprio || mqprio->num_tc != dev->num_tc)
		return -1;

	for (i = 0; i < mqprio->num_tc; i++)
		if (dev->tc_to_txq[i].count != mqprio->count[i] ||
		    dev->tc_to_txq[i].offset != mqprio->offset[i])
			return -1;

	for (i = 0; i <= TC_BITMASK; i++)
		if (dev->prio_tc_map[i] != mqprio->prio_tc_map[i])
			return -1;

	return 0;
}

/* The semantics of the 'flags' argument in relation to 'change()'
 * requests, are interpreted following two rules (which are applied in
 * this order): (1) an omitted 'flags' argument is interpreted as
 * zero; (2) the 'flags' of a "running" taprio instance cannot be
 * changed.
 */
static int taprio_new_flags(const struct nlattr *attr, u32 old,
			    struct netlink_ext_ack *extack)
{
	u32 new = 0;

	if (attr)
		new = nla_get_u32(attr);

	if (old != TAPRIO_FLAGS_INVALID && old != new) {
		NL_SET_ERR_MSG_MOD(extack, "Changing 'flags' of a running schedule is not supported");
		return -EOPNOTSUPP;
	}

	if (!taprio_flags_valid(new)) {
		NL_SET_ERR_MSG_MOD(extack, "Specified 'flags' are not valid");
		return -EINVAL;
	}

	return new;
}

static int taprio_change(struct Qdisc *sch, struct nlattr *opt,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_TAPRIO_ATTR_MAX + 1] = { };
	struct sched_gate_list *oper, *admin, *new_admin;
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct tc_mqprio_qopt *mqprio = NULL;
	unsigned long flags;
	ktime_t start;
	int i, err;

	err = nla_parse_nested_deprecated(tb, TCA_TAPRIO_ATTR_MAX, opt,
					  taprio_policy, extack);
	if (err < 0)
		return err;

	if (tb[TCA_TAPRIO_ATTR_PRIOMAP])
		mqprio = nla_data(tb[TCA_TAPRIO_ATTR_PRIOMAP]);

	err = taprio_new_flags(tb[TCA_TAPRIO_ATTR_FLAGS],
			       q->flags, extack);
	if (err < 0)
		return err;

	q->flags = err;

	err = taprio_parse_mqprio_opt(dev, mqprio, extack, q->flags);
	if (err < 0)
		return err;

	new_admin = kzalloc(sizeof(*new_admin), GFP_KERNEL);
	if (!new_admin) {
		NL_SET_ERR_MSG(extack, "Not enough memory for a new schedule");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&new_admin->entries);

	rcu_read_lock();
	oper = rcu_dereference(q->oper_sched);
	admin = rcu_dereference(q->admin_sched);
	rcu_read_unlock();

	/* no changes - no new mqprio settings */
	if (!taprio_mqprio_cmp(dev, mqprio))
		mqprio = NULL;

	if (mqprio && (oper || admin)) {
		NL_SET_ERR_MSG(extack, "Changing the traffic mapping of a running schedule is not supported");
		err = -ENOTSUPP;
		goto free_sched;
	}

	err = parse_taprio_schedule(q, tb, new_admin, extack);
	if (err < 0)
		goto free_sched;

	if (new_admin->num_entries == 0) {
		NL_SET_ERR_MSG(extack, "There should be at least one entry in the schedule");
		err = -EINVAL;
		goto free_sched;
	}

	err = taprio_parse_clockid(sch, tb, extack);
	if (err < 0)
		goto free_sched;

	taprio_set_picos_per_byte(dev, q);

	if (mqprio) {
		netdev_set_num_tc(dev, mqprio->num_tc);
		for (i = 0; i < mqprio->num_tc; i++)
			netdev_set_tc_queue(dev, i,
					    mqprio->count[i],
					    mqprio->offset[i]);

		/* Always use supplied priority mappings */
		for (i = 0; i <= TC_BITMASK; i++)
			netdev_set_prio_tc_map(dev, i,
					       mqprio->prio_tc_map[i]);
	}

	if (FULL_OFFLOAD_IS_ENABLED(q->flags))
		err = taprio_enable_offload(dev, q, new_admin, extack);
	else
		err = taprio_disable_offload(dev, q, extack);
	if (err)
		goto free_sched;

	/* Protects against enqueue()/dequeue() */
	spin_lock_bh(qdisc_lock(sch));

	if (tb[TCA_TAPRIO_ATTR_TXTIME_DELAY]) {
		if (!TXTIME_ASSIST_IS_ENABLED(q->flags)) {
			NL_SET_ERR_MSG_MOD(extack, "txtime-delay can only be set when txtime-assist mode is enabled");
			err = -EINVAL;
			goto unlock;
		}

		q->txtime_delay = nla_get_u32(tb[TCA_TAPRIO_ATTR_TXTIME_DELAY]);
	}

	if (!TXTIME_ASSIST_IS_ENABLED(q->flags) &&
	    !FULL_OFFLOAD_IS_ENABLED(q->flags) &&
	    !hrtimer_active(&q->advance_timer)) {
		hrtimer_init(&q->advance_timer, q->clockid, HRTIMER_MODE_ABS);
		q->advance_timer.function = advance_sched;
	}

	if (FULL_OFFLOAD_IS_ENABLED(q->flags)) {
		q->dequeue = taprio_dequeue_offload;
		q->peek = taprio_peek_offload;
	} else {
		/* Be sure to always keep the function pointers
		 * in a consistent state.
		 */
		q->dequeue = taprio_dequeue_soft;
		q->peek = taprio_peek_soft;
	}

	err = taprio_get_start_time(sch, new_admin, &start);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Internal error: failed get start time");
		goto unlock;
	}

	setup_txtime(q, new_admin, start);

	if (TXTIME_ASSIST_IS_ENABLED(q->flags)) {
		if (!oper) {
			rcu_assign_pointer(q->oper_sched, new_admin);
			err = 0;
			new_admin = NULL;
			goto unlock;
		}

		rcu_assign_pointer(q->admin_sched, new_admin);
		if (admin)
			call_rcu(&admin->rcu, taprio_free_sched_cb);
	} else {
		setup_first_close_time(q, new_admin, start);

		/* Protects against advance_sched() */
		spin_lock_irqsave(&q->current_entry_lock, flags);

		taprio_start_sched(sch, start, new_admin);

		rcu_assign_pointer(q->admin_sched, new_admin);
		if (admin)
			call_rcu(&admin->rcu, taprio_free_sched_cb);

		spin_unlock_irqrestore(&q->current_entry_lock, flags);

		if (FULL_OFFLOAD_IS_ENABLED(q->flags))
			taprio_offload_config_changed(q);
	}

	new_admin = NULL;
	err = 0;

unlock:
	spin_unlock_bh(qdisc_lock(sch));

free_sched:
	if (new_admin)
		call_rcu(&new_admin->rcu, taprio_free_sched_cb);

	return err;
}

static void taprio_reset(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	int i;

	hrtimer_cancel(&q->advance_timer);
	if (q->qdiscs) {
		for (i = 0; i < dev->num_tx_queues; i++)
			if (q->qdiscs[i])
				qdisc_reset(q->qdiscs[i]);
	}
	sch->qstats.backlog = 0;
	sch->q.qlen = 0;
}

static void taprio_destroy(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	unsigned int i;

	spin_lock(&taprio_list_lock);
	list_del(&q->taprio_list);
	spin_unlock(&taprio_list_lock);


	taprio_disable_offload(dev, q, NULL);

	if (q->qdiscs) {
		for (i = 0; i < dev->num_tx_queues; i++)
			qdisc_put(q->qdiscs[i]);

		kfree(q->qdiscs);
	}
	q->qdiscs = NULL;

	netdev_reset_tc(dev);

	if (q->oper_sched)
		call_rcu(&q->oper_sched->rcu, taprio_free_sched_cb);

	if (q->admin_sched)
		call_rcu(&q->admin_sched->rcu, taprio_free_sched_cb);
}

static int taprio_init(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	int i;

	spin_lock_init(&q->current_entry_lock);

	hrtimer_init(&q->advance_timer, CLOCK_TAI, HRTIMER_MODE_ABS);
	q->advance_timer.function = advance_sched;

	q->dequeue = taprio_dequeue_soft;
	q->peek = taprio_peek_soft;

	q->root = sch;

	/* We only support static clockids. Use an invalid value as default
	 * and get the valid one on taprio_change().
	 */
	q->clockid = -1;
	q->flags = TAPRIO_FLAGS_INVALID;

	spin_lock(&taprio_list_lock);
	list_add(&q->taprio_list, &taprio_list);
	spin_unlock(&taprio_list_lock);

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

	return taprio_change(sch, opt, extack);
}

static void taprio_attach(struct Qdisc *sch)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	unsigned int ntx;

	/* Attach underlying qdisc */
	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		struct Qdisc *qdisc = q->qdiscs[ntx];
		struct Qdisc *old;

		if (FULL_OFFLOAD_IS_ENABLED(q->flags)) {
			qdisc->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
			old = dev_graft_qdisc(qdisc->dev_queue, qdisc);
			if (ntx < dev->real_num_tx_queues)
				qdisc_hash_add(qdisc, false);
		} else {
			old = dev_graft_qdisc(qdisc->dev_queue, sch);
			qdisc_refcount_inc(sch);
		}
		if (old)
			qdisc_put(old);
	}

	/* access to the child qdiscs is not needed in offload mode */
	if (FULL_OFFLOAD_IS_ENABLED(q->flags)) {
		kfree(q->qdiscs);
		q->qdiscs = NULL;
	}
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

	if (FULL_OFFLOAD_IS_ENABLED(q->flags)) {
		*old = dev_graft_qdisc(dev_queue, new);
	} else {
		*old = q->qdiscs[cl - 1];
		q->qdiscs[cl - 1] = new;
	}

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

	item = nla_nest_start_noflag(msg, TCA_TAPRIO_SCHED_ENTRY);
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

static int dump_schedule(struct sk_buff *msg,
			 const struct sched_gate_list *root)
{
	struct nlattr *entry_list;
	struct sched_entry *entry;

	if (nla_put_s64(msg, TCA_TAPRIO_ATTR_SCHED_BASE_TIME,
			root->base_time, TCA_TAPRIO_PAD))
		return -1;

	if (nla_put_s64(msg, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME,
			root->cycle_time, TCA_TAPRIO_PAD))
		return -1;

	if (nla_put_s64(msg, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION,
			root->cycle_time_extension, TCA_TAPRIO_PAD))
		return -1;

	entry_list = nla_nest_start_noflag(msg,
					   TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST);
	if (!entry_list)
		goto error_nest;

	list_for_each_entry(entry, &root->entries, list) {
		if (dump_entry(msg, entry) < 0)
			goto error_nest;
	}

	nla_nest_end(msg, entry_list);
	return 0;

error_nest:
	nla_nest_cancel(msg, entry_list);
	return -1;
}

static int taprio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct taprio_sched *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct sched_gate_list *oper, *admin;
	struct tc_mqprio_qopt opt = { 0 };
	struct nlattr *nest, *sched_nest;
	unsigned int i;

	rcu_read_lock();
	oper = rcu_dereference(q->oper_sched);
	admin = rcu_dereference(q->admin_sched);

	opt.num_tc = netdev_get_num_tc(dev);
	memcpy(opt.prio_tc_map, dev->prio_tc_map, sizeof(opt.prio_tc_map));

	for (i = 0; i < netdev_get_num_tc(dev); i++) {
		opt.count[i] = dev->tc_to_txq[i].count;
		opt.offset[i] = dev->tc_to_txq[i].offset;
	}

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!nest)
		goto start_error;

	if (nla_put(skb, TCA_TAPRIO_ATTR_PRIOMAP, sizeof(opt), &opt))
		goto options_error;

	if (!FULL_OFFLOAD_IS_ENABLED(q->flags) &&
	    nla_put_s32(skb, TCA_TAPRIO_ATTR_SCHED_CLOCKID, q->clockid))
		goto options_error;

	if (q->flags && nla_put_u32(skb, TCA_TAPRIO_ATTR_FLAGS, q->flags))
		goto options_error;

	if (q->txtime_delay &&
	    nla_put_u32(skb, TCA_TAPRIO_ATTR_TXTIME_DELAY, q->txtime_delay))
		goto options_error;

	if (oper && dump_schedule(skb, oper))
		goto options_error;

	if (!admin)
		goto done;

	sched_nest = nla_nest_start_noflag(skb, TCA_TAPRIO_ATTR_ADMIN_SCHED);
	if (!sched_nest)
		goto options_error;

	if (dump_schedule(skb, admin))
		goto admin_error;

	nla_nest_end(skb, sched_nest);

done:
	rcu_read_unlock();

	return nla_nest_end(skb, nest);

admin_error:
	nla_nest_cancel(skb, sched_nest);

options_error:
	nla_nest_cancel(skb, nest);

start_error:
	rcu_read_unlock();
	return -ENOSPC;
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
	    qdisc_qstats_copy(d, sch) < 0)
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
	.change		= taprio_change,
	.destroy	= taprio_destroy,
	.reset		= taprio_reset,
	.attach		= taprio_attach,
	.peek		= taprio_peek,
	.dequeue	= taprio_dequeue,
	.enqueue	= taprio_enqueue,
	.dump		= taprio_dump,
	.owner		= THIS_MODULE,
};

static struct notifier_block taprio_device_notifier = {
	.notifier_call = taprio_dev_notifier,
};

static int __init taprio_module_init(void)
{
	int err = register_netdevice_notifier(&taprio_device_notifier);

	if (err)
		return err;

	return register_qdisc(&taprio_qdisc_ops);
}

static void __exit taprio_module_exit(void)
{
	unregister_qdisc(&taprio_qdisc_ops);
	unregister_netdevice_notifier(&taprio_device_notifier);
}

module_init(taprio_module_init);
module_exit(taprio_module_exit);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Monitoring code for network dropped packet alerts
 *
 * Copyright (C) 2009 Neil Horman <nhorman@tuxdriver.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/netpoll.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/netlink.h>
#include <linux/net_dropmon.h>
#include <linux/bitfield.h>
#include <linux/percpu.h>
#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/genetlink.h>
#include <net/netevent.h>
#include <net/flow_offload.h>
#include <net/dropreason.h>
#include <net/devlink.h>

#include <trace/events/skb.h>
#include <trace/events/napi.h>
#include <trace/events/devlink.h>

#include <asm/unaligned.h>

#define TRACE_ON 1
#define TRACE_OFF 0

/*
 * Globals, our netlink socket pointer
 * and the work handle that will send up
 * netlink alerts
 */
static int trace_state = TRACE_OFF;
static bool monitor_hw;

/* net_dm_mutex
 *
 * An overall lock guarding every operation coming from userspace.
 */
static DEFINE_MUTEX(net_dm_mutex);

struct net_dm_stats {
	u64_stats_t dropped;
	struct u64_stats_sync syncp;
};

#define NET_DM_MAX_HW_TRAP_NAME_LEN 40

struct net_dm_hw_entry {
	char trap_name[NET_DM_MAX_HW_TRAP_NAME_LEN];
	u32 count;
};

struct net_dm_hw_entries {
	u32 num_entries;
	struct net_dm_hw_entry entries[];
};

struct per_cpu_dm_data {
	raw_spinlock_t		lock;	/* Protects 'skb', 'hw_entries' and
					 * 'send_timer'
					 */
	union {
		struct sk_buff			*skb;
		struct net_dm_hw_entries	*hw_entries;
	};
	struct sk_buff_head	drop_queue;
	struct work_struct	dm_alert_work;
	struct timer_list	send_timer;
	struct net_dm_stats	stats;
};

struct dm_hw_stat_delta {
	unsigned long last_rx;
	unsigned long last_drop_val;
	struct rcu_head rcu;
};

static struct genl_family net_drop_monitor_family;

static DEFINE_PER_CPU(struct per_cpu_dm_data, dm_cpu_data);
static DEFINE_PER_CPU(struct per_cpu_dm_data, dm_hw_cpu_data);

static int dm_hit_limit = 64;
static int dm_delay = 1;
static unsigned long dm_hw_check_delta = 2*HZ;

static enum net_dm_alert_mode net_dm_alert_mode = NET_DM_ALERT_MODE_SUMMARY;
static u32 net_dm_trunc_len;
static u32 net_dm_queue_len = 1000;

struct net_dm_alert_ops {
	void (*kfree_skb_probe)(void *ignore, struct sk_buff *skb,
				void *location,
				enum skb_drop_reason reason,
				struct sock *rx_sk);
	void (*napi_poll_probe)(void *ignore, struct napi_struct *napi,
				int work, int budget);
	void (*work_item_func)(struct work_struct *work);
	void (*hw_work_item_func)(struct work_struct *work);
	void (*hw_trap_probe)(void *ignore, const struct devlink *devlink,
			      struct sk_buff *skb,
			      const struct devlink_trap_metadata *metadata);
};

struct net_dm_skb_cb {
	union {
		struct devlink_trap_metadata *hw_metadata;
		void *pc;
	};
	enum skb_drop_reason reason;
};

#define NET_DM_SKB_CB(__skb) ((struct net_dm_skb_cb *)&((__skb)->cb[0]))

static struct sk_buff *reset_per_cpu_data(struct per_cpu_dm_data *data)
{
	size_t al;
	struct net_dm_alert_msg *msg;
	struct nlattr *nla;
	struct sk_buff *skb;
	unsigned long flags;
	void *msg_header;

	al = sizeof(struct net_dm_alert_msg);
	al += dm_hit_limit * sizeof(struct net_dm_drop_point);
	al += sizeof(struct nlattr);

	skb = genlmsg_new(al, GFP_KERNEL);

	if (!skb)
		goto err;

	msg_header = genlmsg_put(skb, 0, 0, &net_drop_monitor_family,
				 0, NET_DM_CMD_ALERT);
	if (!msg_header) {
		nlmsg_free(skb);
		skb = NULL;
		goto err;
	}
	nla = nla_reserve(skb, NLA_UNSPEC,
			  sizeof(struct net_dm_alert_msg));
	if (!nla) {
		nlmsg_free(skb);
		skb = NULL;
		goto err;
	}
	msg = nla_data(nla);
	memset(msg, 0, al);
	goto out;

err:
	mod_timer(&data->send_timer, jiffies + HZ / 10);
out:
	raw_spin_lock_irqsave(&data->lock, flags);
	swap(data->skb, skb);
	raw_spin_unlock_irqrestore(&data->lock, flags);

	if (skb) {
		struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
		struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);

		genlmsg_end(skb, genlmsg_data(gnlh));
	}

	return skb;
}

static const struct genl_multicast_group dropmon_mcgrps[] = {
	{ .name = "events", .flags = GENL_MCAST_CAP_SYS_ADMIN, },
};

static void send_dm_alert(struct work_struct *work)
{
	struct sk_buff *skb;
	struct per_cpu_dm_data *data;

	data = container_of(work, struct per_cpu_dm_data, dm_alert_work);

	skb = reset_per_cpu_data(data);

	if (skb)
		genlmsg_multicast(&net_drop_monitor_family, skb, 0,
				  0, GFP_KERNEL);
}

/*
 * This is the timer function to delay the sending of an alert
 * in the event that more drops will arrive during the
 * hysteresis period.
 */
static void sched_send_work(struct timer_list *t)
{
	struct per_cpu_dm_data *data = from_timer(data, t, send_timer);

	schedule_work(&data->dm_alert_work);
}

static void trace_drop_common(struct sk_buff *skb, void *location)
{
	struct net_dm_alert_msg *msg;
	struct net_dm_drop_point *point;
	struct nlmsghdr *nlh;
	struct nlattr *nla;
	int i;
	struct sk_buff *dskb;
	struct per_cpu_dm_data *data;
	unsigned long flags;

	local_irq_save(flags);
	data = this_cpu_ptr(&dm_cpu_data);
	raw_spin_lock(&data->lock);
	dskb = data->skb;

	if (!dskb)
		goto out;

	nlh = (struct nlmsghdr *)dskb->data;
	nla = genlmsg_data(nlmsg_data(nlh));
	msg = nla_data(nla);
	point = msg->points;
	for (i = 0; i < msg->entries; i++) {
		if (!memcmp(&location, &point->pc, sizeof(void *))) {
			point->count++;
			goto out;
		}
		point++;
	}
	if (msg->entries == dm_hit_limit)
		goto out;
	/*
	 * We need to create a new entry
	 */
	__nla_reserve_nohdr(dskb, sizeof(struct net_dm_drop_point));
	nla->nla_len += NLA_ALIGN(sizeof(struct net_dm_drop_point));
	memcpy(point->pc, &location, sizeof(void *));
	point->count = 1;
	msg->entries++;

	if (!timer_pending(&data->send_timer)) {
		data->send_timer.expires = jiffies + dm_delay * HZ;
		add_timer(&data->send_timer);
	}

out:
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static void trace_kfree_skb_hit(void *ignore, struct sk_buff *skb,
				void *location,
				enum skb_drop_reason reason,
				struct sock *rx_sk)
{
	trace_drop_common(skb, location);
}

static void trace_napi_poll_hit(void *ignore, struct napi_struct *napi,
				int work, int budget)
{
	struct net_device *dev = napi->dev;
	struct dm_hw_stat_delta *stat;
	/*
	 * Don't check napi structures with no associated device
	 */
	if (!dev)
		return;

	rcu_read_lock();
	stat = rcu_dereference(dev->dm_private);
	if (stat) {
		/*
		 * only add a note to our monitor buffer if:
		 * 1) its after the last_rx delta
		 * 2) our rx_dropped count has gone up
		 */
		if (time_after(jiffies, stat->last_rx + dm_hw_check_delta) &&
		    (dev->stats.rx_dropped != stat->last_drop_val)) {
			trace_drop_common(NULL, NULL);
			stat->last_drop_val = dev->stats.rx_dropped;
			stat->last_rx = jiffies;
		}
	}
	rcu_read_unlock();
}

static struct net_dm_hw_entries *
net_dm_hw_reset_per_cpu_data(struct per_cpu_dm_data *hw_data)
{
	struct net_dm_hw_entries *hw_entries;
	unsigned long flags;

	hw_entries = kzalloc(struct_size(hw_entries, entries, dm_hit_limit),
			     GFP_KERNEL);
	if (!hw_entries) {
		/* If the memory allocation failed, we try to perform another
		 * allocation in 1/10 second. Otherwise, the probe function
		 * will constantly bail out.
		 */
		mod_timer(&hw_data->send_timer, jiffies + HZ / 10);
	}

	raw_spin_lock_irqsave(&hw_data->lock, flags);
	swap(hw_data->hw_entries, hw_entries);
	raw_spin_unlock_irqrestore(&hw_data->lock, flags);

	return hw_entries;
}

static int net_dm_hw_entry_put(struct sk_buff *msg,
			       const struct net_dm_hw_entry *hw_entry)
{
	struct nlattr *attr;

	attr = nla_nest_start(msg, NET_DM_ATTR_HW_ENTRY);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_string(msg, NET_DM_ATTR_HW_TRAP_NAME, hw_entry->trap_name))
		goto nla_put_failure;

	if (nla_put_u32(msg, NET_DM_ATTR_HW_TRAP_COUNT, hw_entry->count))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int net_dm_hw_entries_put(struct sk_buff *msg,
				 const struct net_dm_hw_entries *hw_entries)
{
	struct nlattr *attr;
	int i;

	attr = nla_nest_start(msg, NET_DM_ATTR_HW_ENTRIES);
	if (!attr)
		return -EMSGSIZE;

	for (i = 0; i < hw_entries->num_entries; i++) {
		int rc;

		rc = net_dm_hw_entry_put(msg, &hw_entries->entries[i]);
		if (rc)
			goto nla_put_failure;
	}

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int
net_dm_hw_summary_report_fill(struct sk_buff *msg,
			      const struct net_dm_hw_entries *hw_entries)
{
	struct net_dm_alert_msg anc_hdr = { 0 };
	void *hdr;
	int rc;

	hdr = genlmsg_put(msg, 0, 0, &net_drop_monitor_family, 0,
			  NET_DM_CMD_ALERT);
	if (!hdr)
		return -EMSGSIZE;

	/* We need to put the ancillary header in order not to break user
	 * space.
	 */
	if (nla_put(msg, NLA_UNSPEC, sizeof(anc_hdr), &anc_hdr))
		goto nla_put_failure;

	rc = net_dm_hw_entries_put(msg, hw_entries);
	if (rc)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void net_dm_hw_summary_work(struct work_struct *work)
{
	struct net_dm_hw_entries *hw_entries;
	struct per_cpu_dm_data *hw_data;
	struct sk_buff *msg;
	int rc;

	hw_data = container_of(work, struct per_cpu_dm_data, dm_alert_work);

	hw_entries = net_dm_hw_reset_per_cpu_data(hw_data);
	if (!hw_entries)
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out;

	rc = net_dm_hw_summary_report_fill(msg, hw_entries);
	if (rc) {
		nlmsg_free(msg);
		goto out;
	}

	genlmsg_multicast(&net_drop_monitor_family, msg, 0, 0, GFP_KERNEL);

out:
	kfree(hw_entries);
}

static void
net_dm_hw_trap_summary_probe(void *ignore, const struct devlink *devlink,
			     struct sk_buff *skb,
			     const struct devlink_trap_metadata *metadata)
{
	struct net_dm_hw_entries *hw_entries;
	struct net_dm_hw_entry *hw_entry;
	struct per_cpu_dm_data *hw_data;
	unsigned long flags;
	int i;

	if (metadata->trap_type == DEVLINK_TRAP_TYPE_CONTROL)
		return;

	hw_data = this_cpu_ptr(&dm_hw_cpu_data);
	raw_spin_lock_irqsave(&hw_data->lock, flags);
	hw_entries = hw_data->hw_entries;

	if (!hw_entries)
		goto out;

	for (i = 0; i < hw_entries->num_entries; i++) {
		hw_entry = &hw_entries->entries[i];
		if (!strncmp(hw_entry->trap_name, metadata->trap_name,
			     NET_DM_MAX_HW_TRAP_NAME_LEN - 1)) {
			hw_entry->count++;
			goto out;
		}
	}
	if (WARN_ON_ONCE(hw_entries->num_entries == dm_hit_limit))
		goto out;

	hw_entry = &hw_entries->entries[hw_entries->num_entries];
	strscpy(hw_entry->trap_name, metadata->trap_name,
		NET_DM_MAX_HW_TRAP_NAME_LEN - 1);
	hw_entry->count = 1;
	hw_entries->num_entries++;

	if (!timer_pending(&hw_data->send_timer)) {
		hw_data->send_timer.expires = jiffies + dm_delay * HZ;
		add_timer(&hw_data->send_timer);
	}

out:
	raw_spin_unlock_irqrestore(&hw_data->lock, flags);
}

static const struct net_dm_alert_ops net_dm_alert_summary_ops = {
	.kfree_skb_probe	= trace_kfree_skb_hit,
	.napi_poll_probe	= trace_napi_poll_hit,
	.work_item_func		= send_dm_alert,
	.hw_work_item_func	= net_dm_hw_summary_work,
	.hw_trap_probe		= net_dm_hw_trap_summary_probe,
};

static void net_dm_packet_trace_kfree_skb_hit(void *ignore,
					      struct sk_buff *skb,
					      void *location,
					      enum skb_drop_reason reason,
					      struct sock *rx_sk)
{
	ktime_t tstamp = ktime_get_real();
	struct per_cpu_dm_data *data;
	struct net_dm_skb_cb *cb;
	struct sk_buff *nskb;
	unsigned long flags;

	if (!skb_mac_header_was_set(skb))
		return;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return;

	cb = NET_DM_SKB_CB(nskb);
	cb->reason = reason;
	cb->pc = location;
	/* Override the timestamp because we care about the time when the
	 * packet was dropped.
	 */
	nskb->tstamp = tstamp;

	data = this_cpu_ptr(&dm_cpu_data);

	spin_lock_irqsave(&data->drop_queue.lock, flags);
	if (skb_queue_len(&data->drop_queue) < net_dm_queue_len)
		__skb_queue_tail(&data->drop_queue, nskb);
	else
		goto unlock_free;
	spin_unlock_irqrestore(&data->drop_queue.lock, flags);

	schedule_work(&data->dm_alert_work);

	return;

unlock_free:
	spin_unlock_irqrestore(&data->drop_queue.lock, flags);
	u64_stats_update_begin(&data->stats.syncp);
	u64_stats_inc(&data->stats.dropped);
	u64_stats_update_end(&data->stats.syncp);
	consume_skb(nskb);
}

static void net_dm_packet_trace_napi_poll_hit(void *ignore,
					      struct napi_struct *napi,
					      int work, int budget)
{
}

static size_t net_dm_in_port_size(void)
{
	       /* NET_DM_ATTR_IN_PORT nest */
	return nla_total_size(0) +
	       /* NET_DM_ATTR_PORT_NETDEV_IFINDEX */
	       nla_total_size(sizeof(u32)) +
	       /* NET_DM_ATTR_PORT_NETDEV_NAME */
	       nla_total_size(IFNAMSIZ + 1);
}

#define NET_DM_MAX_SYMBOL_LEN 40
#define NET_DM_MAX_REASON_LEN 50

static size_t net_dm_packet_report_size(size_t payload_len)
{
	size_t size;

	size = nlmsg_msg_size(GENL_HDRLEN + net_drop_monitor_family.hdrsize);

	return NLMSG_ALIGN(size) +
	       /* NET_DM_ATTR_ORIGIN */
	       nla_total_size(sizeof(u16)) +
	       /* NET_DM_ATTR_PC */
	       nla_total_size(sizeof(u64)) +
	       /* NET_DM_ATTR_SYMBOL */
	       nla_total_size(NET_DM_MAX_SYMBOL_LEN + 1) +
	       /* NET_DM_ATTR_IN_PORT */
	       net_dm_in_port_size() +
	       /* NET_DM_ATTR_TIMESTAMP */
	       nla_total_size(sizeof(u64)) +
	       /* NET_DM_ATTR_ORIG_LEN */
	       nla_total_size(sizeof(u32)) +
	       /* NET_DM_ATTR_PROTO */
	       nla_total_size(sizeof(u16)) +
	       /* NET_DM_ATTR_REASON */
	       nla_total_size(NET_DM_MAX_REASON_LEN + 1) +
	       /* NET_DM_ATTR_PAYLOAD */
	       nla_total_size(payload_len);
}

static int net_dm_packet_report_in_port_put(struct sk_buff *msg, int ifindex,
					    const char *name)
{
	struct nlattr *attr;

	attr = nla_nest_start(msg, NET_DM_ATTR_IN_PORT);
	if (!attr)
		return -EMSGSIZE;

	if (ifindex &&
	    nla_put_u32(msg, NET_DM_ATTR_PORT_NETDEV_IFINDEX, ifindex))
		goto nla_put_failure;

	if (name && nla_put_string(msg, NET_DM_ATTR_PORT_NETDEV_NAME, name))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int net_dm_packet_report_fill(struct sk_buff *msg, struct sk_buff *skb,
				     size_t payload_len)
{
	struct net_dm_skb_cb *cb = NET_DM_SKB_CB(skb);
	const struct drop_reason_list *list = NULL;
	unsigned int subsys, subsys_reason;
	char buf[NET_DM_MAX_SYMBOL_LEN];
	struct nlattr *attr;
	void *hdr;
	int rc;

	hdr = genlmsg_put(msg, 0, 0, &net_drop_monitor_family, 0,
			  NET_DM_CMD_PACKET_ALERT);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u16(msg, NET_DM_ATTR_ORIGIN, NET_DM_ORIGIN_SW))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, NET_DM_ATTR_PC, (u64)(uintptr_t)cb->pc,
			      NET_DM_ATTR_PAD))
		goto nla_put_failure;

	rcu_read_lock();
	subsys = u32_get_bits(cb->reason, SKB_DROP_REASON_SUBSYS_MASK);
	if (subsys < SKB_DROP_REASON_SUBSYS_NUM)
		list = rcu_dereference(drop_reasons_by_subsys[subsys]);
	subsys_reason = cb->reason & ~SKB_DROP_REASON_SUBSYS_MASK;
	if (!list ||
	    subsys_reason >= list->n_reasons ||
	    !list->reasons[subsys_reason] ||
	    strlen(list->reasons[subsys_reason]) > NET_DM_MAX_REASON_LEN) {
		list = rcu_dereference(drop_reasons_by_subsys[SKB_DROP_REASON_SUBSYS_CORE]);
		subsys_reason = SKB_DROP_REASON_NOT_SPECIFIED;
	}
	if (nla_put_string(msg, NET_DM_ATTR_REASON,
			   list->reasons[subsys_reason])) {
		rcu_read_unlock();
		goto nla_put_failure;
	}
	rcu_read_unlock();

	snprintf(buf, sizeof(buf), "%pS", cb->pc);
	if (nla_put_string(msg, NET_DM_ATTR_SYMBOL, buf))
		goto nla_put_failure;

	rc = net_dm_packet_report_in_port_put(msg, skb->skb_iif, NULL);
	if (rc)
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, NET_DM_ATTR_TIMESTAMP,
			      ktime_to_ns(skb->tstamp), NET_DM_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u32(msg, NET_DM_ATTR_ORIG_LEN, skb->len))
		goto nla_put_failure;

	if (!payload_len)
		goto out;

	if (nla_put_u16(msg, NET_DM_ATTR_PROTO, be16_to_cpu(skb->protocol)))
		goto nla_put_failure;

	attr = skb_put(msg, nla_total_size(payload_len));
	attr->nla_type = NET_DM_ATTR_PAYLOAD;
	attr->nla_len = nla_attr_size(payload_len);
	if (skb_copy_bits(skb, 0, nla_data(attr), payload_len))
		goto nla_put_failure;

out:
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

#define NET_DM_MAX_PACKET_SIZE (0xffff - NLA_HDRLEN - NLA_ALIGNTO)

static void net_dm_packet_report(struct sk_buff *skb)
{
	struct sk_buff *msg;
	size_t payload_len;
	int rc;

	/* Make sure we start copying the packet from the MAC header */
	if (skb->data > skb_mac_header(skb))
		skb_push(skb, skb->data - skb_mac_header(skb));
	else
		skb_pull(skb, skb_mac_header(skb) - skb->data);

	/* Ensure packet fits inside a single netlink attribute */
	payload_len = min_t(size_t, skb->len, NET_DM_MAX_PACKET_SIZE);
	if (net_dm_trunc_len)
		payload_len = min_t(size_t, net_dm_trunc_len, payload_len);

	msg = nlmsg_new(net_dm_packet_report_size(payload_len), GFP_KERNEL);
	if (!msg)
		goto out;

	rc = net_dm_packet_report_fill(msg, skb, payload_len);
	if (rc) {
		nlmsg_free(msg);
		goto out;
	}

	genlmsg_multicast(&net_drop_monitor_family, msg, 0, 0, GFP_KERNEL);

out:
	consume_skb(skb);
}

static void net_dm_packet_work(struct work_struct *work)
{
	struct per_cpu_dm_data *data;
	struct sk_buff_head list;
	struct sk_buff *skb;
	unsigned long flags;

	data = container_of(work, struct per_cpu_dm_data, dm_alert_work);

	__skb_queue_head_init(&list);

	spin_lock_irqsave(&data->drop_queue.lock, flags);
	skb_queue_splice_tail_init(&data->drop_queue, &list);
	spin_unlock_irqrestore(&data->drop_queue.lock, flags);

	while ((skb = __skb_dequeue(&list)))
		net_dm_packet_report(skb);
}

static size_t
net_dm_flow_action_cookie_size(const struct devlink_trap_metadata *hw_metadata)
{
	return hw_metadata->fa_cookie ?
	       nla_total_size(hw_metadata->fa_cookie->cookie_len) : 0;
}

static size_t
net_dm_hw_packet_report_size(size_t payload_len,
			     const struct devlink_trap_metadata *hw_metadata)
{
	size_t size;

	size = nlmsg_msg_size(GENL_HDRLEN + net_drop_monitor_family.hdrsize);

	return NLMSG_ALIGN(size) +
	       /* NET_DM_ATTR_ORIGIN */
	       nla_total_size(sizeof(u16)) +
	       /* NET_DM_ATTR_HW_TRAP_GROUP_NAME */
	       nla_total_size(strlen(hw_metadata->trap_group_name) + 1) +
	       /* NET_DM_ATTR_HW_TRAP_NAME */
	       nla_total_size(strlen(hw_metadata->trap_name) + 1) +
	       /* NET_DM_ATTR_IN_PORT */
	       net_dm_in_port_size() +
	       /* NET_DM_ATTR_FLOW_ACTION_COOKIE */
	       net_dm_flow_action_cookie_size(hw_metadata) +
	       /* NET_DM_ATTR_TIMESTAMP */
	       nla_total_size(sizeof(u64)) +
	       /* NET_DM_ATTR_ORIG_LEN */
	       nla_total_size(sizeof(u32)) +
	       /* NET_DM_ATTR_PROTO */
	       nla_total_size(sizeof(u16)) +
	       /* NET_DM_ATTR_PAYLOAD */
	       nla_total_size(payload_len);
}

static int net_dm_hw_packet_report_fill(struct sk_buff *msg,
					struct sk_buff *skb, size_t payload_len)
{
	struct devlink_trap_metadata *hw_metadata;
	struct nlattr *attr;
	void *hdr;

	hw_metadata = NET_DM_SKB_CB(skb)->hw_metadata;

	hdr = genlmsg_put(msg, 0, 0, &net_drop_monitor_family, 0,
			  NET_DM_CMD_PACKET_ALERT);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u16(msg, NET_DM_ATTR_ORIGIN, NET_DM_ORIGIN_HW))
		goto nla_put_failure;

	if (nla_put_string(msg, NET_DM_ATTR_HW_TRAP_GROUP_NAME,
			   hw_metadata->trap_group_name))
		goto nla_put_failure;

	if (nla_put_string(msg, NET_DM_ATTR_HW_TRAP_NAME,
			   hw_metadata->trap_name))
		goto nla_put_failure;

	if (hw_metadata->input_dev) {
		struct net_device *dev = hw_metadata->input_dev;
		int rc;

		rc = net_dm_packet_report_in_port_put(msg, dev->ifindex,
						      dev->name);
		if (rc)
			goto nla_put_failure;
	}

	if (hw_metadata->fa_cookie &&
	    nla_put(msg, NET_DM_ATTR_FLOW_ACTION_COOKIE,
		    hw_metadata->fa_cookie->cookie_len,
		    hw_metadata->fa_cookie->cookie))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, NET_DM_ATTR_TIMESTAMP,
			      ktime_to_ns(skb->tstamp), NET_DM_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u32(msg, NET_DM_ATTR_ORIG_LEN, skb->len))
		goto nla_put_failure;

	if (!payload_len)
		goto out;

	if (nla_put_u16(msg, NET_DM_ATTR_PROTO, be16_to_cpu(skb->protocol)))
		goto nla_put_failure;

	attr = skb_put(msg, nla_total_size(payload_len));
	attr->nla_type = NET_DM_ATTR_PAYLOAD;
	attr->nla_len = nla_attr_size(payload_len);
	if (skb_copy_bits(skb, 0, nla_data(attr), payload_len))
		goto nla_put_failure;

out:
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static struct devlink_trap_metadata *
net_dm_hw_metadata_copy(const struct devlink_trap_metadata *metadata)
{
	const struct flow_action_cookie *fa_cookie;
	struct devlink_trap_metadata *hw_metadata;
	const char *trap_group_name;
	const char *trap_name;

	hw_metadata = kzalloc(sizeof(*hw_metadata), GFP_ATOMIC);
	if (!hw_metadata)
		return NULL;

	trap_group_name = kstrdup(metadata->trap_group_name, GFP_ATOMIC);
	if (!trap_group_name)
		goto free_hw_metadata;
	hw_metadata->trap_group_name = trap_group_name;

	trap_name = kstrdup(metadata->trap_name, GFP_ATOMIC);
	if (!trap_name)
		goto free_trap_group;
	hw_metadata->trap_name = trap_name;

	if (metadata->fa_cookie) {
		size_t cookie_size = sizeof(*fa_cookie) +
				     metadata->fa_cookie->cookie_len;

		fa_cookie = kmemdup(metadata->fa_cookie, cookie_size,
				    GFP_ATOMIC);
		if (!fa_cookie)
			goto free_trap_name;
		hw_metadata->fa_cookie = fa_cookie;
	}

	hw_metadata->input_dev = metadata->input_dev;
	netdev_hold(hw_metadata->input_dev, &hw_metadata->dev_tracker,
		    GFP_ATOMIC);

	return hw_metadata;

free_trap_name:
	kfree(trap_name);
free_trap_group:
	kfree(trap_group_name);
free_hw_metadata:
	kfree(hw_metadata);
	return NULL;
}

static void
net_dm_hw_metadata_free(struct devlink_trap_metadata *hw_metadata)
{
	netdev_put(hw_metadata->input_dev, &hw_metadata->dev_tracker);
	kfree(hw_metadata->fa_cookie);
	kfree(hw_metadata->trap_name);
	kfree(hw_metadata->trap_group_name);
	kfree(hw_metadata);
}

static void net_dm_hw_packet_report(struct sk_buff *skb)
{
	struct devlink_trap_metadata *hw_metadata;
	struct sk_buff *msg;
	size_t payload_len;
	int rc;

	if (skb->data > skb_mac_header(skb))
		skb_push(skb, skb->data - skb_mac_header(skb));
	else
		skb_pull(skb, skb_mac_header(skb) - skb->data);

	payload_len = min_t(size_t, skb->len, NET_DM_MAX_PACKET_SIZE);
	if (net_dm_trunc_len)
		payload_len = min_t(size_t, net_dm_trunc_len, payload_len);

	hw_metadata = NET_DM_SKB_CB(skb)->hw_metadata;
	msg = nlmsg_new(net_dm_hw_packet_report_size(payload_len, hw_metadata),
			GFP_KERNEL);
	if (!msg)
		goto out;

	rc = net_dm_hw_packet_report_fill(msg, skb, payload_len);
	if (rc) {
		nlmsg_free(msg);
		goto out;
	}

	genlmsg_multicast(&net_drop_monitor_family, msg, 0, 0, GFP_KERNEL);

out:
	net_dm_hw_metadata_free(NET_DM_SKB_CB(skb)->hw_metadata);
	consume_skb(skb);
}

static void net_dm_hw_packet_work(struct work_struct *work)
{
	struct per_cpu_dm_data *hw_data;
	struct sk_buff_head list;
	struct sk_buff *skb;
	unsigned long flags;

	hw_data = container_of(work, struct per_cpu_dm_data, dm_alert_work);

	__skb_queue_head_init(&list);

	spin_lock_irqsave(&hw_data->drop_queue.lock, flags);
	skb_queue_splice_tail_init(&hw_data->drop_queue, &list);
	spin_unlock_irqrestore(&hw_data->drop_queue.lock, flags);

	while ((skb = __skb_dequeue(&list)))
		net_dm_hw_packet_report(skb);
}

static void
net_dm_hw_trap_packet_probe(void *ignore, const struct devlink *devlink,
			    struct sk_buff *skb,
			    const struct devlink_trap_metadata *metadata)
{
	struct devlink_trap_metadata *n_hw_metadata;
	ktime_t tstamp = ktime_get_real();
	struct per_cpu_dm_data *hw_data;
	struct sk_buff *nskb;
	unsigned long flags;

	if (metadata->trap_type == DEVLINK_TRAP_TYPE_CONTROL)
		return;

	if (!skb_mac_header_was_set(skb))
		return;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return;

	n_hw_metadata = net_dm_hw_metadata_copy(metadata);
	if (!n_hw_metadata)
		goto free;

	NET_DM_SKB_CB(nskb)->hw_metadata = n_hw_metadata;
	nskb->tstamp = tstamp;

	hw_data = this_cpu_ptr(&dm_hw_cpu_data);

	spin_lock_irqsave(&hw_data->drop_queue.lock, flags);
	if (skb_queue_len(&hw_data->drop_queue) < net_dm_queue_len)
		__skb_queue_tail(&hw_data->drop_queue, nskb);
	else
		goto unlock_free;
	spin_unlock_irqrestore(&hw_data->drop_queue.lock, flags);

	schedule_work(&hw_data->dm_alert_work);

	return;

unlock_free:
	spin_unlock_irqrestore(&hw_data->drop_queue.lock, flags);
	u64_stats_update_begin(&hw_data->stats.syncp);
	u64_stats_inc(&hw_data->stats.dropped);
	u64_stats_update_end(&hw_data->stats.syncp);
	net_dm_hw_metadata_free(n_hw_metadata);
free:
	consume_skb(nskb);
}

static const struct net_dm_alert_ops net_dm_alert_packet_ops = {
	.kfree_skb_probe	= net_dm_packet_trace_kfree_skb_hit,
	.napi_poll_probe	= net_dm_packet_trace_napi_poll_hit,
	.work_item_func		= net_dm_packet_work,
	.hw_work_item_func	= net_dm_hw_packet_work,
	.hw_trap_probe		= net_dm_hw_trap_packet_probe,
};

static const struct net_dm_alert_ops *net_dm_alert_ops_arr[] = {
	[NET_DM_ALERT_MODE_SUMMARY]	= &net_dm_alert_summary_ops,
	[NET_DM_ALERT_MODE_PACKET]	= &net_dm_alert_packet_ops,
};

#if IS_ENABLED(CONFIG_NET_DEVLINK)
static int net_dm_hw_probe_register(const struct net_dm_alert_ops *ops)
{
	return register_trace_devlink_trap_report(ops->hw_trap_probe, NULL);
}

static void net_dm_hw_probe_unregister(const struct net_dm_alert_ops *ops)
{
	unregister_trace_devlink_trap_report(ops->hw_trap_probe, NULL);
	tracepoint_synchronize_unregister();
}
#else
static int net_dm_hw_probe_register(const struct net_dm_alert_ops *ops)
{
	return -EOPNOTSUPP;
}

static void net_dm_hw_probe_unregister(const struct net_dm_alert_ops *ops)
{
}
#endif

static int net_dm_hw_monitor_start(struct netlink_ext_ack *extack)
{
	const struct net_dm_alert_ops *ops;
	int cpu, rc;

	if (monitor_hw) {
		NL_SET_ERR_MSG_MOD(extack, "Hardware monitoring already enabled");
		return -EAGAIN;
	}

	ops = net_dm_alert_ops_arr[net_dm_alert_mode];

	if (!try_module_get(THIS_MODULE)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to take reference on module");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *hw_data = &per_cpu(dm_hw_cpu_data, cpu);
		struct net_dm_hw_entries *hw_entries;

		INIT_WORK(&hw_data->dm_alert_work, ops->hw_work_item_func);
		timer_setup(&hw_data->send_timer, sched_send_work, 0);
		hw_entries = net_dm_hw_reset_per_cpu_data(hw_data);
		kfree(hw_entries);
	}

	rc = net_dm_hw_probe_register(ops);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to connect probe to devlink_trap_probe() tracepoint");
		goto err_module_put;
	}

	monitor_hw = true;

	return 0;

err_module_put:
	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *hw_data = &per_cpu(dm_hw_cpu_data, cpu);
		struct sk_buff *skb;

		del_timer_sync(&hw_data->send_timer);
		cancel_work_sync(&hw_data->dm_alert_work);
		while ((skb = __skb_dequeue(&hw_data->drop_queue))) {
			struct devlink_trap_metadata *hw_metadata;

			hw_metadata = NET_DM_SKB_CB(skb)->hw_metadata;
			net_dm_hw_metadata_free(hw_metadata);
			consume_skb(skb);
		}
	}
	module_put(THIS_MODULE);
	return rc;
}

static void net_dm_hw_monitor_stop(struct netlink_ext_ack *extack)
{
	const struct net_dm_alert_ops *ops;
	int cpu;

	if (!monitor_hw) {
		NL_SET_ERR_MSG_MOD(extack, "Hardware monitoring already disabled");
		return;
	}

	ops = net_dm_alert_ops_arr[net_dm_alert_mode];

	monitor_hw = false;

	net_dm_hw_probe_unregister(ops);

	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *hw_data = &per_cpu(dm_hw_cpu_data, cpu);
		struct sk_buff *skb;

		del_timer_sync(&hw_data->send_timer);
		cancel_work_sync(&hw_data->dm_alert_work);
		while ((skb = __skb_dequeue(&hw_data->drop_queue))) {
			struct devlink_trap_metadata *hw_metadata;

			hw_metadata = NET_DM_SKB_CB(skb)->hw_metadata;
			net_dm_hw_metadata_free(hw_metadata);
			consume_skb(skb);
		}
	}

	module_put(THIS_MODULE);
}

static int net_dm_trace_on_set(struct netlink_ext_ack *extack)
{
	const struct net_dm_alert_ops *ops;
	int cpu, rc;

	ops = net_dm_alert_ops_arr[net_dm_alert_mode];

	if (!try_module_get(THIS_MODULE)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to take reference on module");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *data = &per_cpu(dm_cpu_data, cpu);
		struct sk_buff *skb;

		INIT_WORK(&data->dm_alert_work, ops->work_item_func);
		timer_setup(&data->send_timer, sched_send_work, 0);
		/* Allocate a new per-CPU skb for the summary alert message and
		 * free the old one which might contain stale data from
		 * previous tracing.
		 */
		skb = reset_per_cpu_data(data);
		consume_skb(skb);
	}

	rc = register_trace_kfree_skb(ops->kfree_skb_probe, NULL);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to connect probe to kfree_skb() tracepoint");
		goto err_module_put;
	}

	rc = register_trace_napi_poll(ops->napi_poll_probe, NULL);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to connect probe to napi_poll() tracepoint");
		goto err_unregister_trace;
	}

	return 0;

err_unregister_trace:
	unregister_trace_kfree_skb(ops->kfree_skb_probe, NULL);
err_module_put:
	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *data = &per_cpu(dm_cpu_data, cpu);
		struct sk_buff *skb;

		del_timer_sync(&data->send_timer);
		cancel_work_sync(&data->dm_alert_work);
		while ((skb = __skb_dequeue(&data->drop_queue)))
			consume_skb(skb);
	}
	module_put(THIS_MODULE);
	return rc;
}

static void net_dm_trace_off_set(void)
{
	const struct net_dm_alert_ops *ops;
	int cpu;

	ops = net_dm_alert_ops_arr[net_dm_alert_mode];

	unregister_trace_napi_poll(ops->napi_poll_probe, NULL);
	unregister_trace_kfree_skb(ops->kfree_skb_probe, NULL);

	tracepoint_synchronize_unregister();

	/* Make sure we do not send notifications to user space after request
	 * to stop tracing returns.
	 */
	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *data = &per_cpu(dm_cpu_data, cpu);
		struct sk_buff *skb;

		del_timer_sync(&data->send_timer);
		cancel_work_sync(&data->dm_alert_work);
		while ((skb = __skb_dequeue(&data->drop_queue)))
			consume_skb(skb);
	}

	module_put(THIS_MODULE);
}

static int set_all_monitor_traces(int state, struct netlink_ext_ack *extack)
{
	int rc = 0;

	if (state == trace_state) {
		NL_SET_ERR_MSG_MOD(extack, "Trace state already set to requested state");
		return -EAGAIN;
	}

	switch (state) {
	case TRACE_ON:
		rc = net_dm_trace_on_set(extack);
		break;
	case TRACE_OFF:
		net_dm_trace_off_set();
		break;
	default:
		rc = 1;
		break;
	}

	if (!rc)
		trace_state = state;
	else
		rc = -EINPROGRESS;

	return rc;
}

static bool net_dm_is_monitoring(void)
{
	return trace_state == TRACE_ON || monitor_hw;
}

static int net_dm_alert_mode_get_from_info(struct genl_info *info,
					   enum net_dm_alert_mode *p_alert_mode)
{
	u8 val;

	val = nla_get_u8(info->attrs[NET_DM_ATTR_ALERT_MODE]);

	switch (val) {
	case NET_DM_ALERT_MODE_SUMMARY:
	case NET_DM_ALERT_MODE_PACKET:
		*p_alert_mode = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int net_dm_alert_mode_set(struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	enum net_dm_alert_mode alert_mode;
	int rc;

	if (!info->attrs[NET_DM_ATTR_ALERT_MODE])
		return 0;

	rc = net_dm_alert_mode_get_from_info(info, &alert_mode);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid alert mode");
		return -EINVAL;
	}

	net_dm_alert_mode = alert_mode;

	return 0;
}

static void net_dm_trunc_len_set(struct genl_info *info)
{
	if (!info->attrs[NET_DM_ATTR_TRUNC_LEN])
		return;

	net_dm_trunc_len = nla_get_u32(info->attrs[NET_DM_ATTR_TRUNC_LEN]);
}

static void net_dm_queue_len_set(struct genl_info *info)
{
	if (!info->attrs[NET_DM_ATTR_QUEUE_LEN])
		return;

	net_dm_queue_len = nla_get_u32(info->attrs[NET_DM_ATTR_QUEUE_LEN]);
}

static int net_dm_cmd_config(struct sk_buff *skb,
			struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	int rc;

	if (net_dm_is_monitoring()) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot configure drop monitor during monitoring");
		return -EBUSY;
	}

	rc = net_dm_alert_mode_set(info);
	if (rc)
		return rc;

	net_dm_trunc_len_set(info);

	net_dm_queue_len_set(info);

	return 0;
}

static int net_dm_monitor_start(bool set_sw, bool set_hw,
				struct netlink_ext_ack *extack)
{
	bool sw_set = false;
	int rc;

	if (set_sw) {
		rc = set_all_monitor_traces(TRACE_ON, extack);
		if (rc)
			return rc;
		sw_set = true;
	}

	if (set_hw) {
		rc = net_dm_hw_monitor_start(extack);
		if (rc)
			goto err_monitor_hw;
	}

	return 0;

err_monitor_hw:
	if (sw_set)
		set_all_monitor_traces(TRACE_OFF, extack);
	return rc;
}

static void net_dm_monitor_stop(bool set_sw, bool set_hw,
				struct netlink_ext_ack *extack)
{
	if (set_hw)
		net_dm_hw_monitor_stop(extack);
	if (set_sw)
		set_all_monitor_traces(TRACE_OFF, extack);
}

static int net_dm_cmd_trace(struct sk_buff *skb,
			struct genl_info *info)
{
	bool set_sw = !!info->attrs[NET_DM_ATTR_SW_DROPS];
	bool set_hw = !!info->attrs[NET_DM_ATTR_HW_DROPS];
	struct netlink_ext_ack *extack = info->extack;

	/* To maintain backward compatibility, we start / stop monitoring of
	 * software drops if no flag is specified.
	 */
	if (!set_sw && !set_hw)
		set_sw = true;

	switch (info->genlhdr->cmd) {
	case NET_DM_CMD_START:
		return net_dm_monitor_start(set_sw, set_hw, extack);
	case NET_DM_CMD_STOP:
		net_dm_monitor_stop(set_sw, set_hw, extack);
		return 0;
	}

	return -EOPNOTSUPP;
}

static int net_dm_config_fill(struct sk_buff *msg, struct genl_info *info)
{
	void *hdr;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &net_drop_monitor_family, 0, NET_DM_CMD_CONFIG_NEW);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u8(msg, NET_DM_ATTR_ALERT_MODE, net_dm_alert_mode))
		goto nla_put_failure;

	if (nla_put_u32(msg, NET_DM_ATTR_TRUNC_LEN, net_dm_trunc_len))
		goto nla_put_failure;

	if (nla_put_u32(msg, NET_DM_ATTR_QUEUE_LEN, net_dm_queue_len))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int net_dm_cmd_config_get(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	int rc;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rc = net_dm_config_fill(msg, info);
	if (rc)
		goto free_msg;

	return genlmsg_reply(msg, info);

free_msg:
	nlmsg_free(msg);
	return rc;
}

static void net_dm_stats_read(struct net_dm_stats *stats)
{
	int cpu;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *data = &per_cpu(dm_cpu_data, cpu);
		struct net_dm_stats *cpu_stats = &data->stats;
		unsigned int start;
		u64 dropped;

		do {
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			dropped = u64_stats_read(&cpu_stats->dropped);
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		u64_stats_add(&stats->dropped, dropped);
	}
}

static int net_dm_stats_put(struct sk_buff *msg)
{
	struct net_dm_stats stats;
	struct nlattr *attr;

	net_dm_stats_read(&stats);

	attr = nla_nest_start(msg, NET_DM_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, NET_DM_ATTR_STATS_DROPPED,
			      u64_stats_read(&stats.dropped), NET_DM_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static void net_dm_hw_stats_read(struct net_dm_stats *stats)
{
	int cpu;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(cpu) {
		struct per_cpu_dm_data *hw_data = &per_cpu(dm_hw_cpu_data, cpu);
		struct net_dm_stats *cpu_stats = &hw_data->stats;
		unsigned int start;
		u64 dropped;

		do {
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			dropped = u64_stats_read(&cpu_stats->dropped);
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		u64_stats_add(&stats->dropped, dropped);
	}
}

static int net_dm_hw_stats_put(struct sk_buff *msg)
{
	struct net_dm_stats stats;
	struct nlattr *attr;

	net_dm_hw_stats_read(&stats);

	attr = nla_nest_start(msg, NET_DM_ATTR_HW_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, NET_DM_ATTR_STATS_DROPPED,
			      u64_stats_read(&stats.dropped), NET_DM_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int net_dm_stats_fill(struct sk_buff *msg, struct genl_info *info)
{
	void *hdr;
	int rc;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &net_drop_monitor_family, 0, NET_DM_CMD_STATS_NEW);
	if (!hdr)
		return -EMSGSIZE;

	rc = net_dm_stats_put(msg);
	if (rc)
		goto nla_put_failure;

	rc = net_dm_hw_stats_put(msg);
	if (rc)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int net_dm_cmd_stats_get(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	int rc;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rc = net_dm_stats_fill(msg, info);
	if (rc)
		goto free_msg;

	return genlmsg_reply(msg, info);

free_msg:
	nlmsg_free(msg);
	return rc;
}

static int dropmon_net_event(struct notifier_block *ev_block,
			     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct dm_hw_stat_delta *stat;

	switch (event) {
	case NETDEV_REGISTER:
		if (WARN_ON_ONCE(rtnl_dereference(dev->dm_private)))
			break;
		stat = kzalloc(sizeof(*stat), GFP_KERNEL);
		if (!stat)
			break;

		stat->last_rx = jiffies;
		rcu_assign_pointer(dev->dm_private, stat);

		break;
	case NETDEV_UNREGISTER:
		stat = rtnl_dereference(dev->dm_private);
		if (stat) {
			rcu_assign_pointer(dev->dm_private, NULL);
			kfree_rcu(stat, rcu);
		}
		break;
	}
	return NOTIFY_DONE;
}

static const struct nla_policy net_dm_nl_policy[NET_DM_ATTR_MAX + 1] = {
	[NET_DM_ATTR_UNSPEC] = { .strict_start_type = NET_DM_ATTR_UNSPEC + 1 },
	[NET_DM_ATTR_ALERT_MODE] = { .type = NLA_U8 },
	[NET_DM_ATTR_TRUNC_LEN] = { .type = NLA_U32 },
	[NET_DM_ATTR_QUEUE_LEN] = { .type = NLA_U32 },
	[NET_DM_ATTR_SW_DROPS]	= {. type = NLA_FLAG },
	[NET_DM_ATTR_HW_DROPS]	= {. type = NLA_FLAG },
};

static const struct genl_small_ops dropmon_ops[] = {
	{
		.cmd = NET_DM_CMD_CONFIG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = net_dm_cmd_config,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_DM_CMD_START,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = net_dm_cmd_trace,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_DM_CMD_STOP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = net_dm_cmd_trace,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_DM_CMD_CONFIG_GET,
		.doit = net_dm_cmd_config_get,
	},
	{
		.cmd = NET_DM_CMD_STATS_GET,
		.doit = net_dm_cmd_stats_get,
	},
};

static int net_dm_nl_pre_doit(const struct genl_split_ops *ops,
			      struct sk_buff *skb, struct genl_info *info)
{
	mutex_lock(&net_dm_mutex);

	return 0;
}

static void net_dm_nl_post_doit(const struct genl_split_ops *ops,
				struct sk_buff *skb, struct genl_info *info)
{
	mutex_unlock(&net_dm_mutex);
}

static struct genl_family net_drop_monitor_family __ro_after_init = {
	.hdrsize        = 0,
	.name           = "NET_DM",
	.version        = 2,
	.maxattr	= NET_DM_ATTR_MAX,
	.policy		= net_dm_nl_policy,
	.pre_doit	= net_dm_nl_pre_doit,
	.post_doit	= net_dm_nl_post_doit,
	.module		= THIS_MODULE,
	.small_ops	= dropmon_ops,
	.n_small_ops	= ARRAY_SIZE(dropmon_ops),
	.resv_start_op	= NET_DM_CMD_STATS_GET + 1,
	.mcgrps		= dropmon_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(dropmon_mcgrps),
};

static struct notifier_block dropmon_net_notifier = {
	.notifier_call = dropmon_net_event
};

static void __net_dm_cpu_data_init(struct per_cpu_dm_data *data)
{
	raw_spin_lock_init(&data->lock);
	skb_queue_head_init(&data->drop_queue);
	u64_stats_init(&data->stats.syncp);
}

static void __net_dm_cpu_data_fini(struct per_cpu_dm_data *data)
{
	WARN_ON(!skb_queue_empty(&data->drop_queue));
}

static void net_dm_cpu_data_init(int cpu)
{
	struct per_cpu_dm_data *data;

	data = &per_cpu(dm_cpu_data, cpu);
	__net_dm_cpu_data_init(data);
}

static void net_dm_cpu_data_fini(int cpu)
{
	struct per_cpu_dm_data *data;

	data = &per_cpu(dm_cpu_data, cpu);
	/* At this point, we should have exclusive access
	 * to this struct and can free the skb inside it.
	 */
	consume_skb(data->skb);
	__net_dm_cpu_data_fini(data);
}

static void net_dm_hw_cpu_data_init(int cpu)
{
	struct per_cpu_dm_data *hw_data;

	hw_data = &per_cpu(dm_hw_cpu_data, cpu);
	__net_dm_cpu_data_init(hw_data);
}

static void net_dm_hw_cpu_data_fini(int cpu)
{
	struct per_cpu_dm_data *hw_data;

	hw_data = &per_cpu(dm_hw_cpu_data, cpu);
	kfree(hw_data->hw_entries);
	__net_dm_cpu_data_fini(hw_data);
}

static int __init init_net_drop_monitor(void)
{
	int cpu, rc;

	pr_info("Initializing network drop monitor service\n");

	if (sizeof(void *) > 8) {
		pr_err("Unable to store program counters on this arch, Drop monitor failed\n");
		return -ENOSPC;
	}

	rc = genl_register_family(&net_drop_monitor_family);
	if (rc) {
		pr_err("Could not create drop monitor netlink family\n");
		return rc;
	}
	WARN_ON(net_drop_monitor_family.mcgrp_offset != NET_DM_GRP_ALERT);

	rc = register_netdevice_notifier(&dropmon_net_notifier);
	if (rc < 0) {
		pr_crit("Failed to register netdevice notifier\n");
		goto out_unreg;
	}

	rc = 0;

	for_each_possible_cpu(cpu) {
		net_dm_cpu_data_init(cpu);
		net_dm_hw_cpu_data_init(cpu);
	}

	goto out;

out_unreg:
	genl_unregister_family(&net_drop_monitor_family);
out:
	return rc;
}

static void exit_net_drop_monitor(void)
{
	int cpu;

	BUG_ON(unregister_netdevice_notifier(&dropmon_net_notifier));

	/*
	 * Because of the module_get/put we do in the trace state change path
	 * we are guaranteed not to have any current users when we get here
	 */

	for_each_possible_cpu(cpu) {
		net_dm_hw_cpu_data_fini(cpu);
		net_dm_cpu_data_fini(cpu);
	}

	BUG_ON(genl_unregister_family(&net_drop_monitor_family));
}

module_init(init_net_drop_monitor);
module_exit(exit_net_drop_monitor);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Neil Horman <nhorman@tuxdriver.com>");
MODULE_ALIAS_GENL_FAMILY("NET_DM");
MODULE_DESCRIPTION("Monitoring code for network dropped packet alerts");

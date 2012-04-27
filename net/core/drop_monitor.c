/*
 * Monitoring code for network dropped packet alerts
 *
 * Copyright (C) 2009 Neil Horman <nhorman@tuxdriver.com>
 */

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
#include <linux/percpu.h>
#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <net/genetlink.h>
#include <net/netevent.h>

#include <trace/events/skb.h>
#include <trace/events/napi.h>

#include <asm/unaligned.h>

#define TRACE_ON 1
#define TRACE_OFF 0

static void send_dm_alert(struct work_struct *unused);


/*
 * Globals, our netlink socket pointer
 * and the work handle that will send up
 * netlink alerts
 */
static int trace_state = TRACE_OFF;
static DEFINE_MUTEX(trace_state_mutex);

struct per_cpu_dm_data {
	struct work_struct dm_alert_work;
	struct sk_buff __rcu *skb;
	atomic_t dm_hit_count;
	struct timer_list send_timer;
};

struct dm_hw_stat_delta {
	struct net_device *dev;
	unsigned long last_rx;
	struct list_head list;
	struct rcu_head rcu;
	unsigned long last_drop_val;
};

static struct genl_family net_drop_monitor_family = {
	.id             = GENL_ID_GENERATE,
	.hdrsize        = 0,
	.name           = "NET_DM",
	.version        = 2,
	.maxattr        = NET_DM_CMD_MAX,
};

static DEFINE_PER_CPU(struct per_cpu_dm_data, dm_cpu_data);

static int dm_hit_limit = 64;
static int dm_delay = 1;
static unsigned long dm_hw_check_delta = 2*HZ;
static LIST_HEAD(hw_stats_list);
static int initialized = 0;

static void reset_per_cpu_data(struct per_cpu_dm_data *data)
{
	size_t al;
	struct net_dm_alert_msg *msg;
	struct nlattr *nla;
	struct sk_buff *skb;
	struct sk_buff *oskb = rcu_dereference_protected(data->skb, 1);

	al = sizeof(struct net_dm_alert_msg);
	al += dm_hit_limit * sizeof(struct net_dm_drop_point);
	al += sizeof(struct nlattr);

	skb = genlmsg_new(al, GFP_KERNEL);

	if (skb) {
		genlmsg_put(skb, 0, 0, &net_drop_monitor_family,
				0, NET_DM_CMD_ALERT);
		nla = nla_reserve(skb, NLA_UNSPEC,
				  sizeof(struct net_dm_alert_msg));
		msg = nla_data(nla);
		memset(msg, 0, al);
	} else if (initialized)
		schedule_work_on(smp_processor_id(), &data->dm_alert_work);

	/*
	 * Don't need to lock this, since we are guaranteed to only
	 * run this on a single cpu at a time.
	 * Note also that we only update data->skb if the old and new skb
	 * pointers don't match.  This ensures that we don't continually call
	 * synchornize_rcu if we repeatedly fail to alloc a new netlink message.
	 */
	if (skb != oskb) {
		rcu_assign_pointer(data->skb, skb);

		synchronize_rcu();

		atomic_set(&data->dm_hit_count, dm_hit_limit);
	}

}

static void send_dm_alert(struct work_struct *unused)
{
	struct sk_buff *skb;
	struct per_cpu_dm_data *data = &get_cpu_var(dm_cpu_data);

	/*
	 * Grab the skb we're about to send
	 */
	skb = rcu_dereference_protected(data->skb, 1);

	/*
	 * Replace it with a new one
	 */
	reset_per_cpu_data(data);

	/*
	 * Ship it!
	 */
	if (skb)
		genlmsg_multicast(skb, 0, NET_DM_GRP_ALERT, GFP_KERNEL);

	put_cpu_var(dm_cpu_data);
}

/*
 * This is the timer function to delay the sending of an alert
 * in the event that more drops will arrive during the
 * hysteresis period.  Note that it operates under the timer interrupt
 * so we don't need to disable preemption here
 */
static void sched_send_work(unsigned long unused)
{
	struct per_cpu_dm_data *data =  &get_cpu_var(dm_cpu_data);

	schedule_work_on(smp_processor_id(), &data->dm_alert_work);

	put_cpu_var(dm_cpu_data);
}

static void trace_drop_common(struct sk_buff *skb, void *location)
{
	struct net_dm_alert_msg *msg;
	struct nlmsghdr *nlh;
	struct nlattr *nla;
	int i;
	struct sk_buff *dskb;
	struct per_cpu_dm_data *data = &get_cpu_var(dm_cpu_data);


	rcu_read_lock();
	dskb = rcu_dereference(data->skb);

	if (!dskb)
		goto out;

	if (!atomic_add_unless(&data->dm_hit_count, -1, 0)) {
		/*
		 * we're already at zero, discard this hit
		 */
		goto out;
	}

	nlh = (struct nlmsghdr *)dskb->data;
	nla = genlmsg_data(nlmsg_data(nlh));
	msg = nla_data(nla);
	for (i = 0; i < msg->entries; i++) {
		if (!memcmp(&location, msg->points[i].pc, sizeof(void *))) {
			msg->points[i].count++;
			goto out;
		}
	}

	/*
	 * We need to create a new entry
	 */
	__nla_reserve_nohdr(dskb, sizeof(struct net_dm_drop_point));
	nla->nla_len += NLA_ALIGN(sizeof(struct net_dm_drop_point));
	memcpy(msg->points[msg->entries].pc, &location, sizeof(void *));
	msg->points[msg->entries].count = 1;
	msg->entries++;

	if (!timer_pending(&data->send_timer)) {
		data->send_timer.expires = jiffies + dm_delay * HZ;
		add_timer_on(&data->send_timer, smp_processor_id());
	}

out:
	rcu_read_unlock();
	put_cpu_var(dm_cpu_data);
	return;
}

static void trace_kfree_skb_hit(void *ignore, struct sk_buff *skb, void *location)
{
	trace_drop_common(skb, location);
}

static void trace_napi_poll_hit(void *ignore, struct napi_struct *napi)
{
	struct dm_hw_stat_delta *new_stat;

	/*
	 * Don't check napi structures with no associated device
	 */
	if (!napi->dev)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(new_stat, &hw_stats_list, list) {
		/*
		 * only add a note to our monitor buffer if:
		 * 1) this is the dev we received on
		 * 2) its after the last_rx delta
		 * 3) our rx_dropped count has gone up
		 */
		if ((new_stat->dev == napi->dev)  &&
		    (time_after(jiffies, new_stat->last_rx + dm_hw_check_delta)) &&
		    (napi->dev->stats.rx_dropped != new_stat->last_drop_val)) {
			trace_drop_common(NULL, NULL);
			new_stat->last_drop_val = napi->dev->stats.rx_dropped;
			new_stat->last_rx = jiffies;
			break;
		}
	}
	rcu_read_unlock();
}

static int set_all_monitor_traces(int state)
{
	int rc = 0;
	struct dm_hw_stat_delta *new_stat = NULL;
	struct dm_hw_stat_delta *temp;

	mutex_lock(&trace_state_mutex);

	if (state == trace_state) {
		rc = -EAGAIN;
		goto out_unlock;
	}

	switch (state) {
	case TRACE_ON:
		rc |= register_trace_kfree_skb(trace_kfree_skb_hit, NULL);
		rc |= register_trace_napi_poll(trace_napi_poll_hit, NULL);
		break;
	case TRACE_OFF:
		rc |= unregister_trace_kfree_skb(trace_kfree_skb_hit, NULL);
		rc |= unregister_trace_napi_poll(trace_napi_poll_hit, NULL);

		tracepoint_synchronize_unregister();

		/*
		 * Clean the device list
		 */
		list_for_each_entry_safe(new_stat, temp, &hw_stats_list, list) {
			if (new_stat->dev == NULL) {
				list_del_rcu(&new_stat->list);
				kfree_rcu(new_stat, rcu);
			}
		}
		break;
	default:
		rc = 1;
		break;
	}

	if (!rc)
		trace_state = state;
	else
		rc = -EINPROGRESS;

out_unlock:
	mutex_unlock(&trace_state_mutex);

	return rc;
}


static int net_dm_cmd_config(struct sk_buff *skb,
			struct genl_info *info)
{
	return -ENOTSUPP;
}

static int net_dm_cmd_trace(struct sk_buff *skb,
			struct genl_info *info)
{
	switch (info->genlhdr->cmd) {
	case NET_DM_CMD_START:
		return set_all_monitor_traces(TRACE_ON);
		break;
	case NET_DM_CMD_STOP:
		return set_all_monitor_traces(TRACE_OFF);
		break;
	}

	return -ENOTSUPP;
}

static int dropmon_net_event(struct notifier_block *ev_block,
			unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct dm_hw_stat_delta *new_stat = NULL;
	struct dm_hw_stat_delta *tmp;

	switch (event) {
	case NETDEV_REGISTER:
		new_stat = kzalloc(sizeof(struct dm_hw_stat_delta), GFP_KERNEL);

		if (!new_stat)
			goto out;

		new_stat->dev = dev;
		new_stat->last_rx = jiffies;
		mutex_lock(&trace_state_mutex);
		list_add_rcu(&new_stat->list, &hw_stats_list);
		mutex_unlock(&trace_state_mutex);
		break;
	case NETDEV_UNREGISTER:
		mutex_lock(&trace_state_mutex);
		list_for_each_entry_safe(new_stat, tmp, &hw_stats_list, list) {
			if (new_stat->dev == dev) {
				new_stat->dev = NULL;
				if (trace_state == TRACE_OFF) {
					list_del_rcu(&new_stat->list);
					kfree_rcu(new_stat, rcu);
					break;
				}
			}
		}
		mutex_unlock(&trace_state_mutex);
		break;
	}
out:
	return NOTIFY_DONE;
}

static struct genl_ops dropmon_ops[] = {
	{
		.cmd = NET_DM_CMD_CONFIG,
		.doit = net_dm_cmd_config,
	},
	{
		.cmd = NET_DM_CMD_START,
		.doit = net_dm_cmd_trace,
	},
	{
		.cmd = NET_DM_CMD_STOP,
		.doit = net_dm_cmd_trace,
	},
};

static struct notifier_block dropmon_net_notifier = {
	.notifier_call = dropmon_net_event
};

static int __init init_net_drop_monitor(void)
{
	struct per_cpu_dm_data *data;
	int cpu, rc;

	printk(KERN_INFO "Initializing network drop monitor service\n");

	if (sizeof(void *) > 8) {
		printk(KERN_ERR "Unable to store program counters on this arch, Drop monitor failed\n");
		return -ENOSPC;
	}

	rc = genl_register_family_with_ops(&net_drop_monitor_family,
					   dropmon_ops,
					   ARRAY_SIZE(dropmon_ops));
	if (rc) {
		printk(KERN_ERR "Could not create drop monitor netlink family\n");
		return rc;
	}

	rc = register_netdevice_notifier(&dropmon_net_notifier);
	if (rc < 0) {
		printk(KERN_CRIT "Failed to register netdevice notifier\n");
		goto out_unreg;
	}

	rc = 0;

	for_each_present_cpu(cpu) {
		data = &per_cpu(dm_cpu_data, cpu);
		reset_per_cpu_data(data);
		INIT_WORK(&data->dm_alert_work, send_dm_alert);
		init_timer(&data->send_timer);
		data->send_timer.data = cpu;
		data->send_timer.function = sched_send_work;
	}

	initialized = 1;

	goto out;

out_unreg:
	genl_unregister_family(&net_drop_monitor_family);
out:
	return rc;
}

late_initcall(init_net_drop_monitor);

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
#include <net/genetlink.h>

#include <trace/skb.h>

#include <asm/unaligned.h>

#define TRACE_ON 1
#define TRACE_OFF 0

static void send_dm_alert(struct work_struct *unused);


/*
 * Globals, our netlink socket pointer
 * and the work handle that will send up
 * netlink alerts
 */
struct sock *dm_sock;

struct per_cpu_dm_data {
	struct work_struct dm_alert_work;
	struct sk_buff *skb;
	atomic_t dm_hit_count;
	struct timer_list send_timer;
};

static struct genl_family net_drop_monitor_family = {
	.id             = GENL_ID_GENERATE,
	.hdrsize        = 0,
	.name           = "NET_DM",
	.version        = 1,
	.maxattr        = NET_DM_CMD_MAX,
};

static DEFINE_PER_CPU(struct per_cpu_dm_data, dm_cpu_data);

static int dm_hit_limit = 64;
static int dm_delay = 1;


static void reset_per_cpu_data(struct per_cpu_dm_data *data)
{
	size_t al;
	struct net_dm_alert_msg *msg;

	al = sizeof(struct net_dm_alert_msg);
	al += dm_hit_limit * sizeof(struct net_dm_drop_point);
	data->skb = genlmsg_new(al, GFP_KERNEL);
	genlmsg_put(data->skb, 0, 0, &net_drop_monitor_family,
			0, NET_DM_CMD_ALERT);
	msg = __nla_reserve_nohdr(data->skb, sizeof(struct net_dm_alert_msg));
	memset(msg, 0, al);
	atomic_set(&data->dm_hit_count, dm_hit_limit);
}

static void send_dm_alert(struct work_struct *unused)
{
	struct sk_buff *skb;
	struct per_cpu_dm_data *data = &__get_cpu_var(dm_cpu_data);

	/*
	 * Grab the skb we're about to send
	 */
	skb = data->skb;

	/*
	 * Replace it with a new one
	 */
	reset_per_cpu_data(data);

	/*
	 * Ship it!
	 */
	genlmsg_multicast(skb, 0, NET_DM_GRP_ALERT, GFP_KERNEL);

}

/*
 * This is the timer function to delay the sending of an alert
 * in the event that more drops will arrive during the
 * hysteresis period.  Note that it operates under the timer interrupt
 * so we don't need to disable preemption here
 */
static void sched_send_work(unsigned long unused)
{
	struct per_cpu_dm_data *data =  &__get_cpu_var(dm_cpu_data);

	schedule_work(&data->dm_alert_work);
}

static void trace_kfree_skb_hit(struct sk_buff *skb, void *location)
{
	struct net_dm_alert_msg *msg;
	struct nlmsghdr *nlh;
	int i;
	struct per_cpu_dm_data *data = &__get_cpu_var(dm_cpu_data);


	if (!atomic_add_unless(&data->dm_hit_count, -1, 0)) {
		/*
		 * we're already at zero, discard this hit
		 */
		goto out;
	}

	nlh = (struct nlmsghdr *)data->skb->data;
	msg = genlmsg_data(nlmsg_data(nlh));
	for (i = 0; i < msg->entries; i++) {
		if (!memcmp(&location, msg->points[i].pc, sizeof(void *))) {
			msg->points[i].count++;
			goto out;
		}
	}

	/*
	 * We need to create a new entry
	 */
	__nla_reserve_nohdr(data->skb, sizeof(struct net_dm_drop_point));
	memcpy(msg->points[msg->entries].pc, &location, sizeof(void *));
	msg->points[msg->entries].count = 1;
	msg->entries++;

	if (!timer_pending(&data->send_timer)) {
		data->send_timer.expires = jiffies + dm_delay * HZ;
		add_timer_on(&data->send_timer, smp_processor_id());
	}

out:
	return;
}

static int set_all_monitor_traces(int state)
{
	int rc = 0;

	switch (state) {
	case TRACE_ON:
		rc |= register_trace_kfree_skb(trace_kfree_skb_hit);
		break;
	case TRACE_OFF:
		rc |= unregister_trace_kfree_skb(trace_kfree_skb_hit);

		tracepoint_synchronize_unregister();
		break;
	default:
		rc = 1;
		break;
	}

	if (rc)
		return -EINPROGRESS;
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

static int __init init_net_drop_monitor(void)
{
	int cpu;
	int rc, i, ret;
	struct per_cpu_dm_data *data;
	printk(KERN_INFO "Initalizing network drop monitor service\n");

	if (sizeof(void *) > 8) {
		printk(KERN_ERR "Unable to store program counters on this arch, Drop monitor failed\n");
		return -ENOSPC;
	}

	if (genl_register_family(&net_drop_monitor_family) < 0) {
		printk(KERN_ERR "Could not create drop monitor netlink family\n");
		return -EFAULT;
	}

	rc = -EFAULT;

	for (i = 0; i < ARRAY_SIZE(dropmon_ops); i++) {
		ret = genl_register_ops(&net_drop_monitor_family,
					&dropmon_ops[i]);
		if (ret) {
			printk(KERN_CRIT "failed to register operation %d\n",
				dropmon_ops[i].cmd);
			goto out_unreg;
		}
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
	goto out;

out_unreg:
	genl_unregister_family(&net_drop_monitor_family);
out:
	return rc;
}

late_initcall(init_net_drop_monitor);

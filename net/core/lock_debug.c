// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright Amazon.com Inc. or its affiliates. */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/netdev_lock.h>
#include <net/netns/generic.h>

int netdev_debug_event(struct notifier_block *nb, unsigned long event,
		       void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	enum netdev_cmd cmd = event;

	/* Keep enum and don't add default to trigger -Werror=switch */
	switch (cmd) {
	case NETDEV_XDP_FEAT_CHANGE:
		netdev_assert_locked(dev);
		fallthrough;
	case NETDEV_CHANGE:
	case NETDEV_REGISTER:
	case NETDEV_UP:
		netdev_ops_assert_locked(dev);
		fallthrough;
	case NETDEV_DOWN:
	case NETDEV_REBOOT:
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
	case NETDEV_PRE_CHANGEADDR:
	case NETDEV_GOING_DOWN:
	case NETDEV_FEAT_CHANGE:
	case NETDEV_BONDING_FAILOVER:
	case NETDEV_PRE_UP:
	case NETDEV_PRE_TYPE_CHANGE:
	case NETDEV_POST_TYPE_CHANGE:
	case NETDEV_POST_INIT:
	case NETDEV_PRE_UNINIT:
	case NETDEV_RELEASE:
	case NETDEV_NOTIFY_PEERS:
	case NETDEV_JOIN:
	case NETDEV_CHANGEUPPER:
	case NETDEV_RESEND_IGMP:
	case NETDEV_PRECHANGEMTU:
	case NETDEV_CHANGEINFODATA:
	case NETDEV_BONDING_INFO:
	case NETDEV_PRECHANGEUPPER:
	case NETDEV_CHANGELOWERSTATE:
	case NETDEV_UDP_TUNNEL_PUSH_INFO:
	case NETDEV_UDP_TUNNEL_DROP_INFO:
	case NETDEV_CHANGE_TX_QUEUE_LEN:
	case NETDEV_CVLAN_FILTER_PUSH_INFO:
	case NETDEV_CVLAN_FILTER_DROP_INFO:
	case NETDEV_SVLAN_FILTER_PUSH_INFO:
	case NETDEV_SVLAN_FILTER_DROP_INFO:
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		ASSERT_RTNL();
		break;

	case NETDEV_CHANGENAME:
		ASSERT_RTNL_NET(net);
		break;
	}

	return NOTIFY_DONE;
}
EXPORT_SYMBOL_NS_GPL(netdev_debug_event, "NETDEV_INTERNAL");

static int rtnl_net_debug_net_id;

static int __net_init rtnl_net_debug_net_init(struct net *net)
{
	struct notifier_block *nb;

	nb = net_generic(net, rtnl_net_debug_net_id);
	nb->notifier_call = netdev_debug_event;

	return register_netdevice_notifier_net(net, nb);
}

static void __net_exit rtnl_net_debug_net_exit(struct net *net)
{
	struct notifier_block *nb;

	nb = net_generic(net, rtnl_net_debug_net_id);
	unregister_netdevice_notifier_net(net, nb);
}

static struct pernet_operations rtnl_net_debug_net_ops __net_initdata = {
	.init = rtnl_net_debug_net_init,
	.exit = rtnl_net_debug_net_exit,
	.id = &rtnl_net_debug_net_id,
	.size = sizeof(struct notifier_block),
};

static struct notifier_block rtnl_net_debug_block = {
	.notifier_call = netdev_debug_event,
};

static int __init rtnl_net_debug_init(void)
{
	int ret;

	ret = register_pernet_subsys(&rtnl_net_debug_net_ops);
	if (ret)
		return ret;

	ret = register_netdevice_notifier(&rtnl_net_debug_block);
	if (ret)
		unregister_pernet_subsys(&rtnl_net_debug_net_ops);

	return ret;
}

subsys_initcall(rtnl_net_debug_init);

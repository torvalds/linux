/*
 * net/switchdev/switchdev.c - Switch device API
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <net/ip_fib.h>
#include <net/switchdev.h>

/**
 *	netdev_switch_parent_id_get - Get ID of a switch
 *	@dev: port device
 *	@psid: switch ID
 *
 *	Get ID of a switch this port is part of.
 */
int netdev_switch_parent_id_get(struct net_device *dev,
				struct netdev_phys_item_id *psid)
{
	const struct swdev_ops *ops = dev->swdev_ops;

	if (!ops || !ops->swdev_parent_id_get)
		return -EOPNOTSUPP;
	return ops->swdev_parent_id_get(dev, psid);
}
EXPORT_SYMBOL_GPL(netdev_switch_parent_id_get);

/**
 *	netdev_switch_port_stp_update - Notify switch device port of STP
 *					state change
 *	@dev: port device
 *	@state: port STP state
 *
 *	Notify switch device port of bridge port STP state change.
 */
int netdev_switch_port_stp_update(struct net_device *dev, u8 state)
{
	const struct swdev_ops *ops = dev->swdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->swdev_port_stp_update)
		return ops->swdev_port_stp_update(dev, state);

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = netdev_switch_port_stp_update(lower_dev, state);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	return err;
}
EXPORT_SYMBOL_GPL(netdev_switch_port_stp_update);

static DEFINE_MUTEX(netdev_switch_mutex);
static RAW_NOTIFIER_HEAD(netdev_switch_notif_chain);

/**
 *	register_netdev_switch_notifier - Register notifier
 *	@nb: notifier_block
 *
 *	Register switch device notifier. This should be used by code
 *	which needs to monitor events happening in particular device.
 *	Return values are same as for atomic_notifier_chain_register().
 */
int register_netdev_switch_notifier(struct notifier_block *nb)
{
	int err;

	mutex_lock(&netdev_switch_mutex);
	err = raw_notifier_chain_register(&netdev_switch_notif_chain, nb);
	mutex_unlock(&netdev_switch_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(register_netdev_switch_notifier);

/**
 *	unregister_netdev_switch_notifier - Unregister notifier
 *	@nb: notifier_block
 *
 *	Unregister switch device notifier.
 *	Return values are same as for atomic_notifier_chain_unregister().
 */
int unregister_netdev_switch_notifier(struct notifier_block *nb)
{
	int err;

	mutex_lock(&netdev_switch_mutex);
	err = raw_notifier_chain_unregister(&netdev_switch_notif_chain, nb);
	mutex_unlock(&netdev_switch_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(unregister_netdev_switch_notifier);

/**
 *	call_netdev_switch_notifiers - Call notifiers
 *	@val: value passed unmodified to notifier function
 *	@dev: port device
 *	@info: notifier information data
 *
 *	Call all network notifier blocks. This should be called by driver
 *	when it needs to propagate hardware event.
 *	Return values are same as for atomic_notifier_call_chain().
 */
int call_netdev_switch_notifiers(unsigned long val, struct net_device *dev,
				 struct netdev_switch_notifier_info *info)
{
	int err;

	info->dev = dev;
	mutex_lock(&netdev_switch_mutex);
	err = raw_notifier_call_chain(&netdev_switch_notif_chain, val, info);
	mutex_unlock(&netdev_switch_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(call_netdev_switch_notifiers);

/**
 *	netdev_switch_port_bridge_setlink - Notify switch device port of bridge
 *	port attributes
 *
 *	@dev: port device
 *	@nlh: netlink msg with bridge port attributes
 *	@flags: bridge setlink flags
 *
 *	Notify switch device port of bridge port attributes
 */
int netdev_switch_port_bridge_setlink(struct net_device *dev,
				      struct nlmsghdr *nlh, u16 flags)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!(dev->features & NETIF_F_HW_SWITCH_OFFLOAD))
		return 0;

	if (!ops->ndo_bridge_setlink)
		return -EOPNOTSUPP;

	return ops->ndo_bridge_setlink(dev, nlh, flags);
}
EXPORT_SYMBOL_GPL(netdev_switch_port_bridge_setlink);

/**
 *	netdev_switch_port_bridge_dellink - Notify switch device port of bridge
 *	port attribute delete
 *
 *	@dev: port device
 *	@nlh: netlink msg with bridge port attributes
 *	@flags: bridge setlink flags
 *
 *	Notify switch device port of bridge port attribute delete
 */
int netdev_switch_port_bridge_dellink(struct net_device *dev,
				      struct nlmsghdr *nlh, u16 flags)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!(dev->features & NETIF_F_HW_SWITCH_OFFLOAD))
		return 0;

	if (!ops->ndo_bridge_dellink)
		return -EOPNOTSUPP;

	return ops->ndo_bridge_dellink(dev, nlh, flags);
}
EXPORT_SYMBOL_GPL(netdev_switch_port_bridge_dellink);

/**
 *	ndo_dflt_netdev_switch_port_bridge_setlink - default ndo bridge setlink
 *						     op for master devices
 *
 *	@dev: port device
 *	@nlh: netlink msg with bridge port attributes
 *	@flags: bridge setlink flags
 *
 *	Notify master device slaves of bridge port attributes
 */
int ndo_dflt_netdev_switch_port_bridge_setlink(struct net_device *dev,
					       struct nlmsghdr *nlh, u16 flags)
{
	struct net_device *lower_dev;
	struct list_head *iter;
	int ret = 0, err = 0;

	if (!(dev->features & NETIF_F_HW_SWITCH_OFFLOAD))
		return ret;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = netdev_switch_port_bridge_setlink(lower_dev, nlh, flags);
		if (err && err != -EOPNOTSUPP)
			ret = err;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ndo_dflt_netdev_switch_port_bridge_setlink);

/**
 *	ndo_dflt_netdev_switch_port_bridge_dellink - default ndo bridge dellink
 *						     op for master devices
 *
 *	@dev: port device
 *	@nlh: netlink msg with bridge port attributes
 *	@flags: bridge dellink flags
 *
 *	Notify master device slaves of bridge port attribute deletes
 */
int ndo_dflt_netdev_switch_port_bridge_dellink(struct net_device *dev,
					       struct nlmsghdr *nlh, u16 flags)
{
	struct net_device *lower_dev;
	struct list_head *iter;
	int ret = 0, err = 0;

	if (!(dev->features & NETIF_F_HW_SWITCH_OFFLOAD))
		return ret;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = netdev_switch_port_bridge_dellink(lower_dev, nlh, flags);
		if (err && err != -EOPNOTSUPP)
			ret = err;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ndo_dflt_netdev_switch_port_bridge_dellink);

static struct net_device *netdev_switch_get_lowest_dev(struct net_device *dev)
{
	const struct swdev_ops *ops = dev->swdev_ops;
	struct net_device *lower_dev;
	struct net_device *port_dev;
	struct list_head *iter;

	/* Recusively search down until we find a sw port dev.
	 * (A sw port dev supports swdev_parent_id_get).
	 */

	if (dev->features & NETIF_F_HW_SWITCH_OFFLOAD &&
	    ops && ops->swdev_parent_id_get)
		return dev;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		port_dev = netdev_switch_get_lowest_dev(lower_dev);
		if (port_dev)
			return port_dev;
	}

	return NULL;
}

static struct net_device *netdev_switch_get_dev_by_nhs(struct fib_info *fi)
{
	struct netdev_phys_item_id psid;
	struct netdev_phys_item_id prev_psid;
	struct net_device *dev = NULL;
	int nhsel;

	/* For this route, all nexthop devs must be on the same switch. */

	for (nhsel = 0; nhsel < fi->fib_nhs; nhsel++) {
		const struct fib_nh *nh = &fi->fib_nh[nhsel];

		if (!nh->nh_dev)
			return NULL;

		dev = netdev_switch_get_lowest_dev(nh->nh_dev);
		if (!dev)
			return NULL;

		if (netdev_switch_parent_id_get(dev, &psid))
			return NULL;

		if (nhsel > 0) {
			if (prev_psid.id_len != psid.id_len)
				return NULL;
			if (memcmp(prev_psid.id, psid.id, psid.id_len))
				return NULL;
		}

		prev_psid = psid;
	}

	return dev;
}

/**
 *	netdev_switch_fib_ipv4_add - Add IPv4 route entry to switch
 *
 *	@dst: route's IPv4 destination address
 *	@dst_len: destination address length (prefix length)
 *	@fi: route FIB info structure
 *	@tos: route TOS
 *	@type: route type
 *	@nlflags: netlink flags passed in (NLM_F_*)
 *	@tb_id: route table ID
 *
 *	Add IPv4 route entry to switch device.
 */
int netdev_switch_fib_ipv4_add(u32 dst, int dst_len, struct fib_info *fi,
			       u8 tos, u8 type, u32 nlflags, u32 tb_id)
{
	struct net_device *dev;
	const struct swdev_ops *ops;
	int err = 0;

	/* Don't offload route if using custom ip rules or if
	 * IPv4 FIB offloading has been disabled completely.
	 */

#ifdef CONFIG_IP_MULTIPLE_TABLES
	if (fi->fib_net->ipv4.fib_has_custom_rules)
		return 0;
#endif

	if (fi->fib_net->ipv4.fib_offload_disabled)
		return 0;

	dev = netdev_switch_get_dev_by_nhs(fi);
	if (!dev)
		return 0;
	ops = dev->swdev_ops;

	if (ops->swdev_fib_ipv4_add) {
		err = ops->swdev_fib_ipv4_add(dev, htonl(dst), dst_len,
					      fi, tos, type, nlflags,
					      tb_id);
		if (!err)
			fi->fib_flags |= RTNH_F_EXTERNAL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(netdev_switch_fib_ipv4_add);

/**
 *	netdev_switch_fib_ipv4_del - Delete IPv4 route entry from switch
 *
 *	@dst: route's IPv4 destination address
 *	@dst_len: destination address length (prefix length)
 *	@fi: route FIB info structure
 *	@tos: route TOS
 *	@type: route type
 *	@tb_id: route table ID
 *
 *	Delete IPv4 route entry from switch device.
 */
int netdev_switch_fib_ipv4_del(u32 dst, int dst_len, struct fib_info *fi,
			       u8 tos, u8 type, u32 tb_id)
{
	struct net_device *dev;
	const struct swdev_ops *ops;
	int err = 0;

	if (!(fi->fib_flags & RTNH_F_EXTERNAL))
		return 0;

	dev = netdev_switch_get_dev_by_nhs(fi);
	if (!dev)
		return 0;
	ops = dev->swdev_ops;

	if (ops->swdev_fib_ipv4_del) {
		err = ops->swdev_fib_ipv4_del(dev, htonl(dst), dst_len,
					      fi, tos, type, tb_id);
		if (!err)
			fi->fib_flags &= ~RTNH_F_EXTERNAL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(netdev_switch_fib_ipv4_del);

/**
 *	netdev_switch_fib_ipv4_abort - Abort an IPv4 FIB operation
 *
 *	@fi: route FIB info structure
 */
void netdev_switch_fib_ipv4_abort(struct fib_info *fi)
{
	/* There was a problem installing this route to the offload
	 * device.  For now, until we come up with more refined
	 * policy handling, abruptly end IPv4 fib offloading for
	 * for entire net by flushing offload device(s) of all
	 * IPv4 routes, and mark IPv4 fib offloading broken from
	 * this point forward.
	 */

	fib_flush_external(fi->fib_net);
	fi->fib_net->ipv4.fib_offload_disabled = true;
}
EXPORT_SYMBOL_GPL(netdev_switch_fib_ipv4_abort);

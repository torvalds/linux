/*
 * net/switchdev/switchdev.c - Switch device API
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
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
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_switch_parent_id_get)
		return -EOPNOTSUPP;
	return ops->ndo_switch_parent_id_get(dev, psid);
}
EXPORT_SYMBOL(netdev_switch_parent_id_get);

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
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_switch_port_stp_update)
		return -EOPNOTSUPP;
	WARN_ON(!ops->ndo_switch_parent_id_get);
	return ops->ndo_switch_port_stp_update(dev, state);
}
EXPORT_SYMBOL(netdev_switch_port_stp_update);

static DEFINE_MUTEX(netdev_switch_mutex);
static RAW_NOTIFIER_HEAD(netdev_switch_notif_chain);

/**
 *	register_netdev_switch_notifier - Register nofifier
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
EXPORT_SYMBOL(register_netdev_switch_notifier);

/**
 *	unregister_netdev_switch_notifier - Unregister nofifier
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
EXPORT_SYMBOL(unregister_netdev_switch_notifier);

/**
 *	call_netdev_switch_notifiers - Call nofifiers
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
EXPORT_SYMBOL(call_netdev_switch_notifiers);

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
EXPORT_SYMBOL(netdev_switch_port_bridge_setlink);

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
EXPORT_SYMBOL(netdev_switch_port_bridge_dellink);

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
EXPORT_SYMBOL(ndo_dflt_netdev_switch_port_bridge_setlink);

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
EXPORT_SYMBOL(ndo_dflt_netdev_switch_port_bridge_dellink);

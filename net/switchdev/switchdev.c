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

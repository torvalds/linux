/*
 * include/net/switchdev.h - Switch device API
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _LINUX_SWITCHDEV_H_
#define _LINUX_SWITCHDEV_H_

#include <linux/netdevice.h>

#ifdef CONFIG_NET_SWITCHDEV

int netdev_switch_parent_id_get(struct net_device *dev,
				struct netdev_phys_item_id *psid);
int netdev_switch_port_stp_update(struct net_device *dev, u8 state);

#else

static inline int netdev_switch_parent_id_get(struct net_device *dev,
					      struct netdev_phys_item_id *psid)
{
	return -EOPNOTSUPP;
}

static inline int netdev_switch_port_stp_update(struct net_device *dev,
						u8 state)
{
	return -EOPNOTSUPP;
}

#endif

#endif /* _LINUX_SWITCHDEV_H_ */

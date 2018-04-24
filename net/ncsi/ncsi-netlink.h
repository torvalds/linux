/*
 * Copyright Samuel Mendoza-Jonas, IBM Corporation 2018.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NCSI_NETLINK_H__
#define __NCSI_NETLINK_H__

#include <linux/netdevice.h>

#include "internal.h"

int ncsi_init_netlink(struct net_device *dev);
int ncsi_unregister_netlink(struct net_device *dev);

#endif /* __NCSI_NETLINK_H__ */

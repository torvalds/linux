/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2007-2011 Nicira, Inc.
 */

#ifndef VPORT_NETDEV_H
#define VPORT_NETDEV_H 1

#include <linux/netdevice.h>
#include <linux/rcupdate.h>

#include "vport.h"

struct vport *ovs_netdev_get_vport(struct net_device *dev);

struct vport *ovs_netdev_link(struct vport *vport, const char *name);
void ovs_netdev_detach_dev(struct vport *);

int __init ovs_netdev_init(void);
void ovs_netdev_exit(void);

void ovs_netdev_tunnel_destroy(struct vport *vport);
#endif /* vport_netdev.h */

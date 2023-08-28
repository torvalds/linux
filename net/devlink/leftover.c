// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/devlink.c - Network physical/parent device Netlink interface
 *
 * Heavily inspired by net/wireless/
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/u64_stats_sync.h>
#include <linux/timekeeping.h>
#include <rdma/ib_verbs.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/devlink.h>

#include "devl_internal.h"

void devlink_notify_register(struct devlink *devlink)
{
	devlink_notify(devlink, DEVLINK_CMD_NEW);
	devlink_linecards_notify_register(devlink);
	devlink_ports_notify_register(devlink);
	devlink_trap_policers_notify_register(devlink);
	devlink_trap_groups_notify_register(devlink);
	devlink_traps_notify_register(devlink);
	devlink_rates_notify_register(devlink);
	devlink_regions_notify_register(devlink);
	devlink_params_notify_register(devlink);
}

void devlink_notify_unregister(struct devlink *devlink)
{
	devlink_params_notify_unregister(devlink);
	devlink_regions_notify_unregister(devlink);
	devlink_rates_notify_unregister(devlink);
	devlink_traps_notify_unregister(devlink);
	devlink_trap_groups_notify_unregister(devlink);
	devlink_trap_policers_notify_unregister(devlink);
	devlink_ports_notify_unregister(devlink);
	devlink_linecards_notify_unregister(devlink);
	devlink_notify(devlink, DEVLINK_CMD_DEL);
}

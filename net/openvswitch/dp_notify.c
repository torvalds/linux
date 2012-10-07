/*
 * Copyright (c) 2007-2012 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include <linux/netdevice.h>
#include <net/genetlink.h>

#include "datapath.h"
#include "vport-internal_dev.h"
#include "vport-netdev.h"

static int dp_device_event(struct notifier_block *unused, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = ptr;
	struct vport *vport;

	if (ovs_is_internal_dev(dev))
		vport = ovs_internal_dev_get_vport(dev);
	else
		vport = ovs_netdev_get_vport(dev);

	if (!vport)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		if (!ovs_is_internal_dev(dev)) {
			struct sk_buff *notify;
			struct datapath *dp = vport->dp;

			notify = ovs_vport_cmd_build_info(vport, 0, 0,
							  OVS_VPORT_CMD_DEL);
			ovs_dp_detach_port(vport);
			if (IS_ERR(notify)) {
				netlink_set_err(ovs_dp_get_net(dp)->genl_sock, 0,
						ovs_dp_vport_multicast_group.id,
						PTR_ERR(notify));
				break;
			}

			genlmsg_multicast_netns(ovs_dp_get_net(dp), notify, 0,
						ovs_dp_vport_multicast_group.id,
						GFP_KERNEL);
		}
		break;
	}

	return NOTIFY_DONE;
}

struct notifier_block ovs_dp_device_notifier = {
	.notifier_call = dp_device_event
};

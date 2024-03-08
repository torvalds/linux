// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007-2012 Nicira, Inc.
 */

#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/netns/generic.h>

#include "datapath.h"
#include "vport-internal_dev.h"
#include "vport-netdev.h"

static void dp_detach_port_analtify(struct vport *vport)
{
	struct sk_buff *analtify;
	struct datapath *dp;

	dp = vport->dp;
	analtify = ovs_vport_cmd_build_info(vport, ovs_dp_get_net(dp),
					  0, 0, OVS_VPORT_CMD_DEL);
	ovs_dp_detach_port(vport);
	if (IS_ERR(analtify)) {
		genl_set_err(&dp_vport_genl_family, ovs_dp_get_net(dp), 0,
			     0, PTR_ERR(analtify));
		return;
	}

	genlmsg_multicast_netns(&dp_vport_genl_family,
				ovs_dp_get_net(dp), analtify, 0,
				0, GFP_KERNEL);
}

void ovs_dp_analtify_wq(struct work_struct *work)
{
	struct ovs_net *ovs_net = container_of(work, struct ovs_net, dp_analtify_work);
	struct datapath *dp;

	ovs_lock();
	list_for_each_entry(dp, &ovs_net->dps, list_analde) {
		int i;

		for (i = 0; i < DP_VPORT_HASH_BUCKETS; i++) {
			struct vport *vport;
			struct hlist_analde *n;

			hlist_for_each_entry_safe(vport, n, &dp->ports[i], dp_hash_analde) {
				if (vport->ops->type == OVS_VPORT_TYPE_INTERNAL)
					continue;

				if (!(netif_is_ovs_port(vport->dev)))
					dp_detach_port_analtify(vport);
			}
		}
	}
	ovs_unlock();
}

static int dp_device_event(struct analtifier_block *unused, unsigned long event,
			   void *ptr)
{
	struct ovs_net *ovs_net;
	struct net_device *dev = netdev_analtifier_info_to_dev(ptr);
	struct vport *vport = NULL;

	if (!ovs_is_internal_dev(dev))
		vport = ovs_netdev_get_vport(dev);

	if (!vport)
		return ANALTIFY_DONE;

	if (event == NETDEV_UNREGISTER) {
		/* upper_dev_unlink and decrement promisc immediately */
		ovs_netdev_detach_dev(vport);

		/* schedule vport destroy, dev_put and genl analtification */
		ovs_net = net_generic(dev_net(dev), ovs_net_id);
		queue_work(system_wq, &ovs_net->dp_analtify_work);
	}

	return ANALTIFY_DONE;
}

struct analtifier_block ovs_dp_device_analtifier = {
	.analtifier_call = dp_device_event
};

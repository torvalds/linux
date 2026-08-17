// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007-2012 Nicira, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/if_arp.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/llc.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/openvswitch.h>
#include <linux/export.h>

#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>

#include "datapath.h"
#include "vport.h"
#include "vport-internal_dev.h"
#include "vport-netdev.h"

static struct vport_ops ovs_netdev_vport_ops;

/* Must be called with rcu_read_lock. */
static void netdev_port_receive(struct sk_buff *skb)
{
	struct vport *vport;

	vport = ovs_netdev_get_vport(skb->dev);
	if (unlikely(!vport))
		goto error;

	if (unlikely(skb_warn_if_lro(skb)))
		goto error;

	/* Make our own copy of the packet.  Otherwise we will mangle the
	 * packet for anyone who came before us (e.g. tcpdump via AF_PACKET).
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return;

	if (skb->dev->type == ARPHRD_ETHER)
		skb_push_rcsum(skb, ETH_HLEN);

	ovs_vport_receive(vport, skb, skb_tunnel_info(skb));
	return;
error:
	kfree_skb(skb);
}

/* Called with rcu_read_lock and bottom-halves disabled. */
static rx_handler_result_t netdev_frame_hook(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;

	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	netdev_port_receive(skb);
	return RX_HANDLER_CONSUMED;
}

static struct net_device *get_dpdev(const struct datapath *dp)
{
	struct vport *local;

	local = ovs_vport_ovsl(dp, OVSP_LOCAL);
	return local->dev;
}

struct vport *ovs_netdev_link(struct vport *vport, bool tunnel)
{
	int err;

	if (WARN_ON_ONCE(!vport->dev)) {
		err = -ENODEV;
		goto error_free_vport;
	}

	rtnl_lock();
	/* Do not link devices that are not registered to avoid a potential
	 * race with the NETDEV_UNREGISTER notification in dp_device_event().
	 */
	if (vport->dev->reg_state != NETREG_REGISTERED) {
		err = -ENODEV;
		goto error_put_unlock;
	}

	err = netdev_master_upper_dev_link(vport->dev,
					   get_dpdev(vport->dp),
					   NULL, NULL, NULL);
	if (err)
		goto error_put_unlock;

	err = netdev_rx_handler_register(vport->dev, netdev_frame_hook,
					 vport);
	if (err)
		goto error_master_upper_dev_unlink;

	dev_disable_lro(vport->dev);
	dev_set_promiscuity(vport->dev, 1);
	vport->dev->priv_flags |= IFF_OVS_DATAPATH;
	rtnl_unlock();

	return vport;

error_master_upper_dev_unlink:
	netdev_upper_dev_unlink(vport->dev, get_dpdev(vport->dp));
error_put_unlock:
	if (tunnel && vport->dev->reg_state == NETREG_REGISTERED)
		rtnl_delete_link(vport->dev, 0, NULL);
	netdev_put(vport->dev, &vport->dev_tracker);
	rtnl_unlock();
error_free_vport:
	ovs_vport_free(vport);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(ovs_netdev_link);

static struct vport *netdev_create(const struct vport_parms *parms)
{
	struct vport *vport;
	int err;

	vport = ovs_vport_alloc(0, &ovs_netdev_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	vport->dev = dev_get_by_name(ovs_dp_get_net(vport->dp), parms->name);
	if (!vport->dev) {
		err = -ENODEV;
		goto error_free_vport;
	}
	netdev_tracker_alloc(vport->dev, &vport->dev_tracker, GFP_KERNEL);

	/* Ensure that the provided name is not an alias. */
	if (strcmp(parms->name, ovs_vport_name(vport))) {
		err = -ENODEV;
		goto error_put;
	}

	if (vport->dev->flags & IFF_LOOPBACK ||
	    (vport->dev->type != ARPHRD_ETHER &&
	     vport->dev->type != ARPHRD_NONE) ||
	    ovs_is_internal_dev(vport->dev)) {
		err = -EINVAL;
		goto error_put;
	}

	return ovs_netdev_link(vport, false);
error_put:
	netdev_put(vport->dev, &vport->dev_tracker);
error_free_vport:
	ovs_vport_free(vport);
	return ERR_PTR(err);
}

static void vport_netdev_free(struct rcu_head *rcu)
{
	struct vport *vport = container_of(rcu, struct vport, rcu);

	netdev_put(vport->dev, &vport->dev_tracker);
	ovs_vport_free(vport);
}

void ovs_netdev_detach_dev(struct vport *vport)
{
	ASSERT_RTNL();
	netdev_rx_handler_unregister(vport->dev);
	netdev_upper_dev_unlink(vport->dev,
				netdev_master_upper_dev_get(vport->dev));
	dev_set_promiscuity(vport->dev, -1);

	/* paired with smp_mb() in netdev_destroy() */
	smp_wmb();

	vport->dev->priv_flags &= ~IFF_OVS_DATAPATH;
}

static void netdev_destroy(struct vport *vport)
{
	/* When called from ovs_db_notify_wq() after a dp_device_event(), the
	 * port has already been detached, so we can avoid taking the RTNL by
	 * checking this first.
	 */
	if (netif_is_ovs_port(vport->dev)) {
		rtnl_lock();
		/* Check again while holding the lock to ensure we don't race
		 * with the netdev notifier and detach twice.
		 */
		if (netif_is_ovs_port(vport->dev))
			ovs_netdev_detach_dev(vport);
		rtnl_unlock();
	}

	/* paired with smp_wmb() in ovs_netdev_detach_dev() */
	smp_mb();

	call_rcu(&vport->rcu, vport_netdev_free);
}

void ovs_netdev_tunnel_destroy(struct vport *vport)
{
	rtnl_lock();
	if (netif_is_ovs_port(vport->dev))
		ovs_netdev_detach_dev(vport);

	/* We can be invoked by both explicit vport deletion and
	 * underlying netdev deregistration; delete the link only
	 * if it's not already shutting down.
	 */
	if (vport->dev->reg_state == NETREG_REGISTERED)
		rtnl_delete_link(vport->dev, 0, NULL);

	/* We can't put the device reference yet, since it can still be in
	 * use, but rtnl_unlock()->netdev_run_todo() will block until all
	 * the references are released, so the RCU call must be before it.
	 */
	call_rcu(&vport->rcu, vport_netdev_free);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(ovs_netdev_tunnel_destroy);

/* Returns null if this device is not attached to a datapath. */
struct vport *ovs_netdev_get_vport(struct net_device *dev)
{
	if (likely(netif_is_ovs_port(dev)))
		return (struct vport *)
			rcu_dereference_rtnl(dev->rx_handler_data);
	else
		return NULL;
}

static struct vport_ops ovs_netdev_vport_ops = {
	.type		= OVS_VPORT_TYPE_NETDEV,
	.create		= netdev_create,
	.destroy	= netdev_destroy,
	.send		= dev_queue_xmit,
};

int __init ovs_netdev_init(void)
{
	return ovs_vport_ops_register(&ovs_netdev_vport_ops);
}

void ovs_netdev_exit(void)
{
	ovs_vport_ops_unregister(&ovs_netdev_vport_ops);
}

/*
 *	Device handling code
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_device.c,v 1.6 2001/12/24 00:59:55 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include "br_private.h"

static struct net_device_stats *br_dev_get_stats(struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	return &br->statistics;
}

int br_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	const unsigned char *dest = skb->data;
	struct net_bridge_fdb_entry *dst;

	br->statistics.tx_packets++;
	br->statistics.tx_bytes += skb->len;

	skb->mac.raw = skb->data;
	skb_pull(skb, ETH_HLEN);

	rcu_read_lock();
	if (dest[0] & 1) 
		br_flood_deliver(br, skb, 0);
	else if ((dst = __br_fdb_get(br, dest)) != NULL)
		br_deliver(dst->dst, skb);
	else
		br_flood_deliver(br, skb, 0);

	rcu_read_unlock();
	return 0;
}

static int br_dev_open(struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);

	br_features_recompute(br);
	netif_start_queue(dev);
	br_stp_enable_bridge(br);

	return 0;
}

static void br_dev_set_multicast_list(struct net_device *dev)
{
}

static int br_dev_stop(struct net_device *dev)
{
	br_stp_disable_bridge(netdev_priv(dev));

	netif_stop_queue(dev);

	return 0;
}

static int br_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > br_min_mtu(netdev_priv(dev)))
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

void br_dev_setup(struct net_device *dev)
{
	memset(dev->dev_addr, 0, ETH_ALEN);

	ether_setup(dev);

	dev->do_ioctl = br_dev_ioctl;
	dev->get_stats = br_dev_get_stats;
	dev->hard_start_xmit = br_dev_xmit;
	dev->open = br_dev_open;
	dev->set_multicast_list = br_dev_set_multicast_list;
	dev->change_mtu = br_change_mtu;
	dev->destructor = free_netdev;
	SET_MODULE_OWNER(dev);
	dev->stop = br_dev_stop;
	dev->tx_queue_len = 0;
	dev->set_mac_address = NULL;
	dev->priv_flags = IFF_EBRIDGE;
}

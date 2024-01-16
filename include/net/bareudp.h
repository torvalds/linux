/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NET_BAREUDP_H
#define __NET_BAREUDP_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/rtnetlink.h>

static inline bool netif_is_bareudp(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "bareudp");
}

#endif

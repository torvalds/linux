/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PFCP_H_
#define _PFCP_H_

#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/types.h>

#define PFCP_PORT 8805

static inline bool netif_is_pfcp(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "pfcp");
}

#endif

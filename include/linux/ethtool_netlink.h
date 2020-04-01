/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_ETHTOOL_NETLINK_H_
#define _LINUX_ETHTOOL_NETLINK_H_

#include <uapi/linux/ethtool_netlink.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>

#define __ETHTOOL_LINK_MODE_MASK_NWORDS \
	DIV_ROUND_UP(__ETHTOOL_LINK_MODE_MASK_NBITS, 32)

enum ethtool_multicast_groups {
	ETHNL_MCGRP_MONITOR,
};

#endif /* _LINUX_ETHTOOL_NETLINK_H_ */

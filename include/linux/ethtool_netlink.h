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

struct phy_device;

#if IS_ENABLED(CONFIG_ETHTOOL_NETLINK)
int ethnl_cable_test_alloc(struct phy_device *phydev);
void ethnl_cable_test_free(struct phy_device *phydev);
void ethnl_cable_test_finished(struct phy_device *phydev);
#else
static inline int ethnl_cable_test_alloc(struct phy_device *phydev)
{
	return -ENOTSUPP;
}

static inline void ethnl_cable_test_free(struct phy_device *phydev)
{
}

static inline void ethnl_cable_test_finished(struct phy_device *phydev)
{
}
#endif /* IS_ENABLED(ETHTOOL_NETLINK) */
#endif /* _LINUX_ETHTOOL_NETLINK_H_ */

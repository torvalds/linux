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
int ethnl_cable_test_alloc(struct phy_device *phydev, u8 cmd);
void ethnl_cable_test_free(struct phy_device *phydev);
void ethnl_cable_test_finished(struct phy_device *phydev);
int ethnl_cable_test_result(struct phy_device *phydev, u8 pair, u8 result);
int ethnl_cable_test_fault_length(struct phy_device *phydev, u8 pair, u32 cm);
int ethnl_cable_test_amplitude(struct phy_device *phydev, u8 pair, s16 mV);
int ethnl_cable_test_pulse(struct phy_device *phydev, u16 mV);
int ethnl_cable_test_step(struct phy_device *phydev, u32 first, u32 last,
			  u32 step);
#else
static inline int ethnl_cable_test_alloc(struct phy_device *phydev, u8 cmd)
{
	return -EOPNOTSUPP;
}

static inline void ethnl_cable_test_free(struct phy_device *phydev)
{
}

static inline void ethnl_cable_test_finished(struct phy_device *phydev)
{
}
static inline int ethnl_cable_test_result(struct phy_device *phydev, u8 pair,
					  u8 result)
{
	return -EOPNOTSUPP;
}

static inline int ethnl_cable_test_fault_length(struct phy_device *phydev,
						u8 pair, u32 cm)
{
	return -EOPNOTSUPP;
}

static inline int ethnl_cable_test_amplitude(struct phy_device *phydev,
					     u8 pair, s16 mV)
{
	return -EOPNOTSUPP;
}

static inline int ethnl_cable_test_pulse(struct phy_device *phydev, u16 mV)
{
	return -EOPNOTSUPP;
}

static inline int ethnl_cable_test_step(struct phy_device *phydev, u32 first,
					u32 last, u32 step)
{
	return -EOPNOTSUPP;
}
#endif /* IS_ENABLED(CONFIG_ETHTOOL_NETLINK) */
#endif /* _LINUX_ETHTOOL_NETLINK_H_ */

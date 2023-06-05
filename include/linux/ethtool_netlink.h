/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_ETHTOOL_NETLINK_H_
#define _LINUX_ETHTOOL_NETLINK_H_

#include <uapi/linux/ethtool_netlink.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>

#define __ETHTOOL_LINK_MODE_MASK_NWORDS \
	DIV_ROUND_UP(__ETHTOOL_LINK_MODE_MASK_NBITS, 32)

#define ETHTOOL_PAUSE_STAT_CNT	(__ETHTOOL_A_PAUSE_STAT_CNT -		\
				 ETHTOOL_A_PAUSE_STAT_TX_FRAMES)

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
void ethtool_aggregate_mac_stats(struct net_device *dev,
				 struct ethtool_eth_mac_stats *mac_stats);
void ethtool_aggregate_phy_stats(struct net_device *dev,
				 struct ethtool_eth_phy_stats *phy_stats);
void ethtool_aggregate_ctrl_stats(struct net_device *dev,
				  struct ethtool_eth_ctrl_stats *ctrl_stats);
void ethtool_aggregate_pause_stats(struct net_device *dev,
				   struct ethtool_pause_stats *pause_stats);
void ethtool_aggregate_rmon_stats(struct net_device *dev,
				  struct ethtool_rmon_stats *rmon_stats);
bool ethtool_dev_mm_supported(struct net_device *dev);

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

static inline void
ethtool_aggregate_mac_stats(struct net_device *dev,
			    struct ethtool_eth_mac_stats *mac_stats)
{
}

static inline void
ethtool_aggregate_phy_stats(struct net_device *dev,
			    struct ethtool_eth_phy_stats *phy_stats)
{
}

static inline void
ethtool_aggregate_ctrl_stats(struct net_device *dev,
			     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
}

static inline void
ethtool_aggregate_pause_stats(struct net_device *dev,
			      struct ethtool_pause_stats *pause_stats)
{
}

static inline void
ethtool_aggregate_rmon_stats(struct net_device *dev,
			     struct ethtool_rmon_stats *rmon_stats)
{
}

static inline bool ethtool_dev_mm_supported(struct net_device *dev)
{
	return false;
}

#endif /* IS_ENABLED(CONFIG_ETHTOOL_NETLINK) */
#endif /* _LINUX_ETHTOOL_NETLINK_H_ */

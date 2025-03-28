/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Stubs for the Network PHY library
 */

#include <linux/rtnetlink.h>

struct ethtool_eth_phy_stats;
struct ethtool_link_ext_stats;
struct ethtool_phy_stats;
struct kernel_hwtstamp_config;
struct netlink_ext_ack;
struct phy_device;

#if IS_ENABLED(CONFIG_PHYLIB)

extern const struct phylib_stubs *phylib_stubs;

struct phylib_stubs {
	int (*hwtstamp_get)(struct phy_device *phydev,
			    struct kernel_hwtstamp_config *config);
	int (*hwtstamp_set)(struct phy_device *phydev,
			    struct kernel_hwtstamp_config *config,
			    struct netlink_ext_ack *extack);
	void (*get_phy_stats)(struct phy_device *phydev,
			      struct ethtool_eth_phy_stats *phy_stats,
			      struct ethtool_phy_stats *phydev_stats);
	void (*get_link_ext_stats)(struct phy_device *phydev,
				   struct ethtool_link_ext_stats *link_stats);
};

static inline int phy_hwtstamp_get(struct phy_device *phydev,
				   struct kernel_hwtstamp_config *config)
{
	/* phylib_register_stubs() and phylib_unregister_stubs()
	 * also run under rtnl_lock().
	 */
	ASSERT_RTNL();

	if (!phylib_stubs)
		return -EOPNOTSUPP;

	return phylib_stubs->hwtstamp_get(phydev, config);
}

static inline int phy_hwtstamp_set(struct phy_device *phydev,
				   struct kernel_hwtstamp_config *config,
				   struct netlink_ext_ack *extack)
{
	/* phylib_register_stubs() and phylib_unregister_stubs()
	 * also run under rtnl_lock().
	 */
	ASSERT_RTNL();

	if (!phylib_stubs)
		return -EOPNOTSUPP;

	return phylib_stubs->hwtstamp_set(phydev, config, extack);
}

static inline void phy_ethtool_get_phy_stats(struct phy_device *phydev,
					struct ethtool_eth_phy_stats *phy_stats,
					struct ethtool_phy_stats *phydev_stats)
{
	ASSERT_RTNL();

	if (!phylib_stubs)
		return;

	phylib_stubs->get_phy_stats(phydev, phy_stats, phydev_stats);
}

static inline void phy_ethtool_get_link_ext_stats(struct phy_device *phydev,
				    struct ethtool_link_ext_stats *link_stats)
{
	ASSERT_RTNL();

	if (!phylib_stubs)
		return;

	phylib_stubs->get_link_ext_stats(phydev, link_stats);
}

#else

static inline int phy_hwtstamp_get(struct phy_device *phydev,
				   struct kernel_hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline int phy_hwtstamp_set(struct phy_device *phydev,
				   struct kernel_hwtstamp_config *config,
				   struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline void phy_ethtool_get_phy_stats(struct phy_device *phydev,
					struct ethtool_eth_phy_stats *phy_stats,
					struct ethtool_phy_stats *phydev_stats)
{
}

static inline void phy_ethtool_get_link_ext_stats(struct phy_device *phydev,
				    struct ethtool_link_ext_stats *link_stats)
{
}

#endif

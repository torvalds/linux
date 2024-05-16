/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Stubs for the Network PHY library
 */

#include <linux/rtnetlink.h>

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

#endif

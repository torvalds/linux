/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/dsa_stubs.h - Stubs for the Distributed Switch Architecture framework
 */

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <net/dsa.h>

#if IS_ENABLED(CONFIG_NET_DSA)

extern const struct dsa_stubs *dsa_stubs;

struct dsa_stubs {
	int (*conduit_hwtstamp_validate)(struct net_device *dev,
					 const struct kernel_hwtstamp_config *config,
					 struct netlink_ext_ack *extack);
};

static inline int dsa_conduit_hwtstamp_validate(struct net_device *dev,
						const struct kernel_hwtstamp_config *config,
						struct netlink_ext_ack *extack)
{
	if (!netdev_uses_dsa(dev))
		return 0;

	/* rtnl_lock() is a sufficient guarantee, because as long as
	 * netdev_uses_dsa() returns true, the dsa_core module is still
	 * registered, and so, dsa_unregister_stubs() couldn't have run.
	 * For netdev_uses_dsa() to start returning false, it would imply that
	 * dsa_conduit_teardown() has executed, which requires rtnl_lock().
	 */
	ASSERT_RTNL();

	return dsa_stubs->conduit_hwtstamp_validate(dev, config, extack);
}

#else

static inline int dsa_conduit_hwtstamp_validate(struct net_device *dev,
						const struct kernel_hwtstamp_config *config,
						struct netlink_ext_ack *extack)
{
	return 0;
}

#endif

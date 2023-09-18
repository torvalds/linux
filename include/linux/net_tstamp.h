/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_NET_TIMESTAMPING_H_
#define _LINUX_NET_TIMESTAMPING_H_

#include <uapi/linux/net_tstamp.h>

enum hwtstamp_source {
	HWTSTAMP_SOURCE_NETDEV,
	HWTSTAMP_SOURCE_PHYLIB,
};

/**
 * struct kernel_hwtstamp_config - Kernel copy of struct hwtstamp_config
 *
 * @flags: see struct hwtstamp_config
 * @tx_type: see struct hwtstamp_config
 * @rx_filter: see struct hwtstamp_config
 * @ifr: pointer to ifreq structure from the original ioctl request, to pass to
 *	a legacy implementation of a lower driver
 * @copied_to_user: request was passed to a legacy implementation which already
 *	copied the ioctl request back to user space
 * @source: indication whether timestamps should come from the netdev or from
 *	an attached phylib PHY
 *
 * Prefer using this structure for in-kernel processing of hardware
 * timestamping configuration, over the inextensible struct hwtstamp_config
 * exposed to the %SIOCGHWTSTAMP and %SIOCSHWTSTAMP ioctl UAPI.
 */
struct kernel_hwtstamp_config {
	int flags;
	int tx_type;
	int rx_filter;
	struct ifreq *ifr;
	bool copied_to_user;
	enum hwtstamp_source source;
};

static inline void hwtstamp_config_to_kernel(struct kernel_hwtstamp_config *kernel_cfg,
					     const struct hwtstamp_config *cfg)
{
	kernel_cfg->flags = cfg->flags;
	kernel_cfg->tx_type = cfg->tx_type;
	kernel_cfg->rx_filter = cfg->rx_filter;
}

static inline void hwtstamp_config_from_kernel(struct hwtstamp_config *cfg,
					       const struct kernel_hwtstamp_config *kernel_cfg)
{
	cfg->flags = kernel_cfg->flags;
	cfg->tx_type = kernel_cfg->tx_type;
	cfg->rx_filter = kernel_cfg->rx_filter;
}

static inline bool kernel_hwtstamp_config_changed(const struct kernel_hwtstamp_config *a,
						  const struct kernel_hwtstamp_config *b)
{
	return a->flags != b->flags ||
	       a->tx_type != b->tx_type ||
	       a->rx_filter != b->rx_filter;
}

#endif /* _LINUX_NET_TIMESTAMPING_H_ */

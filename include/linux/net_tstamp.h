/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_NET_TIMESTAMPING_H_
#define _LINUX_NET_TIMESTAMPING_H_

#include <uapi/linux/net_tstamp.h>

/**
 * struct kernel_hwtstamp_config - Kernel copy of struct hwtstamp_config
 *
 * @flags: see struct hwtstamp_config
 * @tx_type: see struct hwtstamp_config
 * @rx_filter: see struct hwtstamp_config
 *
 * Prefer using this structure for in-kernel processing of hardware
 * timestamping configuration, over the inextensible struct hwtstamp_config
 * exposed to the %SIOCGHWTSTAMP and %SIOCSHWTSTAMP ioctl UAPI.
 */
struct kernel_hwtstamp_config {
	int flags;
	int tx_type;
	int rx_filter;
};

static inline void hwtstamp_config_to_kernel(struct kernel_hwtstamp_config *kernel_cfg,
					     const struct hwtstamp_config *cfg)
{
	kernel_cfg->flags = cfg->flags;
	kernel_cfg->tx_type = cfg->tx_type;
	kernel_cfg->rx_filter = cfg->rx_filter;
}

#endif /* _LINUX_NET_TIMESTAMPING_H_ */

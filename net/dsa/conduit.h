/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_CONDUIT_H
#define __DSA_CONDUIT_H

struct dsa_port;
struct net_device;
struct netdev_lag_upper_info;
struct netlink_ext_ack;

int dsa_conduit_setup(struct net_device *dev, struct dsa_port *cpu_dp);
void dsa_conduit_teardown(struct net_device *dev);
int dsa_conduit_lag_setup(struct net_device *lag_dev, struct dsa_port *cpu_dp,
			  struct netdev_lag_upper_info *uinfo,
			  struct netlink_ext_ack *extack);
void dsa_conduit_lag_teardown(struct net_device *lag_dev,
			      struct dsa_port *cpu_dp);
int __dsa_conduit_hwtstamp_validate(struct net_device *dev,
				    const struct kernel_hwtstamp_config *config,
				    struct netlink_ext_ack *extack);

#endif

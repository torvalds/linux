/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_MASTER_H
#define __DSA_MASTER_H

struct dsa_port;
struct net_device;
struct netdev_lag_upper_info;
struct netlink_ext_ack;

int dsa_master_setup(struct net_device *dev, struct dsa_port *cpu_dp);
void dsa_master_teardown(struct net_device *dev);
int dsa_master_lag_setup(struct net_device *lag_dev, struct dsa_port *cpu_dp,
			 struct netdev_lag_upper_info *uinfo,
			 struct netlink_ext_ack *extack);
void dsa_master_lag_teardown(struct net_device *lag_dev,
			     struct dsa_port *cpu_dp);

#endif

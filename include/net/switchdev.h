/*
 * include/net/switchdev.h - Switch device API
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _LINUX_SWITCHDEV_H_
#define _LINUX_SWITCHDEV_H_

#include <linux/netdevice.h>
#include <linux/notifier.h>

enum netdev_switch_notifier_type {
	NETDEV_SWITCH_FDB_ADD = 1,
	NETDEV_SWITCH_FDB_DEL,
};

struct netdev_switch_notifier_info {
	struct net_device *dev;
};

struct netdev_switch_notifier_fdb_info {
	struct netdev_switch_notifier_info info; /* must be first */
	const unsigned char *addr;
	u16 vid;
};

static inline struct net_device *
netdev_switch_notifier_info_to_dev(const struct netdev_switch_notifier_info *info)
{
	return info->dev;
}

#ifdef CONFIG_NET_SWITCHDEV

int netdev_switch_parent_id_get(struct net_device *dev,
				struct netdev_phys_item_id *psid);
int netdev_switch_port_stp_update(struct net_device *dev, u8 state);
int register_netdev_switch_notifier(struct notifier_block *nb);
int unregister_netdev_switch_notifier(struct notifier_block *nb);
int call_netdev_switch_notifiers(unsigned long val, struct net_device *dev,
				 struct netdev_switch_notifier_info *info);
int netdev_switch_port_bridge_setlink(struct net_device *dev,
				struct nlmsghdr *nlh, u16 flags);
int netdev_switch_port_bridge_dellink(struct net_device *dev,
				struct nlmsghdr *nlh, u16 flags);
int ndo_dflt_netdev_switch_port_bridge_dellink(struct net_device *dev,
					       struct nlmsghdr *nlh, u16 flags);
int ndo_dflt_netdev_switch_port_bridge_setlink(struct net_device *dev,
					       struct nlmsghdr *nlh, u16 flags);
int netdev_switch_fib_ipv4_add(u32 dst, int dst_len, struct fib_info *fi,
			       u8 tos, u8 type, u32 nlflags, u32 tb_id);
int netdev_switch_fib_ipv4_del(u32 dst, int dst_len, struct fib_info *fi,
			       u8 tos, u8 type, u32 tb_id);
void netdev_switch_fib_ipv4_abort(struct fib_info *fi);

#else

static inline int netdev_switch_parent_id_get(struct net_device *dev,
					      struct netdev_phys_item_id *psid)
{
	return -EOPNOTSUPP;
}

static inline int netdev_switch_port_stp_update(struct net_device *dev,
						u8 state)
{
	return -EOPNOTSUPP;
}

static inline int register_netdev_switch_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int unregister_netdev_switch_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int call_netdev_switch_notifiers(unsigned long val, struct net_device *dev,
					       struct netdev_switch_notifier_info *info)
{
	return NOTIFY_DONE;
}

static inline int netdev_switch_port_bridge_setlink(struct net_device *dev,
						    struct nlmsghdr *nlh,
						    u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int netdev_switch_port_bridge_dellink(struct net_device *dev,
						    struct nlmsghdr *nlh,
						    u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int ndo_dflt_netdev_switch_port_bridge_dellink(struct net_device *dev,
							struct nlmsghdr *nlh,
							u16 flags)
{
	return 0;
}

static inline int ndo_dflt_netdev_switch_port_bridge_setlink(struct net_device *dev,
							struct nlmsghdr *nlh,
							u16 flags)
{
	return 0;
}

static inline int netdev_switch_fib_ipv4_add(u32 dst, int dst_len,
					     struct fib_info *fi,
					     u8 tos, u8 type,
					     u32 nlflags, u32 tb_id)
{
	return 0;
}

static inline int netdev_switch_fib_ipv4_del(u32 dst, int dst_len,
					     struct fib_info *fi,
					     u8 tos, u8 type, u32 tb_id)
{
	return 0;
}

static inline void netdev_switch_fib_ipv4_abort(struct fib_info *fi)
{
}

#endif

#endif /* _LINUX_SWITCHDEV_H_ */

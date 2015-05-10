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

#define SWITCHDEV_F_NO_RECURSE		BIT(0)

enum switchdev_trans {
	SWITCHDEV_TRANS_NONE,
	SWITCHDEV_TRANS_PREPARE,
	SWITCHDEV_TRANS_ABORT,
	SWITCHDEV_TRANS_COMMIT,
};

enum switchdev_attr_id {
	SWITCHDEV_ATTR_UNDEFINED,
	SWITCHDEV_ATTR_PORT_PARENT_ID,
	SWITCHDEV_ATTR_PORT_STP_STATE,
};

struct switchdev_attr {
	enum switchdev_attr_id id;
	enum switchdev_trans trans;
	u32 flags;
	union {
		struct netdev_phys_item_id ppid;	/* PORT_PARENT_ID */
		u8 stp_state;				/* PORT_STP_STATE */
	};
};

struct fib_info;

enum switchdev_obj_id {
	SWITCHDEV_OBJ_UNDEFINED,
};

struct switchdev_obj {
	enum switchdev_obj_id id;
	enum switchdev_trans trans;
};

/**
 * struct switchdev_ops - switchdev operations
 *
 * @switchdev_port_attr_get: Get a port attribute (see switchdev_attr).
 *
 * @switchdev_port_attr_set: Set a port attribute (see switchdev_attr).
 *
 * @switchdev_port_obj_add: Add an object to port (see switchdev_obj).
 *
 * @switchdev_port_obj_del: Delete an object from port (see switchdev_obj).
 *
 * @switchdev_fib_ipv4_add: Called to add/modify IPv4 route to switch device.
 *
 * @switchdev_fib_ipv4_del: Called to delete IPv4 route from switch device.
 */
struct switchdev_ops {
	int	(*switchdev_port_attr_get)(struct net_device *dev,
					   struct switchdev_attr *attr);
	int	(*switchdev_port_attr_set)(struct net_device *dev,
					   struct switchdev_attr *attr);
	int	(*switchdev_port_obj_add)(struct net_device *dev,
					  struct switchdev_obj *obj);
	int	(*switchdev_port_obj_del)(struct net_device *dev,
					  struct switchdev_obj *obj);
	int	(*switchdev_fib_ipv4_add)(struct net_device *dev, __be32 dst,
					  int dst_len, struct fib_info *fi,
					  u8 tos, u8 type, u32 nlflags,
					  u32 tb_id);
	int	(*switchdev_fib_ipv4_del)(struct net_device *dev, __be32 dst,
					  int dst_len, struct fib_info *fi,
					  u8 tos, u8 type, u32 tb_id);
};

enum switchdev_notifier_type {
	SWITCHDEV_FDB_ADD = 1,
	SWITCHDEV_FDB_DEL,
};

struct switchdev_notifier_info {
	struct net_device *dev;
};

struct switchdev_notifier_fdb_info {
	struct switchdev_notifier_info info; /* must be first */
	const unsigned char *addr;
	u16 vid;
};

static inline struct net_device *
switchdev_notifier_info_to_dev(const struct switchdev_notifier_info *info)
{
	return info->dev;
}

#ifdef CONFIG_NET_SWITCHDEV

int switchdev_port_attr_get(struct net_device *dev,
			    struct switchdev_attr *attr);
int switchdev_port_attr_set(struct net_device *dev,
			    struct switchdev_attr *attr);
int switchdev_port_obj_add(struct net_device *dev, struct switchdev_obj *obj);
int switchdev_port_obj_del(struct net_device *dev, struct switchdev_obj *obj);
int register_switchdev_notifier(struct notifier_block *nb);
int unregister_switchdev_notifier(struct notifier_block *nb);
int call_switchdev_notifiers(unsigned long val, struct net_device *dev,
			     struct switchdev_notifier_info *info);
int switchdev_port_bridge_setlink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags);
int switchdev_port_bridge_dellink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags);
int ndo_dflt_switchdev_port_bridge_dellink(struct net_device *dev,
					   struct nlmsghdr *nlh, u16 flags);
int ndo_dflt_switchdev_port_bridge_setlink(struct net_device *dev,
					   struct nlmsghdr *nlh, u16 flags);
int switchdev_fib_ipv4_add(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 nlflags, u32 tb_id);
int switchdev_fib_ipv4_del(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 tb_id);
void switchdev_fib_ipv4_abort(struct fib_info *fi);

#else

static inline int switchdev_port_attr_get(struct net_device *dev,
					  struct switchdev_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_attr_set(struct net_device *dev,
					  struct switchdev_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_add(struct net_device *dev,
					 struct switchdev_obj *obj)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_del(struct net_device *dev,
					 struct switchdev_obj *obj)
{
	return -EOPNOTSUPP;
}

static inline int register_switchdev_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int unregister_switchdev_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int call_switchdev_notifiers(unsigned long val,
					   struct net_device *dev,
					   struct switchdev_notifier_info *info)
{
	return NOTIFY_DONE;
}

static inline int switchdev_port_bridge_setlink(struct net_device *dev,
						struct nlmsghdr *nlh,
						u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_bridge_dellink(struct net_device *dev,
						struct nlmsghdr *nlh,
						u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int ndo_dflt_switchdev_port_bridge_dellink(struct net_device *dev,
							 struct nlmsghdr *nlh,
							 u16 flags)
{
	return 0;
}

static inline int ndo_dflt_switchdev_port_bridge_setlink(struct net_device *dev,
							 struct nlmsghdr *nlh,
							 u16 flags)
{
	return 0;
}

static inline int switchdev_fib_ipv4_add(u32 dst, int dst_len,
					 struct fib_info *fi,
					 u8 tos, u8 type,
					 u32 nlflags, u32 tb_id)
{
	return 0;
}

static inline int switchdev_fib_ipv4_del(u32 dst, int dst_len,
					 struct fib_info *fi,
					 u8 tos, u8 type, u32 tb_id)
{
	return 0;
}

static inline void switchdev_fib_ipv4_abort(struct fib_info *fi)
{
}

#endif

#endif /* _LINUX_SWITCHDEV_H_ */

/*
 * include/net/devlink.h - Network physical device Netlink interface
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _NET_DEVLINK_H_
#define _NET_DEVLINK_H_

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <uapi/linux/devlink.h>

struct devlink_ops;

struct devlink {
	struct list_head list;
	struct list_head port_list;
	struct list_head sb_list;
	const struct devlink_ops *ops;
	struct device *dev;
	possible_net_t _net;
	char priv[0] __aligned(NETDEV_ALIGN);
};

struct devlink_port {
	struct list_head list;
	struct devlink *devlink;
	unsigned index;
	bool registered;
	enum devlink_port_type type;
	enum devlink_port_type desired_type;
	void *type_dev;
	bool split;
	u32 split_group;
};

struct devlink_sb_pool_info {
	enum devlink_sb_pool_type pool_type;
	u32 size;
	enum devlink_sb_threshold_type threshold_type;
};

struct devlink_ops {
	size_t priv_size;
	int (*port_type_set)(struct devlink_port *devlink_port,
			     enum devlink_port_type port_type);
	int (*port_split)(struct devlink *devlink, unsigned int port_index,
			  unsigned int count);
	int (*port_unsplit)(struct devlink *devlink, unsigned int port_index);
	int (*sb_pool_get)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index,
			   struct devlink_sb_pool_info *pool_info);
	int (*sb_pool_set)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index, u32 size,
			   enum devlink_sb_threshold_type threshold_type);
	int (*sb_port_pool_get)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 *p_threshold);
	int (*sb_port_pool_set)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 threshold);
	int (*sb_tc_pool_bind_get)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_pool_index, u32 *p_threshold);
	int (*sb_tc_pool_bind_set)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 pool_index, u32 threshold);
	int (*sb_occ_snapshot)(struct devlink *devlink,
			       unsigned int sb_index);
	int (*sb_occ_max_clear)(struct devlink *devlink,
				unsigned int sb_index);
	int (*sb_occ_port_pool_get)(struct devlink_port *devlink_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 *p_cur, u32 *p_max);
	int (*sb_occ_tc_port_bind_get)(struct devlink_port *devlink_port,
				       unsigned int sb_index,
				       u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u32 *p_cur, u32 *p_max);
};

static inline void *devlink_priv(struct devlink *devlink)
{
	BUG_ON(!devlink);
	return &devlink->priv;
}

static inline struct devlink *priv_to_devlink(void *priv)
{
	BUG_ON(!priv);
	return container_of(priv, struct devlink, priv);
}

struct ib_device;

#if IS_ENABLED(CONFIG_NET_DEVLINK)

struct devlink *devlink_alloc(const struct devlink_ops *ops, size_t priv_size);
int devlink_register(struct devlink *devlink, struct device *dev);
void devlink_unregister(struct devlink *devlink);
void devlink_free(struct devlink *devlink);
int devlink_port_register(struct devlink *devlink,
			  struct devlink_port *devlink_port,
			  unsigned int port_index);
void devlink_port_unregister(struct devlink_port *devlink_port);
void devlink_port_type_eth_set(struct devlink_port *devlink_port,
			       struct net_device *netdev);
void devlink_port_type_ib_set(struct devlink_port *devlink_port,
			      struct ib_device *ibdev);
void devlink_port_type_clear(struct devlink_port *devlink_port);
void devlink_port_split_set(struct devlink_port *devlink_port,
			    u32 split_group);
int devlink_sb_register(struct devlink *devlink, unsigned int sb_index,
			u32 size, u16 ingress_pools_count,
			u16 egress_pools_count, u16 ingress_tc_count,
			u16 egress_tc_count);
void devlink_sb_unregister(struct devlink *devlink, unsigned int sb_index);

#else

static inline struct devlink *devlink_alloc(const struct devlink_ops *ops,
					    size_t priv_size)
{
	return kzalloc(sizeof(struct devlink) + priv_size, GFP_KERNEL);
}

static inline int devlink_register(struct devlink *devlink, struct device *dev)
{
	return 0;
}

static inline void devlink_unregister(struct devlink *devlink)
{
}

static inline void devlink_free(struct devlink *devlink)
{
	kfree(devlink);
}

static inline int devlink_port_register(struct devlink *devlink,
					struct devlink_port *devlink_port,
					unsigned int port_index)
{
	return 0;
}

static inline void devlink_port_unregister(struct devlink_port *devlink_port)
{
}

static inline void devlink_port_type_eth_set(struct devlink_port *devlink_port,
					     struct net_device *netdev)
{
}

static inline void devlink_port_type_ib_set(struct devlink_port *devlink_port,
					    struct ib_device *ibdev)
{
}

static inline void devlink_port_type_clear(struct devlink_port *devlink_port)
{
}

static inline void devlink_port_split_set(struct devlink_port *devlink_port,
					  u32 split_group)
{
}

static inline int devlink_sb_register(struct devlink *devlink,
				      unsigned int sb_index, u32 size,
				      u16 ingress_pools_count,
				      u16 egress_pools_count,
				      u16 ingress_tc_count,
				      u16 egress_tc_count)
{
	return 0;
}

static inline void devlink_sb_unregister(struct devlink *devlink,
					 unsigned int sb_index)
{
}

#endif

#endif /* _NET_DEVLINK_H_ */

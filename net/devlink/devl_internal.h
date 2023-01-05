/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <net/devlink.h>
#include <net/net_namespace.h>

#define DEVLINK_REGISTERED XA_MARK_1
#define DEVLINK_UNREGISTERING XA_MARK_2

#define DEVLINK_RELOAD_STATS_ARRAY_SIZE \
	(__DEVLINK_RELOAD_LIMIT_MAX * __DEVLINK_RELOAD_ACTION_MAX)

struct devlink_dev_stats {
	u32 reload_stats[DEVLINK_RELOAD_STATS_ARRAY_SIZE];
	u32 remote_reload_stats[DEVLINK_RELOAD_STATS_ARRAY_SIZE];
};

struct devlink {
	u32 index;
	struct xarray ports;
	struct list_head rate_list;
	struct list_head sb_list;
	struct list_head dpipe_table_list;
	struct list_head resource_list;
	struct list_head param_list;
	struct list_head region_list;
	struct list_head reporter_list;
	struct mutex reporters_lock; /* protects reporter_list */
	struct devlink_dpipe_headers *dpipe_headers;
	struct list_head trap_list;
	struct list_head trap_group_list;
	struct list_head trap_policer_list;
	struct list_head linecard_list;
	struct mutex linecards_lock; /* protects linecard_list */
	const struct devlink_ops *ops;
	u64 features;
	struct xarray snapshot_ids;
	struct devlink_dev_stats stats;
	struct device *dev;
	possible_net_t _net;
	/* Serializes access to devlink instance specific objects such as
	 * port, sb, dpipe, resource, params, region, traps and more.
	 */
	struct mutex lock;
	struct lock_class_key lock_key;
	u8 reload_failed:1;
	refcount_t refcount;
	struct completion comp;
	struct rcu_head rcu;
	struct notifier_block netdevice_nb;
	char priv[] __aligned(NETDEV_ALIGN);
};

extern struct xarray devlinks;
extern struct genl_family devlink_nl_family;

/* devlink instances are open to the access from the user space after
 * devlink_register() call. Such logical barrier allows us to have certain
 * expectations related to locking.
 *
 * Before *_register() - we are in initialization stage and no parallel
 * access possible to the devlink instance. All drivers perform that phase
 * by implicitly holding device_lock.
 *
 * After *_register() - users and driver can access devlink instance at
 * the same time.
 */
#define ASSERT_DEVLINK_REGISTERED(d)                                           \
	WARN_ON_ONCE(!xa_get_mark(&devlinks, (d)->index, DEVLINK_REGISTERED))
#define ASSERT_DEVLINK_NOT_REGISTERED(d)                                       \
	WARN_ON_ONCE(xa_get_mark(&devlinks, (d)->index, DEVLINK_REGISTERED))

/* Iterate over devlink pointers which were possible to get reference to.
 * devlink_put() needs to be called for each iterated devlink pointer
 * in loop body in order to release the reference.
 */
#define devlinks_xa_for_each_get(net, index, devlink, filter)		\
	for (index = 0,							\
	     devlink = devlinks_xa_find_get_first(net, &index, filter);	\
	     devlink; devlink = devlinks_xa_find_get_next(net, &index, filter))

#define devlinks_xa_for_each_registered_get(net, index, devlink)	\
	devlinks_xa_for_each_get(net, index, devlink, DEVLINK_REGISTERED)

struct devlink *
devlinks_xa_find_get_first(struct net *net, unsigned long *indexp,
			   xa_mark_t filter);
struct devlink *
devlinks_xa_find_get_next(struct net *net, unsigned long *indexp,
			  xa_mark_t filter);

/* Netlink */
void devlink_notify_unregister(struct devlink *devlink);
void devlink_notify_register(struct devlink *devlink);

/* Ports */
int devlink_port_netdevice_event(struct notifier_block *nb,
				 unsigned long event, void *ptr);

/* Reload */
bool devlink_reload_actions_valid(const struct devlink_ops *ops);
int devlink_reload(struct devlink *devlink, struct net *dest_net,
		   enum devlink_reload_action action,
		   enum devlink_reload_limit limit,
		   u32 *actions_performed, struct netlink_ext_ack *extack);

static inline bool devlink_reload_supported(const struct devlink_ops *ops)
{
	return ops->reload_down && ops->reload_up;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>
#include <net/devlink.h>
#include <net/net_namespace.h>
#include <net/rtnetlink.h>
#include <rdma/ib_verbs.h>

#include "netlink_gen.h"

struct devlink_rel;

#define DEVLINK_REGISTERED XA_MARK_1

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
	struct xarray params;
	struct list_head region_list;
	struct list_head reporter_list;
	struct devlink_dpipe_headers *dpipe_headers;
	struct list_head trap_list;
	struct list_head trap_group_list;
	struct list_head trap_policer_list;
	struct list_head linecard_list;
	const struct devlink_ops *ops;
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
	struct rcu_work rwork;
	struct devlink_rel *rel;
	struct xarray nested_rels;
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
#define devlinks_xa_for_each_registered_get(net, index, devlink)	\
	for (index = 0; (devlink = devlinks_xa_find_get(net, &index)); index++)

struct devlink *devlinks_xa_find_get(struct net *net, unsigned long *indexp);

static inline bool __devl_is_registered(struct devlink *devlink)
{
	return xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
}

static inline bool devl_is_registered(struct devlink *devlink)
{
	devl_assert_locked(devlink);
	return __devl_is_registered(devlink);
}

static inline void devl_dev_lock(struct devlink *devlink, bool dev_lock)
{
	if (dev_lock)
		device_lock(devlink->dev);
	devl_lock(devlink);
}

static inline void devl_dev_unlock(struct devlink *devlink, bool dev_lock)
{
	devl_unlock(devlink);
	if (dev_lock)
		device_unlock(devlink->dev);
}

typedef void devlink_rel_notify_cb_t(struct devlink *devlink, u32 obj_index);
typedef void devlink_rel_cleanup_cb_t(struct devlink *devlink, u32 obj_index,
				      u32 rel_index);

void devlink_rel_nested_in_clear(u32 rel_index);
int devlink_rel_nested_in_add(u32 *rel_index, u32 devlink_index,
			      u32 obj_index, devlink_rel_notify_cb_t *notify_cb,
			      devlink_rel_cleanup_cb_t *cleanup_cb,
			      struct devlink *devlink);
void devlink_rel_nested_in_notify(struct devlink *devlink);
int devlink_rel_devlink_handle_put(struct sk_buff *msg, struct devlink *devlink,
				   u32 rel_index, int attrtype,
				   bool *msg_updated);

/* Netlink */
enum devlink_multicast_groups {
	DEVLINK_MCGRP_CONFIG,
};

/* state held across netlink dumps */
struct devlink_nl_dump_state {
	unsigned long instance;
	int idx;
	union {
		/* DEVLINK_CMD_REGION_READ */
		struct {
			u64 start_offset;
		};
		/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET */
		struct {
			u64 dump_ts;
		};
	};
};

typedef int devlink_nl_dump_one_func_t(struct sk_buff *msg,
				       struct devlink *devlink,
				       struct netlink_callback *cb,
				       int flags);

struct devlink *
devlink_get_from_attrs_lock(struct net *net, struct nlattr **attrs,
			    bool dev_lock);

int devlink_nl_dumpit(struct sk_buff *msg, struct netlink_callback *cb,
		      devlink_nl_dump_one_func_t *dump_one);

static inline struct devlink_nl_dump_state *
devlink_dump_state(struct netlink_callback *cb)
{
	NL_ASSERT_DUMP_CTX_FITS(struct devlink_nl_dump_state);

	return (struct devlink_nl_dump_state *)cb->ctx;
}

static inline int
devlink_nl_put_handle(struct sk_buff *msg, struct devlink *devlink)
{
	if (nla_put_string(msg, DEVLINK_ATTR_BUS_NAME, devlink->dev->bus->name))
		return -EMSGSIZE;
	if (nla_put_string(msg, DEVLINK_ATTR_DEV_NAME, dev_name(devlink->dev)))
		return -EMSGSIZE;
	return 0;
}

int devlink_nl_put_nested_handle(struct sk_buff *msg, struct net *net,
				 struct devlink *devlink, int attrtype);
int devlink_nl_msg_reply_and_new(struct sk_buff **msg, struct genl_info *info);

static inline bool devlink_nl_notify_need(struct devlink *devlink)
{
	return genl_has_listeners(&devlink_nl_family, devlink_net(devlink),
				  DEVLINK_MCGRP_CONFIG);
}

struct devlink_obj_desc {
	struct rcu_head rcu;
	const char *bus_name;
	const char *dev_name;
	unsigned int port_index;
	bool port_index_valid;
	long data[];
};

static inline void devlink_nl_obj_desc_init(struct devlink_obj_desc *desc,
					    struct devlink *devlink)
{
	memset(desc, 0, sizeof(*desc));
	desc->bus_name = devlink->dev->bus->name;
	desc->dev_name = dev_name(devlink->dev);
}

static inline void devlink_nl_obj_desc_port_set(struct devlink_obj_desc *desc,
						struct devlink_port *devlink_port)
{
	desc->port_index = devlink_port->index;
	desc->port_index_valid = true;
}

int devlink_nl_notify_filter(struct sock *dsk, struct sk_buff *skb, void *data);

static inline void devlink_nl_notify_send_desc(struct devlink *devlink,
					       struct sk_buff *msg,
					       struct devlink_obj_desc *desc)
{
	genlmsg_multicast_netns_filtered(&devlink_nl_family,
					 devlink_net(devlink),
					 msg, 0, DEVLINK_MCGRP_CONFIG,
					 GFP_KERNEL,
					 devlink_nl_notify_filter, desc);
}

static inline void devlink_nl_notify_send(struct devlink *devlink,
					  struct sk_buff *msg)
{
	struct devlink_obj_desc desc;

	devlink_nl_obj_desc_init(&desc, devlink);
	devlink_nl_notify_send_desc(devlink, msg, &desc);
}

/* Notify */
void devlink_notify_register(struct devlink *devlink);
void devlink_notify_unregister(struct devlink *devlink);
void devlink_ports_notify_register(struct devlink *devlink);
void devlink_ports_notify_unregister(struct devlink *devlink);
void devlink_params_notify_register(struct devlink *devlink);
void devlink_params_notify_unregister(struct devlink *devlink);
void devlink_regions_notify_register(struct devlink *devlink);
void devlink_regions_notify_unregister(struct devlink *devlink);
void devlink_trap_policers_notify_register(struct devlink *devlink);
void devlink_trap_policers_notify_unregister(struct devlink *devlink);
void devlink_trap_groups_notify_register(struct devlink *devlink);
void devlink_trap_groups_notify_unregister(struct devlink *devlink);
void devlink_traps_notify_register(struct devlink *devlink);
void devlink_traps_notify_unregister(struct devlink *devlink);
void devlink_rates_notify_register(struct devlink *devlink);
void devlink_rates_notify_unregister(struct devlink *devlink);
void devlink_linecards_notify_register(struct devlink *devlink);
void devlink_linecards_notify_unregister(struct devlink *devlink);

/* Ports */
#define ASSERT_DEVLINK_PORT_INITIALIZED(devlink_port)				\
	WARN_ON_ONCE(!(devlink_port)->initialized)

struct devlink_port *devlink_port_get_by_index(struct devlink *devlink,
					       unsigned int port_index);
int devlink_port_netdevice_event(struct notifier_block *nb,
				 unsigned long event, void *ptr);
struct devlink_port *
devlink_port_get_from_info(struct devlink *devlink, struct genl_info *info);
struct devlink_port *devlink_port_get_from_attrs(struct devlink *devlink,
						 struct nlattr **attrs);

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

/* Params */
void devlink_params_driverinit_load_new(struct devlink *devlink);

/* Resources */
struct devlink_resource;
int devlink_resources_validate(struct devlink *devlink,
			       struct devlink_resource *resource,
			       struct genl_info *info);

/* Rates */
int devlink_rate_nodes_check(struct devlink *devlink, u16 mode,
			     struct netlink_ext_ack *extack);

/* Linecards */
unsigned int devlink_linecard_index(struct devlink_linecard *linecard);

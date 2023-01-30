/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>
#include <net/devlink.h>
#include <net/net_namespace.h>

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
	struct list_head param_list;
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
#define devlinks_xa_for_each_registered_get(net, index, devlink)	\
	for (index = 0; (devlink = devlinks_xa_find_get(net, &index)); index++)

struct devlink *devlinks_xa_find_get(struct net *net, unsigned long *indexp);

static inline bool devl_is_registered(struct devlink *devlink)
{
	devl_assert_locked(devlink);
	return xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
}

/* Netlink */
#define DEVLINK_NL_FLAG_NEED_PORT		BIT(0)
#define DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT	BIT(1)
#define DEVLINK_NL_FLAG_NEED_RATE		BIT(2)
#define DEVLINK_NL_FLAG_NEED_RATE_NODE		BIT(3)
#define DEVLINK_NL_FLAG_NEED_LINECARD		BIT(4)

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

struct devlink_gen_cmd {
	int (*dump_one)(struct sk_buff *msg, struct devlink *devlink,
			struct netlink_callback *cb);
};

extern const struct genl_small_ops devlink_nl_ops[56];

struct devlink *
devlink_get_from_attrs_lock(struct net *net, struct nlattr **attrs);

void devlink_notify_unregister(struct devlink *devlink);
void devlink_notify_register(struct devlink *devlink);

int devlink_nl_instance_iter_dump(struct sk_buff *msg,
				  struct netlink_callback *cb);

static inline struct devlink_nl_dump_state *
devlink_dump_state(struct netlink_callback *cb)
{
	NL_ASSERT_DUMP_CTX_FITS(struct devlink_nl_dump_state);

	return (struct devlink_nl_dump_state *)cb->ctx;
}

/* gen cmds */
extern const struct devlink_gen_cmd devl_gen_inst;
extern const struct devlink_gen_cmd devl_gen_port;
extern const struct devlink_gen_cmd devl_gen_sb;
extern const struct devlink_gen_cmd devl_gen_sb_pool;
extern const struct devlink_gen_cmd devl_gen_sb_port_pool;
extern const struct devlink_gen_cmd devl_gen_sb_tc_pool_bind;
extern const struct devlink_gen_cmd devl_gen_selftests;
extern const struct devlink_gen_cmd devl_gen_param;
extern const struct devlink_gen_cmd devl_gen_region;
extern const struct devlink_gen_cmd devl_gen_info;
extern const struct devlink_gen_cmd devl_gen_health_reporter;
extern const struct devlink_gen_cmd devl_gen_trap;
extern const struct devlink_gen_cmd devl_gen_trap_group;
extern const struct devlink_gen_cmd devl_gen_trap_policer;
extern const struct devlink_gen_cmd devl_gen_linecard;

/* Ports */
int devlink_port_netdevice_event(struct notifier_block *nb,
				 unsigned long event, void *ptr);

struct devlink_port *
devlink_port_get_from_info(struct devlink *devlink, struct genl_info *info);

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

/* Line cards */
struct devlink_linecard;

struct devlink_linecard *
devlink_linecard_get_from_info(struct devlink *devlink, struct genl_info *info);

/* Rates */
extern const struct devlink_gen_cmd devl_gen_rate_get;

struct devlink_rate *
devlink_rate_get_from_info(struct devlink *devlink, struct genl_info *info);
struct devlink_rate *
devlink_rate_node_get_from_info(struct devlink *devlink,
				struct genl_info *info);

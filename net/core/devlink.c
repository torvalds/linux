// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/devlink.c - Network physical/parent device Netlink interface
 *
 * Heavily inspired by net/wireless/
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/u64_stats_sync.h>
#include <linux/timekeeping.h>
#include <rdma/ib_verbs.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/devlink.h>
#define CREATE_TRACE_POINTS
#include <trace/events/devlink.h>

static struct devlink_dpipe_field devlink_dpipe_fields_ethernet[] = {
	{
		.name = "destination mac",
		.id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC,
		.bitwidth = 48,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ethernet = {
	.name = "ethernet",
	.id = DEVLINK_DPIPE_HEADER_ETHERNET,
	.fields = devlink_dpipe_fields_ethernet,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ethernet),
	.global = true,
};
EXPORT_SYMBOL(devlink_dpipe_header_ethernet);

static struct devlink_dpipe_field devlink_dpipe_fields_ipv4[] = {
	{
		.name = "destination ip",
		.id = DEVLINK_DPIPE_FIELD_IPV4_DST_IP,
		.bitwidth = 32,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ipv4 = {
	.name = "ipv4",
	.id = DEVLINK_DPIPE_HEADER_IPV4,
	.fields = devlink_dpipe_fields_ipv4,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ipv4),
	.global = true,
};
EXPORT_SYMBOL(devlink_dpipe_header_ipv4);

static struct devlink_dpipe_field devlink_dpipe_fields_ipv6[] = {
	{
		.name = "destination ip",
		.id = DEVLINK_DPIPE_FIELD_IPV6_DST_IP,
		.bitwidth = 128,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ipv6 = {
	.name = "ipv6",
	.id = DEVLINK_DPIPE_HEADER_IPV6,
	.fields = devlink_dpipe_fields_ipv6,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ipv6),
	.global = true,
};
EXPORT_SYMBOL(devlink_dpipe_header_ipv6);

EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwmsg);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwerr);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_trap_report);

static const struct nla_policy devlink_function_nl_policy[DEVLINK_PORT_FUNCTION_ATTR_MAX + 1] = {
	[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR] = { .type = NLA_BINARY },
	[DEVLINK_PORT_FN_ATTR_STATE] =
		NLA_POLICY_RANGE(NLA_U8, DEVLINK_PORT_FN_STATE_INACTIVE,
				 DEVLINK_PORT_FN_STATE_ACTIVE),
};

static LIST_HEAD(devlink_list);

/* devlink_mutex
 *
 * An overall lock guarding every operation coming from userspace.
 * It also guards devlink devices list and it is taken when
 * driver registers/unregisters it.
 */
static DEFINE_MUTEX(devlink_mutex);

struct net *devlink_net(const struct devlink *devlink)
{
	return read_pnet(&devlink->_net);
}
EXPORT_SYMBOL_GPL(devlink_net);

static void __devlink_net_set(struct devlink *devlink, struct net *net)
{
	write_pnet(&devlink->_net, net);
}

void devlink_net_set(struct devlink *devlink, struct net *net)
{
	if (WARN_ON(devlink->registered))
		return;
	__devlink_net_set(devlink, net);
}
EXPORT_SYMBOL_GPL(devlink_net_set);

static struct devlink *devlink_get_from_attrs(struct net *net,
					      struct nlattr **attrs)
{
	struct devlink *devlink;
	char *busname;
	char *devname;

	if (!attrs[DEVLINK_ATTR_BUS_NAME] || !attrs[DEVLINK_ATTR_DEV_NAME])
		return ERR_PTR(-EINVAL);

	busname = nla_data(attrs[DEVLINK_ATTR_BUS_NAME]);
	devname = nla_data(attrs[DEVLINK_ATTR_DEV_NAME]);

	lockdep_assert_held(&devlink_mutex);

	list_for_each_entry(devlink, &devlink_list, list) {
		if (strcmp(devlink->dev->bus->name, busname) == 0 &&
		    strcmp(dev_name(devlink->dev), devname) == 0 &&
		    net_eq(devlink_net(devlink), net))
			return devlink;
	}

	return ERR_PTR(-ENODEV);
}

static struct devlink *devlink_get_from_info(struct genl_info *info)
{
	return devlink_get_from_attrs(genl_info_net(info), info->attrs);
}

static struct devlink_port *devlink_port_get_by_index(struct devlink *devlink,
						      unsigned int port_index)
{
	struct devlink_port *devlink_port;

	list_for_each_entry(devlink_port, &devlink->port_list, list) {
		if (devlink_port->index == port_index)
			return devlink_port;
	}
	return NULL;
}

static bool devlink_port_index_exists(struct devlink *devlink,
				      unsigned int port_index)
{
	return devlink_port_get_by_index(devlink, port_index);
}

static struct devlink_port *devlink_port_get_from_attrs(struct devlink *devlink,
							struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_PORT_INDEX]) {
		u32 port_index = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);
		struct devlink_port *devlink_port;

		devlink_port = devlink_port_get_by_index(devlink, port_index);
		if (!devlink_port)
			return ERR_PTR(-ENODEV);
		return devlink_port;
	}
	return ERR_PTR(-EINVAL);
}

static struct devlink_port *devlink_port_get_from_info(struct devlink *devlink,
						       struct genl_info *info)
{
	return devlink_port_get_from_attrs(devlink, info->attrs);
}

struct devlink_sb {
	struct list_head list;
	unsigned int index;
	u32 size;
	u16 ingress_pools_count;
	u16 egress_pools_count;
	u16 ingress_tc_count;
	u16 egress_tc_count;
};

static u16 devlink_sb_pool_count(struct devlink_sb *devlink_sb)
{
	return devlink_sb->ingress_pools_count + devlink_sb->egress_pools_count;
}

static struct devlink_sb *devlink_sb_get_by_index(struct devlink *devlink,
						  unsigned int sb_index)
{
	struct devlink_sb *devlink_sb;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		if (devlink_sb->index == sb_index)
			return devlink_sb;
	}
	return NULL;
}

static bool devlink_sb_index_exists(struct devlink *devlink,
				    unsigned int sb_index)
{
	return devlink_sb_get_by_index(devlink, sb_index);
}

static struct devlink_sb *devlink_sb_get_from_attrs(struct devlink *devlink,
						    struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_SB_INDEX]) {
		u32 sb_index = nla_get_u32(attrs[DEVLINK_ATTR_SB_INDEX]);
		struct devlink_sb *devlink_sb;

		devlink_sb = devlink_sb_get_by_index(devlink, sb_index);
		if (!devlink_sb)
			return ERR_PTR(-ENODEV);
		return devlink_sb;
	}
	return ERR_PTR(-EINVAL);
}

static struct devlink_sb *devlink_sb_get_from_info(struct devlink *devlink,
						   struct genl_info *info)
{
	return devlink_sb_get_from_attrs(devlink, info->attrs);
}

static int devlink_sb_pool_index_get_from_attrs(struct devlink_sb *devlink_sb,
						struct nlattr **attrs,
						u16 *p_pool_index)
{
	u16 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_INDEX])
		return -EINVAL;

	val = nla_get_u16(attrs[DEVLINK_ATTR_SB_POOL_INDEX]);
	if (val >= devlink_sb_pool_count(devlink_sb))
		return -EINVAL;
	*p_pool_index = val;
	return 0;
}

static int devlink_sb_pool_index_get_from_info(struct devlink_sb *devlink_sb,
					       struct genl_info *info,
					       u16 *p_pool_index)
{
	return devlink_sb_pool_index_get_from_attrs(devlink_sb, info->attrs,
						    p_pool_index);
}

static int
devlink_sb_pool_type_get_from_attrs(struct nlattr **attrs,
				    enum devlink_sb_pool_type *p_pool_type)
{
	u8 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_TYPE])
		return -EINVAL;

	val = nla_get_u8(attrs[DEVLINK_ATTR_SB_POOL_TYPE]);
	if (val != DEVLINK_SB_POOL_TYPE_INGRESS &&
	    val != DEVLINK_SB_POOL_TYPE_EGRESS)
		return -EINVAL;
	*p_pool_type = val;
	return 0;
}

static int
devlink_sb_pool_type_get_from_info(struct genl_info *info,
				   enum devlink_sb_pool_type *p_pool_type)
{
	return devlink_sb_pool_type_get_from_attrs(info->attrs, p_pool_type);
}

static int
devlink_sb_th_type_get_from_attrs(struct nlattr **attrs,
				  enum devlink_sb_threshold_type *p_th_type)
{
	u8 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE])
		return -EINVAL;

	val = nla_get_u8(attrs[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE]);
	if (val != DEVLINK_SB_THRESHOLD_TYPE_STATIC &&
	    val != DEVLINK_SB_THRESHOLD_TYPE_DYNAMIC)
		return -EINVAL;
	*p_th_type = val;
	return 0;
}

static int
devlink_sb_th_type_get_from_info(struct genl_info *info,
				 enum devlink_sb_threshold_type *p_th_type)
{
	return devlink_sb_th_type_get_from_attrs(info->attrs, p_th_type);
}

static int
devlink_sb_tc_index_get_from_attrs(struct devlink_sb *devlink_sb,
				   struct nlattr **attrs,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_tc_index)
{
	u16 val;

	if (!attrs[DEVLINK_ATTR_SB_TC_INDEX])
		return -EINVAL;

	val = nla_get_u16(attrs[DEVLINK_ATTR_SB_TC_INDEX]);
	if (pool_type == DEVLINK_SB_POOL_TYPE_INGRESS &&
	    val >= devlink_sb->ingress_tc_count)
		return -EINVAL;
	if (pool_type == DEVLINK_SB_POOL_TYPE_EGRESS &&
	    val >= devlink_sb->egress_tc_count)
		return -EINVAL;
	*p_tc_index = val;
	return 0;
}

static int
devlink_sb_tc_index_get_from_info(struct devlink_sb *devlink_sb,
				  struct genl_info *info,
				  enum devlink_sb_pool_type pool_type,
				  u16 *p_tc_index)
{
	return devlink_sb_tc_index_get_from_attrs(devlink_sb, info->attrs,
						  pool_type, p_tc_index);
}

struct devlink_region {
	struct devlink *devlink;
	struct devlink_port *port;
	struct list_head list;
	union {
		const struct devlink_region_ops *ops;
		const struct devlink_port_region_ops *port_ops;
	};
	struct list_head snapshot_list;
	u32 max_snapshots;
	u32 cur_snapshots;
	u64 size;
};

struct devlink_snapshot {
	struct list_head list;
	struct devlink_region *region;
	u8 *data;
	u32 id;
};

static struct devlink_region *
devlink_region_get_by_name(struct devlink *devlink, const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &devlink->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_region *
devlink_port_region_get_by_name(struct devlink_port *port,
				const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &port->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_snapshot *
devlink_region_snapshot_get_by_id(struct devlink_region *region, u32 id)
{
	struct devlink_snapshot *snapshot;

	list_for_each_entry(snapshot, &region->snapshot_list, list)
		if (snapshot->id == id)
			return snapshot;

	return NULL;
}

#define DEVLINK_NL_FLAG_NEED_PORT		BIT(0)
#define DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT	BIT(1)

/* The per devlink instance lock is taken by default in the pre-doit
 * operation, yet several commands do not require this. The global
 * devlink lock is taken and protects from disruption by user-calls.
 */
#define DEVLINK_NL_FLAG_NO_LOCK			BIT(2)

static int devlink_nl_pre_doit(const struct genl_ops *ops,
			       struct sk_buff *skb, struct genl_info *info)
{
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	int err;

	mutex_lock(&devlink_mutex);
	devlink = devlink_get_from_info(info);
	if (IS_ERR(devlink)) {
		mutex_unlock(&devlink_mutex);
		return PTR_ERR(devlink);
	}
	if (~ops->internal_flags & DEVLINK_NL_FLAG_NO_LOCK)
		mutex_lock(&devlink->lock);
	info->user_ptr[0] = devlink;
	if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (IS_ERR(devlink_port)) {
			err = PTR_ERR(devlink_port);
			goto unlock;
		}
		info->user_ptr[1] = devlink_port;
	} else if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (!IS_ERR(devlink_port))
			info->user_ptr[1] = devlink_port;
	}
	return 0;

unlock:
	if (~ops->internal_flags & DEVLINK_NL_FLAG_NO_LOCK)
		mutex_unlock(&devlink->lock);
	mutex_unlock(&devlink_mutex);
	return err;
}

static void devlink_nl_post_doit(const struct genl_ops *ops,
				 struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink;

	devlink = info->user_ptr[0];
	if (~ops->internal_flags & DEVLINK_NL_FLAG_NO_LOCK)
		mutex_unlock(&devlink->lock);
	mutex_unlock(&devlink_mutex);
}

static struct genl_family devlink_nl_family;

enum devlink_multicast_groups {
	DEVLINK_MCGRP_CONFIG,
};

static const struct genl_multicast_group devlink_nl_mcgrps[] = {
	[DEVLINK_MCGRP_CONFIG] = { .name = DEVLINK_GENL_MCGRP_CONFIG_NAME },
};

static int devlink_nl_put_handle(struct sk_buff *msg, struct devlink *devlink)
{
	if (nla_put_string(msg, DEVLINK_ATTR_BUS_NAME, devlink->dev->bus->name))
		return -EMSGSIZE;
	if (nla_put_string(msg, DEVLINK_ATTR_DEV_NAME, dev_name(devlink->dev)))
		return -EMSGSIZE;
	return 0;
}

struct devlink_reload_combination {
	enum devlink_reload_action action;
	enum devlink_reload_limit limit;
};

static const struct devlink_reload_combination devlink_reload_invalid_combinations[] = {
	{
		/* can't reinitialize driver with no down time */
		.action = DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
		.limit = DEVLINK_RELOAD_LIMIT_NO_RESET,
	},
};

static bool
devlink_reload_combination_is_invalid(enum devlink_reload_action action,
				      enum devlink_reload_limit limit)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devlink_reload_invalid_combinations); i++)
		if (devlink_reload_invalid_combinations[i].action == action &&
		    devlink_reload_invalid_combinations[i].limit == limit)
			return true;
	return false;
}

static bool
devlink_reload_action_is_supported(struct devlink *devlink, enum devlink_reload_action action)
{
	return test_bit(action, &devlink->ops->reload_actions);
}

static bool
devlink_reload_limit_is_supported(struct devlink *devlink, enum devlink_reload_limit limit)
{
	return test_bit(limit, &devlink->ops->reload_limits);
}

static int devlink_reload_stat_put(struct sk_buff *msg,
				   enum devlink_reload_limit limit, u32 value)
{
	struct nlattr *reload_stats_entry;

	reload_stats_entry = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_STATS_ENTRY);
	if (!reload_stats_entry)
		return -EMSGSIZE;

	if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_STATS_LIMIT, limit) ||
	    nla_put_u32(msg, DEVLINK_ATTR_RELOAD_STATS_VALUE, value))
		goto nla_put_failure;
	nla_nest_end(msg, reload_stats_entry);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, reload_stats_entry);
	return -EMSGSIZE;
}

static int devlink_reload_stats_put(struct sk_buff *msg, struct devlink *devlink, bool is_remote)
{
	struct nlattr *reload_stats_attr, *act_info, *act_stats;
	int i, j, stat_idx;
	u32 value;

	if (!is_remote)
		reload_stats_attr = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_STATS);
	else
		reload_stats_attr = nla_nest_start(msg, DEVLINK_ATTR_REMOTE_RELOAD_STATS);

	if (!reload_stats_attr)
		return -EMSGSIZE;

	for (i = 0; i <= DEVLINK_RELOAD_ACTION_MAX; i++) {
		if ((!is_remote &&
		     !devlink_reload_action_is_supported(devlink, i)) ||
		    i == DEVLINK_RELOAD_ACTION_UNSPEC)
			continue;
		act_info = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_ACTION_INFO);
		if (!act_info)
			goto nla_put_failure;

		if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_ACTION, i))
			goto action_info_nest_cancel;
		act_stats = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_ACTION_STATS);
		if (!act_stats)
			goto action_info_nest_cancel;

		for (j = 0; j <= DEVLINK_RELOAD_LIMIT_MAX; j++) {
			/* Remote stats are shown even if not locally supported.
			 * Stats of actions with unspecified limit are shown
			 * though drivers don't need to register unspecified
			 * limit.
			 */
			if ((!is_remote && j != DEVLINK_RELOAD_LIMIT_UNSPEC &&
			     !devlink_reload_limit_is_supported(devlink, j)) ||
			    devlink_reload_combination_is_invalid(i, j))
				continue;

			stat_idx = j * __DEVLINK_RELOAD_ACTION_MAX + i;
			if (!is_remote)
				value = devlink->stats.reload_stats[stat_idx];
			else
				value = devlink->stats.remote_reload_stats[stat_idx];
			if (devlink_reload_stat_put(msg, j, value))
				goto action_stats_nest_cancel;
		}
		nla_nest_end(msg, act_stats);
		nla_nest_end(msg, act_info);
	}
	nla_nest_end(msg, reload_stats_attr);
	return 0;

action_stats_nest_cancel:
	nla_nest_cancel(msg, act_stats);
action_info_nest_cancel:
	nla_nest_cancel(msg, act_info);
nla_put_failure:
	nla_nest_cancel(msg, reload_stats_attr);
	return -EMSGSIZE;
}

static int devlink_nl_fill(struct sk_buff *msg, struct devlink *devlink,
			   enum devlink_command cmd, u32 portid,
			   u32 seq, int flags)
{
	struct nlattr *dev_stats;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_FAILED, devlink->reload_failed))
		goto nla_put_failure;

	dev_stats = nla_nest_start(msg, DEVLINK_ATTR_DEV_STATS);
	if (!dev_stats)
		goto nla_put_failure;

	if (devlink_reload_stats_put(msg, devlink, false))
		goto dev_stats_nest_cancel;
	if (devlink_reload_stats_put(msg, devlink, true))
		goto dev_stats_nest_cancel;

	nla_nest_end(msg, dev_stats);
	genlmsg_end(msg, hdr);
	return 0;

dev_stats_nest_cancel:
	nla_nest_cancel(msg, dev_stats);
nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_notify(struct devlink *devlink, enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_NEW && cmd != DEVLINK_CMD_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_fill(msg, devlink, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int devlink_nl_port_attrs_put(struct sk_buff *msg,
				     struct devlink_port *devlink_port)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;

	if (!devlink_port->attrs_set)
		return 0;
	if (attrs->lanes) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_LANES, attrs->lanes))
			return -EMSGSIZE;
	}
	if (nla_put_u8(msg, DEVLINK_ATTR_PORT_SPLITTABLE, attrs->splittable))
		return -EMSGSIZE;
	if (nla_put_u16(msg, DEVLINK_ATTR_PORT_FLAVOUR, attrs->flavour))
		return -EMSGSIZE;
	switch (devlink_port->attrs.flavour) {
	case DEVLINK_PORT_FLAVOUR_PCI_PF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_pf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER, attrs->pci_pf.pf))
			return -EMSGSIZE;
		if (nla_put_u8(msg, DEVLINK_ATTR_PORT_EXTERNAL, attrs->pci_pf.external))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_VF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_vf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER, attrs->pci_vf.pf) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_VF_NUMBER, attrs->pci_vf.vf))
			return -EMSGSIZE;
		if (nla_put_u8(msg, DEVLINK_ATTR_PORT_EXTERNAL, attrs->pci_vf.external))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_SF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_sf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER,
				attrs->pci_sf.pf) ||
		    nla_put_u32(msg, DEVLINK_ATTR_PORT_PCI_SF_NUMBER,
				attrs->pci_sf.sf))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PHYSICAL:
	case DEVLINK_PORT_FLAVOUR_CPU:
	case DEVLINK_PORT_FLAVOUR_DSA:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_NUMBER,
				attrs->phys.port_number))
			return -EMSGSIZE;
		if (!attrs->split)
			return 0;
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_SPLIT_GROUP,
				attrs->phys.port_number))
			return -EMSGSIZE;
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_SPLIT_SUBPORT_NUMBER,
				attrs->phys.split_subport_number))
			return -EMSGSIZE;
		break;
	default:
		break;
	}
	return 0;
}

static int
devlink_port_fn_hw_addr_fill(struct devlink *devlink, const struct devlink_ops *ops,
			     struct devlink_port *port, struct sk_buff *msg,
			     struct netlink_ext_ack *extack, bool *msg_updated)
{
	u8 hw_addr[MAX_ADDR_LEN];
	int hw_addr_len;
	int err;

	if (!ops->port_function_hw_addr_get)
		return 0;

	err = ops->port_function_hw_addr_get(devlink, port, hw_addr, &hw_addr_len, extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}
	err = nla_put(msg, DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR, hw_addr_len, hw_addr);
	if (err)
		return err;
	*msg_updated = true;
	return 0;
}

static bool
devlink_port_fn_state_valid(enum devlink_port_fn_state state)
{
	return state == DEVLINK_PORT_FN_STATE_INACTIVE ||
	       state == DEVLINK_PORT_FN_STATE_ACTIVE;
}

static bool
devlink_port_fn_opstate_valid(enum devlink_port_fn_opstate opstate)
{
	return opstate == DEVLINK_PORT_FN_OPSTATE_DETACHED ||
	       opstate == DEVLINK_PORT_FN_OPSTATE_ATTACHED;
}

static int
devlink_port_fn_state_fill(struct devlink *devlink,
			   const struct devlink_ops *ops,
			   struct devlink_port *port, struct sk_buff *msg,
			   struct netlink_ext_ack *extack,
			   bool *msg_updated)
{
	enum devlink_port_fn_opstate opstate;
	enum devlink_port_fn_state state;
	int err;

	if (!ops->port_fn_state_get)
		return 0;

	err = ops->port_fn_state_get(devlink, port, &state, &opstate, extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}
	if (!devlink_port_fn_state_valid(state)) {
		WARN_ON_ONCE(1);
		NL_SET_ERR_MSG_MOD(extack, "Invalid state read from driver");
		return -EINVAL;
	}
	if (!devlink_port_fn_opstate_valid(opstate)) {
		WARN_ON_ONCE(1);
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid operational state read from driver");
		return -EINVAL;
	}
	if (nla_put_u8(msg, DEVLINK_PORT_FN_ATTR_STATE, state) ||
	    nla_put_u8(msg, DEVLINK_PORT_FN_ATTR_OPSTATE, opstate))
		return -EMSGSIZE;
	*msg_updated = true;
	return 0;
}

static int
devlink_nl_port_function_attrs_put(struct sk_buff *msg, struct devlink_port *port,
				   struct netlink_ext_ack *extack)
{
	struct devlink *devlink = port->devlink;
	const struct devlink_ops *ops;
	struct nlattr *function_attr;
	bool msg_updated = false;
	int err;

	function_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_PORT_FUNCTION);
	if (!function_attr)
		return -EMSGSIZE;

	ops = devlink->ops;
	err = devlink_port_fn_hw_addr_fill(devlink, ops, port, msg,
					   extack, &msg_updated);
	if (err)
		goto out;
	err = devlink_port_fn_state_fill(devlink, ops, port, msg, extack,
					 &msg_updated);
out:
	if (err || !msg_updated)
		nla_nest_cancel(msg, function_attr);
	else
		nla_nest_end(msg, function_attr);
	return err;
}

static int devlink_nl_port_fill(struct sk_buff *msg, struct devlink *devlink,
				struct devlink_port *devlink_port,
				enum devlink_command cmd, u32 portid,
				u32 seq, int flags,
				struct netlink_ext_ack *extack)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;

	/* Hold rtnl lock while accessing port's netdev attributes. */
	rtnl_lock();
	spin_lock_bh(&devlink_port->type_lock);
	if (nla_put_u16(msg, DEVLINK_ATTR_PORT_TYPE, devlink_port->type))
		goto nla_put_failure_type_locked;
	if (devlink_port->desired_type != DEVLINK_PORT_TYPE_NOTSET &&
	    nla_put_u16(msg, DEVLINK_ATTR_PORT_DESIRED_TYPE,
			devlink_port->desired_type))
		goto nla_put_failure_type_locked;
	if (devlink_port->type == DEVLINK_PORT_TYPE_ETH) {
		struct net *net = devlink_net(devlink_port->devlink);
		struct net_device *netdev = devlink_port->type_dev;

		if (netdev && net_eq(net, dev_net(netdev)) &&
		    (nla_put_u32(msg, DEVLINK_ATTR_PORT_NETDEV_IFINDEX,
				 netdev->ifindex) ||
		     nla_put_string(msg, DEVLINK_ATTR_PORT_NETDEV_NAME,
				    netdev->name)))
			goto nla_put_failure_type_locked;
	}
	if (devlink_port->type == DEVLINK_PORT_TYPE_IB) {
		struct ib_device *ibdev = devlink_port->type_dev;

		if (ibdev &&
		    nla_put_string(msg, DEVLINK_ATTR_PORT_IBDEV_NAME,
				   ibdev->name))
			goto nla_put_failure_type_locked;
	}
	spin_unlock_bh(&devlink_port->type_lock);
	rtnl_unlock();
	if (devlink_nl_port_attrs_put(msg, devlink_port))
		goto nla_put_failure;
	if (devlink_nl_port_function_attrs_put(msg, devlink_port, extack))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure_type_locked:
	spin_unlock_bh(&devlink_port->type_lock);
	rtnl_unlock();
nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_port_notify(struct devlink_port *devlink_port,
				enum devlink_command cmd)
{
	struct devlink *devlink = devlink_port->devlink;
	struct sk_buff *msg;
	int err;

	if (!devlink_port->registered)
		return;

	WARN_ON(cmd != DEVLINK_CMD_PORT_NEW && cmd != DEVLINK_CMD_PORT_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_port_fill(msg, devlink, devlink_port, cmd, 0, 0, 0,
				   NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int devlink_nl_cmd_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_fill(msg, devlink, DEVLINK_CMD_NEW,
			      info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_get_dumpit(struct sk_buff *msg,
				     struct netlink_callback *cb)
{
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		if (idx < start) {
			idx++;
			continue;
		}
		err = devlink_nl_fill(msg, devlink, DEVLINK_CMD_NEW,
				      NETLINK_CB(cb->skb).portid,
				      cb->nlh->nlmsg_seq, NLM_F_MULTI);
		if (err)
			goto out;
		idx++;
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_nl_cmd_port_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_port_fill(msg, devlink, devlink_port,
				   DEVLINK_CMD_PORT_NEW,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_port_get_dumpit(struct sk_buff *msg,
					  struct netlink_callback *cb)
{
	struct devlink *devlink;
	struct devlink_port *devlink_port;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_port, &devlink->port_list, list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_port_fill(msg, devlink, devlink_port,
						   DEVLINK_CMD_NEW,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq,
						   NLM_F_MULTI,
						   cb->extack);
			if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_port_type_set(struct devlink *devlink,
				 struct devlink_port *devlink_port,
				 enum devlink_port_type port_type)

{
	int err;

	if (devlink->ops->port_type_set) {
		if (port_type == devlink_port->type)
			return 0;
		err = devlink->ops->port_type_set(devlink_port, port_type);
		if (err)
			return err;
		devlink_port->desired_type = port_type;
		devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
		return 0;
	}
	return -EOPNOTSUPP;
}

static int
devlink_port_function_hw_addr_set(struct devlink *devlink, struct devlink_port *port,
				  const struct nlattr *attr, struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops;
	const u8 *hw_addr;
	int hw_addr_len;

	hw_addr = nla_data(attr);
	hw_addr_len = nla_len(attr);
	if (hw_addr_len > MAX_ADDR_LEN) {
		NL_SET_ERR_MSG_MOD(extack, "Port function hardware address too long");
		return -EINVAL;
	}
	if (port->type == DEVLINK_PORT_TYPE_ETH) {
		if (hw_addr_len != ETH_ALEN) {
			NL_SET_ERR_MSG_MOD(extack, "Address must be 6 bytes for Ethernet device");
			return -EINVAL;
		}
		if (!is_unicast_ether_addr(hw_addr)) {
			NL_SET_ERR_MSG_MOD(extack, "Non-unicast hardware address unsupported");
			return -EINVAL;
		}
	}

	ops = devlink->ops;
	if (!ops->port_function_hw_addr_set) {
		NL_SET_ERR_MSG_MOD(extack, "Port doesn't support function attributes");
		return -EOPNOTSUPP;
	}

	return ops->port_function_hw_addr_set(devlink, port, hw_addr, hw_addr_len, extack);
}

static int devlink_port_fn_state_set(struct devlink *devlink,
				     struct devlink_port *port,
				     const struct nlattr *attr,
				     struct netlink_ext_ack *extack)
{
	enum devlink_port_fn_state state;
	const struct devlink_ops *ops;

	state = nla_get_u8(attr);
	ops = devlink->ops;
	if (!ops->port_fn_state_set) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Function does not support state setting");
		return -EOPNOTSUPP;
	}
	return ops->port_fn_state_set(devlink, port, state, extack);
}

static int
devlink_port_function_set(struct devlink *devlink, struct devlink_port *port,
			  const struct nlattr *attr, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[DEVLINK_PORT_FUNCTION_ATTR_MAX + 1];
	int err;

	err = nla_parse_nested(tb, DEVLINK_PORT_FUNCTION_ATTR_MAX, attr,
			       devlink_function_nl_policy, extack);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Fail to parse port function attributes");
		return err;
	}

	attr = tb[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR];
	if (attr) {
		err = devlink_port_function_hw_addr_set(devlink, port, attr, extack);
		if (err)
			return err;
	}
	/* Keep this as the last function attribute set, so that when
	 * multiple port function attributes are set along with state,
	 * Those can be applied first before activating the state.
	 */
	attr = tb[DEVLINK_PORT_FN_ATTR_STATE];
	if (attr)
		err = devlink_port_fn_state_set(devlink, port, attr, extack);

	if (!err)
		devlink_port_notify(port, DEVLINK_CMD_PORT_NEW);
	return err;
}

static int devlink_nl_cmd_port_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	int err;

	if (info->attrs[DEVLINK_ATTR_PORT_TYPE]) {
		enum devlink_port_type port_type;

		port_type = nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_TYPE]);
		err = devlink_port_type_set(devlink, devlink_port, port_type);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_PORT_FUNCTION]) {
		struct nlattr *attr = info->attrs[DEVLINK_ATTR_PORT_FUNCTION];
		struct netlink_ext_ack *extack = info->extack;

		err = devlink_port_function_set(devlink, devlink_port, attr, extack);
		if (err)
			return err;
	}

	return 0;
}

static int devlink_port_split(struct devlink *devlink, u32 port_index,
			      u32 count, struct netlink_ext_ack *extack)

{
	if (devlink->ops->port_split)
		return devlink->ops->port_split(devlink, port_index, count,
						extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_port_split_doit(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_port *devlink_port;
	u32 port_index;
	u32 count;

	if (!info->attrs[DEVLINK_ATTR_PORT_INDEX] ||
	    !info->attrs[DEVLINK_ATTR_PORT_SPLIT_COUNT])
		return -EINVAL;

	devlink_port = devlink_port_get_from_info(devlink, info);
	port_index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);
	count = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_SPLIT_COUNT]);

	if (IS_ERR(devlink_port))
		return -EINVAL;

	if (!devlink_port->attrs.splittable) {
		/* Split ports cannot be split. */
		if (devlink_port->attrs.split)
			NL_SET_ERR_MSG_MOD(info->extack, "Port cannot be split further");
		else
			NL_SET_ERR_MSG_MOD(info->extack, "Port cannot be split");
		return -EINVAL;
	}

	if (count < 2 || !is_power_of_2(count) || count > devlink_port->attrs.lanes) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid split count");
		return -EINVAL;
	}

	return devlink_port_split(devlink, port_index, count, info->extack);
}

static int devlink_port_unsplit(struct devlink *devlink, u32 port_index,
				struct netlink_ext_ack *extack)

{
	if (devlink->ops->port_unsplit)
		return devlink->ops->port_unsplit(devlink, port_index, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_port_unsplit_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	u32 port_index;

	if (!info->attrs[DEVLINK_ATTR_PORT_INDEX])
		return -EINVAL;

	port_index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);
	return devlink_port_unsplit(devlink, port_index, info->extack);
}

static int devlink_port_new_notifiy(struct devlink *devlink,
				    unsigned int port_index,
				    struct genl_info *info)
{
	struct devlink_port *devlink_port;
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	mutex_lock(&devlink->lock);
	devlink_port = devlink_port_get_by_index(devlink, port_index);
	if (!devlink_port) {
		err = -ENODEV;
		goto out;
	}

	err = devlink_nl_port_fill(msg, devlink, devlink_port,
				   DEVLINK_CMD_NEW, info->snd_portid,
				   info->snd_seq, 0, NULL);
	if (err)
		goto out;

	err = genlmsg_reply(msg, info);
	mutex_unlock(&devlink->lock);
	return err;

out:
	mutex_unlock(&devlink->lock);
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_cmd_port_new_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink_port_new_attrs new_attrs = {};
	struct devlink *devlink = info->user_ptr[0];
	unsigned int new_port_index;
	int err;

	if (!devlink->ops->port_new || !devlink->ops->port_del)
		return -EOPNOTSUPP;

	if (!info->attrs[DEVLINK_ATTR_PORT_FLAVOUR] ||
	    !info->attrs[DEVLINK_ATTR_PORT_PCI_PF_NUMBER]) {
		NL_SET_ERR_MSG_MOD(extack, "Port flavour or PCI PF are not specified");
		return -EINVAL;
	}
	new_attrs.flavour = nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_FLAVOUR]);
	new_attrs.pfnum =
		nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_PCI_PF_NUMBER]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		/* Port index of the new port being created by driver. */
		new_attrs.port_index =
			nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);
		new_attrs.port_index_valid = true;
	}
	if (info->attrs[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER]) {
		new_attrs.controller =
			nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER]);
		new_attrs.controller_valid = true;
	}
	if (new_attrs.flavour == DEVLINK_PORT_FLAVOUR_PCI_SF &&
	    info->attrs[DEVLINK_ATTR_PORT_PCI_SF_NUMBER]) {
		new_attrs.sfnum = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_PCI_SF_NUMBER]);
		new_attrs.sfnum_valid = true;
	}

	err = devlink->ops->port_new(devlink, &new_attrs, extack,
				     &new_port_index);
	if (err)
		return err;

	err = devlink_port_new_notifiy(devlink, new_port_index, info);
	if (err && err != -ENODEV) {
		/* Fail to send the response; destroy newly created port. */
		devlink->ops->port_del(devlink, new_port_index, extack);
	}
	return err;
}

static int devlink_nl_cmd_port_del_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	unsigned int port_index;

	if (!devlink->ops->port_del)
		return -EOPNOTSUPP;

	if (!info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		NL_SET_ERR_MSG_MOD(extack, "Port index is not specified");
		return -EINVAL;
	}
	port_index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

	return devlink->ops->port_del(devlink, port_index, extack);
}

static int devlink_nl_sb_fill(struct sk_buff *msg, struct devlink *devlink,
			      struct devlink_sb *devlink_sb,
			      enum devlink_command cmd, u32 portid,
			      u32 seq, int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_SIZE, devlink_sb->size))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_INGRESS_POOL_COUNT,
			devlink_sb->ingress_pools_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_EGRESS_POOL_COUNT,
			devlink_sb->egress_pools_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_INGRESS_TC_COUNT,
			devlink_sb->ingress_tc_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_EGRESS_TC_COUNT,
			devlink_sb->egress_tc_count))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_get_doit(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_fill(msg, devlink, devlink_sb,
				 DEVLINK_CMD_SB_NEW,
				 info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_sb_get_dumpit(struct sk_buff *msg,
					struct netlink_callback *cb)
{
	struct devlink *devlink;
	struct devlink_sb *devlink_sb;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_sb_fill(msg, devlink, devlink_sb,
						 DEVLINK_CMD_SB_NEW,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 NLM_F_MULTI);
			if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_nl_sb_pool_fill(struct sk_buff *msg, struct devlink *devlink,
				   struct devlink_sb *devlink_sb,
				   u16 pool_index, enum devlink_command cmd,
				   u32 portid, u32 seq, int flags)
{
	struct devlink_sb_pool_info pool_info;
	void *hdr;
	int err;

	err = devlink->ops->sb_pool_get(devlink, devlink_sb->index,
					pool_index, &pool_info);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_TYPE, pool_info.pool_type))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_POOL_SIZE, pool_info.size))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE,
		       pool_info.threshold_type))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_POOL_CELL_SIZE,
			pool_info.cell_size))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_pool_get_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	u16 pool_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!devlink->ops->sb_pool_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_pool_fill(msg, devlink, devlink_sb, pool_index,
				      DEVLINK_CMD_SB_POOL_NEW,
				      info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_pool_get_dumpit(struct sk_buff *msg, int start, int *p_idx,
				struct devlink *devlink,
				struct devlink_sb *devlink_sb,
				u32 portid, u32 seq)
{
	u16 pool_count = devlink_sb_pool_count(devlink_sb);
	u16 pool_index;
	int err;

	for (pool_index = 0; pool_index < pool_count; pool_index++) {
		if (*p_idx < start) {
			(*p_idx)++;
			continue;
		}
		err = devlink_nl_sb_pool_fill(msg, devlink,
					      devlink_sb,
					      pool_index,
					      DEVLINK_CMD_SB_POOL_NEW,
					      portid, seq, NLM_F_MULTI);
		if (err)
			return err;
		(*p_idx)++;
	}
	return 0;
}

static int devlink_nl_cmd_sb_pool_get_dumpit(struct sk_buff *msg,
					     struct netlink_callback *cb)
{
	struct devlink *devlink;
	struct devlink_sb *devlink_sb;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)) ||
		    !devlink->ops->sb_pool_get)
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
			err = __sb_pool_get_dumpit(msg, start, &idx, devlink,
						   devlink_sb,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq);
			if (err == -EOPNOTSUPP) {
				err = 0;
			} else if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_sb_pool_set(struct devlink *devlink, unsigned int sb_index,
			       u16 pool_index, u32 size,
			       enum devlink_sb_threshold_type threshold_type,
			       struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink->ops;

	if (ops->sb_pool_set)
		return ops->sb_pool_set(devlink, sb_index, pool_index,
					size, threshold_type, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_pool_set_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_sb_threshold_type threshold_type;
	struct devlink_sb *devlink_sb;
	u16 pool_index;
	u32 size;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	err = devlink_sb_th_type_get_from_info(info, &threshold_type);
	if (err)
		return err;

	if (!info->attrs[DEVLINK_ATTR_SB_POOL_SIZE])
		return -EINVAL;

	size = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_POOL_SIZE]);
	return devlink_sb_pool_set(devlink, devlink_sb->index,
				   pool_index, size, threshold_type,
				   info->extack);
}

static int devlink_nl_sb_port_pool_fill(struct sk_buff *msg,
					struct devlink *devlink,
					struct devlink_port *devlink_port,
					struct devlink_sb *devlink_sb,
					u16 pool_index,
					enum devlink_command cmd,
					u32 portid, u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	u32 threshold;
	void *hdr;
	int err;

	err = ops->sb_port_pool_get(devlink_port, devlink_sb->index,
				    pool_index, &threshold);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_THRESHOLD, threshold))
		goto nla_put_failure;

	if (ops->sb_occ_port_pool_get) {
		u32 cur;
		u32 max;

		err = ops->sb_occ_port_pool_get(devlink_port, devlink_sb->index,
						pool_index, &cur, &max);
		if (err && err != -EOPNOTSUPP)
			goto sb_occ_get_failure;
		if (!err) {
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_CUR, cur))
				goto nla_put_failure;
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_MAX, max))
				goto nla_put_failure;
		}
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
sb_occ_get_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int devlink_nl_cmd_sb_port_pool_get_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	u16 pool_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!devlink->ops->sb_port_pool_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_port_pool_fill(msg, devlink, devlink_port,
					   devlink_sb, pool_index,
					   DEVLINK_CMD_SB_PORT_POOL_NEW,
					   info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_port_pool_get_dumpit(struct sk_buff *msg, int start, int *p_idx,
				     struct devlink *devlink,
				     struct devlink_sb *devlink_sb,
				     u32 portid, u32 seq)
{
	struct devlink_port *devlink_port;
	u16 pool_count = devlink_sb_pool_count(devlink_sb);
	u16 pool_index;
	int err;

	list_for_each_entry(devlink_port, &devlink->port_list, list) {
		for (pool_index = 0; pool_index < pool_count; pool_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_port_pool_fill(msg, devlink,
							   devlink_port,
							   devlink_sb,
							   pool_index,
							   DEVLINK_CMD_SB_PORT_POOL_NEW,
							   portid, seq,
							   NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
	}
	return 0;
}

static int devlink_nl_cmd_sb_port_pool_get_dumpit(struct sk_buff *msg,
						  struct netlink_callback *cb)
{
	struct devlink *devlink;
	struct devlink_sb *devlink_sb;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)) ||
		    !devlink->ops->sb_port_pool_get)
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
			err = __sb_port_pool_get_dumpit(msg, start, &idx,
							devlink, devlink_sb,
							NETLINK_CB(cb->skb).portid,
							cb->nlh->nlmsg_seq);
			if (err == -EOPNOTSUPP) {
				err = 0;
			} else if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_sb_port_pool_set(struct devlink_port *devlink_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 threshold,
				    struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	if (ops->sb_port_pool_set)
		return ops->sb_port_pool_set(devlink_port, sb_index,
					     pool_index, threshold, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_port_pool_set_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	u16 pool_index;
	u32 threshold;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!info->attrs[DEVLINK_ATTR_SB_THRESHOLD])
		return -EINVAL;

	threshold = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_THRESHOLD]);
	return devlink_sb_port_pool_set(devlink_port, devlink_sb->index,
					pool_index, threshold, info->extack);
}

static int
devlink_nl_sb_tc_pool_bind_fill(struct sk_buff *msg, struct devlink *devlink,
				struct devlink_port *devlink_port,
				struct devlink_sb *devlink_sb, u16 tc_index,
				enum devlink_sb_pool_type pool_type,
				enum devlink_command cmd,
				u32 portid, u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	u16 pool_index;
	u32 threshold;
	void *hdr;
	int err;

	err = ops->sb_tc_pool_bind_get(devlink_port, devlink_sb->index,
				       tc_index, pool_type,
				       &pool_index, &threshold);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_TC_INDEX, tc_index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_TYPE, pool_type))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_THRESHOLD, threshold))
		goto nla_put_failure;

	if (ops->sb_occ_tc_port_bind_get) {
		u32 cur;
		u32 max;

		err = ops->sb_occ_tc_port_bind_get(devlink_port,
						   devlink_sb->index,
						   tc_index, pool_type,
						   &cur, &max);
		if (err && err != -EOPNOTSUPP)
			return err;
		if (!err) {
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_CUR, cur))
				goto nla_put_failure;
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_MAX, max))
				goto nla_put_failure;
		}
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_tc_pool_bind_get_doit(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	enum devlink_sb_pool_type pool_type;
	u16 tc_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_type_get_from_info(info, &pool_type);
	if (err)
		return err;

	err = devlink_sb_tc_index_get_from_info(devlink_sb, info,
						pool_type, &tc_index);
	if (err)
		return err;

	if (!devlink->ops->sb_tc_pool_bind_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink, devlink_port,
					      devlink_sb, tc_index, pool_type,
					      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
					      info->snd_portid,
					      info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_tc_pool_bind_get_dumpit(struct sk_buff *msg,
					int start, int *p_idx,
					struct devlink *devlink,
					struct devlink_sb *devlink_sb,
					u32 portid, u32 seq)
{
	struct devlink_port *devlink_port;
	u16 tc_index;
	int err;

	list_for_each_entry(devlink_port, &devlink->port_list, list) {
		for (tc_index = 0;
		     tc_index < devlink_sb->ingress_tc_count; tc_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink,
							      devlink_port,
							      devlink_sb,
							      tc_index,
							      DEVLINK_SB_POOL_TYPE_INGRESS,
							      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
							      portid, seq,
							      NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
		for (tc_index = 0;
		     tc_index < devlink_sb->egress_tc_count; tc_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink,
							      devlink_port,
							      devlink_sb,
							      tc_index,
							      DEVLINK_SB_POOL_TYPE_EGRESS,
							      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
							      portid, seq,
							      NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
	}
	return 0;
}

static int
devlink_nl_cmd_sb_tc_pool_bind_get_dumpit(struct sk_buff *msg,
					  struct netlink_callback *cb)
{
	struct devlink *devlink;
	struct devlink_sb *devlink_sb;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)) ||
		    !devlink->ops->sb_tc_pool_bind_get)
			continue;

		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
			err = __sb_tc_pool_bind_get_dumpit(msg, start, &idx,
							   devlink,
							   devlink_sb,
							   NETLINK_CB(cb->skb).portid,
							   cb->nlh->nlmsg_seq);
			if (err == -EOPNOTSUPP) {
				err = 0;
			} else if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_sb_tc_pool_bind_set(struct devlink_port *devlink_port,
				       unsigned int sb_index, u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u16 pool_index, u32 threshold,
				       struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	if (ops->sb_tc_pool_bind_set)
		return ops->sb_tc_pool_bind_set(devlink_port, sb_index,
						tc_index, pool_type,
						pool_index, threshold, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_tc_pool_bind_set_doit(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_sb_pool_type pool_type;
	struct devlink_sb *devlink_sb;
	u16 tc_index;
	u16 pool_index;
	u32 threshold;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_type_get_from_info(info, &pool_type);
	if (err)
		return err;

	err = devlink_sb_tc_index_get_from_info(devlink_sb, info,
						pool_type, &tc_index);
	if (err)
		return err;

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!info->attrs[DEVLINK_ATTR_SB_THRESHOLD])
		return -EINVAL;

	threshold = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_THRESHOLD]);
	return devlink_sb_tc_pool_bind_set(devlink_port, devlink_sb->index,
					   tc_index, pool_type,
					   pool_index, threshold, info->extack);
}

static int devlink_nl_cmd_sb_occ_snapshot_doit(struct sk_buff *skb,
					       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	struct devlink_sb *devlink_sb;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	if (ops->sb_occ_snapshot)
		return ops->sb_occ_snapshot(devlink, devlink_sb->index);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_occ_max_clear_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	struct devlink_sb *devlink_sb;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	if (ops->sb_occ_max_clear)
		return ops->sb_occ_max_clear(devlink, devlink_sb->index);
	return -EOPNOTSUPP;
}

static int devlink_nl_eswitch_fill(struct sk_buff *msg, struct devlink *devlink,
				   enum devlink_command cmd, u32 portid,
				   u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	enum devlink_eswitch_encap_mode encap_mode;
	u8 inline_mode;
	void *hdr;
	int err = 0;
	u16 mode;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto nla_put_failure;

	if (ops->eswitch_mode_get) {
		err = ops->eswitch_mode_get(devlink, &mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u16(msg, DEVLINK_ATTR_ESWITCH_MODE, mode);
		if (err)
			goto nla_put_failure;
	}

	if (ops->eswitch_inline_mode_get) {
		err = ops->eswitch_inline_mode_get(devlink, &inline_mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u8(msg, DEVLINK_ATTR_ESWITCH_INLINE_MODE,
				 inline_mode);
		if (err)
			goto nla_put_failure;
	}

	if (ops->eswitch_encap_mode_get) {
		err = ops->eswitch_encap_mode_get(devlink, &encap_mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u8(msg, DEVLINK_ATTR_ESWITCH_ENCAP_MODE, encap_mode);
		if (err)
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int devlink_nl_cmd_eswitch_get_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_eswitch_fill(msg, devlink, DEVLINK_CMD_ESWITCH_GET,
				      info->snd_portid, info->snd_seq, 0);

	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_eswitch_set_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	enum devlink_eswitch_encap_mode encap_mode;
	u8 inline_mode;
	int err = 0;
	u16 mode;

	if (info->attrs[DEVLINK_ATTR_ESWITCH_MODE]) {
		if (!ops->eswitch_mode_set)
			return -EOPNOTSUPP;
		mode = nla_get_u16(info->attrs[DEVLINK_ATTR_ESWITCH_MODE]);
		err = ops->eswitch_mode_set(devlink, mode, info->extack);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_ESWITCH_INLINE_MODE]) {
		if (!ops->eswitch_inline_mode_set)
			return -EOPNOTSUPP;
		inline_mode = nla_get_u8(
				info->attrs[DEVLINK_ATTR_ESWITCH_INLINE_MODE]);
		err = ops->eswitch_inline_mode_set(devlink, inline_mode,
						   info->extack);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_ESWITCH_ENCAP_MODE]) {
		if (!ops->eswitch_encap_mode_set)
			return -EOPNOTSUPP;
		encap_mode = nla_get_u8(info->attrs[DEVLINK_ATTR_ESWITCH_ENCAP_MODE]);
		err = ops->eswitch_encap_mode_set(devlink, encap_mode,
						  info->extack);
		if (err)
			return err;
	}

	return 0;
}

int devlink_dpipe_match_put(struct sk_buff *skb,
			    struct devlink_dpipe_match *match)
{
	struct devlink_dpipe_header *header = match->header;
	struct devlink_dpipe_field *field = &header->fields[match->field_id];
	struct nlattr *match_attr;

	match_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_MATCH);
	if (!match_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_MATCH_TYPE, match->type) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_INDEX, match->header_index) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	nla_nest_end(skb, match_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, match_attr);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_match_put);

static int devlink_dpipe_matches_put(struct devlink_dpipe_table *table,
				     struct sk_buff *skb)
{
	struct nlattr *matches_attr;

	matches_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_TABLE_MATCHES);
	if (!matches_attr)
		return -EMSGSIZE;

	if (table->table_ops->matches_dump(table->priv, skb))
		goto nla_put_failure;

	nla_nest_end(skb, matches_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, matches_attr);
	return -EMSGSIZE;
}

int devlink_dpipe_action_put(struct sk_buff *skb,
			     struct devlink_dpipe_action *action)
{
	struct devlink_dpipe_header *header = action->header;
	struct devlink_dpipe_field *field = &header->fields[action->field_id];
	struct nlattr *action_attr;

	action_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_ACTION);
	if (!action_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_ACTION_TYPE, action->type) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_INDEX, action->header_index) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	nla_nest_end(skb, action_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, action_attr);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_action_put);

static int devlink_dpipe_actions_put(struct devlink_dpipe_table *table,
				     struct sk_buff *skb)
{
	struct nlattr *actions_attr;

	actions_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_TABLE_ACTIONS);
	if (!actions_attr)
		return -EMSGSIZE;

	if (table->table_ops->actions_dump(table->priv, skb))
		goto nla_put_failure;

	nla_nest_end(skb, actions_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, actions_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_table_put(struct sk_buff *skb,
				   struct devlink_dpipe_table *table)
{
	struct nlattr *table_attr;
	u64 table_size;

	table_size = table->table_ops->size_get(table->priv);
	table_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_TABLE);
	if (!table_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_TABLE_NAME, table->name) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_SIZE, table_size,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (nla_put_u8(skb, DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED,
		       table->counters_enabled))
		goto nla_put_failure;

	if (table->resource_valid) {
		if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_ID,
				      table->resource_id, DEVLINK_ATTR_PAD) ||
		    nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_UNITS,
				      table->resource_units, DEVLINK_ATTR_PAD))
			goto nla_put_failure;
	}
	if (devlink_dpipe_matches_put(table, skb))
		goto nla_put_failure;

	if (devlink_dpipe_actions_put(table, skb))
		goto nla_put_failure;

	nla_nest_end(skb, table_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, table_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_send_and_alloc_skb(struct sk_buff **pskb,
					    struct genl_info *info)
{
	int err;

	if (*pskb) {
		err = genlmsg_reply(*pskb, info);
		if (err)
			return err;
	}
	*pskb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!*pskb)
		return -ENOMEM;
	return 0;
}

static int devlink_dpipe_tables_fill(struct genl_info *info,
				     enum devlink_command cmd, int flags,
				     struct list_head *dpipe_tables,
				     const char *table_name)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_dpipe_table *table;
	struct nlattr *tables_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	bool incomplete;
	void *hdr;
	int i;
	int err;

	table = list_first_entry(dpipe_tables,
				 struct devlink_dpipe_table, list);
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;
	tables_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_TABLES);
	if (!tables_attr)
		goto nla_put_failure;

	i = 0;
	incomplete = false;
	list_for_each_entry_from(table, dpipe_tables, list) {
		if (!table_name) {
			err = devlink_dpipe_table_put(skb, table);
			if (err) {
				if (!i)
					goto err_table_put;
				incomplete = true;
				break;
			}
		} else {
			if (!strcmp(table->name, table_name)) {
				err = devlink_dpipe_table_put(skb, table);
				if (err)
					break;
			}
		}
		i++;
	}

	nla_nest_end(skb, tables_attr);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}

	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_table_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_dpipe_table_get(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const char *table_name =  NULL;

	if (info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME])
		table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);

	return devlink_dpipe_tables_fill(info, DEVLINK_CMD_DPIPE_TABLE_GET, 0,
					 &devlink->dpipe_table_list,
					 table_name);
}

static int devlink_dpipe_value_put(struct sk_buff *skb,
				   struct devlink_dpipe_value *value)
{
	if (nla_put(skb, DEVLINK_ATTR_DPIPE_VALUE,
		    value->value_size, value->value))
		return -EMSGSIZE;
	if (value->mask)
		if (nla_put(skb, DEVLINK_ATTR_DPIPE_VALUE_MASK,
			    value->value_size, value->mask))
			return -EMSGSIZE;
	if (value->mapping_valid)
		if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_VALUE_MAPPING,
				value->mapping_value))
			return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_action_value_put(struct sk_buff *skb,
					  struct devlink_dpipe_value *value)
{
	if (!value->action)
		return -EINVAL;
	if (devlink_dpipe_action_put(skb, value->action))
		return -EMSGSIZE;
	if (devlink_dpipe_value_put(skb, value))
		return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_action_values_put(struct sk_buff *skb,
					   struct devlink_dpipe_value *values,
					   unsigned int values_count)
{
	struct nlattr *action_attr;
	int i;
	int err;

	for (i = 0; i < values_count; i++) {
		action_attr = nla_nest_start_noflag(skb,
						    DEVLINK_ATTR_DPIPE_ACTION_VALUE);
		if (!action_attr)
			return -EMSGSIZE;
		err = devlink_dpipe_action_value_put(skb, &values[i]);
		if (err)
			goto err_action_value_put;
		nla_nest_end(skb, action_attr);
	}
	return 0;

err_action_value_put:
	nla_nest_cancel(skb, action_attr);
	return err;
}

static int devlink_dpipe_match_value_put(struct sk_buff *skb,
					 struct devlink_dpipe_value *value)
{
	if (!value->match)
		return -EINVAL;
	if (devlink_dpipe_match_put(skb, value->match))
		return -EMSGSIZE;
	if (devlink_dpipe_value_put(skb, value))
		return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_match_values_put(struct sk_buff *skb,
					  struct devlink_dpipe_value *values,
					  unsigned int values_count)
{
	struct nlattr *match_attr;
	int i;
	int err;

	for (i = 0; i < values_count; i++) {
		match_attr = nla_nest_start_noflag(skb,
						   DEVLINK_ATTR_DPIPE_MATCH_VALUE);
		if (!match_attr)
			return -EMSGSIZE;
		err = devlink_dpipe_match_value_put(skb, &values[i]);
		if (err)
			goto err_match_value_put;
		nla_nest_end(skb, match_attr);
	}
	return 0;

err_match_value_put:
	nla_nest_cancel(skb, match_attr);
	return err;
}

static int devlink_dpipe_entry_put(struct sk_buff *skb,
				   struct devlink_dpipe_entry *entry)
{
	struct nlattr *entry_attr, *matches_attr, *actions_attr;
	int err;

	entry_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_ENTRY);
	if (!entry_attr)
		return  -EMSGSIZE;

	if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_ENTRY_INDEX, entry->index,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (entry->counter_valid)
		if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_ENTRY_COUNTER,
				      entry->counter, DEVLINK_ATTR_PAD))
			goto nla_put_failure;

	matches_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_ENTRY_MATCH_VALUES);
	if (!matches_attr)
		goto nla_put_failure;

	err = devlink_dpipe_match_values_put(skb, entry->match_values,
					     entry->match_values_count);
	if (err) {
		nla_nest_cancel(skb, matches_attr);
		goto err_match_values_put;
	}
	nla_nest_end(skb, matches_attr);

	actions_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_ENTRY_ACTION_VALUES);
	if (!actions_attr)
		goto nla_put_failure;

	err = devlink_dpipe_action_values_put(skb, entry->action_values,
					      entry->action_values_count);
	if (err) {
		nla_nest_cancel(skb, actions_attr);
		goto err_action_values_put;
	}
	nla_nest_end(skb, actions_attr);

	nla_nest_end(skb, entry_attr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
err_match_values_put:
err_action_values_put:
	nla_nest_cancel(skb, entry_attr);
	return err;
}

static struct devlink_dpipe_table *
devlink_dpipe_table_find(struct list_head *dpipe_tables,
			 const char *table_name, struct devlink *devlink)
{
	struct devlink_dpipe_table *table;
	list_for_each_entry_rcu(table, dpipe_tables, list,
				lockdep_is_held(&devlink->lock)) {
		if (!strcmp(table->name, table_name))
			return table;
	}
	return NULL;
}

int devlink_dpipe_entry_ctx_prepare(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct devlink *devlink;
	int err;

	err = devlink_dpipe_send_and_alloc_skb(&dump_ctx->skb,
					       dump_ctx->info);
	if (err)
		return err;

	dump_ctx->hdr = genlmsg_put(dump_ctx->skb,
				    dump_ctx->info->snd_portid,
				    dump_ctx->info->snd_seq,
				    &devlink_nl_family, NLM_F_MULTI,
				    dump_ctx->cmd);
	if (!dump_ctx->hdr)
		goto nla_put_failure;

	devlink = dump_ctx->info->user_ptr[0];
	if (devlink_nl_put_handle(dump_ctx->skb, devlink))
		goto nla_put_failure;
	dump_ctx->nest = nla_nest_start_noflag(dump_ctx->skb,
					       DEVLINK_ATTR_DPIPE_ENTRIES);
	if (!dump_ctx->nest)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	nlmsg_free(dump_ctx->skb);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_prepare);

int devlink_dpipe_entry_ctx_append(struct devlink_dpipe_dump_ctx *dump_ctx,
				   struct devlink_dpipe_entry *entry)
{
	return devlink_dpipe_entry_put(dump_ctx->skb, entry);
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_append);

int devlink_dpipe_entry_ctx_close(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	nla_nest_end(dump_ctx->skb, dump_ctx->nest);
	genlmsg_end(dump_ctx->skb, dump_ctx->hdr);
	return 0;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_close);

void devlink_dpipe_entry_clear(struct devlink_dpipe_entry *entry)

{
	unsigned int value_count, value_index;
	struct devlink_dpipe_value *value;

	value = entry->action_values;
	value_count = entry->action_values_count;
	for (value_index = 0; value_index < value_count; value_index++) {
		kfree(value[value_index].value);
		kfree(value[value_index].mask);
	}

	value = entry->match_values;
	value_count = entry->match_values_count;
	for (value_index = 0; value_index < value_count; value_index++) {
		kfree(value[value_index].value);
		kfree(value[value_index].mask);
	}
}
EXPORT_SYMBOL(devlink_dpipe_entry_clear);

static int devlink_dpipe_entries_fill(struct genl_info *info,
				      enum devlink_command cmd, int flags,
				      struct devlink_dpipe_table *table)
{
	struct devlink_dpipe_dump_ctx dump_ctx;
	struct nlmsghdr *nlh;
	int err;

	dump_ctx.skb = NULL;
	dump_ctx.cmd = cmd;
	dump_ctx.info = info;

	err = table->table_ops->entries_dump(table->priv,
					     table->counters_enabled,
					     &dump_ctx);
	if (err)
		return err;

send_done:
	nlh = nlmsg_put(dump_ctx.skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&dump_ctx.skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(dump_ctx.skb, info);
}

static int devlink_nl_cmd_dpipe_entries_get(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_dpipe_table *table;
	const char *table_name;

	if (!info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME])
		return -EINVAL;

	table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return -EINVAL;

	if (!table->table_ops->entries_dump)
		return -EINVAL;

	return devlink_dpipe_entries_fill(info, DEVLINK_CMD_DPIPE_ENTRIES_GET,
					  0, table);
}

static int devlink_dpipe_fields_put(struct sk_buff *skb,
				    const struct devlink_dpipe_header *header)
{
	struct devlink_dpipe_field *field;
	struct nlattr *field_attr;
	int i;

	for (i = 0; i < header->fields_count; i++) {
		field = &header->fields[i];
		field_attr = nla_nest_start_noflag(skb,
						   DEVLINK_ATTR_DPIPE_FIELD);
		if (!field_attr)
			return -EMSGSIZE;
		if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_FIELD_NAME, field->name) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_BITWIDTH, field->bitwidth) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_MAPPING_TYPE, field->mapping_type))
			goto nla_put_failure;
		nla_nest_end(skb, field_attr);
	}
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, field_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_header_put(struct sk_buff *skb,
				    struct devlink_dpipe_header *header)
{
	struct nlattr *fields_attr, *header_attr;
	int err;

	header_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_HEADER);
	if (!header_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_HEADER_NAME, header->name) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	fields_attr = nla_nest_start_noflag(skb,
					    DEVLINK_ATTR_DPIPE_HEADER_FIELDS);
	if (!fields_attr)
		goto nla_put_failure;

	err = devlink_dpipe_fields_put(skb, header);
	if (err) {
		nla_nest_cancel(skb, fields_attr);
		goto nla_put_failure;
	}
	nla_nest_end(skb, fields_attr);
	nla_nest_end(skb, header_attr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
	nla_nest_cancel(skb, header_attr);
	return err;
}

static int devlink_dpipe_headers_fill(struct genl_info *info,
				      enum devlink_command cmd, int flags,
				      struct devlink_dpipe_headers *
				      dpipe_headers)
{
	struct devlink *devlink = info->user_ptr[0];
	struct nlattr *headers_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	void *hdr;
	int i, j;
	int err;

	i = 0;
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;
	headers_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_HEADERS);
	if (!headers_attr)
		goto nla_put_failure;

	j = 0;
	for (; i < dpipe_headers->headers_count; i++) {
		err = devlink_dpipe_header_put(skb, dpipe_headers->headers[i]);
		if (err) {
			if (!j)
				goto err_table_put;
			break;
		}
		j++;
	}
	nla_nest_end(skb, headers_attr);
	genlmsg_end(skb, hdr);
	if (i != dpipe_headers->headers_count)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_table_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_dpipe_headers_get(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	if (!devlink->dpipe_headers)
		return -EOPNOTSUPP;
	return devlink_dpipe_headers_fill(info, DEVLINK_CMD_DPIPE_HEADERS_GET,
					  0, devlink->dpipe_headers);
}

static int devlink_dpipe_table_counters_set(struct devlink *devlink,
					    const char *table_name,
					    bool enable)
{
	struct devlink_dpipe_table *table;

	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return -EINVAL;

	if (table->counter_control_extern)
		return -EOPNOTSUPP;

	if (!(table->counters_enabled ^ enable))
		return 0;

	table->counters_enabled = enable;
	if (table->table_ops->counters_set_update)
		table->table_ops->counters_set_update(table->priv, enable);
	return 0;
}

static int devlink_nl_cmd_dpipe_table_counters_set(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const char *table_name;
	bool counters_enable;

	if (!info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME] ||
	    !info->attrs[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED])
		return -EINVAL;

	table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);
	counters_enable = !!nla_get_u8(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED]);

	return devlink_dpipe_table_counters_set(devlink, table_name,
						counters_enable);
}

static struct devlink_resource *
devlink_resource_find(struct devlink *devlink,
		      struct devlink_resource *resource, u64 resource_id)
{
	struct list_head *resource_list;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = &devlink->resource_list;

	list_for_each_entry(resource, resource_list, list) {
		struct devlink_resource *child_resource;

		if (resource->id == resource_id)
			return resource;

		child_resource = devlink_resource_find(devlink, resource,
						       resource_id);
		if (child_resource)
			return child_resource;
	}
	return NULL;
}

static void
devlink_resource_validate_children(struct devlink_resource *resource)
{
	struct devlink_resource *child_resource;
	bool size_valid = true;
	u64 parts_size = 0;

	if (list_empty(&resource->resource_list))
		goto out;

	list_for_each_entry(child_resource, &resource->resource_list, list)
		parts_size += child_resource->size_new;

	if (parts_size > resource->size_new)
		size_valid = false;
out:
	resource->size_valid = size_valid;
}

static int
devlink_resource_validate_size(struct devlink_resource *resource, u64 size,
			       struct netlink_ext_ack *extack)
{
	u64 reminder;
	int err = 0;

	if (size > resource->size_params.size_max) {
		NL_SET_ERR_MSG_MOD(extack, "Size larger than maximum");
		err = -EINVAL;
	}

	if (size < resource->size_params.size_min) {
		NL_SET_ERR_MSG_MOD(extack, "Size smaller than minimum");
		err = -EINVAL;
	}

	div64_u64_rem(size, resource->size_params.size_granularity, &reminder);
	if (reminder) {
		NL_SET_ERR_MSG_MOD(extack, "Wrong granularity");
		err = -EINVAL;
	}

	return err;
}

static int devlink_nl_cmd_resource_set(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	u64 resource_id;
	u64 size;
	int err;

	if (!info->attrs[DEVLINK_ATTR_RESOURCE_ID] ||
	    !info->attrs[DEVLINK_ATTR_RESOURCE_SIZE])
		return -EINVAL;
	resource_id = nla_get_u64(info->attrs[DEVLINK_ATTR_RESOURCE_ID]);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (!resource)
		return -EINVAL;

	size = nla_get_u64(info->attrs[DEVLINK_ATTR_RESOURCE_SIZE]);
	err = devlink_resource_validate_size(resource, size, info->extack);
	if (err)
		return err;

	resource->size_new = size;
	devlink_resource_validate_children(resource);
	if (resource->parent)
		devlink_resource_validate_children(resource->parent);
	return 0;
}

static int
devlink_resource_size_params_put(struct devlink_resource *resource,
				 struct sk_buff *skb)
{
	struct devlink_resource_size_params *size_params;

	size_params = &resource->size_params;
	if (nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_GRAN,
			      size_params->size_granularity, DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_MAX,
			      size_params->size_max, DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_MIN,
			      size_params->size_min, DEVLINK_ATTR_PAD) ||
	    nla_put_u8(skb, DEVLINK_ATTR_RESOURCE_UNIT, size_params->unit))
		return -EMSGSIZE;
	return 0;
}

static int devlink_resource_occ_put(struct devlink_resource *resource,
				    struct sk_buff *skb)
{
	if (!resource->occ_get)
		return 0;
	return nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_OCC,
				 resource->occ_get(resource->occ_get_priv),
				 DEVLINK_ATTR_PAD);
}

static int devlink_resource_put(struct devlink *devlink, struct sk_buff *skb,
				struct devlink_resource *resource)
{
	struct devlink_resource *child_resource;
	struct nlattr *child_resource_attr;
	struct nlattr *resource_attr;

	resource_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_RESOURCE);
	if (!resource_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_RESOURCE_NAME, resource->name) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE, resource->size,
			      DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_ID, resource->id,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (resource->size != resource->size_new)
		nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_NEW,
				  resource->size_new, DEVLINK_ATTR_PAD);
	if (devlink_resource_occ_put(resource, skb))
		goto nla_put_failure;
	if (devlink_resource_size_params_put(resource, skb))
		goto nla_put_failure;
	if (list_empty(&resource->resource_list))
		goto out;

	if (nla_put_u8(skb, DEVLINK_ATTR_RESOURCE_SIZE_VALID,
		       resource->size_valid))
		goto nla_put_failure;

	child_resource_attr = nla_nest_start_noflag(skb,
						    DEVLINK_ATTR_RESOURCE_LIST);
	if (!child_resource_attr)
		goto nla_put_failure;

	list_for_each_entry(child_resource, &resource->resource_list, list) {
		if (devlink_resource_put(devlink, skb, child_resource))
			goto resource_put_failure;
	}

	nla_nest_end(skb, child_resource_attr);
out:
	nla_nest_end(skb, resource_attr);
	return 0;

resource_put_failure:
	nla_nest_cancel(skb, child_resource_attr);
nla_put_failure:
	nla_nest_cancel(skb, resource_attr);
	return -EMSGSIZE;
}

static int devlink_resource_fill(struct genl_info *info,
				 enum devlink_command cmd, int flags)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	struct nlattr *resources_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	bool incomplete;
	void *hdr;
	int i;
	int err;

	resource = list_first_entry(&devlink->resource_list,
				    struct devlink_resource, list);
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;

	resources_attr = nla_nest_start_noflag(skb,
					       DEVLINK_ATTR_RESOURCE_LIST);
	if (!resources_attr)
		goto nla_put_failure;

	incomplete = false;
	i = 0;
	list_for_each_entry_from(resource, &devlink->resource_list, list) {
		err = devlink_resource_put(devlink, skb, resource);
		if (err) {
			if (!i)
				goto err_resource_put;
			incomplete = true;
			break;
		}
		i++;
	}
	nla_nest_end(skb, resources_attr);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;
send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_resource_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_resource_dump(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	if (list_empty(&devlink->resource_list))
		return -EOPNOTSUPP;

	return devlink_resource_fill(info, DEVLINK_CMD_RESOURCE_DUMP, 0);
}

static int
devlink_resources_validate(struct devlink *devlink,
			   struct devlink_resource *resource,
			   struct genl_info *info)
{
	struct list_head *resource_list;
	int err = 0;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = &devlink->resource_list;

	list_for_each_entry(resource, resource_list, list) {
		if (!resource->size_valid)
			return -EINVAL;
		err = devlink_resources_validate(devlink, resource, info);
		if (err)
			return err;
	}
	return err;
}

static struct net *devlink_netns_get(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct nlattr *netns_pid_attr = info->attrs[DEVLINK_ATTR_NETNS_PID];
	struct nlattr *netns_fd_attr = info->attrs[DEVLINK_ATTR_NETNS_FD];
	struct nlattr *netns_id_attr = info->attrs[DEVLINK_ATTR_NETNS_ID];
	struct net *net;

	if (!!netns_pid_attr + !!netns_fd_attr + !!netns_id_attr > 1) {
		NL_SET_ERR_MSG_MOD(info->extack, "multiple netns identifying attributes specified");
		return ERR_PTR(-EINVAL);
	}

	if (netns_pid_attr) {
		net = get_net_ns_by_pid(nla_get_u32(netns_pid_attr));
	} else if (netns_fd_attr) {
		net = get_net_ns_by_fd(nla_get_u32(netns_fd_attr));
	} else if (netns_id_attr) {
		net = get_net_ns_by_id(sock_net(skb->sk),
				       nla_get_u32(netns_id_attr));
		if (!net)
			net = ERR_PTR(-EINVAL);
	} else {
		WARN_ON(1);
		net = ERR_PTR(-EINVAL);
	}
	if (IS_ERR(net)) {
		NL_SET_ERR_MSG_MOD(info->extack, "Unknown network namespace");
		return ERR_PTR(-EINVAL);
	}
	if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN)) {
		put_net(net);
		return ERR_PTR(-EPERM);
	}
	return net;
}

static void devlink_param_notify(struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd);

static void devlink_reload_netns_change(struct devlink *devlink,
					struct net *dest_net)
{
	struct devlink_param_item *param_item;

	/* Userspace needs to be notified about devlink objects
	 * removed from original and entering new network namespace.
	 * The rest of the devlink objects are re-created during
	 * reload process so the notifications are generated separatelly.
	 */

	list_for_each_entry(param_item, &devlink->param_list, list)
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_DEL);
	devlink_notify(devlink, DEVLINK_CMD_DEL);

	__devlink_net_set(devlink, dest_net);

	devlink_notify(devlink, DEVLINK_CMD_NEW);
	list_for_each_entry(param_item, &devlink->param_list, list)
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_NEW);
}

static bool devlink_reload_supported(const struct devlink_ops *ops)
{
	return ops->reload_down && ops->reload_up;
}

static void devlink_reload_failed_set(struct devlink *devlink,
				      bool reload_failed)
{
	if (devlink->reload_failed == reload_failed)
		return;
	devlink->reload_failed = reload_failed;
	devlink_notify(devlink, DEVLINK_CMD_NEW);
}

bool devlink_is_reload_failed(const struct devlink *devlink)
{
	return devlink->reload_failed;
}
EXPORT_SYMBOL_GPL(devlink_is_reload_failed);

static void
__devlink_reload_stats_update(struct devlink *devlink, u32 *reload_stats,
			      enum devlink_reload_limit limit, u32 actions_performed)
{
	unsigned long actions = actions_performed;
	int stat_idx;
	int action;

	for_each_set_bit(action, &actions, __DEVLINK_RELOAD_ACTION_MAX) {
		stat_idx = limit * __DEVLINK_RELOAD_ACTION_MAX + action;
		reload_stats[stat_idx]++;
	}
	devlink_notify(devlink, DEVLINK_CMD_NEW);
}

static void
devlink_reload_stats_update(struct devlink *devlink, enum devlink_reload_limit limit,
			    u32 actions_performed)
{
	__devlink_reload_stats_update(devlink, devlink->stats.reload_stats, limit,
				      actions_performed);
}

/**
 *	devlink_remote_reload_actions_performed - Update devlink on reload actions
 *	  performed which are not a direct result of devlink reload call.
 *
 *	This should be called by a driver after performing reload actions in case it was not
 *	a result of devlink reload call. For example fw_activate was performed as a result
 *	of devlink reload triggered fw_activate on another host.
 *	The motivation for this function is to keep data on reload actions performed on this
 *	function whether it was done due to direct devlink reload call or not.
 *
 *	@devlink: devlink
 *	@limit: reload limit
 *	@actions_performed: bitmask of actions performed
 */
void devlink_remote_reload_actions_performed(struct devlink *devlink,
					     enum devlink_reload_limit limit,
					     u32 actions_performed)
{
	if (WARN_ON(!actions_performed ||
		    actions_performed & BIT(DEVLINK_RELOAD_ACTION_UNSPEC) ||
		    actions_performed >= BIT(__DEVLINK_RELOAD_ACTION_MAX) ||
		    limit > DEVLINK_RELOAD_LIMIT_MAX))
		return;

	__devlink_reload_stats_update(devlink, devlink->stats.remote_reload_stats, limit,
				      actions_performed);
}
EXPORT_SYMBOL_GPL(devlink_remote_reload_actions_performed);

static int devlink_reload(struct devlink *devlink, struct net *dest_net,
			  enum devlink_reload_action action, enum devlink_reload_limit limit,
			  u32 *actions_performed, struct netlink_ext_ack *extack)
{
	u32 remote_reload_stats[DEVLINK_RELOAD_STATS_ARRAY_SIZE];
	int err;

	if (!devlink->reload_enabled)
		return -EOPNOTSUPP;

	memcpy(remote_reload_stats, devlink->stats.remote_reload_stats,
	       sizeof(remote_reload_stats));
	err = devlink->ops->reload_down(devlink, !!dest_net, action, limit, extack);
	if (err)
		return err;

	if (dest_net && !net_eq(dest_net, devlink_net(devlink)))
		devlink_reload_netns_change(devlink, dest_net);

	err = devlink->ops->reload_up(devlink, action, limit, actions_performed, extack);
	devlink_reload_failed_set(devlink, !!err);
	if (err)
		return err;

	WARN_ON(!(*actions_performed & BIT(action)));
	/* Catch driver on updating the remote action within devlink reload */
	WARN_ON(memcmp(remote_reload_stats, devlink->stats.remote_reload_stats,
		       sizeof(remote_reload_stats)));
	devlink_reload_stats_update(devlink, limit, *actions_performed);
	return 0;
}

static int
devlink_nl_reload_actions_performed_snd(struct devlink *devlink, u32 actions_performed,
					enum devlink_command cmd, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &devlink_nl_family, 0, cmd);
	if (!hdr)
		goto free_msg;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_bitfield32(msg, DEVLINK_ATTR_RELOAD_ACTIONS_PERFORMED, actions_performed,
			       actions_performed))
		goto nla_put_failure;
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_reload(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_reload_action action;
	enum devlink_reload_limit limit;
	struct net *dest_net = NULL;
	u32 actions_performed;
	int err;

	if (!devlink_reload_supported(devlink->ops))
		return -EOPNOTSUPP;

	err = devlink_resources_validate(devlink, NULL, info);
	if (err) {
		NL_SET_ERR_MSG_MOD(info->extack, "resources size validation failed");
		return err;
	}

	if (info->attrs[DEVLINK_ATTR_NETNS_PID] ||
	    info->attrs[DEVLINK_ATTR_NETNS_FD] ||
	    info->attrs[DEVLINK_ATTR_NETNS_ID]) {
		dest_net = devlink_netns_get(skb, info);
		if (IS_ERR(dest_net))
			return PTR_ERR(dest_net);
	}

	if (info->attrs[DEVLINK_ATTR_RELOAD_ACTION])
		action = nla_get_u8(info->attrs[DEVLINK_ATTR_RELOAD_ACTION]);
	else
		action = DEVLINK_RELOAD_ACTION_DRIVER_REINIT;

	if (!devlink_reload_action_is_supported(devlink, action)) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Requested reload action is not supported by the driver");
		return -EOPNOTSUPP;
	}

	limit = DEVLINK_RELOAD_LIMIT_UNSPEC;
	if (info->attrs[DEVLINK_ATTR_RELOAD_LIMITS]) {
		struct nla_bitfield32 limits;
		u32 limits_selected;

		limits = nla_get_bitfield32(info->attrs[DEVLINK_ATTR_RELOAD_LIMITS]);
		limits_selected = limits.value & limits.selector;
		if (!limits_selected) {
			NL_SET_ERR_MSG_MOD(info->extack, "Invalid limit selected");
			return -EINVAL;
		}
		for (limit = 0 ; limit <= DEVLINK_RELOAD_LIMIT_MAX ; limit++)
			if (limits_selected & BIT(limit))
				break;
		/* UAPI enables multiselection, but currently it is not used */
		if (limits_selected != BIT(limit)) {
			NL_SET_ERR_MSG_MOD(info->extack,
					   "Multiselection of limit is not supported");
			return -EOPNOTSUPP;
		}
		if (!devlink_reload_limit_is_supported(devlink, limit)) {
			NL_SET_ERR_MSG_MOD(info->extack,
					   "Requested limit is not supported by the driver");
			return -EOPNOTSUPP;
		}
		if (devlink_reload_combination_is_invalid(action, limit)) {
			NL_SET_ERR_MSG_MOD(info->extack,
					   "Requested limit is invalid for this action");
			return -EINVAL;
		}
	}
	err = devlink_reload(devlink, dest_net, action, limit, &actions_performed, info->extack);

	if (dest_net)
		put_net(dest_net);

	if (err)
		return err;
	/* For backward compatibility generate reply only if attributes used by user */
	if (!info->attrs[DEVLINK_ATTR_RELOAD_ACTION] && !info->attrs[DEVLINK_ATTR_RELOAD_LIMITS])
		return 0;

	return devlink_nl_reload_actions_performed_snd(devlink, actions_performed,
						       DEVLINK_CMD_RELOAD, info);
}

static int devlink_nl_flash_update_fill(struct sk_buff *msg,
					struct devlink *devlink,
					enum devlink_command cmd,
					struct devlink_flash_notify *params)
{
	void *hdr;

	hdr = genlmsg_put(msg, 0, 0, &devlink_nl_family, 0, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS)
		goto out;

	if (params->status_msg &&
	    nla_put_string(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_MSG,
			   params->status_msg))
		goto nla_put_failure;
	if (params->component &&
	    nla_put_string(msg, DEVLINK_ATTR_FLASH_UPDATE_COMPONENT,
			   params->component))
		goto nla_put_failure;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE,
			      params->done, DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL,
			      params->total, DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_TIMEOUT,
			      params->timeout, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

out:
	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void __devlink_flash_update_notify(struct devlink *devlink,
					  enum devlink_command cmd,
					  struct devlink_flash_notify *params)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_FLASH_UPDATE &&
		cmd != DEVLINK_CMD_FLASH_UPDATE_END &&
		cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_flash_update_fill(msg, devlink, cmd, params);
	if (err)
		goto out_free_msg;

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
	return;

out_free_msg:
	nlmsg_free(msg);
}

static void devlink_flash_update_begin_notify(struct devlink *devlink)
{
	struct devlink_flash_notify params = { 0 };

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE,
				      &params);
}

static void devlink_flash_update_end_notify(struct devlink *devlink)
{
	struct devlink_flash_notify params = { 0 };

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_END,
				      &params);
}

void devlink_flash_update_status_notify(struct devlink *devlink,
					const char *status_msg,
					const char *component,
					unsigned long done,
					unsigned long total)
{
	struct devlink_flash_notify params = {
		.status_msg = status_msg,
		.component = component,
		.done = done,
		.total = total,
	};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_STATUS,
				      &params);
}
EXPORT_SYMBOL_GPL(devlink_flash_update_status_notify);

void devlink_flash_update_timeout_notify(struct devlink *devlink,
					 const char *status_msg,
					 const char *component,
					 unsigned long timeout)
{
	struct devlink_flash_notify params = {
		.status_msg = status_msg,
		.component = component,
		.timeout = timeout,
	};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_STATUS,
				      &params);
}
EXPORT_SYMBOL_GPL(devlink_flash_update_timeout_notify);

static int devlink_nl_cmd_flash_update(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct nlattr *nla_component, *nla_overwrite_mask, *nla_file_name;
	struct devlink_flash_update_params params = {};
	struct devlink *devlink = info->user_ptr[0];
	const char *file_name;
	u32 supported_params;
	int ret;

	if (!devlink->ops->flash_update)
		return -EOPNOTSUPP;

	if (!info->attrs[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME])
		return -EINVAL;

	supported_params = devlink->ops->supported_flash_update_params;

	nla_component = info->attrs[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT];
	if (nla_component) {
		if (!(supported_params & DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT)) {
			NL_SET_ERR_MSG_ATTR(info->extack, nla_component,
					    "component update is not supported by this device");
			return -EOPNOTSUPP;
		}
		params.component = nla_data(nla_component);
	}

	nla_overwrite_mask = info->attrs[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK];
	if (nla_overwrite_mask) {
		struct nla_bitfield32 sections;

		if (!(supported_params & DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK)) {
			NL_SET_ERR_MSG_ATTR(info->extack, nla_overwrite_mask,
					    "overwrite settings are not supported by this device");
			return -EOPNOTSUPP;
		}
		sections = nla_get_bitfield32(nla_overwrite_mask);
		params.overwrite_mask = sections.value & sections.selector;
	}

	nla_file_name = info->attrs[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME];
	file_name = nla_data(nla_file_name);
	ret = request_firmware(&params.fw, file_name, devlink->dev);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, nla_file_name, "failed to locate the requested firmware file");
		return ret;
	}

	devlink_flash_update_begin_notify(devlink);
	ret = devlink->ops->flash_update(devlink, &params, info->extack);
	devlink_flash_update_end_notify(devlink);

	release_firmware(params.fw);

	return ret;
}

static const struct devlink_param devlink_param_generic[] = {
	{
		.id = DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
		.name = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
		.name = DEVLINK_PARAM_GENERIC_MAX_MACS_NAME,
		.type = DEVLINK_PARAM_GENERIC_MAX_MACS_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
		.name = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_NAME,
		.type = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI,
		.name = DEVLINK_PARAM_GENERIC_IGNORE_ARI_NAME,
		.type = DEVLINK_PARAM_GENERIC_IGNORE_ARI_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
		.name = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_NAME,
		.type = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_RESET_DEV_ON_DRV_PROBE,
		.name = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_NAME,
		.type = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_REMOTE_DEV_RESET,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_TYPE,
	},
};

static int devlink_param_generic_verify(const struct devlink_param *param)
{
	/* verify it match generic parameter by id and name */
	if (param->id > DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	if (strcmp(param->name, devlink_param_generic[param->id].name))
		return -ENOENT;

	WARN_ON(param->type != devlink_param_generic[param->id].type);

	return 0;
}

static int devlink_param_driver_verify(const struct devlink_param *param)
{
	int i;

	if (param->id <= DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	/* verify no such name in generic params */
	for (i = 0; i <= DEVLINK_PARAM_GENERIC_ID_MAX; i++)
		if (!strcmp(param->name, devlink_param_generic[i].name))
			return -EEXIST;

	return 0;
}

static struct devlink_param_item *
devlink_param_find_by_name(struct list_head *param_list,
			   const char *param_name)
{
	struct devlink_param_item *param_item;

	list_for_each_entry(param_item, param_list, list)
		if (!strcmp(param_item->param->name, param_name))
			return param_item;
	return NULL;
}

static struct devlink_param_item *
devlink_param_find_by_id(struct list_head *param_list, u32 param_id)
{
	struct devlink_param_item *param_item;

	list_for_each_entry(param_item, param_list, list)
		if (param_item->param->id == param_id)
			return param_item;
	return NULL;
}

static bool
devlink_param_cmode_is_supported(const struct devlink_param *param,
				 enum devlink_param_cmode cmode)
{
	return test_bit(cmode, &param->supported_cmodes);
}

static int devlink_param_get(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx)
{
	if (!param->get)
		return -EOPNOTSUPP;
	return param->get(devlink, param->id, ctx);
}

static int devlink_param_set(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx)
{
	if (!param->set)
		return -EOPNOTSUPP;
	return param->set(devlink, param->id, ctx);
}

static int
devlink_param_type_to_nla_type(enum devlink_param_type param_type)
{
	switch (param_type) {
	case DEVLINK_PARAM_TYPE_U8:
		return NLA_U8;
	case DEVLINK_PARAM_TYPE_U16:
		return NLA_U16;
	case DEVLINK_PARAM_TYPE_U32:
		return NLA_U32;
	case DEVLINK_PARAM_TYPE_STRING:
		return NLA_STRING;
	case DEVLINK_PARAM_TYPE_BOOL:
		return NLA_FLAG;
	default:
		return -EINVAL;
	}
}

static int
devlink_nl_param_value_fill_one(struct sk_buff *msg,
				enum devlink_param_type type,
				enum devlink_param_cmode cmode,
				union devlink_param_value val)
{
	struct nlattr *param_value_attr;

	param_value_attr = nla_nest_start_noflag(msg,
						 DEVLINK_ATTR_PARAM_VALUE);
	if (!param_value_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_CMODE, cmode))
		goto value_nest_cancel;

	switch (type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu8))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_put_u16(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu16))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_put_u32(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu32))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		if (nla_put_string(msg, DEVLINK_ATTR_PARAM_VALUE_DATA,
				   val.vstr))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (val.vbool &&
		    nla_put_flag(msg, DEVLINK_ATTR_PARAM_VALUE_DATA))
			goto value_nest_cancel;
		break;
	}

	nla_nest_end(msg, param_value_attr);
	return 0;

value_nest_cancel:
	nla_nest_cancel(msg, param_value_attr);
nla_put_failure:
	return -EMSGSIZE;
}

static int devlink_nl_param_fill(struct sk_buff *msg, struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd,
				 u32 portid, u32 seq, int flags)
{
	union devlink_param_value param_value[DEVLINK_PARAM_CMODE_MAX + 1];
	bool param_value_set[DEVLINK_PARAM_CMODE_MAX + 1] = {};
	const struct devlink_param *param = param_item->param;
	struct devlink_param_gset_ctx ctx;
	struct nlattr *param_values_list;
	struct nlattr *param_attr;
	int nla_type;
	void *hdr;
	int err;
	int i;

	/* Get value from driver part to driverinit configuration mode */
	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!devlink_param_cmode_is_supported(param, i))
			continue;
		if (i == DEVLINK_PARAM_CMODE_DRIVERINIT) {
			if (!param_item->driverinit_value_valid)
				return -EOPNOTSUPP;
			param_value[i] = param_item->driverinit_value;
		} else {
			if (!param_item->published)
				continue;
			ctx.cmode = i;
			err = devlink_param_get(devlink, param, &ctx);
			if (err)
				return err;
			param_value[i] = ctx.val;
		}
		param_value_set[i] = true;
	}

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	if (cmd == DEVLINK_CMD_PORT_PARAM_GET ||
	    cmd == DEVLINK_CMD_PORT_PARAM_NEW ||
	    cmd == DEVLINK_CMD_PORT_PARAM_DEL)
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, port_index))
			goto genlmsg_cancel;

	param_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_PARAM);
	if (!param_attr)
		goto genlmsg_cancel;
	if (nla_put_string(msg, DEVLINK_ATTR_PARAM_NAME, param->name))
		goto param_nest_cancel;
	if (param->generic && nla_put_flag(msg, DEVLINK_ATTR_PARAM_GENERIC))
		goto param_nest_cancel;

	nla_type = devlink_param_type_to_nla_type(param->type);
	if (nla_type < 0)
		goto param_nest_cancel;
	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_TYPE, nla_type))
		goto param_nest_cancel;

	param_values_list = nla_nest_start_noflag(msg,
						  DEVLINK_ATTR_PARAM_VALUES_LIST);
	if (!param_values_list)
		goto param_nest_cancel;

	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!param_value_set[i])
			continue;
		err = devlink_nl_param_value_fill_one(msg, param->type,
						      i, param_value[i]);
		if (err)
			goto values_list_nest_cancel;
	}

	nla_nest_end(msg, param_values_list);
	nla_nest_end(msg, param_attr);
	genlmsg_end(msg, hdr);
	return 0;

values_list_nest_cancel:
	nla_nest_end(msg, param_values_list);
param_nest_cancel:
	nla_nest_cancel(msg, param_attr);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_param_notify(struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_PARAM_NEW && cmd != DEVLINK_CMD_PARAM_DEL &&
		cmd != DEVLINK_CMD_PORT_PARAM_NEW &&
		cmd != DEVLINK_CMD_PORT_PARAM_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;
	err = devlink_nl_param_fill(msg, devlink, port_index, param_item, cmd,
				    0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int devlink_nl_cmd_param_get_dumpit(struct sk_buff *msg,
					   struct netlink_callback *cb)
{
	struct devlink_param_item *param_item;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(param_item, &devlink->param_list, list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_param_fill(msg, devlink, 0, param_item,
						    DEVLINK_CMD_PARAM_GET,
						    NETLINK_CB(cb->skb).portid,
						    cb->nlh->nlmsg_seq,
						    NLM_F_MULTI);
			if (err == -EOPNOTSUPP) {
				err = 0;
			} else if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

static int
devlink_param_type_get_from_info(struct genl_info *info,
				 enum devlink_param_type *param_type)
{
	if (!info->attrs[DEVLINK_ATTR_PARAM_TYPE])
		return -EINVAL;

	switch (nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_TYPE])) {
	case NLA_U8:
		*param_type = DEVLINK_PARAM_TYPE_U8;
		break;
	case NLA_U16:
		*param_type = DEVLINK_PARAM_TYPE_U16;
		break;
	case NLA_U32:
		*param_type = DEVLINK_PARAM_TYPE_U32;
		break;
	case NLA_STRING:
		*param_type = DEVLINK_PARAM_TYPE_STRING;
		break;
	case NLA_FLAG:
		*param_type = DEVLINK_PARAM_TYPE_BOOL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
devlink_param_value_get_from_info(const struct devlink_param *param,
				  struct genl_info *info,
				  union devlink_param_value *value)
{
	struct nlattr *param_data;
	int len;

	param_data = info->attrs[DEVLINK_ATTR_PARAM_VALUE_DATA];

	if (param->type != DEVLINK_PARAM_TYPE_BOOL && !param_data)
		return -EINVAL;

	switch (param->type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_len(param_data) != sizeof(u8))
			return -EINVAL;
		value->vu8 = nla_get_u8(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_len(param_data) != sizeof(u16))
			return -EINVAL;
		value->vu16 = nla_get_u16(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_len(param_data) != sizeof(u32))
			return -EINVAL;
		value->vu32 = nla_get_u32(param_data);
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		len = strnlen(nla_data(param_data), nla_len(param_data));
		if (len == nla_len(param_data) ||
		    len >= __DEVLINK_PARAM_MAX_STRING_VALUE)
			return -EINVAL;
		strcpy(value->vstr, nla_data(param_data));
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (param_data && nla_len(param_data))
			return -EINVAL;
		value->vbool = nla_get_flag(param_data);
		break;
	}
	return 0;
}

static struct devlink_param_item *
devlink_param_get_from_info(struct list_head *param_list,
			    struct genl_info *info)
{
	char *param_name;

	if (!info->attrs[DEVLINK_ATTR_PARAM_NAME])
		return NULL;

	param_name = nla_data(info->attrs[DEVLINK_ATTR_PARAM_NAME]);
	return devlink_param_find_by_name(param_list, param_name);
}

static int devlink_nl_cmd_param_get_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_param_item *param_item;
	struct sk_buff *msg;
	int err;

	param_item = devlink_param_get_from_info(&devlink->param_list, info);
	if (!param_item)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_param_fill(msg, devlink, 0, param_item,
				    DEVLINK_CMD_PARAM_GET,
				    info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __devlink_nl_cmd_param_set_doit(struct devlink *devlink,
					   unsigned int port_index,
					   struct list_head *param_list,
					   struct genl_info *info,
					   enum devlink_command cmd)
{
	enum devlink_param_type param_type;
	struct devlink_param_gset_ctx ctx;
	enum devlink_param_cmode cmode;
	struct devlink_param_item *param_item;
	const struct devlink_param *param;
	union devlink_param_value value;
	int err = 0;

	param_item = devlink_param_get_from_info(param_list, info);
	if (!param_item)
		return -EINVAL;
	param = param_item->param;
	err = devlink_param_type_get_from_info(info, &param_type);
	if (err)
		return err;
	if (param_type != param->type)
		return -EINVAL;
	err = devlink_param_value_get_from_info(param, info, &value);
	if (err)
		return err;
	if (param->validate) {
		err = param->validate(devlink, param->id, value, info->extack);
		if (err)
			return err;
	}

	if (!info->attrs[DEVLINK_ATTR_PARAM_VALUE_CMODE])
		return -EINVAL;
	cmode = nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_VALUE_CMODE]);
	if (!devlink_param_cmode_is_supported(param, cmode))
		return -EOPNOTSUPP;

	if (cmode == DEVLINK_PARAM_CMODE_DRIVERINIT) {
		if (param->type == DEVLINK_PARAM_TYPE_STRING)
			strcpy(param_item->driverinit_value.vstr, value.vstr);
		else
			param_item->driverinit_value = value;
		param_item->driverinit_value_valid = true;
	} else {
		if (!param->set)
			return -EOPNOTSUPP;
		ctx.val = value;
		ctx.cmode = cmode;
		err = devlink_param_set(devlink, param, &ctx);
		if (err)
			return err;
	}

	devlink_param_notify(devlink, port_index, param_item, cmd);
	return 0;
}

static int devlink_nl_cmd_param_set_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	return __devlink_nl_cmd_param_set_doit(devlink, 0, &devlink->param_list,
					       info, DEVLINK_CMD_PARAM_NEW);
}

static int devlink_param_register_one(struct devlink *devlink,
				      unsigned int port_index,
				      struct list_head *param_list,
				      const struct devlink_param *param,
				      enum devlink_command cmd)
{
	struct devlink_param_item *param_item;

	if (devlink_param_find_by_name(param_list, param->name))
		return -EEXIST;

	if (param->supported_cmodes == BIT(DEVLINK_PARAM_CMODE_DRIVERINIT))
		WARN_ON(param->get || param->set);
	else
		WARN_ON(!param->get || !param->set);

	param_item = kzalloc(sizeof(*param_item), GFP_KERNEL);
	if (!param_item)
		return -ENOMEM;
	param_item->param = param;

	list_add_tail(&param_item->list, param_list);
	devlink_param_notify(devlink, port_index, param_item, cmd);
	return 0;
}

static void devlink_param_unregister_one(struct devlink *devlink,
					 unsigned int port_index,
					 struct list_head *param_list,
					 const struct devlink_param *param,
					 enum devlink_command cmd)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_name(param_list, param->name);
	WARN_ON(!param_item);
	devlink_param_notify(devlink, port_index, param_item, cmd);
	list_del(&param_item->list);
	kfree(param_item);
}

static int devlink_nl_cmd_port_param_get_dumpit(struct sk_buff *msg,
						struct netlink_callback *cb)
{
	struct devlink_param_item *param_item;
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(devlink_port, &devlink->port_list, list) {
			list_for_each_entry(param_item,
					    &devlink_port->param_list, list) {
				if (idx < start) {
					idx++;
					continue;
				}
				err = devlink_nl_param_fill(msg,
						devlink_port->devlink,
						devlink_port->index, param_item,
						DEVLINK_CMD_PORT_PARAM_GET,
						NETLINK_CB(cb->skb).portid,
						cb->nlh->nlmsg_seq,
						NLM_F_MULTI);
				if (err == -EOPNOTSUPP) {
					err = 0;
				} else if (err) {
					mutex_unlock(&devlink->lock);
					goto out;
				}
				idx++;
			}
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

static int devlink_nl_cmd_port_param_get_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink_param_item *param_item;
	struct sk_buff *msg;
	int err;

	param_item = devlink_param_get_from_info(&devlink_port->param_list,
						 info);
	if (!param_item)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_param_fill(msg, devlink_port->devlink,
				    devlink_port->index, param_item,
				    DEVLINK_CMD_PORT_PARAM_GET,
				    info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_port_param_set_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];

	return __devlink_nl_cmd_param_set_doit(devlink_port->devlink,
					       devlink_port->index,
					       &devlink_port->param_list, info,
					       DEVLINK_CMD_PORT_PARAM_NEW);
}

static int devlink_nl_region_snapshot_id_put(struct sk_buff *msg,
					     struct devlink *devlink,
					     struct devlink_snapshot *snapshot)
{
	struct nlattr *snap_attr;
	int err;

	snap_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_SNAPSHOT);
	if (!snap_attr)
		return -EINVAL;

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID, snapshot->id);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, snap_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snap_attr);
	return err;
}

static int devlink_nl_region_snapshots_id_put(struct sk_buff *msg,
					      struct devlink *devlink,
					      struct devlink_region *region)
{
	struct devlink_snapshot *snapshot;
	struct nlattr *snapshots_attr;
	int err;

	snapshots_attr = nla_nest_start_noflag(msg,
					       DEVLINK_ATTR_REGION_SNAPSHOTS);
	if (!snapshots_attr)
		return -EINVAL;

	list_for_each_entry(snapshot, &region->snapshot_list, list) {
		err = devlink_nl_region_snapshot_id_put(msg, devlink, snapshot);
		if (err)
			goto nla_put_failure;
	}

	nla_nest_end(msg, snapshots_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snapshots_attr);
	return err;
}

static int devlink_nl_region_fill(struct sk_buff *msg, struct devlink *devlink,
				  enum devlink_command cmd, u32 portid,
				  u32 seq, int flags,
				  struct devlink_region *region)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME, region->ops->name);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
				region->size,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	err = devlink_nl_region_snapshots_id_put(msg, devlink, region);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static struct sk_buff *
devlink_nl_region_notify_build(struct devlink_region *region,
			       struct devlink_snapshot *snapshot,
			       enum devlink_command cmd, u32 portid, u32 seq)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;
	void *hdr;
	int err;


	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, 0, cmd);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_free_msg;
	}

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto out_cancel_msg;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto out_cancel_msg;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME,
			     region->ops->name);
	if (err)
		goto out_cancel_msg;

	if (snapshot) {
		err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID,
				  snapshot->id);
		if (err)
			goto out_cancel_msg;
	} else {
		err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
					region->size, DEVLINK_ATTR_PAD);
		if (err)
			goto out_cancel_msg;
	}
	genlmsg_end(msg, hdr);

	return msg;

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);
	return ERR_PTR(err);
}

static void devlink_nl_region_notify(struct devlink_region *region,
				     struct devlink_snapshot *snapshot,
				     enum devlink_command cmd)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;

	WARN_ON(cmd != DEVLINK_CMD_REGION_NEW && cmd != DEVLINK_CMD_REGION_DEL);

	msg = devlink_nl_region_notify_build(region, snapshot, cmd, 0, 0);
	if (IS_ERR(msg))
		return;

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

/**
 * __devlink_snapshot_id_increment - Increment number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a new snapshot begins using an id. Load the count for the
 *	given id from the snapshot xarray, increment it, and store it back.
 *
 *	Called when a new snapshot is created with the given id.
 *
 *	The id *must* have been previously allocated by
 *	devlink_region_snapshot_id_get().
 *
 *	Returns 0 on success, or an error on failure.
 */
static int __devlink_snapshot_id_increment(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;

	lockdep_assert_held(&devlink->lock);

	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p))
		return -EINVAL;

	if (WARN_ON(!xa_is_value(p)))
		return -EINVAL;

	count = xa_to_value(p);
	count++;

	return xa_err(xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
			       GFP_KERNEL));
}

/**
 * __devlink_snapshot_id_decrement - Decrease number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a snapshot is deleted and stops using an id. Load the count
 *	for the given id from the snapshot xarray, decrement it, and store it
 *	back.
 *
 *	If the count reaches zero, erase this id from the xarray, freeing it
 *	up for future re-use by devlink_region_snapshot_id_get().
 *
 *	Called when a snapshot using the given id is deleted, and when the
 *	initial allocator of the id is finished using it.
 */
static void __devlink_snapshot_id_decrement(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;

	lockdep_assert_held(&devlink->lock);

	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p))
		return;

	if (WARN_ON(!xa_is_value(p)))
		return;

	count = xa_to_value(p);

	if (count > 1) {
		count--;
		xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
			 GFP_KERNEL);
	} else {
		/* If this was the last user, we can erase this id */
		xa_erase(&devlink->snapshot_ids, id);
	}
}

/**
 *	__devlink_snapshot_id_insert - Insert a specific snapshot ID
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Mark the given snapshot id as used by inserting a zero value into the
 *	snapshot xarray.
 *
 *	This must be called while holding the devlink instance lock. Unlike
 *	devlink_snapshot_id_get, the initial reference count is zero, not one.
 *	It is expected that the id will immediately be used before
 *	releasing the devlink instance lock.
 *
 *	Returns zero on success, or an error code if the snapshot id could not
 *	be inserted.
 */
static int __devlink_snapshot_id_insert(struct devlink *devlink, u32 id)
{
	lockdep_assert_held(&devlink->lock);

	if (xa_load(&devlink->snapshot_ids, id))
		return -EEXIST;

	return xa_err(xa_store(&devlink->snapshot_ids, id, xa_mk_value(0),
			       GFP_KERNEL));
}

/**
 *	__devlink_region_snapshot_id_get - get snapshot ID
 *	@devlink: devlink instance
 *	@id: storage to return snapshot id
 *
 *	Allocates a new snapshot id. Returns zero on success, or a negative
 *	error on failure. Must be called while holding the devlink instance
 *	lock.
 *
 *	Snapshot IDs are tracked using an xarray which stores the number of
 *	users of the snapshot id.
 *
 *	Note that the caller of this function counts as a 'user', in order to
 *	avoid race conditions. The caller must release its hold on the
 *	snapshot by using devlink_region_snapshot_id_put.
 */
static int __devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	lockdep_assert_held(&devlink->lock);

	return xa_alloc(&devlink->snapshot_ids, id, xa_mk_value(1),
			xa_limit_32b, GFP_KERNEL);
}

/**
 *	__devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	Must be called only while holding the devlink instance lock.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
static int
__devlink_region_snapshot_create(struct devlink_region *region,
				 u8 *data, u32 snapshot_id)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot;
	int err;

	lockdep_assert_held(&devlink->lock);

	/* check if region can hold one more snapshot */
	if (region->cur_snapshots == region->max_snapshots)
		return -ENOSPC;

	if (devlink_region_snapshot_get_by_id(region, snapshot_id))
		return -EEXIST;

	snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	err = __devlink_snapshot_id_increment(devlink, snapshot_id);
	if (err)
		goto err_snapshot_id_increment;

	snapshot->id = snapshot_id;
	snapshot->region = region;
	snapshot->data = data;

	list_add_tail(&snapshot->list, &region->snapshot_list);

	region->cur_snapshots++;

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_NEW);
	return 0;

err_snapshot_id_increment:
	kfree(snapshot);
	return err;
}

static void devlink_region_snapshot_del(struct devlink_region *region,
					struct devlink_snapshot *snapshot)
{
	struct devlink *devlink = region->devlink;

	lockdep_assert_held(&devlink->lock);

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_DEL);
	region->cur_snapshots--;
	list_del(&snapshot->list);
	region->ops->destructor(snapshot->data);
	__devlink_snapshot_id_decrement(devlink, snapshot->id);
	kfree(snapshot);
}

static int devlink_nl_cmd_region_get_doit(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	struct sk_buff *msg;
	unsigned int index;
	int err;

	if (!info->attrs[DEVLINK_ATTR_REGION_NAME])
		return -EINVAL;

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_region_fill(msg, devlink, DEVLINK_CMD_REGION_GET,
				     info->snd_portid, info->snd_seq, 0,
				     region);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_region_get_port_dumpit(struct sk_buff *msg,
						 struct netlink_callback *cb,
						 struct devlink_port *port,
						 int *idx,
						 int start)
{
	struct devlink_region *region;
	int err = 0;

	list_for_each_entry(region, &port->region_list, list) {
		if (*idx < start) {
			(*idx)++;
			continue;
		}
		err = devlink_nl_region_fill(msg, port->devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     NLM_F_MULTI, region);
		if (err)
			goto out;
		(*idx)++;
	}

out:
	return err;
}

static int devlink_nl_cmd_region_get_devlink_dumpit(struct sk_buff *msg,
						    struct netlink_callback *cb,
						    struct devlink *devlink,
						    int *idx,
						    int start)
{
	struct devlink_region *region;
	struct devlink_port *port;
	int err = 0;

	mutex_lock(&devlink->lock);
	list_for_each_entry(region, &devlink->region_list, list) {
		if (*idx < start) {
			(*idx)++;
			continue;
		}
		err = devlink_nl_region_fill(msg, devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     NLM_F_MULTI, region);
		if (err)
			goto out;
		(*idx)++;
	}

	list_for_each_entry(port, &devlink->port_list, list) {
		err = devlink_nl_cmd_region_get_port_dumpit(msg, cb, port, idx,
							    start);
		if (err)
			goto out;
	}

out:
	mutex_unlock(&devlink->lock);
	return err;
}

static int devlink_nl_cmd_region_get_dumpit(struct sk_buff *msg,
					    struct netlink_callback *cb)
{
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		err = devlink_nl_cmd_region_get_devlink_dumpit(msg, cb, devlink,
							       &idx, start);
		if (err)
			goto out;
	}
out:
	mutex_unlock(&devlink_mutex);
	cb->args[0] = idx;
	return msg->len;
}

static int devlink_nl_cmd_region_del(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;

	if (!info->attrs[DEVLINK_ATTR_REGION_NAME] ||
	    !info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID])
		return -EINVAL;

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	snapshot_id = nla_get_u32(info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
	if (!snapshot)
		return -EINVAL;

	devlink_region_snapshot_del(region, snapshot);
	return 0;
}

static int
devlink_nl_cmd_region_new(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct nlattr *snapshot_id_attr;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;
	u8 *data;
	int err;

	if (!info->attrs[DEVLINK_ATTR_REGION_NAME]) {
		NL_SET_ERR_MSG_MOD(info->extack, "No region name provided");
		return -EINVAL;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG_MOD(info->extack, "The requested region does not exist");
		return -EINVAL;
	}

	if (!region->ops->snapshot) {
		NL_SET_ERR_MSG_MOD(info->extack, "The requested region does not support taking an immediate snapshot");
		return -EOPNOTSUPP;
	}

	if (region->cur_snapshots == region->max_snapshots) {
		NL_SET_ERR_MSG_MOD(info->extack, "The region has reached the maximum number of stored snapshots");
		return -ENOSPC;
	}

	snapshot_id_attr = info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (snapshot_id_attr) {
		snapshot_id = nla_get_u32(snapshot_id_attr);

		if (devlink_region_snapshot_get_by_id(region, snapshot_id)) {
			NL_SET_ERR_MSG_MOD(info->extack, "The requested snapshot id is already in use");
			return -EEXIST;
		}

		err = __devlink_snapshot_id_insert(devlink, snapshot_id);
		if (err)
			return err;
	} else {
		err = __devlink_region_snapshot_id_get(devlink, &snapshot_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(info->extack, "Failed to allocate a new snapshot id");
			return err;
		}
	}

	if (port)
		err = region->port_ops->snapshot(port, region->port_ops,
						 info->extack, &data);
	else
		err = region->ops->snapshot(devlink, region->ops,
					    info->extack, &data);
	if (err)
		goto err_snapshot_capture;

	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	if (err)
		goto err_snapshot_create;

	if (!snapshot_id_attr) {
		struct sk_buff *msg;

		snapshot = devlink_region_snapshot_get_by_id(region,
							     snapshot_id);
		if (WARN_ON(!snapshot))
			return -EINVAL;

		msg = devlink_nl_region_notify_build(region, snapshot,
						     DEVLINK_CMD_REGION_NEW,
						     info->snd_portid,
						     info->snd_seq);
		err = PTR_ERR_OR_ZERO(msg);
		if (err)
			goto err_notify;

		err = genlmsg_reply(msg, info);
		if (err)
			goto err_notify;
	}

	return 0;

err_snapshot_create:
	region->ops->destructor(data);
err_snapshot_capture:
	__devlink_snapshot_id_decrement(devlink, snapshot_id);
	return err;

err_notify:
	devlink_region_snapshot_del(region, snapshot);
	return err;
}

static int devlink_nl_cmd_region_read_chunk_fill(struct sk_buff *msg,
						 struct devlink *devlink,
						 u8 *chunk, u32 chunk_size,
						 u64 addr)
{
	struct nlattr *chunk_attr;
	int err;

	chunk_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_CHUNK);
	if (!chunk_attr)
		return -EINVAL;

	err = nla_put(msg, DEVLINK_ATTR_REGION_CHUNK_DATA, chunk_size, chunk);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_CHUNK_ADDR, addr,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, chunk_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, chunk_attr);
	return err;
}

#define DEVLINK_REGION_READ_CHUNK_SIZE 256

static int devlink_nl_region_read_snapshot_fill(struct sk_buff *skb,
						struct devlink *devlink,
						struct devlink_region *region,
						struct nlattr **attrs,
						u64 start_offset,
						u64 end_offset,
						u64 *new_offset)
{
	struct devlink_snapshot *snapshot;
	u64 curr_offset = start_offset;
	u32 snapshot_id;
	int err = 0;

	*new_offset = start_offset;

	snapshot_id = nla_get_u32(attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID]);
	snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
	if (!snapshot)
		return -EINVAL;

	while (curr_offset < end_offset) {
		u32 data_size;
		u8 *data;

		if (end_offset - curr_offset < DEVLINK_REGION_READ_CHUNK_SIZE)
			data_size = end_offset - curr_offset;
		else
			data_size = DEVLINK_REGION_READ_CHUNK_SIZE;

		data = &snapshot->data[curr_offset];
		err = devlink_nl_cmd_region_read_chunk_fill(skb, devlink,
							    data, data_size,
							    curr_offset);
		if (err)
			break;

		curr_offset += data_size;
	}
	*new_offset = curr_offset;

	return err;
}

static int devlink_nl_cmd_region_read_dumpit(struct sk_buff *skb,
					     struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	u64 ret_offset, start_offset, end_offset = U64_MAX;
	struct nlattr **attrs = info->attrs;
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	struct nlattr *chunks_attr;
	const char *region_name;
	struct devlink *devlink;
	unsigned int index;
	void *hdr;
	int err;

	start_offset = *((u64 *)&cb->args[0]);

	mutex_lock(&devlink_mutex);
	devlink = devlink_get_from_attrs(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink)) {
		err = PTR_ERR(devlink);
		goto out_dev;
	}

	mutex_lock(&devlink->lock);

	if (!attrs[DEVLINK_ATTR_REGION_NAME] ||
	    !attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID]) {
		err = -EINVAL;
		goto out_unlock;
	}

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	region_name = nla_data(attrs[DEVLINK_ATTR_REGION_NAME]);

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		err = -EINVAL;
		goto out_unlock;
	}

	if (attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR] &&
	    attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]) {
		if (!start_offset)
			start_offset =
				nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);

		end_offset = nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);
		end_offset += nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]);
	}

	if (end_offset > region->size)
		end_offset = region->size;

	/* return 0 if there is no further data to read */
	if (start_offset == end_offset) {
		err = 0;
		goto out_unlock;
	}

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, NLM_F_ACK | NLM_F_MULTI,
			  DEVLINK_CMD_REGION_READ);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_unlock;
	}

	err = devlink_nl_put_handle(skb, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(skb, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(skb, DEVLINK_ATTR_REGION_NAME, region_name);
	if (err)
		goto nla_put_failure;

	chunks_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_REGION_CHUNKS);
	if (!chunks_attr) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	err = devlink_nl_region_read_snapshot_fill(skb, devlink,
						   region, attrs,
						   start_offset,
						   end_offset, &ret_offset);

	if (err && err != -EMSGSIZE)
		goto nla_put_failure;

	/* Check if there was any progress done to prevent infinite loop */
	if (ret_offset == start_offset) {
		err = -EINVAL;
		goto nla_put_failure;
	}

	*((u64 *)&cb->args[0]) = ret_offset;

	nla_nest_end(skb, chunks_attr);
	genlmsg_end(skb, hdr);
	mutex_unlock(&devlink->lock);
	mutex_unlock(&devlink_mutex);

	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out_unlock:
	mutex_unlock(&devlink->lock);
out_dev:
	mutex_unlock(&devlink_mutex);
	return err;
}

struct devlink_info_req {
	struct sk_buff *msg;
};

int devlink_info_driver_name_put(struct devlink_info_req *req, const char *name)
{
	return nla_put_string(req->msg, DEVLINK_ATTR_INFO_DRIVER_NAME, name);
}
EXPORT_SYMBOL_GPL(devlink_info_driver_name_put);

int devlink_info_serial_number_put(struct devlink_info_req *req, const char *sn)
{
	return nla_put_string(req->msg, DEVLINK_ATTR_INFO_SERIAL_NUMBER, sn);
}
EXPORT_SYMBOL_GPL(devlink_info_serial_number_put);

int devlink_info_board_serial_number_put(struct devlink_info_req *req,
					 const char *bsn)
{
	return nla_put_string(req->msg, DEVLINK_ATTR_INFO_BOARD_SERIAL_NUMBER,
			      bsn);
}
EXPORT_SYMBOL_GPL(devlink_info_board_serial_number_put);

static int devlink_info_version_put(struct devlink_info_req *req, int attr,
				    const char *version_name,
				    const char *version_value)
{
	struct nlattr *nest;
	int err;

	nest = nla_nest_start_noflag(req->msg, attr);
	if (!nest)
		return -EMSGSIZE;

	err = nla_put_string(req->msg, DEVLINK_ATTR_INFO_VERSION_NAME,
			     version_name);
	if (err)
		goto nla_put_failure;

	err = nla_put_string(req->msg, DEVLINK_ATTR_INFO_VERSION_VALUE,
			     version_value);
	if (err)
		goto nla_put_failure;

	nla_nest_end(req->msg, nest);

	return 0;

nla_put_failure:
	nla_nest_cancel(req->msg, nest);
	return err;
}

int devlink_info_version_fixed_put(struct devlink_info_req *req,
				   const char *version_name,
				   const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_FIXED,
					version_name, version_value);
}
EXPORT_SYMBOL_GPL(devlink_info_version_fixed_put);

int devlink_info_version_stored_put(struct devlink_info_req *req,
				    const char *version_name,
				    const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_STORED,
					version_name, version_value);
}
EXPORT_SYMBOL_GPL(devlink_info_version_stored_put);

int devlink_info_version_running_put(struct devlink_info_req *req,
				     const char *version_name,
				     const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_RUNNING,
					version_name, version_value);
}
EXPORT_SYMBOL_GPL(devlink_info_version_running_put);

static int
devlink_nl_info_fill(struct sk_buff *msg, struct devlink *devlink,
		     enum devlink_command cmd, u32 portid,
		     u32 seq, int flags, struct netlink_ext_ack *extack)
{
	struct devlink_info_req req;
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto err_cancel_msg;

	req.msg = msg;
	err = devlink->ops->info_get(devlink, &req, extack);
	if (err)
		goto err_cancel_msg;

	genlmsg_end(msg, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int devlink_nl_cmd_info_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	if (!devlink->ops->info_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_info_fill(msg, devlink, DEVLINK_CMD_INFO_GET,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_info_get_dumpit(struct sk_buff *msg,
					  struct netlink_callback *cb)
{
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err = 0;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		if (idx < start) {
			idx++;
			continue;
		}

		if (!devlink->ops->info_get) {
			idx++;
			continue;
		}

		mutex_lock(&devlink->lock);
		err = devlink_nl_info_fill(msg, devlink, DEVLINK_CMD_INFO_GET,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   cb->extack);
		mutex_unlock(&devlink->lock);
		if (err == -EOPNOTSUPP)
			err = 0;
		else if (err)
			break;
		idx++;
	}
	mutex_unlock(&devlink_mutex);

	if (err != -EMSGSIZE)
		return err;

	cb->args[0] = idx;
	return msg->len;
}

struct devlink_fmsg_item {
	struct list_head list;
	int attrtype;
	u8 nla_type;
	u16 len;
	int value[];
};

struct devlink_fmsg {
	struct list_head item_list;
	bool putting_binary; /* This flag forces enclosing of binary data
			      * in an array brackets. It forces using
			      * of designated API:
			      * devlink_fmsg_binary_pair_nest_start()
			      * devlink_fmsg_binary_pair_nest_end()
			      */
};

static struct devlink_fmsg *devlink_fmsg_alloc(void)
{
	struct devlink_fmsg *fmsg;

	fmsg = kzalloc(sizeof(*fmsg), GFP_KERNEL);
	if (!fmsg)
		return NULL;

	INIT_LIST_HEAD(&fmsg->item_list);

	return fmsg;
}

static void devlink_fmsg_free(struct devlink_fmsg *fmsg)
{
	struct devlink_fmsg_item *item, *tmp;

	list_for_each_entry_safe(item, tmp, &fmsg->item_list, list) {
		list_del(&item->list);
		kfree(item);
	}
	kfree(fmsg);
}

static int devlink_fmsg_nest_common(struct devlink_fmsg *fmsg,
				    int attrtype)
{
	struct devlink_fmsg_item *item;

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->attrtype = attrtype;
	list_add_tail(&item->list, &fmsg->item_list);

	return 0;
}

int devlink_fmsg_obj_nest_start(struct devlink_fmsg *fmsg)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_OBJ_NEST_START);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_obj_nest_start);

static int devlink_fmsg_nest_end(struct devlink_fmsg *fmsg)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_NEST_END);
}

int devlink_fmsg_obj_nest_end(struct devlink_fmsg *fmsg)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_obj_nest_end);

#define DEVLINK_FMSG_MAX_SIZE (GENLMSG_DEFAULT_SIZE - GENL_HDRLEN - NLA_HDRLEN)

static int devlink_fmsg_put_name(struct devlink_fmsg *fmsg, const char *name)
{
	struct devlink_fmsg_item *item;

	if (fmsg->putting_binary)
		return -EINVAL;

	if (strlen(name) + 1 > DEVLINK_FMSG_MAX_SIZE)
		return -EMSGSIZE;

	item = kzalloc(sizeof(*item) + strlen(name) + 1, GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->nla_type = NLA_NUL_STRING;
	item->len = strlen(name) + 1;
	item->attrtype = DEVLINK_ATTR_FMSG_OBJ_NAME;
	memcpy(&item->value, name, item->len);
	list_add_tail(&item->list, &fmsg->item_list);

	return 0;
}

int devlink_fmsg_pair_nest_start(struct devlink_fmsg *fmsg, const char *name)
{
	int err;

	if (fmsg->putting_binary)
		return -EINVAL;

	err = devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_PAIR_NEST_START);
	if (err)
		return err;

	err = devlink_fmsg_put_name(fmsg, name);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_pair_nest_start);

int devlink_fmsg_pair_nest_end(struct devlink_fmsg *fmsg)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_pair_nest_end);

int devlink_fmsg_arr_pair_nest_start(struct devlink_fmsg *fmsg,
				     const char *name)
{
	int err;

	if (fmsg->putting_binary)
		return -EINVAL;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_ARR_NEST_START);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_arr_pair_nest_start);

int devlink_fmsg_arr_pair_nest_end(struct devlink_fmsg *fmsg)
{
	int err;

	if (fmsg->putting_binary)
		return -EINVAL;

	err = devlink_fmsg_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_arr_pair_nest_end);

int devlink_fmsg_binary_pair_nest_start(struct devlink_fmsg *fmsg,
					const char *name)
{
	int err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, name);
	if (err)
		return err;

	fmsg->putting_binary = true;
	return err;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_nest_start);

int devlink_fmsg_binary_pair_nest_end(struct devlink_fmsg *fmsg)
{
	if (!fmsg->putting_binary)
		return -EINVAL;

	fmsg->putting_binary = false;
	return devlink_fmsg_arr_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_nest_end);

static int devlink_fmsg_put_value(struct devlink_fmsg *fmsg,
				  const void *value, u16 value_len,
				  u8 value_nla_type)
{
	struct devlink_fmsg_item *item;

	if (value_len > DEVLINK_FMSG_MAX_SIZE)
		return -EMSGSIZE;

	item = kzalloc(sizeof(*item) + value_len, GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->nla_type = value_nla_type;
	item->len = value_len;
	item->attrtype = DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA;
	memcpy(&item->value, value, item->len);
	list_add_tail(&item->list, &fmsg->item_list);

	return 0;
}

int devlink_fmsg_bool_put(struct devlink_fmsg *fmsg, bool value)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_FLAG);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_bool_put);

int devlink_fmsg_u8_put(struct devlink_fmsg *fmsg, u8 value)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U8);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u8_put);

int devlink_fmsg_u32_put(struct devlink_fmsg *fmsg, u32 value)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U32);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u32_put);

int devlink_fmsg_u64_put(struct devlink_fmsg *fmsg, u64 value)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U64);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u64_put);

int devlink_fmsg_string_put(struct devlink_fmsg *fmsg, const char *value)
{
	if (fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, value, strlen(value) + 1,
				      NLA_NUL_STRING);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_string_put);

int devlink_fmsg_binary_put(struct devlink_fmsg *fmsg, const void *value,
			    u16 value_len)
{
	if (!fmsg->putting_binary)
		return -EINVAL;

	return devlink_fmsg_put_value(fmsg, value, value_len, NLA_BINARY);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_put);

int devlink_fmsg_bool_pair_put(struct devlink_fmsg *fmsg, const char *name,
			       bool value)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_bool_put(fmsg, value);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_bool_pair_put);

int devlink_fmsg_u8_pair_put(struct devlink_fmsg *fmsg, const char *name,
			     u8 value)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_u8_put(fmsg, value);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u8_pair_put);

int devlink_fmsg_u32_pair_put(struct devlink_fmsg *fmsg, const char *name,
			      u32 value)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_u32_put(fmsg, value);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u32_pair_put);

int devlink_fmsg_u64_pair_put(struct devlink_fmsg *fmsg, const char *name,
			      u64 value)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_u64_put(fmsg, value);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u64_pair_put);

int devlink_fmsg_string_pair_put(struct devlink_fmsg *fmsg, const char *name,
				 const char *value)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_string_put(fmsg, value);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_string_pair_put);

int devlink_fmsg_binary_pair_put(struct devlink_fmsg *fmsg, const char *name,
				 const void *value, u32 value_len)
{
	u32 data_size;
	int end_err;
	u32 offset;
	int err;

	err = devlink_fmsg_binary_pair_nest_start(fmsg, name);
	if (err)
		return err;

	for (offset = 0; offset < value_len; offset += data_size) {
		data_size = value_len - offset;
		if (data_size > DEVLINK_FMSG_MAX_SIZE)
			data_size = DEVLINK_FMSG_MAX_SIZE;
		err = devlink_fmsg_binary_put(fmsg, value + offset, data_size);
		if (err)
			break;
		/* Exit from loop with a break (instead of
		 * return) to make sure putting_binary is turned off in
		 * devlink_fmsg_binary_pair_nest_end
		 */
	}

	end_err = devlink_fmsg_binary_pair_nest_end(fmsg);
	if (end_err)
		err = end_err;

	return err;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_put);

static int
devlink_fmsg_item_fill_type(struct devlink_fmsg_item *msg, struct sk_buff *skb)
{
	switch (msg->nla_type) {
	case NLA_FLAG:
	case NLA_U8:
	case NLA_U32:
	case NLA_U64:
	case NLA_NUL_STRING:
	case NLA_BINARY:
		return nla_put_u8(skb, DEVLINK_ATTR_FMSG_OBJ_VALUE_TYPE,
				  msg->nla_type);
	default:
		return -EINVAL;
	}
}

static int
devlink_fmsg_item_fill_data(struct devlink_fmsg_item *msg, struct sk_buff *skb)
{
	int attrtype = DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA;
	u8 tmp;

	switch (msg->nla_type) {
	case NLA_FLAG:
		/* Always provide flag data, regardless of its value */
		tmp = *(bool *) msg->value;

		return nla_put_u8(skb, attrtype, tmp);
	case NLA_U8:
		return nla_put_u8(skb, attrtype, *(u8 *) msg->value);
	case NLA_U32:
		return nla_put_u32(skb, attrtype, *(u32 *) msg->value);
	case NLA_U64:
		return nla_put_u64_64bit(skb, attrtype, *(u64 *) msg->value,
					 DEVLINK_ATTR_PAD);
	case NLA_NUL_STRING:
		return nla_put_string(skb, attrtype, (char *) &msg->value);
	case NLA_BINARY:
		return nla_put(skb, attrtype, msg->len, (void *) &msg->value);
	default:
		return -EINVAL;
	}
}

static int
devlink_fmsg_prepare_skb(struct devlink_fmsg *fmsg, struct sk_buff *skb,
			 int *start)
{
	struct devlink_fmsg_item *item;
	struct nlattr *fmsg_nlattr;
	int i = 0;
	int err;

	fmsg_nlattr = nla_nest_start_noflag(skb, DEVLINK_ATTR_FMSG);
	if (!fmsg_nlattr)
		return -EMSGSIZE;

	list_for_each_entry(item, &fmsg->item_list, list) {
		if (i < *start) {
			i++;
			continue;
		}

		switch (item->attrtype) {
		case DEVLINK_ATTR_FMSG_OBJ_NEST_START:
		case DEVLINK_ATTR_FMSG_PAIR_NEST_START:
		case DEVLINK_ATTR_FMSG_ARR_NEST_START:
		case DEVLINK_ATTR_FMSG_NEST_END:
			err = nla_put_flag(skb, item->attrtype);
			break;
		case DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA:
			err = devlink_fmsg_item_fill_type(item, skb);
			if (err)
				break;
			err = devlink_fmsg_item_fill_data(item, skb);
			break;
		case DEVLINK_ATTR_FMSG_OBJ_NAME:
			err = nla_put_string(skb, item->attrtype,
					     (char *) &item->value);
			break;
		default:
			err = -EINVAL;
			break;
		}
		if (!err)
			*start = ++i;
		else
			break;
	}

	nla_nest_end(skb, fmsg_nlattr);
	return err;
}

static int devlink_fmsg_snd(struct devlink_fmsg *fmsg,
			    struct genl_info *info,
			    enum devlink_command cmd, int flags)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	bool last = false;
	int index = 0;
	void *hdr;
	int err;

	while (!last) {
		int tmp_index = index;

		skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
				  &devlink_nl_family, flags | NLM_F_MULTI, cmd);
		if (!hdr) {
			err = -EMSGSIZE;
			goto nla_put_failure;
		}

		err = devlink_fmsg_prepare_skb(fmsg, skb, &index);
		if (!err)
			last = true;
		else if (err != -EMSGSIZE || tmp_index == index)
			goto nla_put_failure;

		genlmsg_end(skb, hdr);
		err = genlmsg_reply(skb, info);
		if (err)
			return err;
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	return genlmsg_reply(skb, info);

nla_put_failure:
	nlmsg_free(skb);
	return err;
}

static int devlink_fmsg_dumpit(struct devlink_fmsg *fmsg, struct sk_buff *skb,
			       struct netlink_callback *cb,
			       enum devlink_command cmd)
{
	int index = cb->args[0];
	int tmp_index = index;
	void *hdr;
	int err;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, NLM_F_ACK | NLM_F_MULTI, cmd);
	if (!hdr) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	err = devlink_fmsg_prepare_skb(fmsg, skb, &index);
	if ((err && err != -EMSGSIZE) || tmp_index == index)
		goto nla_put_failure;

	cb->args[0] = index;
	genlmsg_end(skb, hdr);
	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return err;
}

struct devlink_health_reporter {
	struct list_head list;
	void *priv;
	const struct devlink_health_reporter_ops *ops;
	struct devlink *devlink;
	struct devlink_port *devlink_port;
	struct devlink_fmsg *dump_fmsg;
	struct mutex dump_lock; /* lock parallel read/write from dump buffers */
	u64 graceful_period;
	bool auto_recover;
	bool auto_dump;
	u8 health_state;
	u64 dump_ts;
	u64 dump_real_ts;
	u64 error_count;
	u64 recovery_count;
	u64 last_recovery_ts;
	refcount_t refcount;
};

void *
devlink_health_reporter_priv(struct devlink_health_reporter *reporter)
{
	return reporter->priv;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_priv);

static struct devlink_health_reporter *
__devlink_health_reporter_find_by_name(struct list_head *reporter_list,
				       struct mutex *list_lock,
				       const char *reporter_name)
{
	struct devlink_health_reporter *reporter;

	lockdep_assert_held(list_lock);
	list_for_each_entry(reporter, reporter_list, list)
		if (!strcmp(reporter->ops->name, reporter_name))
			return reporter;
	return NULL;
}

static struct devlink_health_reporter *
devlink_health_reporter_find_by_name(struct devlink *devlink,
				     const char *reporter_name)
{
	return __devlink_health_reporter_find_by_name(&devlink->reporter_list,
						      &devlink->reporters_lock,
						      reporter_name);
}

static struct devlink_health_reporter *
devlink_port_health_reporter_find_by_name(struct devlink_port *devlink_port,
					  const char *reporter_name)
{
	return __devlink_health_reporter_find_by_name(&devlink_port->reporter_list,
						      &devlink_port->reporters_lock,
						      reporter_name);
}

static struct devlink_health_reporter *
__devlink_health_reporter_create(struct devlink *devlink,
				 const struct devlink_health_reporter_ops *ops,
				 u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	if (WARN_ON(graceful_period && !ops->recover))
		return ERR_PTR(-EINVAL);

	reporter = kzalloc(sizeof(*reporter), GFP_KERNEL);
	if (!reporter)
		return ERR_PTR(-ENOMEM);

	reporter->priv = priv;
	reporter->ops = ops;
	reporter->devlink = devlink;
	reporter->graceful_period = graceful_period;
	reporter->auto_recover = !!ops->recover;
	reporter->auto_dump = !!ops->dump;
	mutex_init(&reporter->dump_lock);
	refcount_set(&reporter->refcount, 1);
	return reporter;
}

/**
 *	devlink_port_health_reporter_create - create devlink health reporter for
 *	                                      specified port instance
 *
 *	@port: devlink_port which should contain the new reporter
 *	@ops: ops
 *	@graceful_period: to avoid recovery loops, in msecs
 *	@priv: priv
 */
struct devlink_health_reporter *
devlink_port_health_reporter_create(struct devlink_port *port,
				    const struct devlink_health_reporter_ops *ops,
				    u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	mutex_lock(&port->reporters_lock);
	if (__devlink_health_reporter_find_by_name(&port->reporter_list,
						   &port->reporters_lock, ops->name)) {
		reporter = ERR_PTR(-EEXIST);
		goto unlock;
	}

	reporter = __devlink_health_reporter_create(port->devlink, ops,
						    graceful_period, priv);
	if (IS_ERR(reporter))
		goto unlock;

	reporter->devlink_port = port;
	list_add_tail(&reporter->list, &port->reporter_list);
unlock:
	mutex_unlock(&port->reporters_lock);
	return reporter;
}
EXPORT_SYMBOL_GPL(devlink_port_health_reporter_create);

/**
 *	devlink_health_reporter_create - create devlink health reporter
 *
 *	@devlink: devlink
 *	@ops: ops
 *	@graceful_period: to avoid recovery loops, in msecs
 *	@priv: priv
 */
struct devlink_health_reporter *
devlink_health_reporter_create(struct devlink *devlink,
			       const struct devlink_health_reporter_ops *ops,
			       u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	mutex_lock(&devlink->reporters_lock);
	if (devlink_health_reporter_find_by_name(devlink, ops->name)) {
		reporter = ERR_PTR(-EEXIST);
		goto unlock;
	}

	reporter = __devlink_health_reporter_create(devlink, ops,
						    graceful_period, priv);
	if (IS_ERR(reporter))
		goto unlock;

	list_add_tail(&reporter->list, &devlink->reporter_list);
unlock:
	mutex_unlock(&devlink->reporters_lock);
	return reporter;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_create);

static void
devlink_health_reporter_free(struct devlink_health_reporter *reporter)
{
	mutex_destroy(&reporter->dump_lock);
	if (reporter->dump_fmsg)
		devlink_fmsg_free(reporter->dump_fmsg);
	kfree(reporter);
}

static void
devlink_health_reporter_put(struct devlink_health_reporter *reporter)
{
	if (refcount_dec_and_test(&reporter->refcount))
		devlink_health_reporter_free(reporter);
}

static void
__devlink_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	list_del(&reporter->list);
	devlink_health_reporter_put(reporter);
}

/**
 *	devlink_health_reporter_destroy - destroy devlink health reporter
 *
 *	@reporter: devlink health reporter to destroy
 */
void
devlink_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	struct mutex *lock = &reporter->devlink->reporters_lock;

	mutex_lock(lock);
	__devlink_health_reporter_destroy(reporter);
	mutex_unlock(lock);
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_destroy);

/**
 *	devlink_port_health_reporter_destroy - destroy devlink port health reporter
 *
 *	@reporter: devlink health reporter to destroy
 */
void
devlink_port_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	struct mutex *lock = &reporter->devlink_port->reporters_lock;

	mutex_lock(lock);
	__devlink_health_reporter_destroy(reporter);
	mutex_unlock(lock);
}
EXPORT_SYMBOL_GPL(devlink_port_health_reporter_destroy);

static int
devlink_nl_health_reporter_fill(struct sk_buff *msg,
				struct devlink *devlink,
				struct devlink_health_reporter *reporter,
				enum devlink_command cmd, u32 portid,
				u32 seq, int flags)
{
	struct nlattr *reporter_attr;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	if (reporter->devlink_port) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, reporter->devlink_port->index))
			goto genlmsg_cancel;
	}
	reporter_attr = nla_nest_start_noflag(msg,
					      DEVLINK_ATTR_HEALTH_REPORTER);
	if (!reporter_attr)
		goto genlmsg_cancel;
	if (nla_put_string(msg, DEVLINK_ATTR_HEALTH_REPORTER_NAME,
			   reporter->ops->name))
		goto reporter_nest_cancel;
	if (nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_STATE,
		       reporter->health_state))
		goto reporter_nest_cancel;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_ERR_COUNT,
			      reporter->error_count, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_RECOVER_COUNT,
			      reporter->recovery_count, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->recover &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD,
			      reporter->graceful_period,
			      DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->recover &&
	    nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER,
		       reporter->auto_recover))
		goto reporter_nest_cancel;
	if (reporter->dump_fmsg &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_DUMP_TS,
			      jiffies_to_msecs(reporter->dump_ts),
			      DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->dump_fmsg &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_DUMP_TS_NS,
			      reporter->dump_real_ts, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->dump &&
	    nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP,
		       reporter->auto_dump))
		goto reporter_nest_cancel;

	nla_nest_end(msg, reporter_attr);
	genlmsg_end(msg, hdr);
	return 0;

reporter_nest_cancel:
	nla_nest_end(msg, reporter_attr);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_recover_notify(struct devlink_health_reporter *reporter,
				   enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_HEALTH_REPORTER_RECOVER);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_health_reporter_fill(msg, reporter->devlink,
					      reporter, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family,
				devlink_net(reporter->devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void
devlink_health_reporter_recovery_done(struct devlink_health_reporter *reporter)
{
	reporter->recovery_count++;
	reporter->last_recovery_ts = jiffies;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_recovery_done);

static int
devlink_health_reporter_recover(struct devlink_health_reporter *reporter,
				void *priv_ctx, struct netlink_ext_ack *extack)
{
	int err;

	if (reporter->health_state == DEVLINK_HEALTH_REPORTER_STATE_HEALTHY)
		return 0;

	if (!reporter->ops->recover)
		return -EOPNOTSUPP;

	err = reporter->ops->recover(reporter, priv_ctx, extack);
	if (err)
		return err;

	devlink_health_reporter_recovery_done(reporter);
	reporter->health_state = DEVLINK_HEALTH_REPORTER_STATE_HEALTHY;
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);

	return 0;
}

static void
devlink_health_dump_clear(struct devlink_health_reporter *reporter)
{
	if (!reporter->dump_fmsg)
		return;
	devlink_fmsg_free(reporter->dump_fmsg);
	reporter->dump_fmsg = NULL;
}

static int devlink_health_do_dump(struct devlink_health_reporter *reporter,
				  void *priv_ctx,
				  struct netlink_ext_ack *extack)
{
	int err;

	if (!reporter->ops->dump)
		return 0;

	if (reporter->dump_fmsg)
		return 0;

	reporter->dump_fmsg = devlink_fmsg_alloc();
	if (!reporter->dump_fmsg) {
		err = -ENOMEM;
		return err;
	}

	err = devlink_fmsg_obj_nest_start(reporter->dump_fmsg);
	if (err)
		goto dump_err;

	err = reporter->ops->dump(reporter, reporter->dump_fmsg,
				  priv_ctx, extack);
	if (err)
		goto dump_err;

	err = devlink_fmsg_obj_nest_end(reporter->dump_fmsg);
	if (err)
		goto dump_err;

	reporter->dump_ts = jiffies;
	reporter->dump_real_ts = ktime_get_real_ns();

	return 0;

dump_err:
	devlink_health_dump_clear(reporter);
	return err;
}

int devlink_health_report(struct devlink_health_reporter *reporter,
			  const char *msg, void *priv_ctx)
{
	enum devlink_health_reporter_state prev_health_state;
	struct devlink *devlink = reporter->devlink;
	unsigned long recover_ts_threshold;

	/* write a log message of the current error */
	WARN_ON(!msg);
	trace_devlink_health_report(devlink, reporter->ops->name, msg);
	reporter->error_count++;
	prev_health_state = reporter->health_state;
	reporter->health_state = DEVLINK_HEALTH_REPORTER_STATE_ERROR;
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);

	/* abort if the previous error wasn't recovered */
	recover_ts_threshold = reporter->last_recovery_ts +
			       msecs_to_jiffies(reporter->graceful_period);
	if (reporter->auto_recover &&
	    (prev_health_state != DEVLINK_HEALTH_REPORTER_STATE_HEALTHY ||
	     (reporter->last_recovery_ts && reporter->recovery_count &&
	      time_is_after_jiffies(recover_ts_threshold)))) {
		trace_devlink_health_recover_aborted(devlink,
						     reporter->ops->name,
						     reporter->health_state,
						     jiffies -
						     reporter->last_recovery_ts);
		return -ECANCELED;
	}

	reporter->health_state = DEVLINK_HEALTH_REPORTER_STATE_ERROR;

	if (reporter->auto_dump) {
		mutex_lock(&reporter->dump_lock);
		/* store current dump of current error, for later analysis */
		devlink_health_do_dump(reporter, priv_ctx, NULL);
		mutex_unlock(&reporter->dump_lock);
	}

	if (reporter->auto_recover)
		return devlink_health_reporter_recover(reporter,
						       priv_ctx, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(devlink_health_report);

static struct devlink_health_reporter *
devlink_health_reporter_get_from_attrs(struct devlink *devlink,
				       struct nlattr **attrs)
{
	struct devlink_health_reporter *reporter;
	struct devlink_port *devlink_port;
	char *reporter_name;

	if (!attrs[DEVLINK_ATTR_HEALTH_REPORTER_NAME])
		return NULL;

	reporter_name = nla_data(attrs[DEVLINK_ATTR_HEALTH_REPORTER_NAME]);
	devlink_port = devlink_port_get_from_attrs(devlink, attrs);
	if (IS_ERR(devlink_port)) {
		mutex_lock(&devlink->reporters_lock);
		reporter = devlink_health_reporter_find_by_name(devlink, reporter_name);
		if (reporter)
			refcount_inc(&reporter->refcount);
		mutex_unlock(&devlink->reporters_lock);
	} else {
		mutex_lock(&devlink_port->reporters_lock);
		reporter = devlink_port_health_reporter_find_by_name(devlink_port, reporter_name);
		if (reporter)
			refcount_inc(&reporter->refcount);
		mutex_unlock(&devlink_port->reporters_lock);
	}

	return reporter;
}

static struct devlink_health_reporter *
devlink_health_reporter_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	return devlink_health_reporter_get_from_attrs(devlink, info->attrs);
}

static struct devlink_health_reporter *
devlink_health_reporter_get_from_cb(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct devlink_health_reporter *reporter;
	struct nlattr **attrs = info->attrs;
	struct devlink *devlink;

	mutex_lock(&devlink_mutex);
	devlink = devlink_get_from_attrs(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink))
		goto unlock;

	reporter = devlink_health_reporter_get_from_attrs(devlink, attrs);
	mutex_unlock(&devlink_mutex);
	return reporter;
unlock:
	mutex_unlock(&devlink_mutex);
	return NULL;
}

void
devlink_health_reporter_state_update(struct devlink_health_reporter *reporter,
				     enum devlink_health_reporter_state state)
{
	if (WARN_ON(state != DEVLINK_HEALTH_REPORTER_STATE_HEALTHY &&
		    state != DEVLINK_HEALTH_REPORTER_STATE_ERROR))
		return;

	if (reporter->health_state == state)
		return;

	reporter->health_state = state;
	trace_devlink_health_reporter_state_update(reporter->devlink,
						   reporter->ops->name, state);
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_state_update);

static int devlink_nl_cmd_health_reporter_get_doit(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	struct sk_buff *msg;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	err = devlink_nl_health_reporter_fill(msg, devlink, reporter,
					      DEVLINK_CMD_HEALTH_REPORTER_GET,
					      info->snd_portid, info->snd_seq,
					      0);
	if (err) {
		nlmsg_free(msg);
		goto out;
	}

	err = genlmsg_reply(msg, info);
out:
	devlink_health_reporter_put(reporter);
	return err;
}

static int
devlink_nl_cmd_health_reporter_get_dumpit(struct sk_buff *msg,
					  struct netlink_callback *cb)
{
	struct devlink_health_reporter *reporter;
	struct devlink_port *port;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->reporters_lock);
		list_for_each_entry(reporter, &devlink->reporter_list,
				    list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_health_reporter_fill(msg, devlink,
							      reporter,
							      DEVLINK_CMD_HEALTH_REPORTER_GET,
							      NETLINK_CB(cb->skb).portid,
							      cb->nlh->nlmsg_seq,
							      NLM_F_MULTI);
			if (err) {
				mutex_unlock(&devlink->reporters_lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->reporters_lock);
	}

	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(port, &devlink->port_list, list) {
			mutex_lock(&port->reporters_lock);
			list_for_each_entry(reporter, &port->reporter_list, list) {
				if (idx < start) {
					idx++;
					continue;
				}
				err = devlink_nl_health_reporter_fill(msg, devlink, reporter,
								      DEVLINK_CMD_HEALTH_REPORTER_GET,
								      NETLINK_CB(cb->skb).portid,
								      cb->nlh->nlmsg_seq,
								      NLM_F_MULTI);
				if (err) {
					mutex_unlock(&port->reporters_lock);
					mutex_unlock(&devlink->lock);
					goto out;
				}
				idx++;
			}
			mutex_unlock(&port->reporters_lock);
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int
devlink_nl_cmd_health_reporter_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->recover &&
	    (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] ||
	     info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER])) {
		err = -EOPNOTSUPP;
		goto out;
	}
	if (!reporter->ops->dump &&
	    info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP]) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD])
		reporter->graceful_period =
			nla_get_u64(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD]);

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER])
		reporter->auto_recover =
			nla_get_u8(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER]);

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP])
		reporter->auto_dump =
		nla_get_u8(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP]);

	devlink_health_reporter_put(reporter);
	return 0;
out:
	devlink_health_reporter_put(reporter);
	return err;
}

static int devlink_nl_cmd_health_reporter_recover_doit(struct sk_buff *skb,
						       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	err = devlink_health_reporter_recover(reporter, NULL, info->extack);

	devlink_health_reporter_put(reporter);
	return err;
}

static int devlink_nl_cmd_health_reporter_diagnose_doit(struct sk_buff *skb,
							struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	struct devlink_fmsg *fmsg;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->diagnose) {
		devlink_health_reporter_put(reporter);
		return -EOPNOTSUPP;
	}

	fmsg = devlink_fmsg_alloc();
	if (!fmsg) {
		devlink_health_reporter_put(reporter);
		return -ENOMEM;
	}

	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		goto out;

	err = reporter->ops->diagnose(reporter, fmsg, info->extack);
	if (err)
		goto out;

	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		goto out;

	err = devlink_fmsg_snd(fmsg, info,
			       DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE, 0);

out:
	devlink_fmsg_free(fmsg);
	devlink_health_reporter_put(reporter);
	return err;
}

static int
devlink_nl_cmd_health_reporter_dump_get_dumpit(struct sk_buff *skb,
					       struct netlink_callback *cb)
{
	struct devlink_health_reporter *reporter;
	u64 start = cb->args[0];
	int err;

	reporter = devlink_health_reporter_get_from_cb(cb);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->dump) {
		err = -EOPNOTSUPP;
		goto out;
	}
	mutex_lock(&reporter->dump_lock);
	if (!start) {
		err = devlink_health_do_dump(reporter, NULL, cb->extack);
		if (err)
			goto unlock;
		cb->args[1] = reporter->dump_ts;
	}
	if (!reporter->dump_fmsg || cb->args[1] != reporter->dump_ts) {
		NL_SET_ERR_MSG_MOD(cb->extack, "Dump trampled, please retry");
		err = -EAGAIN;
		goto unlock;
	}

	err = devlink_fmsg_dumpit(reporter->dump_fmsg, skb, cb,
				  DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET);
unlock:
	mutex_unlock(&reporter->dump_lock);
out:
	devlink_health_reporter_put(reporter);
	return err;
}

static int
devlink_nl_cmd_health_reporter_dump_clear_doit(struct sk_buff *skb,
					       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->dump) {
		devlink_health_reporter_put(reporter);
		return -EOPNOTSUPP;
	}

	mutex_lock(&reporter->dump_lock);
	devlink_health_dump_clear(reporter);
	mutex_unlock(&reporter->dump_lock);
	devlink_health_reporter_put(reporter);
	return 0;
}

static int devlink_nl_cmd_health_reporter_test_doit(struct sk_buff *skb,
						    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->test) {
		devlink_health_reporter_put(reporter);
		return -EOPNOTSUPP;
	}

	err = reporter->ops->test(reporter, info->extack);

	devlink_health_reporter_put(reporter);
	return err;
}

struct devlink_stats {
	u64 rx_bytes;
	u64 rx_packets;
	struct u64_stats_sync syncp;
};

/**
 * struct devlink_trap_policer_item - Packet trap policer attributes.
 * @policer: Immutable packet trap policer attributes.
 * @rate: Rate in packets / sec.
 * @burst: Burst size in packets.
 * @list: trap_policer_list member.
 *
 * Describes packet trap policer attributes. Created by devlink during trap
 * policer registration.
 */
struct devlink_trap_policer_item {
	const struct devlink_trap_policer *policer;
	u64 rate;
	u64 burst;
	struct list_head list;
};

/**
 * struct devlink_trap_group_item - Packet trap group attributes.
 * @group: Immutable packet trap group attributes.
 * @policer_item: Associated policer item. Can be NULL.
 * @list: trap_group_list member.
 * @stats: Trap group statistics.
 *
 * Describes packet trap group attributes. Created by devlink during trap
 * group registration.
 */
struct devlink_trap_group_item {
	const struct devlink_trap_group *group;
	struct devlink_trap_policer_item *policer_item;
	struct list_head list;
	struct devlink_stats __percpu *stats;
};

/**
 * struct devlink_trap_item - Packet trap attributes.
 * @trap: Immutable packet trap attributes.
 * @group_item: Associated group item.
 * @list: trap_list member.
 * @action: Trap action.
 * @stats: Trap statistics.
 * @priv: Driver private information.
 *
 * Describes both mutable and immutable packet trap attributes. Created by
 * devlink during trap registration and used for all trap related operations.
 */
struct devlink_trap_item {
	const struct devlink_trap *trap;
	struct devlink_trap_group_item *group_item;
	struct list_head list;
	enum devlink_trap_action action;
	struct devlink_stats __percpu *stats;
	void *priv;
};

static struct devlink_trap_policer_item *
devlink_trap_policer_item_lookup(struct devlink *devlink, u32 id)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (policer_item->policer->id == id)
			return policer_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (!strcmp(trap_item->trap->name, name))
			return trap_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_get_from_info(struct devlink *devlink,
				struct genl_info *info)
{
	struct nlattr *attr;

	if (!info->attrs[DEVLINK_ATTR_TRAP_NAME])
		return NULL;
	attr = info->attrs[DEVLINK_ATTR_TRAP_NAME];

	return devlink_trap_item_lookup(devlink, nla_data(attr));
}

static int
devlink_trap_action_get_from_info(struct genl_info *info,
				  enum devlink_trap_action *p_trap_action)
{
	u8 val;

	val = nla_get_u8(info->attrs[DEVLINK_ATTR_TRAP_ACTION]);
	switch (val) {
	case DEVLINK_TRAP_ACTION_DROP:
	case DEVLINK_TRAP_ACTION_TRAP:
	case DEVLINK_TRAP_ACTION_MIRROR:
		*p_trap_action = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int devlink_trap_metadata_put(struct sk_buff *msg,
				     const struct devlink_trap *trap)
{
	struct nlattr *attr;

	attr = nla_nest_start(msg, DEVLINK_ATTR_TRAP_METADATA);
	if (!attr)
		return -EMSGSIZE;

	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_IN_PORT))
		goto nla_put_failure;
	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_FA_COOKIE))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static void devlink_trap_stats_read(struct devlink_stats __percpu *trap_stats,
				    struct devlink_stats *stats)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(i) {
		struct devlink_stats *cpu_stats;
		u64 rx_packets, rx_bytes;
		unsigned int start;

		cpu_stats = per_cpu_ptr(trap_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			rx_packets = cpu_stats->rx_packets;
			rx_bytes = cpu_stats->rx_bytes;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes += rx_bytes;
	}
}

static int devlink_trap_stats_put(struct sk_buff *msg,
				  struct devlink_stats __percpu *trap_stats)
{
	struct devlink_stats stats;
	struct nlattr *attr;

	devlink_trap_stats_read(trap_stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      stats.rx_packets, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      stats.rx_bytes, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_nl_trap_fill(struct sk_buff *msg, struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags)
{
	struct devlink_trap_group_item *group_item = trap_item->group_item;
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_NAME, trap_item->trap->name))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_TYPE, trap_item->trap->type))
		goto nla_put_failure;

	if (trap_item->trap->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_ACTION, trap_item->action))
		goto nla_put_failure;

	err = devlink_trap_metadata_put(msg, trap_item->trap);
	if (err)
		goto nla_put_failure;

	err = devlink_trap_stats_put(msg, trap_item->stats);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_fill(msg, devlink, trap_item,
				   DEVLINK_CMD_TRAP_NEW, info->snd_portid,
				   info->snd_seq, 0);
	if (err)
		goto err_trap_fill;

	return genlmsg_reply(msg, info);

err_trap_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_cmd_trap_get_dumpit(struct sk_buff *msg,
					  struct netlink_callback *cb)
{
	struct devlink_trap_item *trap_item;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(trap_item, &devlink->trap_list, list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_trap_fill(msg, devlink, trap_item,
						   DEVLINK_CMD_TRAP_NEW,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq,
						   NLM_F_MULTI);
			if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int __devlink_trap_action_set(struct devlink *devlink,
				     struct devlink_trap_item *trap_item,
				     enum devlink_trap_action trap_action,
				     struct netlink_ext_ack *extack)
{
	int err;

	if (trap_item->action != trap_action &&
	    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot change action of non-drop traps. Skipping");
		return 0;
	}

	err = devlink->ops->trap_action_set(devlink, trap_item->trap,
					    trap_action, extack);
	if (err)
		return err;

	trap_item->action = trap_action;

	return 0;
}

static int devlink_trap_action_set(struct devlink *devlink,
				   struct devlink_trap_item *trap_item,
				   struct genl_info *info)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	return __devlink_trap_action_set(devlink, trap_item, trap_action,
					 info->extack);
}

static int devlink_nl_cmd_trap_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap");
		return -ENOENT;
	}

	return devlink_trap_action_set(devlink, trap_item, info);
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (!strcmp(group_item->group->name, name))
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup_by_id(struct devlink *devlink, u16 id)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (group_item->group->id == id)
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	char *name;

	if (!info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME])
		return NULL;
	name = nla_data(info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME]);

	return devlink_trap_group_item_lookup(devlink, name);
}

static int
devlink_nl_trap_group_fill(struct sk_buff *msg, struct devlink *devlink,
			   const struct devlink_trap_group_item *group_item,
			   enum devlink_command cmd, u32 portid, u32 seq,
			   int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (group_item->group->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (group_item->policer_item &&
	    nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			group_item->policer_item->policer->id))
		goto nla_put_failure;

	err = devlink_trap_stats_put(msg, group_item->stats);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_group_get_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item,
					 DEVLINK_CMD_TRAP_GROUP_NEW,
					 info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_group_fill;

	return genlmsg_reply(msg, info);

err_trap_group_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_cmd_trap_group_get_dumpit(struct sk_buff *msg,
						struct netlink_callback *cb)
{
	enum devlink_command cmd = DEVLINK_CMD_TRAP_GROUP_NEW;
	struct devlink_trap_group_item *group_item;
	u32 portid = NETLINK_CB(cb->skb).portid;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(group_item, &devlink->trap_group_list,
				    list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_trap_group_fill(msg, devlink,
							 group_item, cmd,
							 portid,
							 cb->nlh->nlmsg_seq,
							 NLM_F_MULTI);
			if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int
__devlink_trap_group_action_set(struct devlink *devlink,
				struct devlink_trap_group_item *group_item,
				enum devlink_trap_action trap_action,
				struct netlink_ext_ack *extack)
{
	const char *group_name = group_item->group->name;
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink->ops->trap_group_action_set) {
		err = devlink->ops->trap_group_action_set(devlink, group_item->group,
							  trap_action, extack);
		if (err)
			return err;

		list_for_each_entry(trap_item, &devlink->trap_list, list) {
			if (strcmp(trap_item->group_item->group->name, group_name))
				continue;
			if (trap_item->action != trap_action &&
			    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP)
				continue;
			trap_item->action = trap_action;
		}

		return 0;
	}

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (strcmp(trap_item->group_item->group->name, group_name))
			continue;
		err = __devlink_trap_action_set(devlink, trap_item,
						trap_action, extack);
		if (err)
			return err;
	}

	return 0;
}

static int
devlink_trap_group_action_set(struct devlink *devlink,
			      struct devlink_trap_group_item *group_item,
			      struct genl_info *info, bool *p_modified)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	err = __devlink_trap_group_action_set(devlink, group_item, trap_action,
					      info->extack);
	if (err)
		return err;

	*p_modified = true;

	return 0;
}

static int devlink_trap_group_set(struct devlink *devlink,
				  struct devlink_trap_group_item *group_item,
				  struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	const struct devlink_trap_policer *policer;
	struct nlattr **attrs = info->attrs;
	int err;

	if (!attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return 0;

	if (!devlink->ops->trap_group_set)
		return -EOPNOTSUPP;

	policer_item = group_item->policer_item;
	if (attrs[DEVLINK_ATTR_TRAP_POLICER_ID]) {
		u32 policer_id;

		policer_id = nla_get_u32(attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);
		policer_item = devlink_trap_policer_item_lookup(devlink,
								policer_id);
		if (policer_id && !policer_item) {
			NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap policer");
			return -ENOENT;
		}
	}
	policer = policer_item ? policer_item->policer : NULL;

	err = devlink->ops->trap_group_set(devlink, group_item->group, policer,
					   extack);
	if (err)
		return err;

	group_item->policer_item = policer_item;

	return 0;
}

static int devlink_nl_cmd_trap_group_set_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	bool modified = false;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	err = devlink_trap_group_action_set(devlink, group_item, info,
					    &modified);
	if (err)
		return err;

	err = devlink_trap_group_set(devlink, group_item, info);
	if (err)
		goto err_trap_group_set;

	return 0;

err_trap_group_set:
	if (modified)
		NL_SET_ERR_MSG_MOD(extack, "Trap group set failed, but some changes were committed already");
	return err;
}

static struct devlink_trap_policer_item *
devlink_trap_policer_item_get_from_info(struct devlink *devlink,
					struct genl_info *info)
{
	u32 id;

	if (!info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return NULL;
	id = nla_get_u32(info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);

	return devlink_trap_policer_item_lookup(devlink, id);
}

static int
devlink_trap_policer_stats_put(struct sk_buff *msg, struct devlink *devlink,
			       const struct devlink_trap_policer *policer)
{
	struct nlattr *attr;
	u64 drops;
	int err;

	if (!devlink->ops->trap_policer_counter_get)
		return 0;

	err = devlink->ops->trap_policer_counter_get(devlink, policer, &drops);
	if (err)
		return err;

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int
devlink_nl_trap_policer_fill(struct sk_buff *msg, struct devlink *devlink,
			     const struct devlink_trap_policer_item *policer_item,
			     enum devlink_command cmd, u32 portid, u32 seq,
			     int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			policer_item->policer->id))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_RATE,
			      policer_item->rate, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_BURST,
			      policer_item->burst, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	err = devlink_trap_policer_stats_put(msg, devlink,
					     policer_item->policer);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_policer_get_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
					   DEVLINK_CMD_TRAP_POLICER_NEW,
					   info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_policer_fill;

	return genlmsg_reply(msg, info);

err_trap_policer_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_cmd_trap_policer_get_dumpit(struct sk_buff *msg,
						  struct netlink_callback *cb)
{
	enum devlink_command cmd = DEVLINK_CMD_TRAP_POLICER_NEW;
	struct devlink_trap_policer_item *policer_item;
	u32 portid = NETLINK_CB(cb->skb).portid;
	struct devlink *devlink;
	int start = cb->args[0];
	int idx = 0;
	int err;

	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (!net_eq(devlink_net(devlink), sock_net(msg->sk)))
			continue;
		mutex_lock(&devlink->lock);
		list_for_each_entry(policer_item, &devlink->trap_policer_list,
				    list) {
			if (idx < start) {
				idx++;
				continue;
			}
			err = devlink_nl_trap_policer_fill(msg, devlink,
							   policer_item, cmd,
							   portid,
							   cb->nlh->nlmsg_seq,
							   NLM_F_MULTI);
			if (err) {
				mutex_unlock(&devlink->lock);
				goto out;
			}
			idx++;
		}
		mutex_unlock(&devlink->lock);
	}
out:
	mutex_unlock(&devlink_mutex);

	cb->args[0] = idx;
	return msg->len;
}

static int
devlink_trap_policer_set(struct devlink *devlink,
			 struct devlink_trap_policer_item *policer_item,
			 struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **attrs = info->attrs;
	u64 rate, burst;
	int err;

	rate = policer_item->rate;
	burst = policer_item->burst;

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_RATE])
		rate = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_RATE]);

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_BURST])
		burst = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_BURST]);

	if (rate < policer_item->policer->min_rate) {
		NL_SET_ERR_MSG_MOD(extack, "Policer rate lower than limit");
		return -EINVAL;
	}

	if (rate > policer_item->policer->max_rate) {
		NL_SET_ERR_MSG_MOD(extack, "Policer rate higher than limit");
		return -EINVAL;
	}

	if (burst < policer_item->policer->min_burst) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size lower than limit");
		return -EINVAL;
	}

	if (burst > policer_item->policer->max_burst) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size higher than limit");
		return -EINVAL;
	}

	err = devlink->ops->trap_policer_set(devlink, policer_item->policer,
					     rate, burst, info->extack);
	if (err)
		return err;

	policer_item->rate = rate;
	policer_item->burst = burst;

	return 0;
}

static int devlink_nl_cmd_trap_policer_set_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	if (!devlink->ops->trap_policer_set)
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG_MOD(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	return devlink_trap_policer_set(devlink, policer_item, info);
}

static const struct nla_policy devlink_nl_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_UNSPEC] = { .strict_start_type =
		DEVLINK_ATTR_TRAP_POLICER_ID },
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32 },
	[DEVLINK_ATTR_PORT_TYPE] = NLA_POLICY_RANGE(NLA_U16, DEVLINK_PORT_TYPE_AUTO,
						    DEVLINK_PORT_TYPE_IB),
	[DEVLINK_ATTR_PORT_SPLIT_COUNT] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16 },
	[DEVLINK_ATTR_SB_POOL_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_SB_POOL_SIZE] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_SB_THRESHOLD] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_TC_INDEX] = { .type = NLA_U16 },
	[DEVLINK_ATTR_ESWITCH_MODE] = NLA_POLICY_RANGE(NLA_U16, DEVLINK_ESWITCH_MODE_LEGACY,
						       DEVLINK_ESWITCH_MODE_SWITCHDEV),
	[DEVLINK_ATTR_ESWITCH_INLINE_MODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_ESWITCH_ENCAP_MODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED] = { .type = NLA_U8 },
	[DEVLINK_ATTR_RESOURCE_ID] = { .type = NLA_U64},
	[DEVLINK_ATTR_RESOURCE_SIZE] = { .type = NLA_U64},
	[DEVLINK_ATTR_PARAM_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_PARAM_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_PARAM_VALUE_CMODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_REGION_CHUNK_ADDR] = { .type = NLA_U64 },
	[DEVLINK_ATTR_REGION_CHUNK_LEN] = { .type = NLA_U64 },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] = { .type = NLA_U64 },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER] = { .type = NLA_U8 },
	[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK] =
		NLA_POLICY_BITFIELD32(DEVLINK_SUPPORTED_FLASH_OVERWRITE_SECTIONS),
	[DEVLINK_ATTR_TRAP_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_TRAP_ACTION] = { .type = NLA_U8 },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_NETNS_PID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_NETNS_FD] = { .type = NLA_U32 },
	[DEVLINK_ATTR_NETNS_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP] = { .type = NLA_U8 },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_TRAP_POLICER_RATE] = { .type = NLA_U64 },
	[DEVLINK_ATTR_TRAP_POLICER_BURST] = { .type = NLA_U64 },
	[DEVLINK_ATTR_PORT_FUNCTION] = { .type = NLA_NESTED },
	[DEVLINK_ATTR_RELOAD_ACTION] = NLA_POLICY_RANGE(NLA_U8, DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
							DEVLINK_RELOAD_ACTION_MAX),
	[DEVLINK_ATTR_RELOAD_LIMITS] = NLA_POLICY_BITFIELD32(DEVLINK_RELOAD_LIMITS_VALID_MASK),
	[DEVLINK_ATTR_PORT_FLAVOUR] = { .type = NLA_U16 },
	[DEVLINK_ATTR_PORT_PCI_PF_NUMBER] = { .type = NLA_U16 },
	[DEVLINK_ATTR_PORT_PCI_SF_NUMBER] = { .type = NLA_U32 },
	[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER] = { .type = NLA_U32 },
};

static const struct genl_small_ops devlink_nl_ops[] = {
	{
		.cmd = DEVLINK_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_get_doit,
		.dumpit = devlink_nl_cmd_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_get_doit,
		.dumpit = devlink_nl_cmd_port_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_PORT_SPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_split_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_PORT_UNSPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_unsplit_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_PORT_NEW,
		.doit = devlink_nl_cmd_port_new_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_PORT_DEL,
		.doit = devlink_nl_cmd_port_del_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_SB_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_get_doit,
		.dumpit = devlink_nl_cmd_sb_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_POOL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_pool_get_doit,
		.dumpit = devlink_nl_cmd_sb_pool_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_PORT_POOL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_port_pool_get_doit,
		.dumpit = devlink_nl_cmd_sb_port_pool_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_PORT_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_port_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_SB_TC_POOL_BIND_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_tc_pool_bind_get_doit,
		.dumpit = devlink_nl_cmd_sb_tc_pool_bind_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_TC_POOL_BIND_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_tc_pool_bind_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_SB_OCC_SNAPSHOT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_occ_snapshot_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_OCC_MAX_CLEAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_occ_max_clear_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_ESWITCH_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_eswitch_get_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_ESWITCH_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_eswitch_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_TABLE_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_table_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_ENTRIES_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_entries_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_HEADERS_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_headers_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_table_counters_set,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RESOURCE_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_resource_set,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RESOURCE_DUMP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_resource_dump,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_RELOAD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_reload,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_PARAM_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_param_get_doit,
		.dumpit = devlink_nl_cmd_param_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PARAM_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_param_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PORT_PARAM_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_param_get_doit,
		.dumpit = devlink_nl_cmd_port_param_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_PARAM_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_param_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_REGION_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_get_doit,
		.dumpit = devlink_nl_cmd_region_get_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_NEW,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_new,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_del,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_READ,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = devlink_nl_cmd_region_read_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_INFO_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_info_get_doit,
		.dumpit = devlink_nl_cmd_info_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_get_doit,
		.dumpit = devlink_nl_cmd_health_reporter_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_RECOVER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_recover_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_diagnose_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = devlink_nl_cmd_health_reporter_dump_get_dumpit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_dump_clear_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_TEST,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_test_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT |
				  DEVLINK_NL_FLAG_NO_LOCK,
	},
	{
		.cmd = DEVLINK_CMD_FLASH_UPDATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_flash_update,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GET,
		.doit = devlink_nl_cmd_trap_get_doit,
		.dumpit = devlink_nl_cmd_trap_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_SET,
		.doit = devlink_nl_cmd_trap_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GROUP_GET,
		.doit = devlink_nl_cmd_trap_group_get_doit,
		.dumpit = devlink_nl_cmd_trap_group_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GROUP_SET,
		.doit = devlink_nl_cmd_trap_group_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_POLICER_GET,
		.doit = devlink_nl_cmd_trap_policer_get_doit,
		.dumpit = devlink_nl_cmd_trap_policer_get_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_POLICER_SET,
		.doit = devlink_nl_cmd_trap_policer_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family devlink_nl_family __ro_after_init = {
	.name		= DEVLINK_GENL_NAME,
	.version	= DEVLINK_GENL_VERSION,
	.maxattr	= DEVLINK_ATTR_MAX,
	.policy = devlink_nl_policy,
	.netnsok	= true,
	.pre_doit	= devlink_nl_pre_doit,
	.post_doit	= devlink_nl_post_doit,
	.module		= THIS_MODULE,
	.small_ops	= devlink_nl_ops,
	.n_small_ops	= ARRAY_SIZE(devlink_nl_ops),
	.mcgrps		= devlink_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(devlink_nl_mcgrps),
};

static bool devlink_reload_actions_valid(const struct devlink_ops *ops)
{
	const struct devlink_reload_combination *comb;
	int i;

	if (!devlink_reload_supported(ops)) {
		if (WARN_ON(ops->reload_actions))
			return false;
		return true;
	}

	if (WARN_ON(!ops->reload_actions ||
		    ops->reload_actions & BIT(DEVLINK_RELOAD_ACTION_UNSPEC) ||
		    ops->reload_actions >= BIT(__DEVLINK_RELOAD_ACTION_MAX)))
		return false;

	if (WARN_ON(ops->reload_limits & BIT(DEVLINK_RELOAD_LIMIT_UNSPEC) ||
		    ops->reload_limits >= BIT(__DEVLINK_RELOAD_LIMIT_MAX)))
		return false;

	for (i = 0; i < ARRAY_SIZE(devlink_reload_invalid_combinations); i++)  {
		comb = &devlink_reload_invalid_combinations[i];
		if (ops->reload_actions == BIT(comb->action) &&
		    ops->reload_limits == BIT(comb->limit))
			return false;
	}
	return true;
}

/**
 *	devlink_alloc - Allocate new devlink instance resources
 *
 *	@ops: ops
 *	@priv_size: size of user private data
 *
 *	Allocate new devlink instance resources, including devlink index
 *	and name.
 */
struct devlink *devlink_alloc(const struct devlink_ops *ops, size_t priv_size)
{
	struct devlink *devlink;

	if (WARN_ON(!ops))
		return NULL;

	if (!devlink_reload_actions_valid(ops))
		return NULL;

	devlink = kzalloc(sizeof(*devlink) + priv_size, GFP_KERNEL);
	if (!devlink)
		return NULL;
	devlink->ops = ops;
	xa_init_flags(&devlink->snapshot_ids, XA_FLAGS_ALLOC);
	__devlink_net_set(devlink, &init_net);
	INIT_LIST_HEAD(&devlink->port_list);
	INIT_LIST_HEAD(&devlink->sb_list);
	INIT_LIST_HEAD_RCU(&devlink->dpipe_table_list);
	INIT_LIST_HEAD(&devlink->resource_list);
	INIT_LIST_HEAD(&devlink->param_list);
	INIT_LIST_HEAD(&devlink->region_list);
	INIT_LIST_HEAD(&devlink->reporter_list);
	INIT_LIST_HEAD(&devlink->trap_list);
	INIT_LIST_HEAD(&devlink->trap_group_list);
	INIT_LIST_HEAD(&devlink->trap_policer_list);
	mutex_init(&devlink->lock);
	mutex_init(&devlink->reporters_lock);
	return devlink;
}
EXPORT_SYMBOL_GPL(devlink_alloc);

/**
 *	devlink_register - Register devlink instance
 *
 *	@devlink: devlink
 *	@dev: parent device
 */
int devlink_register(struct devlink *devlink, struct device *dev)
{
	devlink->dev = dev;
	devlink->registered = true;
	mutex_lock(&devlink_mutex);
	list_add_tail(&devlink->list, &devlink_list);
	devlink_notify(devlink, DEVLINK_CMD_NEW);
	mutex_unlock(&devlink_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(devlink_register);

/**
 *	devlink_unregister - Unregister devlink instance
 *
 *	@devlink: devlink
 */
void devlink_unregister(struct devlink *devlink)
{
	mutex_lock(&devlink_mutex);
	WARN_ON(devlink_reload_supported(devlink->ops) &&
		devlink->reload_enabled);
	devlink_notify(devlink, DEVLINK_CMD_DEL);
	list_del(&devlink->list);
	mutex_unlock(&devlink_mutex);
}
EXPORT_SYMBOL_GPL(devlink_unregister);

/**
 *	devlink_reload_enable - Enable reload of devlink instance
 *
 *	@devlink: devlink
 *
 *	Should be called at end of device initialization
 *	process when reload operation is supported.
 */
void devlink_reload_enable(struct devlink *devlink)
{
	mutex_lock(&devlink_mutex);
	devlink->reload_enabled = true;
	mutex_unlock(&devlink_mutex);
}
EXPORT_SYMBOL_GPL(devlink_reload_enable);

/**
 *	devlink_reload_disable - Disable reload of devlink instance
 *
 *	@devlink: devlink
 *
 *	Should be called at the beginning of device cleanup
 *	process when reload operation is supported.
 */
void devlink_reload_disable(struct devlink *devlink)
{
	mutex_lock(&devlink_mutex);
	/* Mutex is taken which ensures that no reload operation is in
	 * progress while setting up forbidded flag.
	 */
	devlink->reload_enabled = false;
	mutex_unlock(&devlink_mutex);
}
EXPORT_SYMBOL_GPL(devlink_reload_disable);

/**
 *	devlink_free - Free devlink instance resources
 *
 *	@devlink: devlink
 */
void devlink_free(struct devlink *devlink)
{
	mutex_destroy(&devlink->reporters_lock);
	mutex_destroy(&devlink->lock);
	WARN_ON(!list_empty(&devlink->trap_policer_list));
	WARN_ON(!list_empty(&devlink->trap_group_list));
	WARN_ON(!list_empty(&devlink->trap_list));
	WARN_ON(!list_empty(&devlink->reporter_list));
	WARN_ON(!list_empty(&devlink->region_list));
	WARN_ON(!list_empty(&devlink->param_list));
	WARN_ON(!list_empty(&devlink->resource_list));
	WARN_ON(!list_empty(&devlink->dpipe_table_list));
	WARN_ON(!list_empty(&devlink->sb_list));
	WARN_ON(!list_empty(&devlink->port_list));

	xa_destroy(&devlink->snapshot_ids);

	kfree(devlink);
}
EXPORT_SYMBOL_GPL(devlink_free);

static void devlink_port_type_warn(struct work_struct *work)
{
	WARN(true, "Type was not set for devlink port.");
}

static bool devlink_port_type_should_warn(struct devlink_port *devlink_port)
{
	/* Ignore CPU and DSA flavours. */
	return devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_CPU &&
	       devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_DSA &&
	       devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_UNUSED;
}

#define DEVLINK_PORT_TYPE_WARN_TIMEOUT (HZ * 3600)

static void devlink_port_type_warn_schedule(struct devlink_port *devlink_port)
{
	if (!devlink_port_type_should_warn(devlink_port))
		return;
	/* Schedule a work to WARN in case driver does not set port
	 * type within timeout.
	 */
	schedule_delayed_work(&devlink_port->type_warn_dw,
			      DEVLINK_PORT_TYPE_WARN_TIMEOUT);
}

static void devlink_port_type_warn_cancel(struct devlink_port *devlink_port)
{
	if (!devlink_port_type_should_warn(devlink_port))
		return;
	cancel_delayed_work_sync(&devlink_port->type_warn_dw);
}

/**
 *	devlink_port_register - Register devlink port
 *
 *	@devlink: devlink
 *	@devlink_port: devlink port
 *	@port_index: driver-specific numerical identifier of the port
 *
 *	Register devlink port with provided port index. User can use
 *	any indexing, even hw-related one. devlink_port structure
 *	is convenient to be embedded inside user driver private structure.
 *	Note that the caller should take care of zeroing the devlink_port
 *	structure.
 */
int devlink_port_register(struct devlink *devlink,
			  struct devlink_port *devlink_port,
			  unsigned int port_index)
{
	mutex_lock(&devlink->lock);
	if (devlink_port_index_exists(devlink, port_index)) {
		mutex_unlock(&devlink->lock);
		return -EEXIST;
	}
	devlink_port->devlink = devlink;
	devlink_port->index = port_index;
	devlink_port->registered = true;
	spin_lock_init(&devlink_port->type_lock);
	INIT_LIST_HEAD(&devlink_port->reporter_list);
	mutex_init(&devlink_port->reporters_lock);
	list_add_tail(&devlink_port->list, &devlink->port_list);
	INIT_LIST_HEAD(&devlink_port->param_list);
	INIT_LIST_HEAD(&devlink_port->region_list);
	mutex_unlock(&devlink->lock);
	INIT_DELAYED_WORK(&devlink_port->type_warn_dw, &devlink_port_type_warn);
	devlink_port_type_warn_schedule(devlink_port);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
	return 0;
}
EXPORT_SYMBOL_GPL(devlink_port_register);

/**
 *	devlink_port_unregister - Unregister devlink port
 *
 *	@devlink_port: devlink port
 */
void devlink_port_unregister(struct devlink_port *devlink_port)
{
	struct devlink *devlink = devlink_port->devlink;

	devlink_port_type_warn_cancel(devlink_port);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_DEL);
	mutex_lock(&devlink->lock);
	list_del(&devlink_port->list);
	mutex_unlock(&devlink->lock);
	WARN_ON(!list_empty(&devlink_port->reporter_list));
	WARN_ON(!list_empty(&devlink_port->region_list));
	mutex_destroy(&devlink_port->reporters_lock);
}
EXPORT_SYMBOL_GPL(devlink_port_unregister);

static void __devlink_port_type_set(struct devlink_port *devlink_port,
				    enum devlink_port_type type,
				    void *type_dev)
{
	if (WARN_ON(!devlink_port->registered))
		return;
	devlink_port_type_warn_cancel(devlink_port);
	spin_lock_bh(&devlink_port->type_lock);
	devlink_port->type = type;
	devlink_port->type_dev = type_dev;
	spin_unlock_bh(&devlink_port->type_lock);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
}

static void devlink_port_type_netdev_checks(struct devlink_port *devlink_port,
					    struct net_device *netdev)
{
	const struct net_device_ops *ops = netdev->netdev_ops;

	/* If driver registers devlink port, it should set devlink port
	 * attributes accordingly so the compat functions are called
	 * and the original ops are not used.
	 */
	if (ops->ndo_get_phys_port_name) {
		/* Some drivers use the same set of ndos for netdevs
		 * that have devlink_port registered and also for
		 * those who don't. Make sure that ndo_get_phys_port_name
		 * returns -EOPNOTSUPP here in case it is defined.
		 * Warn if not.
		 */
		char name[IFNAMSIZ];
		int err;

		err = ops->ndo_get_phys_port_name(netdev, name, sizeof(name));
		WARN_ON(err != -EOPNOTSUPP);
	}
	if (ops->ndo_get_port_parent_id) {
		/* Some drivers use the same set of ndos for netdevs
		 * that have devlink_port registered and also for
		 * those who don't. Make sure that ndo_get_port_parent_id
		 * returns -EOPNOTSUPP here in case it is defined.
		 * Warn if not.
		 */
		struct netdev_phys_item_id ppid;
		int err;

		err = ops->ndo_get_port_parent_id(netdev, &ppid);
		WARN_ON(err != -EOPNOTSUPP);
	}
}

/**
 *	devlink_port_type_eth_set - Set port type to Ethernet
 *
 *	@devlink_port: devlink port
 *	@netdev: related netdevice
 */
void devlink_port_type_eth_set(struct devlink_port *devlink_port,
			       struct net_device *netdev)
{
	if (netdev)
		devlink_port_type_netdev_checks(devlink_port, netdev);
	else
		dev_warn(devlink_port->devlink->dev,
			 "devlink port type for port %d set to Ethernet without a software interface reference, device type not supported by the kernel?\n",
			 devlink_port->index);

	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_ETH, netdev);
}
EXPORT_SYMBOL_GPL(devlink_port_type_eth_set);

/**
 *	devlink_port_type_ib_set - Set port type to InfiniBand
 *
 *	@devlink_port: devlink port
 *	@ibdev: related IB device
 */
void devlink_port_type_ib_set(struct devlink_port *devlink_port,
			      struct ib_device *ibdev)
{
	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_IB, ibdev);
}
EXPORT_SYMBOL_GPL(devlink_port_type_ib_set);

/**
 *	devlink_port_type_clear - Clear port type
 *
 *	@devlink_port: devlink port
 */
void devlink_port_type_clear(struct devlink_port *devlink_port)
{
	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_NOTSET, NULL);
	devlink_port_type_warn_schedule(devlink_port);
}
EXPORT_SYMBOL_GPL(devlink_port_type_clear);

static int __devlink_port_attrs_set(struct devlink_port *devlink_port,
				    enum devlink_port_flavour flavour)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;

	devlink_port->attrs_set = true;
	attrs->flavour = flavour;
	if (attrs->switch_id.id_len) {
		devlink_port->switch_port = true;
		if (WARN_ON(attrs->switch_id.id_len > MAX_PHYS_ITEM_ID_LEN))
			attrs->switch_id.id_len = MAX_PHYS_ITEM_ID_LEN;
	} else {
		devlink_port->switch_port = false;
	}
	return 0;
}

/**
 *	devlink_port_attrs_set - Set port attributes
 *
 *	@devlink_port: devlink port
 *	@attrs: devlink port attrs
 */
void devlink_port_attrs_set(struct devlink_port *devlink_port,
			    struct devlink_port_attrs *attrs)
{
	int ret;

	if (WARN_ON(devlink_port->registered))
		return;
	devlink_port->attrs = *attrs;
	ret = __devlink_port_attrs_set(devlink_port, attrs->flavour);
	if (ret)
		return;
	WARN_ON(attrs->splittable && attrs->split);
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_set);

/**
 *	devlink_port_attrs_pci_pf_set - Set PCI PF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_pf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	if (WARN_ON(devlink_port->registered))
		return;
	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_PF);
	if (ret)
		return;
	attrs->pci_pf.controller = controller;
	attrs->pci_pf.pf = pf;
	attrs->pci_pf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_pf_set);

/**
 *	devlink_port_attrs_pci_vf_set - Set PCI VF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@vf: associated VF of a PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_vf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, u16 vf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	if (WARN_ON(devlink_port->registered))
		return;
	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_VF);
	if (ret)
		return;
	attrs->pci_vf.controller = controller;
	attrs->pci_vf.pf = pf;
	attrs->pci_vf.vf = vf;
	attrs->pci_vf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_vf_set);

/**
 *	devlink_port_attrs_pci_sf_set - Set PCI SF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@sf: associated SF of a PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_sf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, u32 sf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	if (WARN_ON(devlink_port->registered))
		return;
	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_SF);
	if (ret)
		return;
	attrs->pci_sf.controller = controller;
	attrs->pci_sf.pf = pf;
	attrs->pci_sf.sf = sf;
	attrs->pci_sf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_sf_set);

static int __devlink_port_phys_port_name_get(struct devlink_port *devlink_port,
					     char *name, size_t len)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int n = 0;

	if (!devlink_port->attrs_set)
		return -EOPNOTSUPP;

	switch (attrs->flavour) {
	case DEVLINK_PORT_FLAVOUR_PHYSICAL:
		if (!attrs->split)
			n = snprintf(name, len, "p%u", attrs->phys.port_number);
		else
			n = snprintf(name, len, "p%us%u",
				     attrs->phys.port_number,
				     attrs->phys.split_subport_number);
		break;
	case DEVLINK_PORT_FLAVOUR_CPU:
	case DEVLINK_PORT_FLAVOUR_DSA:
	case DEVLINK_PORT_FLAVOUR_UNUSED:
		/* As CPU and DSA ports do not have a netdevice associated
		 * case should not ever happen.
		 */
		WARN_ON(1);
		return -EINVAL;
	case DEVLINK_PORT_FLAVOUR_PCI_PF:
		if (attrs->pci_pf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_pf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%u", attrs->pci_pf.pf);
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_VF:
		if (attrs->pci_vf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_vf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%uvf%u",
			     attrs->pci_vf.pf, attrs->pci_vf.vf);
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_SF:
		if (attrs->pci_sf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_sf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%usf%u", attrs->pci_sf.pf,
			     attrs->pci_sf.sf);
		break;
	case DEVLINK_PORT_FLAVOUR_VIRTUAL:
		return -EOPNOTSUPP;
	}

	if (n >= len)
		return -EINVAL;

	return 0;
}

int devlink_sb_register(struct devlink *devlink, unsigned int sb_index,
			u32 size, u16 ingress_pools_count,
			u16 egress_pools_count, u16 ingress_tc_count,
			u16 egress_tc_count)
{
	struct devlink_sb *devlink_sb;
	int err = 0;

	mutex_lock(&devlink->lock);
	if (devlink_sb_index_exists(devlink, sb_index)) {
		err = -EEXIST;
		goto unlock;
	}

	devlink_sb = kzalloc(sizeof(*devlink_sb), GFP_KERNEL);
	if (!devlink_sb) {
		err = -ENOMEM;
		goto unlock;
	}
	devlink_sb->index = sb_index;
	devlink_sb->size = size;
	devlink_sb->ingress_pools_count = ingress_pools_count;
	devlink_sb->egress_pools_count = egress_pools_count;
	devlink_sb->ingress_tc_count = ingress_tc_count;
	devlink_sb->egress_tc_count = egress_tc_count;
	list_add_tail(&devlink_sb->list, &devlink->sb_list);
unlock:
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_sb_register);

void devlink_sb_unregister(struct devlink *devlink, unsigned int sb_index)
{
	struct devlink_sb *devlink_sb;

	mutex_lock(&devlink->lock);
	devlink_sb = devlink_sb_get_by_index(devlink, sb_index);
	WARN_ON(!devlink_sb);
	list_del(&devlink_sb->list);
	mutex_unlock(&devlink->lock);
	kfree(devlink_sb);
}
EXPORT_SYMBOL_GPL(devlink_sb_unregister);

/**
 *	devlink_dpipe_headers_register - register dpipe headers
 *
 *	@devlink: devlink
 *	@dpipe_headers: dpipe header array
 *
 *	Register the headers supported by hardware.
 */
int devlink_dpipe_headers_register(struct devlink *devlink,
				   struct devlink_dpipe_headers *dpipe_headers)
{
	mutex_lock(&devlink->lock);
	devlink->dpipe_headers = dpipe_headers;
	mutex_unlock(&devlink->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_headers_register);

/**
 *	devlink_dpipe_headers_unregister - unregister dpipe headers
 *
 *	@devlink: devlink
 *
 *	Unregister the headers supported by hardware.
 */
void devlink_dpipe_headers_unregister(struct devlink *devlink)
{
	mutex_lock(&devlink->lock);
	devlink->dpipe_headers = NULL;
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_dpipe_headers_unregister);

/**
 *	devlink_dpipe_table_counter_enabled - check if counter allocation
 *					      required
 *	@devlink: devlink
 *	@table_name: tables name
 *
 *	Used by driver to check if counter allocation is required.
 *	After counter allocation is turned on the table entries
 *	are updated to include counter statistics.
 *
 *	After that point on the driver must respect the counter
 *	state so that each entry added to the table is added
 *	with a counter.
 */
bool devlink_dpipe_table_counter_enabled(struct devlink *devlink,
					 const char *table_name)
{
	struct devlink_dpipe_table *table;
	bool enabled;

	rcu_read_lock();
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	enabled = false;
	if (table)
		enabled = table->counters_enabled;
	rcu_read_unlock();
	return enabled;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_table_counter_enabled);

/**
 *	devlink_dpipe_table_register - register dpipe table
 *
 *	@devlink: devlink
 *	@table_name: table name
 *	@table_ops: table ops
 *	@priv: priv
 *	@counter_control_extern: external control for counters
 */
int devlink_dpipe_table_register(struct devlink *devlink,
				 const char *table_name,
				 struct devlink_dpipe_table_ops *table_ops,
				 void *priv, bool counter_control_extern)
{
	struct devlink_dpipe_table *table;
	int err = 0;

	if (WARN_ON(!table_ops->size_get))
		return -EINVAL;

	mutex_lock(&devlink->lock);

	if (devlink_dpipe_table_find(&devlink->dpipe_table_list, table_name,
				     devlink)) {
		err = -EEXIST;
		goto unlock;
	}

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table) {
		err = -ENOMEM;
		goto unlock;
	}

	table->name = table_name;
	table->table_ops = table_ops;
	table->priv = priv;
	table->counter_control_extern = counter_control_extern;

	list_add_tail_rcu(&table->list, &devlink->dpipe_table_list);
unlock:
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_table_register);

/**
 *	devlink_dpipe_table_unregister - unregister dpipe table
 *
 *	@devlink: devlink
 *	@table_name: table name
 */
void devlink_dpipe_table_unregister(struct devlink *devlink,
				    const char *table_name)
{
	struct devlink_dpipe_table *table;

	mutex_lock(&devlink->lock);
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		goto unlock;
	list_del_rcu(&table->list);
	mutex_unlock(&devlink->lock);
	kfree_rcu(table, rcu);
	return;
unlock:
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_dpipe_table_unregister);

/**
 *	devlink_resource_register - devlink resource register
 *
 *	@devlink: devlink
 *	@resource_name: resource's name
 *	@resource_size: resource's size
 *	@resource_id: resource's id
 *	@parent_resource_id: resource's parent id
 *	@size_params: size parameters
 *
 *	Generic resources should reuse the same names across drivers.
 *	Please see the generic resources list at:
 *	Documentation/networking/devlink/devlink-resource.rst
 */
int devlink_resource_register(struct devlink *devlink,
			      const char *resource_name,
			      u64 resource_size,
			      u64 resource_id,
			      u64 parent_resource_id,
			      const struct devlink_resource_size_params *size_params)
{
	struct devlink_resource *resource;
	struct list_head *resource_list;
	bool top_hierarchy;
	int err = 0;

	top_hierarchy = parent_resource_id == DEVLINK_RESOURCE_ID_PARENT_TOP;

	mutex_lock(&devlink->lock);
	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (resource) {
		err = -EINVAL;
		goto out;
	}

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource) {
		err = -ENOMEM;
		goto out;
	}

	if (top_hierarchy) {
		resource_list = &devlink->resource_list;
	} else {
		struct devlink_resource *parent_resource;

		parent_resource = devlink_resource_find(devlink, NULL,
							parent_resource_id);
		if (parent_resource) {
			resource_list = &parent_resource->resource_list;
			resource->parent = parent_resource;
		} else {
			kfree(resource);
			err = -EINVAL;
			goto out;
		}
	}

	resource->name = resource_name;
	resource->size = resource_size;
	resource->size_new = resource_size;
	resource->id = resource_id;
	resource->size_valid = true;
	memcpy(&resource->size_params, size_params,
	       sizeof(resource->size_params));
	INIT_LIST_HEAD(&resource->resource_list);
	list_add_tail(&resource->list, resource_list);
out:
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_resource_register);

/**
 *	devlink_resources_unregister - free all resources
 *
 *	@devlink: devlink
 *	@resource: resource
 */
void devlink_resources_unregister(struct devlink *devlink,
				  struct devlink_resource *resource)
{
	struct devlink_resource *tmp, *child_resource;
	struct list_head *resource_list;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = &devlink->resource_list;

	if (!resource)
		mutex_lock(&devlink->lock);

	list_for_each_entry_safe(child_resource, tmp, resource_list, list) {
		devlink_resources_unregister(devlink, child_resource);
		list_del(&child_resource->list);
		kfree(child_resource);
	}

	if (!resource)
		mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_resources_unregister);

/**
 *	devlink_resource_size_get - get and update size
 *
 *	@devlink: devlink
 *	@resource_id: the requested resource id
 *	@p_resource_size: ptr to update
 */
int devlink_resource_size_get(struct devlink *devlink,
			      u64 resource_id,
			      u64 *p_resource_size)
{
	struct devlink_resource *resource;
	int err = 0;

	mutex_lock(&devlink->lock);
	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (!resource) {
		err = -EINVAL;
		goto out;
	}
	*p_resource_size = resource->size_new;
	resource->size = resource->size_new;
out:
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_resource_size_get);

/**
 *	devlink_dpipe_table_resource_set - set the resource id
 *
 *	@devlink: devlink
 *	@table_name: table name
 *	@resource_id: resource id
 *	@resource_units: number of resource's units consumed per table's entry
 */
int devlink_dpipe_table_resource_set(struct devlink *devlink,
				     const char *table_name, u64 resource_id,
				     u64 resource_units)
{
	struct devlink_dpipe_table *table;
	int err = 0;

	mutex_lock(&devlink->lock);
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table) {
		err = -EINVAL;
		goto out;
	}
	table->resource_id = resource_id;
	table->resource_units = resource_units;
	table->resource_valid = true;
out:
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_table_resource_set);

/**
 *	devlink_resource_occ_get_register - register occupancy getter
 *
 *	@devlink: devlink
 *	@resource_id: resource id
 *	@occ_get: occupancy getter callback
 *	@occ_get_priv: occupancy getter callback priv
 */
void devlink_resource_occ_get_register(struct devlink *devlink,
				       u64 resource_id,
				       devlink_resource_occ_get_t *occ_get,
				       void *occ_get_priv)
{
	struct devlink_resource *resource;

	mutex_lock(&devlink->lock);
	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		goto out;
	WARN_ON(resource->occ_get);

	resource->occ_get = occ_get;
	resource->occ_get_priv = occ_get_priv;
out:
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_resource_occ_get_register);

/**
 *	devlink_resource_occ_get_unregister - unregister occupancy getter
 *
 *	@devlink: devlink
 *	@resource_id: resource id
 */
void devlink_resource_occ_get_unregister(struct devlink *devlink,
					 u64 resource_id)
{
	struct devlink_resource *resource;

	mutex_lock(&devlink->lock);
	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		goto out;
	WARN_ON(!resource->occ_get);

	resource->occ_get = NULL;
	resource->occ_get_priv = NULL;
out:
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_resource_occ_get_unregister);

static int devlink_param_verify(const struct devlink_param *param)
{
	if (!param || !param->name || !param->supported_cmodes)
		return -EINVAL;
	if (param->generic)
		return devlink_param_generic_verify(param);
	else
		return devlink_param_driver_verify(param);
}

static int __devlink_params_register(struct devlink *devlink,
				     unsigned int port_index,
				     struct list_head *param_list,
				     const struct devlink_param *params,
				     size_t params_count,
				     enum devlink_command reg_cmd,
				     enum devlink_command unreg_cmd)
{
	const struct devlink_param *param = params;
	int i;
	int err;

	mutex_lock(&devlink->lock);
	for (i = 0; i < params_count; i++, param++) {
		err = devlink_param_verify(param);
		if (err)
			goto rollback;

		err = devlink_param_register_one(devlink, port_index,
						 param_list, param, reg_cmd);
		if (err)
			goto rollback;
	}

	mutex_unlock(&devlink->lock);
	return 0;

rollback:
	if (!i)
		goto unlock;
	for (param--; i > 0; i--, param--)
		devlink_param_unregister_one(devlink, port_index, param_list,
					     param, unreg_cmd);
unlock:
	mutex_unlock(&devlink->lock);
	return err;
}

static void __devlink_params_unregister(struct devlink *devlink,
					unsigned int port_index,
					struct list_head *param_list,
					const struct devlink_param *params,
					size_t params_count,
					enum devlink_command cmd)
{
	const struct devlink_param *param = params;
	int i;

	mutex_lock(&devlink->lock);
	for (i = 0; i < params_count; i++, param++)
		devlink_param_unregister_one(devlink, 0, param_list, param,
					     cmd);
	mutex_unlock(&devlink->lock);
}

/**
 *	devlink_params_register - register configuration parameters
 *
 *	@devlink: devlink
 *	@params: configuration parameters array
 *	@params_count: number of parameters provided
 *
 *	Register the configuration parameters supported by the driver.
 */
int devlink_params_register(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count)
{
	return __devlink_params_register(devlink, 0, &devlink->param_list,
					 params, params_count,
					 DEVLINK_CMD_PARAM_NEW,
					 DEVLINK_CMD_PARAM_DEL);
}
EXPORT_SYMBOL_GPL(devlink_params_register);

/**
 *	devlink_params_unregister - unregister configuration parameters
 *	@devlink: devlink
 *	@params: configuration parameters to unregister
 *	@params_count: number of parameters provided
 */
void devlink_params_unregister(struct devlink *devlink,
			       const struct devlink_param *params,
			       size_t params_count)
{
	return __devlink_params_unregister(devlink, 0, &devlink->param_list,
					   params, params_count,
					   DEVLINK_CMD_PARAM_DEL);
}
EXPORT_SYMBOL_GPL(devlink_params_unregister);

/**
 *	devlink_params_publish - publish configuration parameters
 *
 *	@devlink: devlink
 *
 *	Publish previously registered configuration parameters.
 */
void devlink_params_publish(struct devlink *devlink)
{
	struct devlink_param_item *param_item;

	list_for_each_entry(param_item, &devlink->param_list, list) {
		if (param_item->published)
			continue;
		param_item->published = true;
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_NEW);
	}
}
EXPORT_SYMBOL_GPL(devlink_params_publish);

/**
 *	devlink_params_unpublish - unpublish configuration parameters
 *
 *	@devlink: devlink
 *
 *	Unpublish previously registered configuration parameters.
 */
void devlink_params_unpublish(struct devlink *devlink)
{
	struct devlink_param_item *param_item;

	list_for_each_entry(param_item, &devlink->param_list, list) {
		if (!param_item->published)
			continue;
		param_item->published = false;
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_DEL);
	}
}
EXPORT_SYMBOL_GPL(devlink_params_unpublish);

/**
 *	devlink_port_params_register - register port configuration parameters
 *
 *	@devlink_port: devlink port
 *	@params: configuration parameters array
 *	@params_count: number of parameters provided
 *
 *	Register the configuration parameters supported by the port.
 */
int devlink_port_params_register(struct devlink_port *devlink_port,
				 const struct devlink_param *params,
				 size_t params_count)
{
	return __devlink_params_register(devlink_port->devlink,
					 devlink_port->index,
					 &devlink_port->param_list, params,
					 params_count,
					 DEVLINK_CMD_PORT_PARAM_NEW,
					 DEVLINK_CMD_PORT_PARAM_DEL);
}
EXPORT_SYMBOL_GPL(devlink_port_params_register);

/**
 *	devlink_port_params_unregister - unregister port configuration
 *	parameters
 *
 *	@devlink_port: devlink port
 *	@params: configuration parameters array
 *	@params_count: number of parameters provided
 */
void devlink_port_params_unregister(struct devlink_port *devlink_port,
				    const struct devlink_param *params,
				    size_t params_count)
{
	return __devlink_params_unregister(devlink_port->devlink,
					   devlink_port->index,
					   &devlink_port->param_list,
					   params, params_count,
					   DEVLINK_CMD_PORT_PARAM_DEL);
}
EXPORT_SYMBOL_GPL(devlink_port_params_unregister);

static int
__devlink_param_driverinit_value_get(struct list_head *param_list, u32 param_id,
				     union devlink_param_value *init_val)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(param_list, param_id);
	if (!param_item)
		return -EINVAL;

	if (!param_item->driverinit_value_valid ||
	    !devlink_param_cmode_is_supported(param_item->param,
					      DEVLINK_PARAM_CMODE_DRIVERINIT))
		return -EOPNOTSUPP;

	if (param_item->param->type == DEVLINK_PARAM_TYPE_STRING)
		strcpy(init_val->vstr, param_item->driverinit_value.vstr);
	else
		*init_val = param_item->driverinit_value;

	return 0;
}

static int
__devlink_param_driverinit_value_set(struct devlink *devlink,
				     unsigned int port_index,
				     struct list_head *param_list, u32 param_id,
				     union devlink_param_value init_val,
				     enum devlink_command cmd)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(param_list, param_id);
	if (!param_item)
		return -EINVAL;

	if (!devlink_param_cmode_is_supported(param_item->param,
					      DEVLINK_PARAM_CMODE_DRIVERINIT))
		return -EOPNOTSUPP;

	if (param_item->param->type == DEVLINK_PARAM_TYPE_STRING)
		strcpy(param_item->driverinit_value.vstr, init_val.vstr);
	else
		param_item->driverinit_value = init_val;
	param_item->driverinit_value_valid = true;

	devlink_param_notify(devlink, port_index, param_item, cmd);
	return 0;
}

/**
 *	devlink_param_driverinit_value_get - get configuration parameter
 *					     value for driver initializing
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@init_val: value of parameter in driverinit configuration mode
 *
 *	This function should be used by the driver to get driverinit
 *	configuration for initialization after reload command.
 */
int devlink_param_driverinit_value_get(struct devlink *devlink, u32 param_id,
				       union devlink_param_value *init_val)
{
	if (!devlink_reload_supported(devlink->ops))
		return -EOPNOTSUPP;

	return __devlink_param_driverinit_value_get(&devlink->param_list,
						    param_id, init_val);
}
EXPORT_SYMBOL_GPL(devlink_param_driverinit_value_get);

/**
 *	devlink_param_driverinit_value_set - set value of configuration
 *					     parameter for driverinit
 *					     configuration mode
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@init_val: value of parameter to set for driverinit configuration mode
 *
 *	This function should be used by the driver to set driverinit
 *	configuration mode default value.
 */
int devlink_param_driverinit_value_set(struct devlink *devlink, u32 param_id,
				       union devlink_param_value init_val)
{
	return __devlink_param_driverinit_value_set(devlink, 0,
						    &devlink->param_list,
						    param_id, init_val,
						    DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devlink_param_driverinit_value_set);

/**
 *	devlink_port_param_driverinit_value_get - get configuration parameter
 *						value for driver initializing
 *
 *	@devlink_port: devlink_port
 *	@param_id: parameter ID
 *	@init_val: value of parameter in driverinit configuration mode
 *
 *	This function should be used by the driver to get driverinit
 *	configuration for initialization after reload command.
 */
int devlink_port_param_driverinit_value_get(struct devlink_port *devlink_port,
					    u32 param_id,
					    union devlink_param_value *init_val)
{
	struct devlink *devlink = devlink_port->devlink;

	if (!devlink_reload_supported(devlink->ops))
		return -EOPNOTSUPP;

	return __devlink_param_driverinit_value_get(&devlink_port->param_list,
						    param_id, init_val);
}
EXPORT_SYMBOL_GPL(devlink_port_param_driverinit_value_get);

/**
 *     devlink_port_param_driverinit_value_set - set value of configuration
 *                                               parameter for driverinit
 *                                               configuration mode
 *
 *     @devlink_port: devlink_port
 *     @param_id: parameter ID
 *     @init_val: value of parameter to set for driverinit configuration mode
 *
 *     This function should be used by the driver to set driverinit
 *     configuration mode default value.
 */
int devlink_port_param_driverinit_value_set(struct devlink_port *devlink_port,
					    u32 param_id,
					    union devlink_param_value init_val)
{
	return __devlink_param_driverinit_value_set(devlink_port->devlink,
						    devlink_port->index,
						    &devlink_port->param_list,
						    param_id, init_val,
						    DEVLINK_CMD_PORT_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devlink_port_param_driverinit_value_set);

/**
 *	devlink_param_value_changed - notify devlink on a parameter's value
 *				      change. Should be called by the driver
 *				      right after the change.
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *
 *	This function should be used by the driver to notify devlink on value
 *	change, excluding driverinit configuration mode.
 *	For driverinit configuration mode driver should use the function
 */
void devlink_param_value_changed(struct devlink *devlink, u32 param_id)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink->param_list, param_id);
	WARN_ON(!param_item);

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devlink_param_value_changed);

/**
 *     devlink_port_param_value_changed - notify devlink on a parameter's value
 *                                      change. Should be called by the driver
 *                                      right after the change.
 *
 *     @devlink_port: devlink_port
 *     @param_id: parameter ID
 *
 *     This function should be used by the driver to notify devlink on value
 *     change, excluding driverinit configuration mode.
 *     For driverinit configuration mode driver should use the function
 *     devlink_port_param_driverinit_value_set() instead.
 */
void devlink_port_param_value_changed(struct devlink_port *devlink_port,
				      u32 param_id)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink_port->param_list,
					      param_id);
	WARN_ON(!param_item);

	devlink_param_notify(devlink_port->devlink, devlink_port->index,
			     param_item, DEVLINK_CMD_PORT_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devlink_port_param_value_changed);

/**
 *	devlink_param_value_str_fill - Safely fill-up the string preventing
 *				       from overflow of the preallocated buffer
 *
 *	@dst_val: destination devlink_param_value
 *	@src: source buffer
 */
void devlink_param_value_str_fill(union devlink_param_value *dst_val,
				  const char *src)
{
	size_t len;

	len = strlcpy(dst_val->vstr, src, __DEVLINK_PARAM_MAX_STRING_VALUE);
	WARN_ON(len >= __DEVLINK_PARAM_MAX_STRING_VALUE);
}
EXPORT_SYMBOL_GPL(devlink_param_value_str_fill);

/**
 *	devlink_region_create - create a new address region
 *
 *	@devlink: devlink
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 */
struct devlink_region *
devlink_region_create(struct devlink *devlink,
		      const struct devlink_region_ops *ops,
		      u32 region_max_snapshots, u64 region_size)
{
	struct devlink_region *region;
	int err = 0;

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	mutex_lock(&devlink->lock);

	if (devlink_region_get_by_name(devlink, ops->name)) {
		err = -EEXIST;
		goto unlock;
	}

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		err = -ENOMEM;
		goto unlock;
	}

	region->devlink = devlink;
	region->max_snapshots = region_max_snapshots;
	region->ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	list_add_tail(&region->list, &devlink->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	mutex_unlock(&devlink->lock);
	return region;

unlock:
	mutex_unlock(&devlink->lock);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(devlink_region_create);

/**
 *	devlink_port_region_create - create a new address region for a port
 *
 *	@port: devlink port
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 */
struct devlink_region *
devlink_port_region_create(struct devlink_port *port,
			   const struct devlink_port_region_ops *ops,
			   u32 region_max_snapshots, u64 region_size)
{
	struct devlink *devlink = port->devlink;
	struct devlink_region *region;
	int err = 0;

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	mutex_lock(&devlink->lock);

	if (devlink_port_region_get_by_name(port, ops->name)) {
		err = -EEXIST;
		goto unlock;
	}

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		err = -ENOMEM;
		goto unlock;
	}

	region->devlink = devlink;
	region->port = port;
	region->max_snapshots = region_max_snapshots;
	region->port_ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	list_add_tail(&region->list, &port->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	mutex_unlock(&devlink->lock);
	return region;

unlock:
	mutex_unlock(&devlink->lock);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(devlink_port_region_create);

/**
 *	devlink_region_destroy - destroy address region
 *
 *	@region: devlink region to destroy
 */
void devlink_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot, *ts;

	mutex_lock(&devlink->lock);

	/* Free all snapshots of region */
	list_for_each_entry_safe(snapshot, ts, &region->snapshot_list, list)
		devlink_region_snapshot_del(region, snapshot);

	list_del(&region->list);

	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
	mutex_unlock(&devlink->lock);
	kfree(region);
}
EXPORT_SYMBOL_GPL(devlink_region_destroy);

/**
 *	devlink_region_snapshot_id_get - get snapshot ID
 *
 *	This callback should be called when adding a new snapshot,
 *	Driver should use the same id for multiple snapshots taken
 *	on multiple regions at the same time/by the same trigger.
 *
 *	The caller of this function must use devlink_region_snapshot_id_put
 *	when finished creating regions using this id.
 *
 *	Returns zero on success, or a negative error code on failure.
 *
 *	@devlink: devlink
 *	@id: storage to return id
 */
int devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	int err;

	mutex_lock(&devlink->lock);
	err = __devlink_region_snapshot_id_get(devlink, id);
	mutex_unlock(&devlink->lock);

	return err;
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_get);

/**
 *	devlink_region_snapshot_id_put - put snapshot ID reference
 *
 *	This should be called by a driver after finishing creating snapshots
 *	with an id. Doing so ensures that the ID can later be released in the
 *	event that all snapshots using it have been destroyed.
 *
 *	@devlink: devlink
 *	@id: id to release reference on
 */
void devlink_region_snapshot_id_put(struct devlink *devlink, u32 id)
{
	mutex_lock(&devlink->lock);
	__devlink_snapshot_id_decrement(devlink, id);
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_put);

/**
 *	devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
int devlink_region_snapshot_create(struct devlink_region *region,
				   u8 *data, u32 snapshot_id)
{
	struct devlink *devlink = region->devlink;
	int err;

	mutex_lock(&devlink->lock);
	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	mutex_unlock(&devlink->lock);

	return err;
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_create);

#define DEVLINK_TRAP(_id, _type)					      \
	{								      \
		.type = DEVLINK_TRAP_TYPE_##_type,			      \
		.id = DEVLINK_TRAP_GENERIC_ID_##_id,			      \
		.name = DEVLINK_TRAP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap devlink_trap_generic[] = {
	DEVLINK_TRAP(SMAC_MC, DROP),
	DEVLINK_TRAP(VLAN_TAG_MISMATCH, DROP),
	DEVLINK_TRAP(INGRESS_VLAN_FILTER, DROP),
	DEVLINK_TRAP(INGRESS_STP_FILTER, DROP),
	DEVLINK_TRAP(EMPTY_TX_LIST, DROP),
	DEVLINK_TRAP(PORT_LOOPBACK_FILTER, DROP),
	DEVLINK_TRAP(BLACKHOLE_ROUTE, DROP),
	DEVLINK_TRAP(TTL_ERROR, EXCEPTION),
	DEVLINK_TRAP(TAIL_DROP, DROP),
	DEVLINK_TRAP(NON_IP_PACKET, DROP),
	DEVLINK_TRAP(UC_DIP_MC_DMAC, DROP),
	DEVLINK_TRAP(DIP_LB, DROP),
	DEVLINK_TRAP(SIP_MC, DROP),
	DEVLINK_TRAP(SIP_LB, DROP),
	DEVLINK_TRAP(CORRUPTED_IP_HDR, DROP),
	DEVLINK_TRAP(IPV4_SIP_BC, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_RESERVED_SCOPE, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE, DROP),
	DEVLINK_TRAP(MTU_ERROR, EXCEPTION),
	DEVLINK_TRAP(UNRESOLVED_NEIGH, EXCEPTION),
	DEVLINK_TRAP(RPF, EXCEPTION),
	DEVLINK_TRAP(REJECT_ROUTE, EXCEPTION),
	DEVLINK_TRAP(IPV4_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(IPV6_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(NON_ROUTABLE, DROP),
	DEVLINK_TRAP(DECAP_ERROR, EXCEPTION),
	DEVLINK_TRAP(OVERLAY_SMAC_MC, DROP),
	DEVLINK_TRAP(INGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(EGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(STP, CONTROL),
	DEVLINK_TRAP(LACP, CONTROL),
	DEVLINK_TRAP(LLDP, CONTROL),
	DEVLINK_TRAP(IGMP_QUERY, CONTROL),
	DEVLINK_TRAP(IGMP_V1_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V3_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_LEAVE, CONTROL),
	DEVLINK_TRAP(MLD_QUERY, CONTROL),
	DEVLINK_TRAP(MLD_V1_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V2_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V1_DONE, CONTROL),
	DEVLINK_TRAP(IPV4_DHCP, CONTROL),
	DEVLINK_TRAP(IPV6_DHCP, CONTROL),
	DEVLINK_TRAP(ARP_REQUEST, CONTROL),
	DEVLINK_TRAP(ARP_RESPONSE, CONTROL),
	DEVLINK_TRAP(ARP_OVERLAY, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV4_BFD, CONTROL),
	DEVLINK_TRAP(IPV6_BFD, CONTROL),
	DEVLINK_TRAP(IPV4_OSPF, CONTROL),
	DEVLINK_TRAP(IPV6_OSPF, CONTROL),
	DEVLINK_TRAP(IPV4_BGP, CONTROL),
	DEVLINK_TRAP(IPV6_BGP, CONTROL),
	DEVLINK_TRAP(IPV4_VRRP, CONTROL),
	DEVLINK_TRAP(IPV6_VRRP, CONTROL),
	DEVLINK_TRAP(IPV4_PIM, CONTROL),
	DEVLINK_TRAP(IPV6_PIM, CONTROL),
	DEVLINK_TRAP(UC_LB, CONTROL),
	DEVLINK_TRAP(LOCAL_ROUTE, CONTROL),
	DEVLINK_TRAP(EXTERNAL_ROUTE, CONTROL),
	DEVLINK_TRAP(IPV6_UC_DIP_LINK_LOCAL_SCOPE, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_NODES, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_ROUTERS, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV6_REDIRECT, CONTROL),
	DEVLINK_TRAP(IPV4_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(PTP_EVENT, CONTROL),
	DEVLINK_TRAP(PTP_GENERAL, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_SAMPLE, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_TRAP, CONTROL),
	DEVLINK_TRAP(EARLY_DROP, DROP),
	DEVLINK_TRAP(VXLAN_PARSING, DROP),
	DEVLINK_TRAP(LLC_SNAP_PARSING, DROP),
	DEVLINK_TRAP(VLAN_PARSING, DROP),
	DEVLINK_TRAP(PPPOE_PPP_PARSING, DROP),
	DEVLINK_TRAP(MPLS_PARSING, DROP),
	DEVLINK_TRAP(ARP_PARSING, DROP),
	DEVLINK_TRAP(IP_1_PARSING, DROP),
	DEVLINK_TRAP(IP_N_PARSING, DROP),
	DEVLINK_TRAP(GRE_PARSING, DROP),
	DEVLINK_TRAP(UDP_PARSING, DROP),
	DEVLINK_TRAP(TCP_PARSING, DROP),
	DEVLINK_TRAP(IPSEC_PARSING, DROP),
	DEVLINK_TRAP(SCTP_PARSING, DROP),
	DEVLINK_TRAP(DCCP_PARSING, DROP),
	DEVLINK_TRAP(GTP_PARSING, DROP),
	DEVLINK_TRAP(ESP_PARSING, DROP),
	DEVLINK_TRAP(BLACKHOLE_NEXTHOP, DROP),
	DEVLINK_TRAP(DMAC_FILTER, DROP),
};

#define DEVLINK_TRAP_GROUP(_id)						      \
	{								      \
		.id = DEVLINK_TRAP_GROUP_GENERIC_ID_##_id,		      \
		.name = DEVLINK_TRAP_GROUP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap_group devlink_trap_group_generic[] = {
	DEVLINK_TRAP_GROUP(L2_DROPS),
	DEVLINK_TRAP_GROUP(L3_DROPS),
	DEVLINK_TRAP_GROUP(L3_EXCEPTIONS),
	DEVLINK_TRAP_GROUP(BUFFER_DROPS),
	DEVLINK_TRAP_GROUP(TUNNEL_DROPS),
	DEVLINK_TRAP_GROUP(ACL_DROPS),
	DEVLINK_TRAP_GROUP(STP),
	DEVLINK_TRAP_GROUP(LACP),
	DEVLINK_TRAP_GROUP(LLDP),
	DEVLINK_TRAP_GROUP(MC_SNOOPING),
	DEVLINK_TRAP_GROUP(DHCP),
	DEVLINK_TRAP_GROUP(NEIGH_DISCOVERY),
	DEVLINK_TRAP_GROUP(BFD),
	DEVLINK_TRAP_GROUP(OSPF),
	DEVLINK_TRAP_GROUP(BGP),
	DEVLINK_TRAP_GROUP(VRRP),
	DEVLINK_TRAP_GROUP(PIM),
	DEVLINK_TRAP_GROUP(UC_LB),
	DEVLINK_TRAP_GROUP(LOCAL_DELIVERY),
	DEVLINK_TRAP_GROUP(EXTERNAL_DELIVERY),
	DEVLINK_TRAP_GROUP(IPV6),
	DEVLINK_TRAP_GROUP(PTP_EVENT),
	DEVLINK_TRAP_GROUP(PTP_GENERAL),
	DEVLINK_TRAP_GROUP(ACL_SAMPLE),
	DEVLINK_TRAP_GROUP(ACL_TRAP),
	DEVLINK_TRAP_GROUP(PARSER_ERROR_DROPS),
};

static int devlink_trap_generic_verify(const struct devlink_trap *trap)
{
	if (trap->id > DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(trap->name, devlink_trap_generic[trap->id].name))
		return -EINVAL;

	if (trap->type != devlink_trap_generic[trap->id].type)
		return -EINVAL;

	return 0;
}

static int devlink_trap_driver_verify(const struct devlink_trap *trap)
{
	int i;

	if (trap->id <= DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_generic); i++) {
		if (!strcmp(trap->name, devlink_trap_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_verify(const struct devlink_trap *trap)
{
	if (!trap || !trap->name)
		return -EINVAL;

	if (trap->generic)
		return devlink_trap_generic_verify(trap);
	else
		return devlink_trap_driver_verify(trap);
}

static int
devlink_trap_group_generic_verify(const struct devlink_trap_group *group)
{
	if (group->id > DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(group->name, devlink_trap_group_generic[group->id].name))
		return -EINVAL;

	return 0;
}

static int
devlink_trap_group_driver_verify(const struct devlink_trap_group *group)
{
	int i;

	if (group->id <= DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_group_generic); i++) {
		if (!strcmp(group->name, devlink_trap_group_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_group_verify(const struct devlink_trap_group *group)
{
	if (group->generic)
		return devlink_trap_group_generic_verify(group);
	else
		return devlink_trap_group_driver_verify(group);
}

static void
devlink_trap_group_notify(struct devlink *devlink,
			  const struct devlink_trap_group_item *group_item,
			  enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_GROUP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_GROUP_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item, cmd, 0, 0,
					 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_item_group_link(struct devlink *devlink,
			     struct devlink_trap_item *trap_item)
{
	u16 group_id = trap_item->trap->init_group_id;
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup_by_id(devlink, group_id);
	if (WARN_ON_ONCE(!group_item))
		return -EINVAL;

	trap_item->group_item = group_item;

	return 0;
}

static void devlink_trap_notify(struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_fill(msg, devlink, trap_item, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_register(struct devlink *devlink,
		      const struct devlink_trap *trap, void *priv)
{
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink_trap_item_lookup(devlink, trap->name))
		return -EEXIST;

	trap_item = kzalloc(sizeof(*trap_item), GFP_KERNEL);
	if (!trap_item)
		return -ENOMEM;

	trap_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!trap_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	trap_item->trap = trap;
	trap_item->action = trap->init_action;
	trap_item->priv = priv;

	err = devlink_trap_item_group_link(devlink, trap_item);
	if (err)
		goto err_group_link;

	err = devlink->ops->trap_init(devlink, trap, trap_item);
	if (err)
		goto err_trap_init;

	list_add_tail(&trap_item->list, &devlink->trap_list);
	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);

	return 0;

err_trap_init:
err_group_link:
	free_percpu(trap_item->stats);
err_stats_alloc:
	kfree(trap_item);
	return err;
}

static void devlink_trap_unregister(struct devlink *devlink,
				    const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);
	list_del(&trap_item->list);
	if (devlink->ops->trap_fini)
		devlink->ops->trap_fini(devlink, trap, trap_item);
	free_percpu(trap_item->stats);
	kfree(trap_item);
}

static void devlink_trap_disable(struct devlink *devlink,
				 const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink->ops->trap_action_set(devlink, trap, DEVLINK_TRAP_ACTION_DROP,
				      NULL);
	trap_item->action = DEVLINK_TRAP_ACTION_DROP;
}

/**
 * devlink_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Return: Non-zero value on failure.
 */
int devlink_traps_register(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count, void *priv)
{
	int i, err;

	if (!devlink->ops->trap_init || !devlink->ops->trap_action_set)
		return -EINVAL;

	mutex_lock(&devlink->lock);
	for (i = 0; i < traps_count; i++) {
		const struct devlink_trap *trap = &traps[i];

		err = devlink_trap_verify(trap);
		if (err)
			goto err_trap_verify;

		err = devlink_trap_register(devlink, trap, priv);
		if (err)
			goto err_trap_register;
	}
	mutex_unlock(&devlink->lock);

	return 0;

err_trap_register:
err_trap_verify:
	for (i--; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_traps_register);

/**
 * devlink_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 */
void devlink_traps_unregister(struct devlink *devlink,
			      const struct devlink_trap *traps,
			      size_t traps_count)
{
	int i;

	mutex_lock(&devlink->lock);
	/* Make sure we do not have any packets in-flight while unregistering
	 * traps by disabling all of them and waiting for a grace period.
	 */
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_disable(devlink, &traps[i]);
	synchronize_rcu();
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_traps_unregister);

static void
devlink_trap_stats_update(struct devlink_stats __percpu *trap_stats,
			  size_t skb_len)
{
	struct devlink_stats *stats;

	stats = this_cpu_ptr(trap_stats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_bytes += skb_len;
	stats->rx_packets++;
	u64_stats_update_end(&stats->syncp);
}

static void
devlink_trap_report_metadata_set(struct devlink_trap_metadata *metadata,
				 const struct devlink_trap_item *trap_item,
				 struct devlink_port *in_devlink_port,
				 const struct flow_action_cookie *fa_cookie)
{
	metadata->trap_name = trap_item->trap->name;
	metadata->trap_group_name = trap_item->group_item->group->name;
	metadata->fa_cookie = fa_cookie;
	metadata->trap_type = trap_item->trap->type;

	spin_lock(&in_devlink_port->type_lock);
	if (in_devlink_port->type == DEVLINK_PORT_TYPE_ETH)
		metadata->input_dev = in_devlink_port->type_dev;
	spin_unlock(&in_devlink_port->type_lock);
}

/**
 * devlink_trap_report - Report trapped packet to drop monitor.
 * @devlink: devlink.
 * @skb: Trapped packet.
 * @trap_ctx: Trap context.
 * @in_devlink_port: Input devlink port.
 * @fa_cookie: Flow action cookie. Could be NULL.
 */
void devlink_trap_report(struct devlink *devlink, struct sk_buff *skb,
			 void *trap_ctx, struct devlink_port *in_devlink_port,
			 const struct flow_action_cookie *fa_cookie)

{
	struct devlink_trap_item *trap_item = trap_ctx;

	devlink_trap_stats_update(trap_item->stats, skb->len);
	devlink_trap_stats_update(trap_item->group_item->stats, skb->len);

	if (trace_devlink_trap_report_enabled()) {
		struct devlink_trap_metadata metadata = {};

		devlink_trap_report_metadata_set(&metadata, trap_item,
						 in_devlink_port, fa_cookie);
		trace_devlink_trap_report(devlink, skb, &metadata);
	}
}
EXPORT_SYMBOL_GPL(devlink_trap_report);

/**
 * devlink_trap_ctx_priv - Trap context to driver private information.
 * @trap_ctx: Trap context.
 *
 * Return: Driver private information passed during registration.
 */
void *devlink_trap_ctx_priv(void *trap_ctx)
{
	struct devlink_trap_item *trap_item = trap_ctx;

	return trap_item->priv;
}
EXPORT_SYMBOL_GPL(devlink_trap_ctx_priv);

static int
devlink_trap_group_item_policer_link(struct devlink *devlink,
				     struct devlink_trap_group_item *group_item)
{
	u32 policer_id = group_item->group->init_policer_id;
	struct devlink_trap_policer_item *policer_item;

	if (policer_id == 0)
		return 0;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (WARN_ON_ONCE(!policer_item))
		return -EINVAL;

	group_item->policer_item = policer_item;

	return 0;
}

static int
devlink_trap_group_register(struct devlink *devlink,
			    const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;
	int err;

	if (devlink_trap_group_item_lookup(devlink, group->name))
		return -EEXIST;

	group_item = kzalloc(sizeof(*group_item), GFP_KERNEL);
	if (!group_item)
		return -ENOMEM;

	group_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!group_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	group_item->group = group;

	err = devlink_trap_group_item_policer_link(devlink, group_item);
	if (err)
		goto err_policer_link;

	if (devlink->ops->trap_group_init) {
		err = devlink->ops->trap_group_init(devlink, group);
		if (err)
			goto err_group_init;
	}

	list_add_tail(&group_item->list, &devlink->trap_group_list);
	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_NEW);

	return 0;

err_group_init:
err_policer_link:
	free_percpu(group_item->stats);
err_stats_alloc:
	kfree(group_item);
	return err;
}

static void
devlink_trap_group_unregister(struct devlink *devlink,
			      const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup(devlink, group->name);
	if (WARN_ON_ONCE(!group_item))
		return;

	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_DEL);
	list_del(&group_item->list);
	free_percpu(group_item->stats);
	kfree(group_item);
}

/**
 * devlink_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Return: Non-zero value on failure.
 */
int devlink_trap_groups_register(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int i, err;

	mutex_lock(&devlink->lock);
	for (i = 0; i < groups_count; i++) {
		const struct devlink_trap_group *group = &groups[i];

		err = devlink_trap_group_verify(group);
		if (err)
			goto err_trap_group_verify;

		err = devlink_trap_group_register(devlink, group);
		if (err)
			goto err_trap_group_register;
	}
	mutex_unlock(&devlink->lock);

	return 0;

err_trap_group_register:
err_trap_group_verify:
	for (i--; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_register);

/**
 * devlink_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 */
void devlink_trap_groups_unregister(struct devlink *devlink,
				    const struct devlink_trap_group *groups,
				    size_t groups_count)
{
	int i;

	mutex_lock(&devlink->lock);
	for (i = groups_count - 1; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_unregister);

static void
devlink_trap_policer_notify(struct devlink *devlink,
			    const struct devlink_trap_policer_item *policer_item,
			    enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_POLICER_NEW &&
		     cmd != DEVLINK_CMD_TRAP_POLICER_DEL);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item, cmd, 0,
					   0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_policer_register(struct devlink *devlink,
			      const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;
	int err;

	if (devlink_trap_policer_item_lookup(devlink, policer->id))
		return -EEXIST;

	policer_item = kzalloc(sizeof(*policer_item), GFP_KERNEL);
	if (!policer_item)
		return -ENOMEM;

	policer_item->policer = policer;
	policer_item->rate = policer->init_rate;
	policer_item->burst = policer->init_burst;

	if (devlink->ops->trap_policer_init) {
		err = devlink->ops->trap_policer_init(devlink, policer);
		if (err)
			goto err_policer_init;
	}

	list_add_tail(&policer_item->list, &devlink->trap_policer_list);
	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_NEW);

	return 0;

err_policer_init:
	kfree(policer_item);
	return err;
}

static void
devlink_trap_policer_unregister(struct devlink *devlink,
				const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer->id);
	if (WARN_ON_ONCE(!policer_item))
		return;

	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_DEL);
	list_del(&policer_item->list);
	if (devlink->ops->trap_policer_fini)
		devlink->ops->trap_policer_fini(devlink, policer);
	kfree(policer_item);
}

/**
 * devlink_trap_policers_register - Register packet trap policers with devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 *
 * Return: Non-zero value on failure.
 */
int
devlink_trap_policers_register(struct devlink *devlink,
			       const struct devlink_trap_policer *policers,
			       size_t policers_count)
{
	int i, err;

	mutex_lock(&devlink->lock);
	for (i = 0; i < policers_count; i++) {
		const struct devlink_trap_policer *policer = &policers[i];

		if (WARN_ON(policer->id == 0 ||
			    policer->max_rate < policer->min_rate ||
			    policer->max_burst < policer->min_burst)) {
			err = -EINVAL;
			goto err_trap_policer_verify;
		}

		err = devlink_trap_policer_register(devlink, policer);
		if (err)
			goto err_trap_policer_register;
	}
	mutex_unlock(&devlink->lock);

	return 0;

err_trap_policer_register:
err_trap_policer_verify:
	for (i--; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
	mutex_unlock(&devlink->lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_trap_policers_register);

/**
 * devlink_trap_policers_unregister - Unregister packet trap policers from devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 */
void
devlink_trap_policers_unregister(struct devlink *devlink,
				 const struct devlink_trap_policer *policers,
				 size_t policers_count)
{
	int i;

	mutex_lock(&devlink->lock);
	for (i = policers_count - 1; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devlink_trap_policers_unregister);

static void __devlink_compat_running_version(struct devlink *devlink,
					     char *buf, size_t len)
{
	const struct nlattr *nlattr;
	struct devlink_info_req req;
	struct sk_buff *msg;
	int rem, err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	req.msg = msg;
	err = devlink->ops->info_get(devlink, &req, NULL);
	if (err)
		goto free_msg;

	nla_for_each_attr(nlattr, (void *)msg->data, msg->len, rem) {
		const struct nlattr *kv;
		int rem_kv;

		if (nla_type(nlattr) != DEVLINK_ATTR_INFO_VERSION_RUNNING)
			continue;

		nla_for_each_nested(kv, nlattr, rem_kv) {
			if (nla_type(kv) != DEVLINK_ATTR_INFO_VERSION_VALUE)
				continue;

			strlcat(buf, nla_data(kv), len);
			strlcat(buf, " ", len);
		}
	}
free_msg:
	nlmsg_free(msg);
}

void devlink_compat_running_version(struct net_device *dev,
				    char *buf, size_t len)
{
	struct devlink *devlink;

	dev_hold(dev);
	rtnl_unlock();

	devlink = netdev_to_devlink(dev);
	if (!devlink || !devlink->ops->info_get)
		goto out;

	mutex_lock(&devlink->lock);
	__devlink_compat_running_version(devlink, buf, len);
	mutex_unlock(&devlink->lock);

out:
	rtnl_lock();
	dev_put(dev);
}

int devlink_compat_flash_update(struct net_device *dev, const char *file_name)
{
	struct devlink_flash_update_params params = {};
	struct devlink *devlink;
	int ret;

	dev_hold(dev);
	rtnl_unlock();

	devlink = netdev_to_devlink(dev);
	if (!devlink || !devlink->ops->flash_update) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = request_firmware(&params.fw, file_name, devlink->dev);
	if (ret)
		goto out;

	mutex_lock(&devlink->lock);
	devlink_flash_update_begin_notify(devlink);
	ret = devlink->ops->flash_update(devlink, &params, NULL);
	devlink_flash_update_end_notify(devlink);
	mutex_unlock(&devlink->lock);

	release_firmware(params.fw);

out:
	rtnl_lock();
	dev_put(dev);

	return ret;
}

int devlink_compat_phys_port_name_get(struct net_device *dev,
				      char *name, size_t len)
{
	struct devlink_port *devlink_port;

	/* RTNL mutex is held here which ensures that devlink_port
	 * instance cannot disappear in the middle. No need to take
	 * any devlink lock as only permanent values are accessed.
	 */
	ASSERT_RTNL();

	devlink_port = netdev_to_devlink_port(dev);
	if (!devlink_port)
		return -EOPNOTSUPP;

	return __devlink_port_phys_port_name_get(devlink_port, name, len);
}

int devlink_compat_switch_id_get(struct net_device *dev,
				 struct netdev_phys_item_id *ppid)
{
	struct devlink_port *devlink_port;

	/* Caller must hold RTNL mutex or reference to dev, which ensures that
	 * devlink_port instance cannot disappear in the middle. No need to take
	 * any devlink lock as only permanent values are accessed.
	 */
	devlink_port = netdev_to_devlink_port(dev);
	if (!devlink_port || !devlink_port->switch_port)
		return -EOPNOTSUPP;

	memcpy(ppid, &devlink_port->attrs.switch_id, sizeof(*ppid));

	return 0;
}

static void __net_exit devlink_pernet_pre_exit(struct net *net)
{
	struct devlink *devlink;
	u32 actions_performed;
	int err;

	/* In case network namespace is getting destroyed, reload
	 * all devlink instances from this namespace into init_net.
	 */
	mutex_lock(&devlink_mutex);
	list_for_each_entry(devlink, &devlink_list, list) {
		if (net_eq(devlink_net(devlink), net)) {
			if (WARN_ON(!devlink_reload_supported(devlink->ops)))
				continue;
			err = devlink_reload(devlink, &init_net,
					     DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
					     DEVLINK_RELOAD_LIMIT_UNSPEC,
					     &actions_performed, NULL);
			if (err && err != -EOPNOTSUPP)
				pr_warn("Failed to reload devlink instance into init_net\n");
		}
	}
	mutex_unlock(&devlink_mutex);
}

static struct pernet_operations devlink_pernet_ops __net_initdata = {
	.pre_exit = devlink_pernet_pre_exit,
};

static int __init devlink_init(void)
{
	int err;

	err = genl_register_family(&devlink_nl_family);
	if (err)
		goto out;
	err = register_pernet_subsys(&devlink_pernet_ops);

out:
	WARN_ON(err);
	return err;
}

subsys_initcall(devlink_init);

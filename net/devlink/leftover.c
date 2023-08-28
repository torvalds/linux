// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/devlink.c - Network physical/parent device Netlink interface
 *
 * Heavily inspired by net/wireless/
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/etherdevice.h>
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

#include "devl_internal.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwmsg);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwerr);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_trap_report);

static inline bool
devlink_rate_is_leaf(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_LEAF;
}

static inline bool
devlink_rate_is_node(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_NODE;
}

static struct devlink_rate *
devlink_rate_leaf_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	struct devlink_rate *devlink_rate;
	struct devlink_port *devlink_port;

	devlink_port = devlink_port_get_from_attrs(devlink, info->attrs);
	if (IS_ERR(devlink_port))
		return ERR_CAST(devlink_port);
	devlink_rate = devlink_port->devlink_rate;
	return devlink_rate ?: ERR_PTR(-ENODEV);
}

static struct devlink_rate *
devlink_rate_node_get_by_name(struct devlink *devlink, const char *node_name)
{
	static struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		if (devlink_rate_is_node(devlink_rate) &&
		    !strcmp(node_name, devlink_rate->name))
			return devlink_rate;
	}
	return ERR_PTR(-ENODEV);
}

static struct devlink_rate *
devlink_rate_node_get_from_attrs(struct devlink *devlink, struct nlattr **attrs)
{
	const char *rate_node_name;
	size_t len;

	if (!attrs[DEVLINK_ATTR_RATE_NODE_NAME])
		return ERR_PTR(-EINVAL);
	rate_node_name = nla_data(attrs[DEVLINK_ATTR_RATE_NODE_NAME]);
	len = strlen(rate_node_name);
	/* Name cannot be empty or decimal number */
	if (!len || strspn(rate_node_name, "0123456789") == len)
		return ERR_PTR(-EINVAL);

	return devlink_rate_node_get_by_name(devlink, rate_node_name);
}

static struct devlink_rate *
devlink_rate_node_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	return devlink_rate_node_get_from_attrs(devlink, info->attrs);
}

static struct devlink_rate *
devlink_rate_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;

	if (attrs[DEVLINK_ATTR_PORT_INDEX])
		return devlink_rate_leaf_get_from_info(devlink, info);
	else if (attrs[DEVLINK_ATTR_RATE_NODE_NAME])
		return devlink_rate_node_get_from_info(devlink, info);
	else
		return ERR_PTR(-EINVAL);
}

static struct devlink_linecard *
devlink_linecard_get_by_index(struct devlink *devlink,
			      unsigned int linecard_index)
{
	struct devlink_linecard *devlink_linecard;

	list_for_each_entry(devlink_linecard, &devlink->linecard_list, list) {
		if (devlink_linecard->index == linecard_index)
			return devlink_linecard;
	}
	return NULL;
}

static bool devlink_linecard_index_exists(struct devlink *devlink,
					  unsigned int linecard_index)
{
	return devlink_linecard_get_by_index(devlink, linecard_index);
}

static struct devlink_linecard *
devlink_linecard_get_from_attrs(struct devlink *devlink, struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_LINECARD_INDEX]) {
		u32 linecard_index = nla_get_u32(attrs[DEVLINK_ATTR_LINECARD_INDEX]);
		struct devlink_linecard *linecard;

		linecard = devlink_linecard_get_by_index(devlink, linecard_index);
		if (!linecard)
			return ERR_PTR(-ENODEV);
		return linecard;
	}
	return ERR_PTR(-EINVAL);
}

static struct devlink_linecard *
devlink_linecard_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	return devlink_linecard_get_from_attrs(devlink, info->attrs);
}

struct devlink_region {
	struct devlink *devlink;
	struct devlink_port *port;
	struct list_head list;
	union {
		const struct devlink_region_ops *ops;
		const struct devlink_port_region_ops *port_ops;
	};
	struct mutex snapshot_lock; /* protects snapshot_list,
				     * max_snapshots and cur_snapshots
				     * consistency.
				     */
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

static int devlink_nl_put_nested_handle(struct sk_buff *msg, struct devlink *devlink)
{
	struct nlattr *nested_attr;

	nested_attr = nla_nest_start(msg, DEVLINK_ATTR_NESTED_DEVLINK);
	if (!nested_attr)
		return -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	nla_nest_end(msg, nested_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, nested_attr);
	return -EMSGSIZE;
}

static int devlink_nl_rate_fill(struct sk_buff *msg,
				struct devlink_rate *devlink_rate,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags, struct netlink_ext_ack *extack)
{
	struct devlink *devlink = devlink_rate->devlink;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_u16(msg, DEVLINK_ATTR_RATE_TYPE, devlink_rate->type))
		goto nla_put_failure;

	if (devlink_rate_is_leaf(devlink_rate)) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				devlink_rate->devlink_port->index))
			goto nla_put_failure;
	} else if (devlink_rate_is_node(devlink_rate)) {
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_NODE_NAME,
				   devlink_rate->name))
			goto nla_put_failure;
	}

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_RATE_TX_SHARE,
			      devlink_rate->tx_share, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_RATE_TX_MAX,
			      devlink_rate->tx_max, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_PRIORITY,
			devlink_rate->tx_priority))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_WEIGHT,
			devlink_rate->tx_weight))
		goto nla_put_failure;

	if (devlink_rate->parent)
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_PARENT_NODE_NAME,
				   devlink_rate->parent->name))
			goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_rate_notify(struct devlink_rate *devlink_rate,
				enum devlink_command cmd)
{
	struct devlink *devlink = devlink_rate->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_RATE_NEW && cmd != DEVLINK_CMD_RATE_DEL);

	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_rate_fill(msg, devlink_rate, cmd, 0, 0, 0, NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static void devlink_rates_notify_register(struct devlink *devlink)
{
	struct devlink_rate *rate_node;

	list_for_each_entry(rate_node, &devlink->rate_list, list)
		devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
}

static void devlink_rates_notify_unregister(struct devlink *devlink)
{
	struct devlink_rate *rate_node;

	list_for_each_entry_reverse(rate_node, &devlink->rate_list, list)
		devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);
}

static int
devlink_nl_rate_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
			     struct netlink_callback *cb, int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_rate *devlink_rate;
	int idx = 0;
	int err = 0;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		enum devlink_command cmd = DEVLINK_CMD_RATE_NEW;
		u32 id = NETLINK_CB(cb->skb).portid;

		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_rate_fill(msg, devlink_rate, cmd, id,
					   cb->nlh->nlmsg_seq, flags, NULL);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_rate_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_rate_get_dump_one);
}

int devlink_nl_rate_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *devlink_rate;
	struct sk_buff *msg;
	int err;

	devlink_rate = devlink_rate_get_from_info(devlink, info);
	if (IS_ERR(devlink_rate))
		return PTR_ERR(devlink_rate);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_rate_fill(msg, devlink_rate, DEVLINK_CMD_RATE_NEW,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static bool
devlink_rate_is_parent_node(struct devlink_rate *devlink_rate,
			    struct devlink_rate *parent)
{
	while (parent) {
		if (parent == devlink_rate)
			return true;
		parent = parent->parent;
	}
	return false;
}

static int
devlink_nl_rate_parent_node_set(struct devlink_rate *devlink_rate,
				struct genl_info *info,
				struct nlattr *nla_parent)
{
	struct devlink *devlink = devlink_rate->devlink;
	const char *parent_name = nla_data(nla_parent);
	const struct devlink_ops *ops = devlink->ops;
	size_t len = strlen(parent_name);
	struct devlink_rate *parent;
	int err = -EOPNOTSUPP;

	parent = devlink_rate->parent;

	if (parent && !len) {
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		if (err)
			return err;

		refcount_dec(&parent->refcnt);
		devlink_rate->parent = NULL;
	} else if (len) {
		parent = devlink_rate_node_get_by_name(devlink, parent_name);
		if (IS_ERR(parent))
			return -ENODEV;

		if (parent == devlink_rate) {
			NL_SET_ERR_MSG(info->extack, "Parent to self is not allowed");
			return -EINVAL;
		}

		if (devlink_rate_is_node(devlink_rate) &&
		    devlink_rate_is_parent_node(devlink_rate, parent->parent)) {
			NL_SET_ERR_MSG(info->extack, "Node is already a parent of parent node.");
			return -EEXIST;
		}

		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, parent,
							devlink_rate->priv, parent->priv,
							info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_parent_set(devlink_rate, parent,
							devlink_rate->priv, parent->priv,
							info->extack);
		if (err)
			return err;

		if (devlink_rate->parent)
			/* we're reassigning to other parent in this case */
			refcount_dec(&devlink_rate->parent->refcnt);

		refcount_inc(&parent->refcnt);
		devlink_rate->parent = parent;
	}

	return 0;
}

static int devlink_nl_rate_set(struct devlink_rate *devlink_rate,
			       const struct devlink_ops *ops,
			       struct genl_info *info)
{
	struct nlattr *nla_parent, **attrs = info->attrs;
	int err = -EOPNOTSUPP;
	u32 priority;
	u32 weight;
	u64 rate;

	if (attrs[DEVLINK_ATTR_RATE_TX_SHARE]) {
		rate = nla_get_u64(attrs[DEVLINK_ATTR_RATE_TX_SHARE]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_share_set(devlink_rate, devlink_rate->priv,
							  rate, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_share_set(devlink_rate, devlink_rate->priv,
							  rate, info->extack);
		if (err)
			return err;
		devlink_rate->tx_share = rate;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_MAX]) {
		rate = nla_get_u64(attrs[DEVLINK_ATTR_RATE_TX_MAX]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_max_set(devlink_rate, devlink_rate->priv,
							rate, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_max_set(devlink_rate, devlink_rate->priv,
							rate, info->extack);
		if (err)
			return err;
		devlink_rate->tx_max = rate;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY]) {
		priority = nla_get_u32(attrs[DEVLINK_ATTR_RATE_TX_PRIORITY]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_priority_set(devlink_rate, devlink_rate->priv,
							     priority, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_priority_set(devlink_rate, devlink_rate->priv,
							     priority, info->extack);

		if (err)
			return err;
		devlink_rate->tx_priority = priority;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT]) {
		weight = nla_get_u32(attrs[DEVLINK_ATTR_RATE_TX_WEIGHT]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_weight_set(devlink_rate, devlink_rate->priv,
							   weight, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_weight_set(devlink_rate, devlink_rate->priv,
							   weight, info->extack);

		if (err)
			return err;
		devlink_rate->tx_weight = weight;
	}

	nla_parent = attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME];
	if (nla_parent) {
		err = devlink_nl_rate_parent_node_set(devlink_rate, info,
						      nla_parent);
		if (err)
			return err;
	}

	return 0;
}

static bool devlink_rate_set_ops_supported(const struct devlink_ops *ops,
					   struct genl_info *info,
					   enum devlink_rate_type type)
{
	struct nlattr **attrs = info->attrs;

	if (type == DEVLINK_RATE_TYPE_LEAF) {
		if (attrs[DEVLINK_ATTR_RATE_TX_SHARE] && !ops->rate_leaf_tx_share_set) {
			NL_SET_ERR_MSG(info->extack, "TX share set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_MAX] && !ops->rate_leaf_tx_max_set) {
			NL_SET_ERR_MSG(info->extack, "TX max set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] &&
		    !ops->rate_leaf_parent_set) {
			NL_SET_ERR_MSG(info->extack, "Parent set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY] && !ops->rate_leaf_tx_priority_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_PRIORITY],
					    "TX priority set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT] && !ops->rate_leaf_tx_weight_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_WEIGHT],
					    "TX weight set isn't supported for the leafs");
			return false;
		}
	} else if (type == DEVLINK_RATE_TYPE_NODE) {
		if (attrs[DEVLINK_ATTR_RATE_TX_SHARE] && !ops->rate_node_tx_share_set) {
			NL_SET_ERR_MSG(info->extack, "TX share set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_MAX] && !ops->rate_node_tx_max_set) {
			NL_SET_ERR_MSG(info->extack, "TX max set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] &&
		    !ops->rate_node_parent_set) {
			NL_SET_ERR_MSG(info->extack, "Parent set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY] && !ops->rate_node_tx_priority_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_PRIORITY],
					    "TX priority set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT] && !ops->rate_node_tx_weight_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_WEIGHT],
					    "TX weight set isn't supported for the nodes");
			return false;
		}
	} else {
		WARN(1, "Unknown type of rate object");
		return false;
	}

	return true;
}

static int devlink_nl_cmd_rate_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *devlink_rate;
	const struct devlink_ops *ops;
	int err;

	devlink_rate = devlink_rate_get_from_info(devlink, info);
	if (IS_ERR(devlink_rate))
		return PTR_ERR(devlink_rate);

	ops = devlink->ops;
	if (!ops || !devlink_rate_set_ops_supported(ops, info, devlink_rate->type))
		return -EOPNOTSUPP;

	err = devlink_nl_rate_set(devlink_rate, ops, info);

	if (!err)
		devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);
	return err;
}

static int devlink_nl_cmd_rate_new_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *rate_node;
	const struct devlink_ops *ops;
	int err;

	ops = devlink->ops;
	if (!ops || !ops->rate_node_new || !ops->rate_node_del) {
		NL_SET_ERR_MSG(info->extack, "Rate nodes aren't supported");
		return -EOPNOTSUPP;
	}

	if (!devlink_rate_set_ops_supported(ops, info, DEVLINK_RATE_TYPE_NODE))
		return -EOPNOTSUPP;

	rate_node = devlink_rate_node_get_from_attrs(devlink, info->attrs);
	if (!IS_ERR(rate_node))
		return -EEXIST;
	else if (rate_node == ERR_PTR(-EINVAL))
		return -EINVAL;

	rate_node = kzalloc(sizeof(*rate_node), GFP_KERNEL);
	if (!rate_node)
		return -ENOMEM;

	rate_node->devlink = devlink;
	rate_node->type = DEVLINK_RATE_TYPE_NODE;
	rate_node->name = nla_strdup(info->attrs[DEVLINK_ATTR_RATE_NODE_NAME], GFP_KERNEL);
	if (!rate_node->name) {
		err = -ENOMEM;
		goto err_strdup;
	}

	err = ops->rate_node_new(rate_node, &rate_node->priv, info->extack);
	if (err)
		goto err_node_new;

	err = devlink_nl_rate_set(rate_node, ops, info);
	if (err)
		goto err_rate_set;

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	return 0;

err_rate_set:
	ops->rate_node_del(rate_node, rate_node->priv, info->extack);
err_node_new:
	kfree(rate_node->name);
err_strdup:
	kfree(rate_node);
	return err;
}

static int devlink_nl_cmd_rate_del_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *rate_node;
	int err;

	rate_node = devlink_rate_node_get_from_info(devlink, info);
	if (IS_ERR(rate_node))
		return PTR_ERR(rate_node);

	if (refcount_read(&rate_node->refcnt) > 1) {
		NL_SET_ERR_MSG(info->extack, "Node has children. Cannot delete node.");
		return -EBUSY;
	}

	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);
	err = devlink->ops->rate_node_del(rate_node, rate_node->priv,
					  info->extack);
	if (rate_node->parent)
		refcount_dec(&rate_node->parent->refcnt);
	list_del(&rate_node->list);
	kfree(rate_node->name);
	kfree(rate_node);
	return err;
}

struct devlink_linecard_type {
	const char *type;
	const void *priv;
};

static int devlink_nl_linecard_fill(struct sk_buff *msg,
				    struct devlink *devlink,
				    struct devlink_linecard *linecard,
				    enum devlink_command cmd, u32 portid,
				    u32 seq, int flags,
				    struct netlink_ext_ack *extack)
{
	struct devlink_linecard_type *linecard_type;
	struct nlattr *attr;
	void *hdr;
	int i;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_LINECARD_INDEX, linecard->index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_LINECARD_STATE, linecard->state))
		goto nla_put_failure;
	if (linecard->type &&
	    nla_put_string(msg, DEVLINK_ATTR_LINECARD_TYPE, linecard->type))
		goto nla_put_failure;

	if (linecard->types_count) {
		attr = nla_nest_start(msg,
				      DEVLINK_ATTR_LINECARD_SUPPORTED_TYPES);
		if (!attr)
			goto nla_put_failure;
		for (i = 0; i < linecard->types_count; i++) {
			linecard_type = &linecard->types[i];
			if (nla_put_string(msg, DEVLINK_ATTR_LINECARD_TYPE,
					   linecard_type->type)) {
				nla_nest_cancel(msg, attr);
				goto nla_put_failure;
			}
		}
		nla_nest_end(msg, attr);
	}

	if (linecard->nested_devlink &&
	    devlink_nl_put_nested_handle(msg, linecard->nested_devlink))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_linecard_notify(struct devlink_linecard *linecard,
				    enum devlink_command cmd)
{
	struct devlink *devlink = linecard->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_LINECARD_NEW &&
		cmd != DEVLINK_CMD_LINECARD_DEL);

	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_linecard_fill(msg, devlink, linecard, cmd, 0, 0, 0,
				       NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static void devlink_linecards_notify_register(struct devlink *devlink)
{
	struct devlink_linecard *linecard;

	list_for_each_entry(linecard, &devlink->linecard_list, list)
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
}

static void devlink_linecards_notify_unregister(struct devlink *devlink)
{
	struct devlink_linecard *linecard;

	list_for_each_entry_reverse(linecard, &devlink->linecard_list, list)
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_DEL);
}

int devlink_nl_linecard_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_linecard *linecard;
	struct sk_buff *msg;
	int err;

	linecard = devlink_linecard_get_from_info(devlink, info);
	if (IS_ERR(linecard))
		return PTR_ERR(linecard);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	mutex_lock(&linecard->state_lock);
	err = devlink_nl_linecard_fill(msg, devlink, linecard,
				       DEVLINK_CMD_LINECARD_NEW,
				       info->snd_portid, info->snd_seq, 0,
				       info->extack);
	mutex_unlock(&linecard->state_lock);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_linecard_get_dump_one(struct sk_buff *msg,
					    struct devlink *devlink,
					    struct netlink_callback *cb,
					    int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_linecard *linecard;
	int idx = 0;
	int err = 0;

	list_for_each_entry(linecard, &devlink->linecard_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		mutex_lock(&linecard->state_lock);
		err = devlink_nl_linecard_fill(msg, devlink, linecard,
					       DEVLINK_CMD_LINECARD_NEW,
					       NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq, flags,
					       cb->extack);
		mutex_unlock(&linecard->state_lock);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_linecard_get_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_linecard_get_dump_one);
}

static struct devlink_linecard_type *
devlink_linecard_type_lookup(struct devlink_linecard *linecard,
			     const char *type)
{
	struct devlink_linecard_type *linecard_type;
	int i;

	for (i = 0; i < linecard->types_count; i++) {
		linecard_type = &linecard->types[i];
		if (!strcmp(type, linecard_type->type))
			return linecard_type;
	}
	return NULL;
}

static int devlink_linecard_type_set(struct devlink_linecard *linecard,
				     const char *type,
				     struct netlink_ext_ack *extack)
{
	const struct devlink_linecard_ops *ops = linecard->ops;
	struct devlink_linecard_type *linecard_type;
	int err;

	mutex_lock(&linecard->state_lock);
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being provisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being unprovisioned");
		err = -EBUSY;
		goto out;
	}

	linecard_type = devlink_linecard_type_lookup(linecard, type);
	if (!linecard_type) {
		NL_SET_ERR_MSG(extack, "Unsupported line card type provided");
		err = -EINVAL;
		goto out;
	}

	if (linecard->state != DEVLINK_LINECARD_STATE_UNPROVISIONED &&
	    linecard->state != DEVLINK_LINECARD_STATE_PROVISIONING_FAILED) {
		NL_SET_ERR_MSG(extack, "Line card already provisioned");
		err = -EBUSY;
		/* Check if the line card is provisioned in the same
		 * way the user asks. In case it is, make the operation
		 * to return success.
		 */
		if (ops->same_provision &&
		    ops->same_provision(linecard, linecard->priv,
					linecard_type->type,
					linecard_type->priv))
			err = 0;
		goto out;
	}

	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONING;
	linecard->type = linecard_type->type;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
	err = ops->provision(linecard, linecard->priv, linecard_type->type,
			     linecard_type->priv, extack);
	if (err) {
		/* Provisioning failed. Assume the linecard is unprovisioned
		 * for future operations.
		 */
		mutex_lock(&linecard->state_lock);
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		mutex_unlock(&linecard->state_lock);
	}
	return err;

out:
	mutex_unlock(&linecard->state_lock);
	return err;
}

static int devlink_linecard_type_unset(struct devlink_linecard *linecard,
				       struct netlink_ext_ack *extack)
{
	int err;

	mutex_lock(&linecard->state_lock);
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being provisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being unprovisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING_FAILED) {
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		err = 0;
		goto out;
	}

	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONED) {
		NL_SET_ERR_MSG(extack, "Line card is not provisioned");
		err = 0;
		goto out;
	}
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONING;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
	err = linecard->ops->unprovision(linecard, linecard->priv,
					 extack);
	if (err) {
		/* Unprovisioning failed. Assume the linecard is unprovisioned
		 * for future operations.
		 */
		mutex_lock(&linecard->state_lock);
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		mutex_unlock(&linecard->state_lock);
	}
	return err;

out:
	mutex_unlock(&linecard->state_lock);
	return err;
}

static int devlink_nl_cmd_linecard_set_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_linecard *linecard;
	int err;

	linecard = devlink_linecard_get_from_info(devlink, info);
	if (IS_ERR(linecard))
		return PTR_ERR(linecard);

	if (info->attrs[DEVLINK_ATTR_LINECARD_TYPE]) {
		const char *type;

		type = nla_data(info->attrs[DEVLINK_ATTR_LINECARD_TYPE]);
		if (strcmp(type, "")) {
			err = devlink_linecard_type_set(linecard, type, extack);
			if (err)
				return err;
		} else {
			err = devlink_linecard_type_unset(linecard, extack);
			if (err)
				return err;
		}
	}

	return 0;
}

int devlink_rate_nodes_check(struct devlink *devlink, u16 mode,
			     struct netlink_ext_ack *extack)
{
	struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list)
		if (devlink_rate_is_node(devlink_rate)) {
			NL_SET_ERR_MSG(extack, "Rate node(s) exists.");
			return -EBUSY;
		}
	return 0;
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

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_MAX_SNAPSHOTS,
			  region->max_snapshots);
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
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = devlink_nl_region_notify_build(region, snapshot, cmd, 0, 0);
	if (IS_ERR(msg))
		return;

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static void devlink_regions_notify_register(struct devlink *devlink)
{
	struct devlink_region *region;

	list_for_each_entry(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);
}

static void devlink_regions_notify_unregister(struct devlink *devlink)
{
	struct devlink_region *region;

	list_for_each_entry_reverse(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
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
	int err;

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p)) {
		err = -EINVAL;
		goto unlock;
	}

	if (WARN_ON(!xa_is_value(p))) {
		err = -EINVAL;
		goto unlock;
	}

	count = xa_to_value(p);
	count++;

	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
				GFP_ATOMIC));
unlock:
	xa_unlock(&devlink->snapshot_ids);
	return err;
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

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p))
		goto unlock;

	if (WARN_ON(!xa_is_value(p)))
		goto unlock;

	count = xa_to_value(p);

	if (count > 1) {
		count--;
		__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
			   GFP_ATOMIC);
	} else {
		/* If this was the last user, we can erase this id */
		__xa_erase(&devlink->snapshot_ids, id);
	}
unlock:
	xa_unlock(&devlink->snapshot_ids);
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
	int err;

	xa_lock(&devlink->snapshot_ids);
	if (xa_load(&devlink->snapshot_ids, id)) {
		xa_unlock(&devlink->snapshot_ids);
		return -EEXIST;
	}
	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(0),
				GFP_ATOMIC));
	xa_unlock(&devlink->snapshot_ids);
	return err;
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
 *	Must be called only while holding the region snapshot lock.
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

	lockdep_assert_held(&region->snapshot_lock);

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

	lockdep_assert_held(&region->snapshot_lock);

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_DEL);
	region->cur_snapshots--;
	list_del(&snapshot->list);
	region->ops->destructor(snapshot->data);
	__devlink_snapshot_id_decrement(devlink, snapshot->id);
	kfree(snapshot);
}

int devlink_nl_region_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	struct sk_buff *msg;
	unsigned int index;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME))
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
						 int *idx, int start, int flags)
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
					     flags, region);
		if (err)
			goto out;
		(*idx)++;
	}

out:
	return err;
}

static int devlink_nl_region_get_dump_one(struct sk_buff *msg,
					  struct devlink *devlink,
					  struct netlink_callback *cb,
					  int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_region *region;
	struct devlink_port *port;
	unsigned long port_index;
	int idx = 0;
	int err;

	list_for_each_entry(region, &devlink->region_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_region_fill(msg, devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq, flags,
					     region);
		if (err) {
			state->idx = idx;
			return err;
		}
		idx++;
	}

	xa_for_each(&devlink->ports, port_index, port) {
		err = devlink_nl_cmd_region_get_port_dumpit(msg, cb, port, &idx,
							    state->idx, flags);
		if (err) {
			state->idx = idx;
			return err;
		}
	}

	return 0;
}

int devlink_nl_region_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_region_get_dump_one);
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

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME) ||
	    GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_SNAPSHOT_ID))
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

	mutex_lock(&region->snapshot_lock);
	snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
	if (!snapshot) {
		mutex_unlock(&region->snapshot_lock);
		return -EINVAL;
	}

	devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);
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

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME)) {
		NL_SET_ERR_MSG(info->extack, "No region name provided");
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
		NL_SET_ERR_MSG(info->extack, "The requested region does not exist");
		return -EINVAL;
	}

	if (!region->ops->snapshot) {
		NL_SET_ERR_MSG(info->extack, "The requested region does not support taking an immediate snapshot");
		return -EOPNOTSUPP;
	}

	mutex_lock(&region->snapshot_lock);

	if (region->cur_snapshots == region->max_snapshots) {
		NL_SET_ERR_MSG(info->extack, "The region has reached the maximum number of stored snapshots");
		err = -ENOSPC;
		goto unlock;
	}

	snapshot_id_attr = info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (snapshot_id_attr) {
		snapshot_id = nla_get_u32(snapshot_id_attr);

		if (devlink_region_snapshot_get_by_id(region, snapshot_id)) {
			NL_SET_ERR_MSG(info->extack, "The requested snapshot id is already in use");
			err = -EEXIST;
			goto unlock;
		}

		err = __devlink_snapshot_id_insert(devlink, snapshot_id);
		if (err)
			goto unlock;
	} else {
		err = __devlink_region_snapshot_id_get(devlink, &snapshot_id);
		if (err) {
			NL_SET_ERR_MSG(info->extack, "Failed to allocate a new snapshot id");
			goto unlock;
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
		if (WARN_ON(!snapshot)) {
			err = -EINVAL;
			goto unlock;
		}

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

	mutex_unlock(&region->snapshot_lock);
	return 0;

err_snapshot_create:
	region->ops->destructor(data);
err_snapshot_capture:
	__devlink_snapshot_id_decrement(devlink, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
	return err;

err_notify:
	devlink_region_snapshot_del(region, snapshot);
unlock:
	mutex_unlock(&region->snapshot_lock);
	return err;
}

static int devlink_nl_cmd_region_read_chunk_fill(struct sk_buff *msg,
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

typedef int devlink_chunk_fill_t(void *cb_priv, u8 *chunk, u32 chunk_size,
				 u64 curr_offset,
				 struct netlink_ext_ack *extack);

static int
devlink_nl_region_read_fill(struct sk_buff *skb, devlink_chunk_fill_t *cb,
			    void *cb_priv, u64 start_offset, u64 end_offset,
			    u64 *new_offset, struct netlink_ext_ack *extack)
{
	u64 curr_offset = start_offset;
	int err = 0;
	u8 *data;

	/* Allocate and re-use a single buffer */
	data = kmalloc(DEVLINK_REGION_READ_CHUNK_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	*new_offset = start_offset;

	while (curr_offset < end_offset) {
		u32 data_size;

		data_size = min_t(u32, end_offset - curr_offset,
				  DEVLINK_REGION_READ_CHUNK_SIZE);

		err = cb(cb_priv, data, data_size, curr_offset, extack);
		if (err)
			break;

		err = devlink_nl_cmd_region_read_chunk_fill(skb, data, data_size, curr_offset);
		if (err)
			break;

		curr_offset += data_size;
	}
	*new_offset = curr_offset;

	kfree(data);

	return err;
}

static int
devlink_region_snapshot_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			     u64 curr_offset,
			     struct netlink_ext_ack __always_unused *extack)
{
	struct devlink_snapshot *snapshot = cb_priv;

	memcpy(chunk, &snapshot->data[curr_offset], chunk_size);

	return 0;
}

static int
devlink_region_port_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
				u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->port_ops->read(region->port, region->port_ops, extack,
				      curr_offset, chunk_size, chunk);
}

static int
devlink_region_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			   u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->ops->read(region->devlink, region->ops, extack,
				 curr_offset, chunk_size, chunk);
}

static int devlink_nl_cmd_region_read_dumpit(struct sk_buff *skb,
					     struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct nlattr *chunks_attr, *region_attr, *snapshot_attr;
	u64 ret_offset, start_offset, end_offset = U64_MAX;
	struct nlattr **attrs = info->info.attrs;
	struct devlink_port *port = NULL;
	devlink_chunk_fill_t *region_cb;
	struct devlink_region *region;
	const char *region_name;
	struct devlink *devlink;
	unsigned int index;
	void *region_cb_priv;
	void *hdr;
	int err;

	start_offset = state->start_offset;

	devlink = devlink_get_from_attrs_lock(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);

	if (!attrs[DEVLINK_ATTR_REGION_NAME]) {
		NL_SET_ERR_MSG(cb->extack, "No region name provided");
		err = -EINVAL;
		goto out_unlock;
	}

	if (attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	region_attr = attrs[DEVLINK_ATTR_REGION_NAME];
	region_name = nla_data(region_attr);

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG_ATTR(cb->extack, region_attr, "Requested region does not exist");
		err = -EINVAL;
		goto out_unlock;
	}

	snapshot_attr = attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (!snapshot_attr) {
		if (!nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG(cb->extack, "No snapshot id provided");
			err = -EINVAL;
			goto out_unlock;
		}

		if (!region->ops->read) {
			NL_SET_ERR_MSG(cb->extack, "Requested region does not support direct read");
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		if (port)
			region_cb = &devlink_region_port_direct_fill;
		else
			region_cb = &devlink_region_direct_fill;
		region_cb_priv = region;
	} else {
		struct devlink_snapshot *snapshot;
		u32 snapshot_id;

		if (nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Direct region read does not use snapshot");
			err = -EINVAL;
			goto out_unlock;
		}

		snapshot_id = nla_get_u32(snapshot_attr);
		snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
		if (!snapshot) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Requested snapshot does not exist");
			err = -EINVAL;
			goto out_unlock;
		}
		region_cb = &devlink_region_snapshot_fill;
		region_cb_priv = snapshot;
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

	err = devlink_nl_region_read_fill(skb, region_cb, region_cb_priv,
					  start_offset, end_offset, &ret_offset,
					  cb->extack);

	if (err && err != -EMSGSIZE)
		goto nla_put_failure;

	/* Check if there was any progress done to prevent infinite loop */
	if (ret_offset == start_offset) {
		err = -EINVAL;
		goto nla_put_failure;
	}

	state->start_offset = ret_offset;

	nla_nest_end(skb, chunks_attr);
	genlmsg_end(skb, hdr);
	devl_unlock(devlink);
	devlink_put(devlink);
	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out_unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

struct devlink_stats {
	u64_stats_t rx_bytes;
	u64_stats_t rx_packets;
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
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			rx_packets = u64_stats_read(&cpu_stats->rx_packets);
			rx_bytes = u64_stats_read(&cpu_stats->rx_bytes);
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		u64_stats_add(&stats->rx_packets, rx_packets);
		u64_stats_add(&stats->rx_bytes, rx_bytes);
	}
}

static int
devlink_trap_group_stats_put(struct sk_buff *msg,
			     struct devlink_stats __percpu *trap_stats)
{
	struct devlink_stats stats;
	struct nlattr *attr;

	devlink_trap_stats_read(trap_stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_trap_stats_put(struct sk_buff *msg, struct devlink *devlink,
				  const struct devlink_trap_item *trap_item)
{
	struct devlink_stats stats;
	struct nlattr *attr;
	u64 drops = 0;
	int err;

	if (devlink->ops->trap_drop_counter_get) {
		err = devlink->ops->trap_drop_counter_get(devlink,
							  trap_item->trap,
							  &drops);
		if (err)
			return err;
	}

	devlink_trap_stats_read(trap_item->stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (devlink->ops->trap_drop_counter_get &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
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

	err = devlink_trap_stats_put(msg, devlink, trap_item);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int devlink_nl_trap_get_doit(struct sk_buff *skb, struct genl_info *info)
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
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

static int devlink_nl_trap_get_dump_one(struct sk_buff *msg,
					struct devlink *devlink,
					struct netlink_callback *cb, int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_item *trap_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_fill(msg, devlink, trap_item,
					   DEVLINK_CMD_TRAP_NEW,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_get_dump_one);
}

static int __devlink_trap_action_set(struct devlink *devlink,
				     struct devlink_trap_item *trap_item,
				     enum devlink_trap_action trap_action,
				     struct netlink_ext_ack *extack)
{
	int err;

	if (trap_item->action != trap_action &&
	    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP) {
		NL_SET_ERR_MSG(extack, "Cannot change action of non-drop traps. Skipping");
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
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
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

	err = devlink_trap_group_stats_put(msg, group_item->stats);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int devlink_nl_trap_group_get_doit(struct sk_buff *skb, struct genl_info *info)
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
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

static int devlink_nl_trap_group_get_dump_one(struct sk_buff *msg,
					      struct devlink *devlink,
					      struct netlink_callback *cb,
					      int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_group_item *group_item;
	int idx = 0;
	int err = 0;


	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_group_fill(msg, devlink, group_item,
						 DEVLINK_CMD_TRAP_GROUP_NEW,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_group_get_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_group_get_dump_one);
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
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
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
	u32 policer_id;
	int err;

	if (!attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return 0;

	if (!devlink->ops->trap_group_set)
		return -EOPNOTSUPP;

	policer_id = nla_get_u32(attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);
	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (policer_id && !policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
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
		NL_SET_ERR_MSG(extack, "Trap group set failed, but some changes were committed already");
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

int devlink_nl_trap_policer_get_doit(struct sk_buff *skb,
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
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

static int devlink_nl_trap_policer_get_dump_one(struct sk_buff *msg,
						struct devlink *devlink,
						struct netlink_callback *cb,
						int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_policer_item *policer_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
						   DEVLINK_CMD_TRAP_POLICER_NEW,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_policer_get_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_policer_get_dump_one);
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
		NL_SET_ERR_MSG(extack, "Policer rate lower than limit");
		return -EINVAL;
	}

	if (rate > policer_item->policer->max_rate) {
		NL_SET_ERR_MSG(extack, "Policer rate higher than limit");
		return -EINVAL;
	}

	if (burst < policer_item->policer->min_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size lower than limit");
		return -EINVAL;
	}

	if (burst > policer_item->policer->max_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size higher than limit");
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
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	return devlink_trap_policer_set(devlink, policer_item, info);
}

const struct genl_small_ops devlink_nl_small_ops[40] = {
	{
		.cmd = DEVLINK_CMD_PORT_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_RATE_SET,
		.doit = devlink_nl_cmd_rate_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RATE_NEW,
		.doit = devlink_nl_cmd_rate_new_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RATE_DEL,
		.doit = devlink_nl_cmd_rate_del_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PORT_SPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_split_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_PORT_UNSPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_unsplit_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_PORT_NEW,
		.doit = devlink_nl_cmd_port_new_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PORT_DEL,
		.doit = devlink_nl_cmd_port_del_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},

	{
		.cmd = DEVLINK_CMD_LINECARD_SET,
		.doit = devlink_nl_cmd_linecard_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_PORT_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_port_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
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
	},
	{
		.cmd = DEVLINK_CMD_ESWITCH_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_eswitch_set_doit,
		.flags = GENL_ADMIN_PERM,
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
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_RECOVER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_recover_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_diagnose_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = devlink_nl_cmd_health_reporter_dump_get_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_dump_clear_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_TEST,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_test_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_FLASH_UPDATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_flash_update,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_SET,
		.doit = devlink_nl_cmd_trap_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GROUP_SET,
		.doit = devlink_nl_cmd_trap_group_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_POLICER_SET,
		.doit = devlink_nl_cmd_trap_policer_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SELFTESTS_RUN,
		.doit = devlink_nl_cmd_selftests_run,
		.flags = GENL_ADMIN_PERM,
	},
	/* -- No new ops here! Use split ops going forward! -- */
};

static void devlink_trap_policers_notify_register(struct devlink *devlink);
static void devlink_trap_policers_notify_unregister(struct devlink *devlink);
static void devlink_trap_groups_notify_register(struct devlink *devlink);
static void devlink_trap_groups_notify_unregister(struct devlink *devlink);
static void devlink_traps_notify_register(struct devlink *devlink);
static void devlink_traps_notify_unregister(struct devlink *devlink);

void devlink_notify_register(struct devlink *devlink)
{
	devlink_notify(devlink, DEVLINK_CMD_NEW);
	devlink_linecards_notify_register(devlink);
	devlink_ports_notify_register(devlink);
	devlink_trap_policers_notify_register(devlink);
	devlink_trap_groups_notify_register(devlink);
	devlink_traps_notify_register(devlink);
	devlink_rates_notify_register(devlink);
	devlink_regions_notify_register(devlink);
	devlink_params_notify_register(devlink);
}

void devlink_notify_unregister(struct devlink *devlink)
{
	devlink_params_notify_unregister(devlink);
	devlink_regions_notify_unregister(devlink);
	devlink_rates_notify_unregister(devlink);
	devlink_traps_notify_unregister(devlink);
	devlink_trap_groups_notify_unregister(devlink);
	devlink_trap_policers_notify_unregister(devlink);
	devlink_ports_notify_unregister(devlink);
	devlink_linecards_notify_unregister(devlink);
	devlink_notify(devlink, DEVLINK_CMD_DEL);
}

/**
 * devl_rate_node_create - create devlink rate node
 * @devlink: devlink instance
 * @priv: driver private data
 * @node_name: name of the resulting node
 * @parent: parent devlink_rate struct
 *
 * Create devlink rate object of type node
 */
struct devlink_rate *
devl_rate_node_create(struct devlink *devlink, void *priv, char *node_name,
		      struct devlink_rate *parent)
{
	struct devlink_rate *rate_node;

	rate_node = devlink_rate_node_get_by_name(devlink, node_name);
	if (!IS_ERR(rate_node))
		return ERR_PTR(-EEXIST);

	rate_node = kzalloc(sizeof(*rate_node), GFP_KERNEL);
	if (!rate_node)
		return ERR_PTR(-ENOMEM);

	if (parent) {
		rate_node->parent = parent;
		refcount_inc(&rate_node->parent->refcnt);
	}

	rate_node->type = DEVLINK_RATE_TYPE_NODE;
	rate_node->devlink = devlink;
	rate_node->priv = priv;

	rate_node->name = kstrdup(node_name, GFP_KERNEL);
	if (!rate_node->name) {
		kfree(rate_node);
		return ERR_PTR(-ENOMEM);
	}

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	return rate_node;
}
EXPORT_SYMBOL_GPL(devl_rate_node_create);

/**
 * devl_rate_leaf_create - create devlink rate leaf
 * @devlink_port: devlink port object to create rate object on
 * @priv: driver private data
 * @parent: parent devlink_rate struct
 *
 * Create devlink rate object of type leaf on provided @devlink_port.
 */
int devl_rate_leaf_create(struct devlink_port *devlink_port, void *priv,
			  struct devlink_rate *parent)
{
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_rate *devlink_rate;

	devl_assert_locked(devlink_port->devlink);

	if (WARN_ON(devlink_port->devlink_rate))
		return -EBUSY;

	devlink_rate = kzalloc(sizeof(*devlink_rate), GFP_KERNEL);
	if (!devlink_rate)
		return -ENOMEM;

	if (parent) {
		devlink_rate->parent = parent;
		refcount_inc(&devlink_rate->parent->refcnt);
	}

	devlink_rate->type = DEVLINK_RATE_TYPE_LEAF;
	devlink_rate->devlink = devlink;
	devlink_rate->devlink_port = devlink_port;
	devlink_rate->priv = priv;
	list_add_tail(&devlink_rate->list, &devlink->rate_list);
	devlink_port->devlink_rate = devlink_rate;
	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);

	return 0;
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_create);

/**
 * devl_rate_leaf_destroy - destroy devlink rate leaf
 *
 * @devlink_port: devlink port linked to the rate object
 *
 * Destroy the devlink rate object of type leaf on provided @devlink_port.
 */
void devl_rate_leaf_destroy(struct devlink_port *devlink_port)
{
	struct devlink_rate *devlink_rate = devlink_port->devlink_rate;

	devl_assert_locked(devlink_port->devlink);
	if (!devlink_rate)
		return;

	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_DEL);
	if (devlink_rate->parent)
		refcount_dec(&devlink_rate->parent->refcnt);
	list_del(&devlink_rate->list);
	devlink_port->devlink_rate = NULL;
	kfree(devlink_rate);
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_destroy);

/**
 * devl_rate_nodes_destroy - destroy all devlink rate nodes on device
 * @devlink: devlink instance
 *
 * Unset parent for all rate objects and destroy all rate nodes
 * on specified device.
 */
void devl_rate_nodes_destroy(struct devlink *devlink)
{
	static struct devlink_rate *devlink_rate, *tmp;
	const struct devlink_ops *ops = devlink->ops;

	devl_assert_locked(devlink);

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		if (!devlink_rate->parent)
			continue;

		refcount_dec(&devlink_rate->parent->refcnt);
		if (devlink_rate_is_leaf(devlink_rate))
			ops->rate_leaf_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
		else if (devlink_rate_is_node(devlink_rate))
			ops->rate_node_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
	}
	list_for_each_entry_safe(devlink_rate, tmp, &devlink->rate_list, list) {
		if (devlink_rate_is_node(devlink_rate)) {
			ops->rate_node_del(devlink_rate, devlink_rate->priv, NULL);
			list_del(&devlink_rate->list);
			kfree(devlink_rate->name);
			kfree(devlink_rate);
		}
	}
}
EXPORT_SYMBOL_GPL(devl_rate_nodes_destroy);

static int devlink_linecard_types_init(struct devlink_linecard *linecard)
{
	struct devlink_linecard_type *linecard_type;
	unsigned int count;
	int i;

	count = linecard->ops->types_count(linecard, linecard->priv);
	linecard->types = kmalloc_array(count, sizeof(*linecard_type),
					GFP_KERNEL);
	if (!linecard->types)
		return -ENOMEM;
	linecard->types_count = count;

	for (i = 0; i < count; i++) {
		linecard_type = &linecard->types[i];
		linecard->ops->types_get(linecard, linecard->priv, i,
					 &linecard_type->type,
					 &linecard_type->priv);
	}
	return 0;
}

static void devlink_linecard_types_fini(struct devlink_linecard *linecard)
{
	kfree(linecard->types);
}

/**
 *	devl_linecard_create - Create devlink linecard
 *
 *	@devlink: devlink
 *	@linecard_index: driver-specific numerical identifier of the linecard
 *	@ops: linecards ops
 *	@priv: user priv pointer
 *
 *	Create devlink linecard instance with provided linecard index.
 *	Caller can use any indexing, even hw-related one.
 *
 *	Return: Line card structure or an ERR_PTR() encoded error code.
 */
struct devlink_linecard *
devl_linecard_create(struct devlink *devlink, unsigned int linecard_index,
		     const struct devlink_linecard_ops *ops, void *priv)
{
	struct devlink_linecard *linecard;
	int err;

	if (WARN_ON(!ops || !ops->provision || !ops->unprovision ||
		    !ops->types_count || !ops->types_get))
		return ERR_PTR(-EINVAL);

	if (devlink_linecard_index_exists(devlink, linecard_index))
		return ERR_PTR(-EEXIST);

	linecard = kzalloc(sizeof(*linecard), GFP_KERNEL);
	if (!linecard)
		return ERR_PTR(-ENOMEM);

	linecard->devlink = devlink;
	linecard->index = linecard_index;
	linecard->ops = ops;
	linecard->priv = priv;
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
	mutex_init(&linecard->state_lock);

	err = devlink_linecard_types_init(linecard);
	if (err) {
		mutex_destroy(&linecard->state_lock);
		kfree(linecard);
		return ERR_PTR(err);
	}

	list_add_tail(&linecard->list, &devlink->linecard_list);
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	return linecard;
}
EXPORT_SYMBOL_GPL(devl_linecard_create);

/**
 *	devl_linecard_destroy - Destroy devlink linecard
 *
 *	@linecard: devlink linecard
 */
void devl_linecard_destroy(struct devlink_linecard *linecard)
{
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_DEL);
	list_del(&linecard->list);
	devlink_linecard_types_fini(linecard);
	mutex_destroy(&linecard->state_lock);
	kfree(linecard);
}
EXPORT_SYMBOL_GPL(devl_linecard_destroy);

/**
 *	devlink_linecard_provision_set - Set provisioning on linecard
 *
 *	@linecard: devlink linecard
 *	@type: linecard type
 *
 *	This is either called directly from the provision() op call or
 *	as a result of the provision() op call asynchronously.
 */
void devlink_linecard_provision_set(struct devlink_linecard *linecard,
				    const char *type)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->type && strcmp(linecard->type, type));
	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONED;
	linecard->type = type;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_set);

/**
 *	devlink_linecard_provision_clear - Clear provisioning on linecard
 *
 *	@linecard: devlink linecard
 *
 *	This is either called directly from the unprovision() op call or
 *	as a result of the unprovision() op call asynchronously.
 */
void devlink_linecard_provision_clear(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->nested_devlink);
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
	linecard->type = NULL;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_clear);

/**
 *	devlink_linecard_provision_fail - Fail provisioning on linecard
 *
 *	@linecard: devlink linecard
 *
 *	This is either called directly from the provision() op call or
 *	as a result of the provision() op call asynchronously.
 */
void devlink_linecard_provision_fail(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->nested_devlink);
	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONING_FAILED;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_fail);

/**
 *	devlink_linecard_activate - Set linecard active
 *
 *	@linecard: devlink linecard
 */
void devlink_linecard_activate(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->state != DEVLINK_LINECARD_STATE_PROVISIONED);
	linecard->state = DEVLINK_LINECARD_STATE_ACTIVE;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_activate);

/**
 *	devlink_linecard_deactivate - Set linecard inactive
 *
 *	@linecard: devlink linecard
 */
void devlink_linecard_deactivate(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	switch (linecard->state) {
	case DEVLINK_LINECARD_STATE_ACTIVE:
		linecard->state = DEVLINK_LINECARD_STATE_PROVISIONED;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		break;
	case DEVLINK_LINECARD_STATE_UNPROVISIONING:
		/* Line card is being deactivated as part
		 * of unprovisioning flow.
		 */
		break;
	default:
		WARN_ON(1);
		break;
	}
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_deactivate);

/**
 *	devlink_linecard_nested_dl_set - Attach/detach nested devlink
 *					 instance to linecard.
 *
 *	@linecard: devlink linecard
 *	@nested_devlink: devlink instance to attach or NULL to detach
 */
void devlink_linecard_nested_dl_set(struct devlink_linecard *linecard,
				    struct devlink *nested_devlink)
{
	mutex_lock(&linecard->state_lock);
	linecard->nested_devlink = nested_devlink;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_nested_dl_set);

/**
 * devl_region_create - create a new address region
 *
 * @devlink: devlink
 * @ops: region operations and name
 * @region_max_snapshots: Maximum supported number of snapshots for region
 * @region_size: size of region
 */
struct devlink_region *devl_region_create(struct devlink *devlink,
					  const struct devlink_region_ops *ops,
					  u32 region_max_snapshots,
					  u64 region_size)
{
	struct devlink_region *region;

	devl_assert_locked(devlink);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	if (devlink_region_get_by_name(devlink, ops->name))
		return ERR_PTR(-EEXIST);

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);

	region->devlink = devlink;
	region->max_snapshots = region_max_snapshots;
	region->ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &devlink->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	return region;
}
EXPORT_SYMBOL_GPL(devl_region_create);

/**
 *	devlink_region_create - create a new address region
 *
 *	@devlink: devlink
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_region_create(struct devlink *devlink,
		      const struct devlink_region_ops *ops,
		      u32 region_max_snapshots, u64 region_size)
{
	struct devlink_region *region;

	devl_lock(devlink);
	region = devl_region_create(devlink, ops, region_max_snapshots,
				    region_size);
	devl_unlock(devlink);
	return region;
}
EXPORT_SYMBOL_GPL(devlink_region_create);

/**
 *	devlink_port_region_create - create a new address region for a port
 *
 *	@port: devlink port
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_port_region_create(struct devlink_port *port,
			   const struct devlink_port_region_ops *ops,
			   u32 region_max_snapshots, u64 region_size)
{
	struct devlink *devlink = port->devlink;
	struct devlink_region *region;
	int err = 0;

	ASSERT_DEVLINK_PORT_INITIALIZED(port);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	devl_lock(devlink);

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
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &port->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	devl_unlock(devlink);
	return region;

unlock:
	devl_unlock(devlink);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(devlink_port_region_create);

/**
 * devl_region_destroy - destroy address region
 *
 * @region: devlink region to destroy
 */
void devl_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot, *ts;

	devl_assert_locked(devlink);

	/* Free all snapshots of region */
	mutex_lock(&region->snapshot_lock);
	list_for_each_entry_safe(snapshot, ts, &region->snapshot_list, list)
		devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);

	list_del(&region->list);
	mutex_destroy(&region->snapshot_lock);

	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
	kfree(region);
}
EXPORT_SYMBOL_GPL(devl_region_destroy);

/**
 *	devlink_region_destroy - destroy address region
 *
 *	@region: devlink region to destroy
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;

	devl_lock(devlink);
	devl_region_destroy(region);
	devl_unlock(devlink);
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
	return __devlink_region_snapshot_id_get(devlink, id);
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
	__devlink_snapshot_id_decrement(devlink, id);
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
	int err;

	mutex_lock(&region->snapshot_lock);
	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
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
	DEVLINK_TRAP(EAPOL, CONTROL),
	DEVLINK_TRAP(LOCKED_PORT, DROP),
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
	DEVLINK_TRAP_GROUP(EAPOL),
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
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

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

static void devlink_trap_groups_notify_register(struct devlink *devlink)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_NEW);
}

static void devlink_trap_groups_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry_reverse(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_DEL);
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
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

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

static void devlink_traps_notify_register(struct devlink *devlink)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);
}

static void devlink_traps_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry_reverse(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);
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
 * devl_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Return: Non-zero value on failure.
 */
int devl_traps_register(struct devlink *devlink,
			const struct devlink_trap *traps,
			size_t traps_count, void *priv)
{
	int i, err;

	if (!devlink->ops->trap_init || !devlink->ops->trap_action_set)
		return -EINVAL;

	devl_assert_locked(devlink);
	for (i = 0; i < traps_count; i++) {
		const struct devlink_trap *trap = &traps[i];

		err = devlink_trap_verify(trap);
		if (err)
			goto err_trap_verify;

		err = devlink_trap_register(devlink, trap, priv);
		if (err)
			goto err_trap_register;
	}

	return 0;

err_trap_register:
err_trap_verify:
	for (i--; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_traps_register);

/**
 * devlink_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_traps_register(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count, void *priv)
{
	int err;

	devl_lock(devlink);
	err = devl_traps_register(devlink, traps, traps_count, priv);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_traps_register);

/**
 * devl_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 */
void devl_traps_unregister(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count)
{
	int i;

	devl_assert_locked(devlink);
	/* Make sure we do not have any packets in-flight while unregistering
	 * traps by disabling all of them and waiting for a grace period.
	 */
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_disable(devlink, &traps[i]);
	synchronize_rcu();
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
}
EXPORT_SYMBOL_GPL(devl_traps_unregister);

/**
 * devlink_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_traps_unregister(struct devlink *devlink,
			      const struct devlink_trap *traps,
			      size_t traps_count)
{
	devl_lock(devlink);
	devl_traps_unregister(devlink, traps, traps_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_traps_unregister);

static void
devlink_trap_stats_update(struct devlink_stats __percpu *trap_stats,
			  size_t skb_len)
{
	struct devlink_stats *stats;

	stats = this_cpu_ptr(trap_stats);
	u64_stats_update_begin(&stats->syncp);
	u64_stats_add(&stats->rx_bytes, skb_len);
	u64_stats_inc(&stats->rx_packets);
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
		metadata->input_dev = in_devlink_port->type_eth.netdev;
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
 * devl_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Return: Non-zero value on failure.
 */
int devl_trap_groups_register(struct devlink *devlink,
			      const struct devlink_trap_group *groups,
			      size_t groups_count)
{
	int i, err;

	devl_assert_locked(devlink);
	for (i = 0; i < groups_count; i++) {
		const struct devlink_trap_group *group = &groups[i];

		err = devlink_trap_group_verify(group);
		if (err)
			goto err_trap_group_verify;

		err = devlink_trap_group_register(devlink, group);
		if (err)
			goto err_trap_group_register;
	}

	return 0;

err_trap_group_register:
err_trap_group_verify:
	for (i--; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_groups_register);

/**
 * devlink_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_trap_groups_register(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int err;

	devl_lock(devlink);
	err = devl_trap_groups_register(devlink, groups, groups_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_register);

/**
 * devl_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 */
void devl_trap_groups_unregister(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = groups_count - 1; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_groups_unregister);

/**
 * devlink_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_trap_groups_unregister(struct devlink *devlink,
				    const struct devlink_trap_group *groups,
				    size_t groups_count)
{
	devl_lock(devlink);
	devl_trap_groups_unregister(devlink, groups, groups_count);
	devl_unlock(devlink);
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
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

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

static void devlink_trap_policers_notify_register(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_NEW);
}

static void devlink_trap_policers_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry_reverse(policer_item, &devlink->trap_policer_list,
				    list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_DEL);
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
 * devl_trap_policers_register - Register packet trap policers with devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 *
 * Return: Non-zero value on failure.
 */
int
devl_trap_policers_register(struct devlink *devlink,
			    const struct devlink_trap_policer *policers,
			    size_t policers_count)
{
	int i, err;

	devl_assert_locked(devlink);
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
	return 0;

err_trap_policer_register:
err_trap_policer_verify:
	for (i--; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_policers_register);

/**
 * devl_trap_policers_unregister - Unregister packet trap policers from devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 */
void
devl_trap_policers_unregister(struct devlink *devlink,
			      const struct devlink_trap_policer *policers,
			      size_t policers_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = policers_count - 1; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_policers_unregister);

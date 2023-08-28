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

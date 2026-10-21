// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include "devl_internal.h"

static inline bool
devlink_rate_is_leaf(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_LEAF;
}

bool devlink_rate_is_node(const struct devlink_rate *devlink_rate)
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

/* Repeatedly walks the nested devlink chain while cross device rate nodes are
 * supported and finds the topmost instance where rates should be stored.
 * That instance is locked, referenced and returned.
 * When cross device rate nodes aren't supported the original devlink instance
 * is returned.
 */
static struct devlink *devl_rate_lock(struct devlink *devlink)
{
	struct devlink *rate_devlink = devlink, *parent;

	devl_assert_locked(devlink);

	while (rate_devlink->ops &&
	       rate_devlink->ops->supported_cross_device_rate_nodes) {
		parent = devlink_nested_in_get_lock(rate_devlink);
		if (!parent)
			break;
		if (rate_devlink != devlink) {
			/* Unlock intermediate instances. */
			devl_unlock(rate_devlink);
			devlink_put(rate_devlink);
		}
		rate_devlink = parent;
	}
	return rate_devlink;
}

/* Unlocks and puts 'rate devlink' if different than 'devlink'. */
static void devl_rate_unlock(struct devlink *devlink,
			     struct devlink *rate_devlink)
{
	if (devlink == rate_devlink)
		return;

	devl_unlock(rate_devlink);
	devlink_put(rate_devlink);
}

static struct devlink_rate *
devlink_rate_node_get_by_name(struct devlink *rate_devlink,
			      struct devlink *devlink, const char *node_name)
{
	struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &rate_devlink->rate_list, list) {
		if (devlink_rate->devlink == devlink &&
		    devlink_rate_is_node(devlink_rate) &&
		    !strcmp(node_name, devlink_rate->name))
			return devlink_rate;
	}
	return ERR_PTR(-ENODEV);
}

static struct devlink_rate *
devlink_rate_node_get_from_attrs(struct devlink *rate_devlink,
				 struct devlink *devlink, struct nlattr **attrs)
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

	return devlink_rate_node_get_by_name(rate_devlink, devlink,
					     rate_node_name);
}

static struct devlink_rate *
devlink_rate_node_get_from_info(struct devlink *rate_devlink,
				struct devlink *devlink,
				struct genl_info *info)
{
	return devlink_rate_node_get_from_attrs(rate_devlink, devlink,
						info->attrs);
}

static struct devlink_rate *
devlink_rate_get_from_info(struct devlink *rate_devlink,
			   struct devlink *devlink, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;

	if (attrs[DEVLINK_ATTR_PORT_INDEX])
		return devlink_rate_leaf_get_from_info(devlink, info);
	else if (attrs[DEVLINK_ATTR_RATE_NODE_NAME])
		return devlink_rate_node_get_from_info(rate_devlink, devlink,
						       info);
	else
		return ERR_PTR(-EINVAL);
}

static int devlink_rate_put_tc_bws(struct sk_buff *msg, u32 *tc_bw)
{
	struct nlattr *nla_tc_bw;
	int i;

	for (i = 0; i < DEVLINK_RATE_TCS_MAX; i++) {
		nla_tc_bw = nla_nest_start(msg, DEVLINK_ATTR_RATE_TC_BWS);
		if (!nla_tc_bw)
			return -EMSGSIZE;

		if (nla_put_u8(msg, DEVLINK_RATE_TC_ATTR_INDEX, i) ||
		    nla_put_u32(msg, DEVLINK_RATE_TC_ATTR_BW, tc_bw[i]))
			goto nla_put_failure;

		nla_nest_end(msg, nla_tc_bw);
	}
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, nla_tc_bw);
	return -EMSGSIZE;
}

static int devlink_nl_rate_parent_fill(struct sk_buff *msg,
				       struct devlink_rate *devlink_rate)
{
	struct devlink_rate *parent = devlink_rate->parent;
	struct devlink *devlink = parent->devlink;

	if (nla_put_string(msg, DEVLINK_ATTR_RATE_PARENT_NODE_NAME,
			   parent->name))
		return -EMSGSIZE;

	if (devlink != devlink_rate->devlink &&
	    devlink_nl_put_nested_handle(msg,
					 devlink_net(devlink_rate->devlink),
					 devlink, DEVLINK_ATTR_PARENT_DEV))
		return -EMSGSIZE;

	return 0;
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

	if (devlink_nl_put_u64(msg, DEVLINK_ATTR_RATE_TX_SHARE,
			       devlink_rate->tx_share))
		goto nla_put_failure;

	if (devlink_nl_put_u64(msg, DEVLINK_ATTR_RATE_TX_MAX,
			       devlink_rate->tx_max))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_PRIORITY,
			devlink_rate->tx_priority))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_WEIGHT,
			devlink_rate->tx_weight))
		goto nla_put_failure;

	if (devlink_rate->parent &&
	    devlink_nl_rate_parent_fill(msg, devlink_rate))
		goto nla_put_failure;

	if (devlink_rate_put_tc_bws(msg, devlink_rate->tc_bw))
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

	if (!devl_is_registered(devlink) || !devlink_nl_notify_need(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_rate_fill(msg, devlink_rate, cmd, 0, 0, 0, NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	devlink_nl_notify_send(devlink, msg);
}

void devlink_rates_notify_register(struct devlink *devlink)
{
	struct devlink_rate *rate_node;
	struct devlink *rate_devlink;

	rate_devlink = devl_rate_lock(devlink);
	list_for_each_entry(rate_node, &rate_devlink->rate_list, list)
		if (rate_node->devlink == devlink)
			devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	devl_rate_unlock(devlink, rate_devlink);
}

void devlink_rates_notify_unregister(struct devlink *devlink)
{
	struct devlink_rate *rate_node;
	struct devlink *rate_devlink;

	rate_devlink = devl_rate_lock(devlink);
	list_for_each_entry_reverse(rate_node, &rate_devlink->rate_list, list)
		if (rate_node->devlink == devlink)
			devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);
	devl_rate_unlock(devlink, rate_devlink);
}

static int
devlink_nl_rate_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
			     struct netlink_callback *cb, int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_rate *devlink_rate;
	struct devlink *rate_devlink;
	int idx = 0;
	int err = 0;

	rate_devlink = devl_rate_lock(devlink);
	list_for_each_entry(devlink_rate, &rate_devlink->rate_list, list) {
		enum devlink_command cmd = DEVLINK_CMD_RATE_NEW;
		u32 id = NETLINK_CB(cb->skb).portid;

		if (idx < state->idx || devlink_rate->devlink != devlink) {
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
	devl_rate_unlock(devlink, rate_devlink);

	return err;
}

int devlink_nl_rate_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_rate_get_dump_one);
}

int devlink_nl_rate_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *rate_devlink, *devlink = devlink_nl_ctx(info)->devlink;
	struct devlink_rate *devlink_rate;
	struct sk_buff *msg;
	int err;

	rate_devlink = devl_rate_lock(devlink);
	devlink_rate = devlink_rate_get_from_info(rate_devlink, devlink, info);
	if (IS_ERR(devlink_rate)) {
		err = PTR_ERR(devlink_rate);
		goto unlock;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto unlock;
	}

	err = devlink_nl_rate_fill(msg, devlink_rate, DEVLINK_CMD_RATE_NEW,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err)
		goto err_fill;

	devl_rate_unlock(devlink, rate_devlink);
	return genlmsg_reply(msg, info);

err_fill:
	nlmsg_free(msg);
unlock:
	devl_rate_unlock(devlink, rate_devlink);
	return err;
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
				struct devlink *rate_devlink,
				struct genl_info *info,
				struct nlattr *nla_parent)
{
	struct devlink *devlink = devlink_rate->devlink, *parent_devlink;
	const char *parent_name = nla_data(nla_parent);
	const struct devlink_ops *ops = devlink->ops;
	size_t len = strlen(parent_name);
	struct devlink_rate *parent;
	int err = -EOPNOTSUPP;

	parent_devlink = devlink_nl_ctx(info)->parent_devlink ? : devlink;
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
		/* parent_devlink (when different than devlink) isn't locked,
		 * but the rate node devlink instance is, so nobody from the
		 * same group of devices sharing rates could change the used
		 * fields or unregister the parent.
		 */
		parent = devlink_rate_node_get_by_name(rate_devlink,
						       parent_devlink,
						       parent_name);
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

static int devlink_nl_rate_tc_bw_parse(struct nlattr *parent_nest, u32 *tc_bw,
				       unsigned long *bitmap,
				       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[DEVLINK_RATE_TC_ATTR_MAX + 1];
	u8 tc_index;
	int err;

	err = nla_parse_nested(tb, DEVLINK_RATE_TC_ATTR_MAX, parent_nest,
			       devlink_dl_rate_tc_bws_nl_policy, extack);
	if (err)
		return err;

	if (!tb[DEVLINK_RATE_TC_ATTR_INDEX]) {
		NL_SET_ERR_ATTR_MISS(extack, parent_nest,
				     DEVLINK_RATE_TC_ATTR_INDEX);
		return -EINVAL;
	}

	tc_index = nla_get_u8(tb[DEVLINK_RATE_TC_ATTR_INDEX]);

	if (!tb[DEVLINK_RATE_TC_ATTR_BW]) {
		NL_SET_ERR_ATTR_MISS(extack, parent_nest,
				     DEVLINK_RATE_TC_ATTR_BW);
		return -EINVAL;
	}

	if (test_and_set_bit(tc_index, bitmap)) {
		NL_SET_ERR_MSG_FMT(extack,
				   "Duplicate traffic class index specified (%u)",
				   tc_index);
		return -EINVAL;
	}

	tc_bw[tc_index] = nla_get_u32(tb[DEVLINK_RATE_TC_ATTR_BW]);

	return 0;
}

static int devlink_nl_rate_tc_bw_set(struct devlink_rate *devlink_rate,
				     struct genl_info *info)
{
	DECLARE_BITMAP(bitmap, DEVLINK_RATE_TCS_MAX) = {};
	struct devlink *devlink = devlink_rate->devlink;
	const struct devlink_ops *ops = devlink->ops;
	u32 tc_bw[DEVLINK_RATE_TCS_MAX] = {};
	int rem, err = -EOPNOTSUPP, i;
	struct nlattr *attr;

	nlmsg_for_each_attr_type(attr, DEVLINK_ATTR_RATE_TC_BWS, info->nlhdr,
				 GENL_HDRLEN, rem) {
		err = devlink_nl_rate_tc_bw_parse(attr, tc_bw, bitmap,
						  info->extack);
		if (err)
			return err;
	}

	for (i = 0; i < DEVLINK_RATE_TCS_MAX; i++) {
		if (!test_bit(i, bitmap)) {
			NL_SET_ERR_MSG_FMT(info->extack,
					   "Bandwidth values must be specified for all %u traffic classes",
					   DEVLINK_RATE_TCS_MAX);
			return -EINVAL;
		}
	}

	if (devlink_rate_is_leaf(devlink_rate))
		err = ops->rate_leaf_tc_bw_set(devlink_rate, devlink_rate->priv,
					       tc_bw, info->extack);
	else if (devlink_rate_is_node(devlink_rate))
		err = ops->rate_node_tc_bw_set(devlink_rate, devlink_rate->priv,
					       tc_bw, info->extack);

	if (err)
		return err;

	memcpy(devlink_rate->tc_bw, tc_bw, sizeof(tc_bw));

	return 0;
}

static int devlink_nl_rate_set(struct devlink_rate *devlink_rate,
			       struct devlink *rate_devlink,
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

	if (attrs[DEVLINK_ATTR_RATE_TC_BWS]) {
		err = devlink_nl_rate_tc_bw_set(devlink_rate, info);
		if (err)
			return err;
	}

	/* Keep parent setting last because it takes a reference. This function
	 * has no rollback, so failing after taking the ref would leak it.
	 */
	nla_parent = attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME];
	if (nla_parent) {
		err = devlink_nl_rate_parent_node_set(devlink_rate,
						      rate_devlink, info,
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
		if (attrs[DEVLINK_ATTR_RATE_TC_BWS] &&
		    !ops->rate_leaf_tc_bw_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TC_BWS],
					    "TC bandwidth set isn't supported for the leafs");
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
		if (attrs[DEVLINK_ATTR_RATE_TC_BWS] &&
		    !ops->rate_node_tc_bw_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TC_BWS],
					    "TC bandwidth set isn't supported for the nodes");
			return false;
		}
	} else {
		WARN(1, "Unknown type of rate object");
		return false;
	}

	return true;
}

int devlink_nl_rate_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink_nl_ctx *ctx = devlink_nl_ctx(info);
	struct devlink *devlink = ctx->devlink;
	struct devlink_rate *devlink_rate;
	const struct devlink_ops *ops;
	struct devlink *rate_devlink;
	int err;

	rate_devlink = devl_rate_lock(devlink);
	devlink_rate = devlink_rate_get_from_info(rate_devlink, devlink, info);
	if (IS_ERR(devlink_rate)) {
		err = PTR_ERR(devlink_rate);
		goto unlock;
	}

	ops = devlink->ops;
	if (!ops ||
	    !devlink_rate_set_ops_supported(ops, info, devlink_rate->type)) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (ctx->parent_devlink && ctx->parent_devlink != devlink &&
	    !ops->supported_cross_device_rate_nodes) {
		NL_SET_ERR_MSG(info->extack,
			       "Cross-device rate parents aren't supported");
		err = -EOPNOTSUPP;
		goto unlock;
	}

	err = devlink_nl_rate_set(devlink_rate, rate_devlink, ops, info);

	if (!err)
		devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);
unlock:
	devl_rate_unlock(devlink, rate_devlink);
	return err;
}

int devlink_nl_rate_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink_nl_ctx *ctx = devlink_nl_ctx(info);
	struct devlink *devlink = ctx->devlink;
	struct devlink_rate *rate_node;
	const struct devlink_ops *ops;
	struct devlink *rate_devlink;
	int err;

	ops = devlink->ops;
	if (!ops || !ops->rate_node_new || !ops->rate_node_del) {
		NL_SET_ERR_MSG(info->extack, "Rate nodes aren't supported");
		return -EOPNOTSUPP;
	}

	if (!devlink_rate_set_ops_supported(ops, info, DEVLINK_RATE_TYPE_NODE))
		return -EOPNOTSUPP;

	if (ctx->parent_devlink && ctx->parent_devlink != devlink &&
	    !ops->supported_cross_device_rate_nodes) {
		NL_SET_ERR_MSG(info->extack,
			       "Cross-device rate parents aren't supported");
		return -EOPNOTSUPP;
	}

	rate_devlink = devl_rate_lock(devlink);
	rate_node = devlink_rate_node_get_from_attrs(rate_devlink, devlink,
						     info->attrs);
	if (!IS_ERR(rate_node)) {
		err = -EEXIST;
		goto unlock;
	} else if (rate_node == ERR_PTR(-EINVAL)) {
		err = -EINVAL;
		goto unlock;
	}

	rate_node = kzalloc_obj(*rate_node);
	if (!rate_node) {
		err = -ENOMEM;
		goto unlock;
	}

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

	err = devlink_nl_rate_set(rate_node, rate_devlink, ops, info);
	if (err)
		goto err_rate_set;

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &rate_devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	devl_rate_unlock(devlink, rate_devlink);
	return 0;

err_rate_set:
	ops->rate_node_del(rate_node, rate_node->priv, info->extack);
err_node_new:
	kfree(rate_node->name);
err_strdup:
	kfree(rate_node);
unlock:
	devl_rate_unlock(devlink, rate_devlink);
	return err;
}

int devlink_nl_rate_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *rate_devlink, *devlink = devlink_nl_ctx(info)->devlink;
	struct devlink_rate *rate_node;
	int err;

	rate_devlink = devl_rate_lock(devlink);
	rate_node = devlink_rate_node_get_from_info(rate_devlink, devlink,
						    info);
	if (IS_ERR(rate_node)) {
		err = PTR_ERR(rate_node);
		goto unlock;
	}

	if (refcount_read(&rate_node->refcnt) > 1) {
		NL_SET_ERR_MSG(info->extack, "Node has children. Cannot delete node.");
		err = -EBUSY;
		goto unlock;
	}

	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);
	err = devlink->ops->rate_node_del(rate_node, rate_node->priv,
					  info->extack);
	if (rate_node->parent)
		refcount_dec(&rate_node->parent->refcnt);
	list_del(&rate_node->list);
	kfree(rate_node->name);
	kfree(rate_node);
unlock:
	devl_rate_unlock(devlink, rate_devlink);
	return err;
}

int devlink_rates_check(struct devlink *devlink,
			bool (*rate_filter)(const struct devlink_rate *),
			struct netlink_ext_ack *extack)
{
	struct devlink_rate *devlink_rate;
	struct devlink *rate_devlink;
	int err = 0;

	rate_devlink = devl_rate_lock(devlink);
	list_for_each_entry(devlink_rate, &rate_devlink->rate_list, list)
		if (devlink_rate->devlink == devlink &&
		    (!rate_filter || rate_filter(devlink_rate))) {
			if (extack)
				NL_SET_ERR_MSG(extack, "Rate node(s) exists.");
			err = -EBUSY;
			break;
		}
	devl_rate_unlock(devlink, rate_devlink);
	return err;
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
	struct devlink *rate_devlink;

	rate_devlink = devl_rate_lock(devlink);
	rate_node = devlink_rate_node_get_by_name(rate_devlink, devlink,
						  node_name);
	if (!IS_ERR(rate_node)) {
		rate_node = ERR_PTR(-EEXIST);
		goto unlock;
	}

	rate_node = kzalloc_obj(*rate_node);
	if (!rate_node) {
		rate_node = ERR_PTR(-ENOMEM);
		goto unlock;
	}

	rate_node->type = DEVLINK_RATE_TYPE_NODE;
	rate_node->devlink = devlink;
	rate_node->priv = priv;

	rate_node->name = kstrdup(node_name, GFP_KERNEL);
	if (!rate_node->name) {
		kfree(rate_node);
		rate_node = ERR_PTR(-ENOMEM);
		goto unlock;
	}

	if (parent) {
		rate_node->parent = parent;
		refcount_inc(&rate_node->parent->refcnt);
	}

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &rate_devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
unlock:
	devl_rate_unlock(devlink, rate_devlink);
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
	struct devlink *rate_devlink, *devlink = devlink_port->devlink;
	struct devlink_rate *devlink_rate;

	devl_assert_locked(devlink);

	if (WARN_ON(devlink_port->devlink_rate))
		return -EBUSY;

	devlink_rate = kzalloc_obj(*devlink_rate);
	if (!devlink_rate)
		return -ENOMEM;

	rate_devlink = devl_rate_lock(devlink);
	if (parent) {
		devlink_rate->parent = parent;
		refcount_inc(&devlink_rate->parent->refcnt);
	}

	devlink_rate->type = DEVLINK_RATE_TYPE_LEAF;
	devlink_rate->devlink = devlink;
	devlink_rate->devlink_port = devlink_port;
	devlink_rate->priv = priv;
	list_add_tail(&devlink_rate->list, &rate_devlink->rate_list);
	devlink_port->devlink_rate = devlink_rate;
	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);
	devl_rate_unlock(devlink, rate_devlink);

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
	struct devlink *rate_devlink, *devlink = devlink_port->devlink;

	devl_assert_locked(devlink);
	if (!devlink_rate)
		return;

	rate_devlink = devl_rate_lock(devlink);
	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_DEL);
	if (devlink_rate->parent)
		refcount_dec(&devlink_rate->parent->refcnt);
	list_del(&devlink_rate->list);
	devlink_port->devlink_rate = NULL;
	devl_rate_unlock(devlink, rate_devlink);
	kfree(devlink_rate);
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_destroy);

/**
 * devl_rate_nodes_destroy - destroy all devlink rate nodes on device
 * @devlink: devlink instance
 *
 * Unset parent for all rate objects involving this device and destroy all rate
 * nodes on it.
 */
void devl_rate_nodes_destroy(struct devlink *devlink)
{
	struct devlink_rate *devlink_rate, *tmp;
	const struct devlink_ops *ops;
	struct devlink *rate_devlink;

	devl_assert_locked(devlink);
	rate_devlink = devl_rate_lock(devlink);

	list_for_each_entry(devlink_rate, &rate_devlink->rate_list, list) {
		if (!devlink_rate->parent ||
		    (devlink_rate->devlink != devlink &&
		     devlink_rate->parent->devlink != devlink))
			continue;

		/* This could destroy rate objects on other devlinks in the
		 * same hierarchy under 'rate_devlink'. This is safe because
		 * the shared common ancestor is locked so there can be no
		 * other concurrent rate operations on devlink_rate->devlink.
		 */
		ops = devlink_rate->devlink->ops;
		if (devlink_rate_is_leaf(devlink_rate))
			ops->rate_leaf_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
		else if (devlink_rate_is_node(devlink_rate))
			ops->rate_node_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);

		refcount_dec(&devlink_rate->parent->refcnt);
		devlink_rate->parent = NULL;
	}
	ops = devlink->ops;
	list_for_each_entry_safe(devlink_rate, tmp, &rate_devlink->rate_list,
				 list) {
		if (devlink_rate->devlink == devlink &&
		    devlink_rate_is_node(devlink_rate)) {
			ops->rate_node_del(devlink_rate, devlink_rate->priv, NULL);
			list_del(&devlink_rate->list);
			kfree(devlink_rate->name);
			kfree(devlink_rate);
		}
	}
	devl_rate_unlock(devlink, rate_devlink);
}
EXPORT_SYMBOL_GPL(devl_rate_nodes_destroy);

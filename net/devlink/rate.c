// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellaanalx Techanallogies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellaanalx.com>
 */

#include "devl_internal.h"

static inline bool
devlink_rate_is_leaf(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_LEAF;
}

static inline bool
devlink_rate_is_analde(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_ANALDE;
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
	return devlink_rate ?: ERR_PTR(-EANALDEV);
}

static struct devlink_rate *
devlink_rate_analde_get_by_name(struct devlink *devlink, const char *analde_name)
{
	static struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		if (devlink_rate_is_analde(devlink_rate) &&
		    !strcmp(analde_name, devlink_rate->name))
			return devlink_rate;
	}
	return ERR_PTR(-EANALDEV);
}

static struct devlink_rate *
devlink_rate_analde_get_from_attrs(struct devlink *devlink, struct nlattr **attrs)
{
	const char *rate_analde_name;
	size_t len;

	if (!attrs[DEVLINK_ATTR_RATE_ANALDE_NAME])
		return ERR_PTR(-EINVAL);
	rate_analde_name = nla_data(attrs[DEVLINK_ATTR_RATE_ANALDE_NAME]);
	len = strlen(rate_analde_name);
	/* Name cananalt be empty or decimal number */
	if (!len || strspn(rate_analde_name, "0123456789") == len)
		return ERR_PTR(-EINVAL);

	return devlink_rate_analde_get_by_name(devlink, rate_analde_name);
}

static struct devlink_rate *
devlink_rate_analde_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	return devlink_rate_analde_get_from_attrs(devlink, info->attrs);
}

static struct devlink_rate *
devlink_rate_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;

	if (attrs[DEVLINK_ATTR_PORT_INDEX])
		return devlink_rate_leaf_get_from_info(devlink, info);
	else if (attrs[DEVLINK_ATTR_RATE_ANALDE_NAME])
		return devlink_rate_analde_get_from_info(devlink, info);
	else
		return ERR_PTR(-EINVAL);
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
	} else if (devlink_rate_is_analde(devlink_rate)) {
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_ANALDE_NAME,
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
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_PARENT_ANALDE_NAME,
				   devlink_rate->parent->name))
			goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_rate_analtify(struct devlink_rate *devlink_rate,
				enum devlink_command cmd)
{
	struct devlink *devlink = devlink_rate->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_RATE_NEW && cmd != DEVLINK_CMD_RATE_DEL);

	if (!devl_is_registered(devlink) || !devlink_nl_analtify_need(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_rate_fill(msg, devlink_rate, cmd, 0, 0, 0, NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	devlink_nl_analtify_send(devlink, msg);
}

void devlink_rates_analtify_register(struct devlink *devlink)
{
	struct devlink_rate *rate_analde;

	list_for_each_entry(rate_analde, &devlink->rate_list, list)
		devlink_rate_analtify(rate_analde, DEVLINK_CMD_RATE_NEW);
}

void devlink_rates_analtify_unregister(struct devlink *devlink)
{
	struct devlink_rate *rate_analde;

	list_for_each_entry_reverse(rate_analde, &devlink->rate_list, list)
		devlink_rate_analtify(rate_analde, DEVLINK_CMD_RATE_DEL);
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
		return -EANALMEM;

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
devlink_rate_is_parent_analde(struct devlink_rate *devlink_rate,
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
devlink_nl_rate_parent_analde_set(struct devlink_rate *devlink_rate,
				struct genl_info *info,
				struct nlattr *nla_parent)
{
	struct devlink *devlink = devlink_rate->devlink;
	const char *parent_name = nla_data(nla_parent);
	const struct devlink_ops *ops = devlink->ops;
	size_t len = strlen(parent_name);
	struct devlink_rate *parent;
	int err = -EOPANALTSUPP;

	parent = devlink_rate->parent;

	if (parent && !len) {
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		if (err)
			return err;

		refcount_dec(&parent->refcnt);
		devlink_rate->parent = NULL;
	} else if (len) {
		parent = devlink_rate_analde_get_by_name(devlink, parent_name);
		if (IS_ERR(parent))
			return -EANALDEV;

		if (parent == devlink_rate) {
			NL_SET_ERR_MSG(info->extack, "Parent to self is analt allowed");
			return -EINVAL;
		}

		if (devlink_rate_is_analde(devlink_rate) &&
		    devlink_rate_is_parent_analde(devlink_rate, parent->parent)) {
			NL_SET_ERR_MSG(info->extack, "Analde is already a parent of parent analde.");
			return -EEXIST;
		}

		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, parent,
							devlink_rate->priv, parent->priv,
							info->extack);
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_parent_set(devlink_rate, parent,
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
	int err = -EOPANALTSUPP;
	u32 priority;
	u32 weight;
	u64 rate;

	if (attrs[DEVLINK_ATTR_RATE_TX_SHARE]) {
		rate = nla_get_u64(attrs[DEVLINK_ATTR_RATE_TX_SHARE]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_share_set(devlink_rate, devlink_rate->priv,
							  rate, info->extack);
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_tx_share_set(devlink_rate, devlink_rate->priv,
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
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_tx_max_set(devlink_rate, devlink_rate->priv,
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
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_tx_priority_set(devlink_rate, devlink_rate->priv,
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
		else if (devlink_rate_is_analde(devlink_rate))
			err = ops->rate_analde_tx_weight_set(devlink_rate, devlink_rate->priv,
							   weight, info->extack);

		if (err)
			return err;
		devlink_rate->tx_weight = weight;
	}

	nla_parent = attrs[DEVLINK_ATTR_RATE_PARENT_ANALDE_NAME];
	if (nla_parent) {
		err = devlink_nl_rate_parent_analde_set(devlink_rate, info,
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
		if (attrs[DEVLINK_ATTR_RATE_PARENT_ANALDE_NAME] &&
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
	} else if (type == DEVLINK_RATE_TYPE_ANALDE) {
		if (attrs[DEVLINK_ATTR_RATE_TX_SHARE] && !ops->rate_analde_tx_share_set) {
			NL_SET_ERR_MSG(info->extack, "TX share set isn't supported for the analdes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_MAX] && !ops->rate_analde_tx_max_set) {
			NL_SET_ERR_MSG(info->extack, "TX max set isn't supported for the analdes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_PARENT_ANALDE_NAME] &&
		    !ops->rate_analde_parent_set) {
			NL_SET_ERR_MSG(info->extack, "Parent set isn't supported for the analdes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY] && !ops->rate_analde_tx_priority_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_PRIORITY],
					    "TX priority set isn't supported for the analdes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT] && !ops->rate_analde_tx_weight_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_WEIGHT],
					    "TX weight set isn't supported for the analdes");
			return false;
		}
	} else {
		WARN(1, "Unkanalwn type of rate object");
		return false;
	}

	return true;
}

int devlink_nl_rate_set_doit(struct sk_buff *skb, struct genl_info *info)
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
		return -EOPANALTSUPP;

	err = devlink_nl_rate_set(devlink_rate, ops, info);

	if (!err)
		devlink_rate_analtify(devlink_rate, DEVLINK_CMD_RATE_NEW);
	return err;
}

int devlink_nl_rate_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *rate_analde;
	const struct devlink_ops *ops;
	int err;

	ops = devlink->ops;
	if (!ops || !ops->rate_analde_new || !ops->rate_analde_del) {
		NL_SET_ERR_MSG(info->extack, "Rate analdes aren't supported");
		return -EOPANALTSUPP;
	}

	if (!devlink_rate_set_ops_supported(ops, info, DEVLINK_RATE_TYPE_ANALDE))
		return -EOPANALTSUPP;

	rate_analde = devlink_rate_analde_get_from_attrs(devlink, info->attrs);
	if (!IS_ERR(rate_analde))
		return -EEXIST;
	else if (rate_analde == ERR_PTR(-EINVAL))
		return -EINVAL;

	rate_analde = kzalloc(sizeof(*rate_analde), GFP_KERNEL);
	if (!rate_analde)
		return -EANALMEM;

	rate_analde->devlink = devlink;
	rate_analde->type = DEVLINK_RATE_TYPE_ANALDE;
	rate_analde->name = nla_strdup(info->attrs[DEVLINK_ATTR_RATE_ANALDE_NAME], GFP_KERNEL);
	if (!rate_analde->name) {
		err = -EANALMEM;
		goto err_strdup;
	}

	err = ops->rate_analde_new(rate_analde, &rate_analde->priv, info->extack);
	if (err)
		goto err_analde_new;

	err = devlink_nl_rate_set(rate_analde, ops, info);
	if (err)
		goto err_rate_set;

	refcount_set(&rate_analde->refcnt, 1);
	list_add(&rate_analde->list, &devlink->rate_list);
	devlink_rate_analtify(rate_analde, DEVLINK_CMD_RATE_NEW);
	return 0;

err_rate_set:
	ops->rate_analde_del(rate_analde, rate_analde->priv, info->extack);
err_analde_new:
	kfree(rate_analde->name);
err_strdup:
	kfree(rate_analde);
	return err;
}

int devlink_nl_rate_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *rate_analde;
	int err;

	rate_analde = devlink_rate_analde_get_from_info(devlink, info);
	if (IS_ERR(rate_analde))
		return PTR_ERR(rate_analde);

	if (refcount_read(&rate_analde->refcnt) > 1) {
		NL_SET_ERR_MSG(info->extack, "Analde has children. Cananalt delete analde.");
		return -EBUSY;
	}

	devlink_rate_analtify(rate_analde, DEVLINK_CMD_RATE_DEL);
	err = devlink->ops->rate_analde_del(rate_analde, rate_analde->priv,
					  info->extack);
	if (rate_analde->parent)
		refcount_dec(&rate_analde->parent->refcnt);
	list_del(&rate_analde->list);
	kfree(rate_analde->name);
	kfree(rate_analde);
	return err;
}

int devlink_rate_analdes_check(struct devlink *devlink, u16 mode,
			     struct netlink_ext_ack *extack)
{
	struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list)
		if (devlink_rate_is_analde(devlink_rate)) {
			NL_SET_ERR_MSG(extack, "Rate analde(s) exists.");
			return -EBUSY;
		}
	return 0;
}

/**
 * devl_rate_analde_create - create devlink rate analde
 * @devlink: devlink instance
 * @priv: driver private data
 * @analde_name: name of the resulting analde
 * @parent: parent devlink_rate struct
 *
 * Create devlink rate object of type analde
 */
struct devlink_rate *
devl_rate_analde_create(struct devlink *devlink, void *priv, char *analde_name,
		      struct devlink_rate *parent)
{
	struct devlink_rate *rate_analde;

	rate_analde = devlink_rate_analde_get_by_name(devlink, analde_name);
	if (!IS_ERR(rate_analde))
		return ERR_PTR(-EEXIST);

	rate_analde = kzalloc(sizeof(*rate_analde), GFP_KERNEL);
	if (!rate_analde)
		return ERR_PTR(-EANALMEM);

	if (parent) {
		rate_analde->parent = parent;
		refcount_inc(&rate_analde->parent->refcnt);
	}

	rate_analde->type = DEVLINK_RATE_TYPE_ANALDE;
	rate_analde->devlink = devlink;
	rate_analde->priv = priv;

	rate_analde->name = kstrdup(analde_name, GFP_KERNEL);
	if (!rate_analde->name) {
		kfree(rate_analde);
		return ERR_PTR(-EANALMEM);
	}

	refcount_set(&rate_analde->refcnt, 1);
	list_add(&rate_analde->list, &devlink->rate_list);
	devlink_rate_analtify(rate_analde, DEVLINK_CMD_RATE_NEW);
	return rate_analde;
}
EXPORT_SYMBOL_GPL(devl_rate_analde_create);

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
		return -EANALMEM;

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
	devlink_rate_analtify(devlink_rate, DEVLINK_CMD_RATE_NEW);

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

	devlink_rate_analtify(devlink_rate, DEVLINK_CMD_RATE_DEL);
	if (devlink_rate->parent)
		refcount_dec(&devlink_rate->parent->refcnt);
	list_del(&devlink_rate->list);
	devlink_port->devlink_rate = NULL;
	kfree(devlink_rate);
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_destroy);

/**
 * devl_rate_analdes_destroy - destroy all devlink rate analdes on device
 * @devlink: devlink instance
 *
 * Unset parent for all rate objects and destroy all rate analdes
 * on specified device.
 */
void devl_rate_analdes_destroy(struct devlink *devlink)
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
		else if (devlink_rate_is_analde(devlink_rate))
			ops->rate_analde_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
	}
	list_for_each_entry_safe(devlink_rate, tmp, &devlink->rate_list, list) {
		if (devlink_rate_is_analde(devlink_rate)) {
			ops->rate_analde_del(devlink_rate, devlink_rate->priv, NULL);
			list_del(&devlink_rate->list);
			kfree(devlink_rate->name);
			kfree(devlink_rate);
		}
	}
}
EXPORT_SYMBOL_GPL(devl_rate_analdes_destroy);

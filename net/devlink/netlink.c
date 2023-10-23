// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#include <net/sock.h>

#include "devl_internal.h"

static const struct genl_multicast_group devlink_nl_mcgrps[] = {
	[DEVLINK_MCGRP_CONFIG] = { .name = DEVLINK_GENL_MCGRP_CONFIG_NAME },
};

int devlink_nl_put_nested_handle(struct sk_buff *msg, struct net *net,
				 struct devlink *devlink, int attrtype)
{
	struct nlattr *nested_attr;
	struct net *devl_net;

	nested_attr = nla_nest_start(msg, attrtype);
	if (!nested_attr)
		return -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	rcu_read_lock();
	devl_net = read_pnet_rcu(&devlink->_net);
	if (!net_eq(net, devl_net)) {
		int id = peernet2id_alloc(net, devl_net, GFP_ATOMIC);

		rcu_read_unlock();
		if (nla_put_s32(msg, DEVLINK_ATTR_NETNS_ID, id))
			return -EMSGSIZE;
	} else {
		rcu_read_unlock();
	}

	nla_nest_end(msg, nested_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, nested_attr);
	return -EMSGSIZE;
}

int devlink_nl_msg_reply_and_new(struct sk_buff **msg, struct genl_info *info)
{
	int err;

	if (*msg) {
		err = genlmsg_reply(*msg, info);
		if (err)
			return err;
	}
	*msg = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!*msg)
		return -ENOMEM;
	return 0;
}

struct devlink *
devlink_get_from_attrs_lock(struct net *net, struct nlattr **attrs)
{
	struct devlink *devlink;
	unsigned long index;
	char *busname;
	char *devname;

	if (!attrs[DEVLINK_ATTR_BUS_NAME] || !attrs[DEVLINK_ATTR_DEV_NAME])
		return ERR_PTR(-EINVAL);

	busname = nla_data(attrs[DEVLINK_ATTR_BUS_NAME]);
	devname = nla_data(attrs[DEVLINK_ATTR_DEV_NAME]);

	devlinks_xa_for_each_registered_get(net, index, devlink) {
		devl_lock(devlink);
		if (devl_is_registered(devlink) &&
		    strcmp(devlink->dev->bus->name, busname) == 0 &&
		    strcmp(dev_name(devlink->dev), devname) == 0)
			return devlink;
		devl_unlock(devlink);
		devlink_put(devlink);
	}

	return ERR_PTR(-ENODEV);
}

static int __devlink_nl_pre_doit(struct sk_buff *skb, struct genl_info *info,
				 u8 flags)
{
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	int err;

	devlink = devlink_get_from_attrs_lock(genl_info_net(info), info->attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);

	info->user_ptr[0] = devlink;
	if (flags & DEVLINK_NL_FLAG_NEED_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (IS_ERR(devlink_port)) {
			err = PTR_ERR(devlink_port);
			goto unlock;
		}
		info->user_ptr[1] = devlink_port;
	} else if (flags & DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (!IS_ERR(devlink_port))
			info->user_ptr[1] = devlink_port;
	}
	return 0;

unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

int devlink_nl_pre_doit(const struct genl_split_ops *ops,
			struct sk_buff *skb, struct genl_info *info)
{
	return __devlink_nl_pre_doit(skb, info, 0);
}

int devlink_nl_pre_doit_port(const struct genl_split_ops *ops,
			     struct sk_buff *skb, struct genl_info *info)
{
	return __devlink_nl_pre_doit(skb, info, DEVLINK_NL_FLAG_NEED_PORT);
}

int devlink_nl_pre_doit_port_optional(const struct genl_split_ops *ops,
				      struct sk_buff *skb,
				      struct genl_info *info)
{
	return __devlink_nl_pre_doit(skb, info, DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT);
}

void devlink_nl_post_doit(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink;

	devlink = info->user_ptr[0];
	devl_unlock(devlink);
	devlink_put(devlink);
}

static int devlink_nl_inst_single_dumpit(struct sk_buff *msg,
					 struct netlink_callback *cb, int flags,
					 devlink_nl_dump_one_func_t *dump_one,
					 struct nlattr **attrs)
{
	struct devlink *devlink;
	int err;

	devlink = devlink_get_from_attrs_lock(sock_net(msg->sk), attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);
	err = dump_one(msg, devlink, cb, flags | NLM_F_DUMP_FILTERED);

	devl_unlock(devlink);
	devlink_put(devlink);

	if (err != -EMSGSIZE)
		return err;
	return msg->len;
}

static int devlink_nl_inst_iter_dumpit(struct sk_buff *msg,
				       struct netlink_callback *cb, int flags,
				       devlink_nl_dump_one_func_t *dump_one)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink *devlink;
	int err = 0;

	while ((devlink = devlinks_xa_find_get(sock_net(msg->sk),
					       &state->instance))) {
		devl_lock(devlink);

		if (devl_is_registered(devlink))
			err = dump_one(msg, devlink, cb, flags);
		else
			err = 0;

		devl_unlock(devlink);
		devlink_put(devlink);

		if (err)
			break;

		state->instance++;

		/* restart sub-object walk for the next instance */
		state->idx = 0;
	}

	if (err != -EMSGSIZE)
		return err;
	return msg->len;
}

int devlink_nl_dumpit(struct sk_buff *msg, struct netlink_callback *cb,
		      devlink_nl_dump_one_func_t *dump_one)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct nlattr **attrs = info->attrs;
	int flags = NLM_F_MULTI;

	if (attrs &&
	    (attrs[DEVLINK_ATTR_BUS_NAME] || attrs[DEVLINK_ATTR_DEV_NAME]))
		return devlink_nl_inst_single_dumpit(msg, cb, flags, dump_one,
						     attrs);
	else
		return devlink_nl_inst_iter_dumpit(msg, cb, flags, dump_one);
}

struct genl_family devlink_nl_family __ro_after_init = {
	.name		= DEVLINK_GENL_NAME,
	.version	= DEVLINK_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= devlink_nl_ops,
	.n_split_ops	= ARRAY_SIZE(devlink_nl_ops),
	.resv_start_op	= DEVLINK_CMD_SELFTESTS_RUN + 1,
	.mcgrps		= devlink_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(devlink_nl_mcgrps),
};

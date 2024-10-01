// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#include <net/sock.h>

#include "devl_internal.h"

#define DEVLINK_NL_FLAG_NEED_PORT		BIT(0)
#define DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT	BIT(1)
#define DEVLINK_NL_FLAG_NEED_DEV_LOCK		BIT(2)

static const struct genl_multicast_group devlink_nl_mcgrps[] = {
	[DEVLINK_MCGRP_CONFIG] = { .name = DEVLINK_GENL_MCGRP_CONFIG_NAME },
};

struct devlink_nl_sock_priv {
	struct devlink_obj_desc __rcu *flt;
	spinlock_t flt_lock; /* Protects flt. */
};

static void devlink_nl_sock_priv_init(void *priv)
{
	struct devlink_nl_sock_priv *sk_priv = priv;

	spin_lock_init(&sk_priv->flt_lock);
}

static void devlink_nl_sock_priv_destroy(void *priv)
{
	struct devlink_nl_sock_priv *sk_priv = priv;
	struct devlink_obj_desc *flt;

	flt = rcu_dereference_protected(sk_priv->flt, true);
	kfree_rcu(flt, rcu);
}

int devlink_nl_notify_filter_set_doit(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct devlink_nl_sock_priv *sk_priv;
	struct nlattr **attrs = info->attrs;
	struct devlink_obj_desc *flt;
	size_t data_offset = 0;
	size_t data_size = 0;
	char *pos;

	if (attrs[DEVLINK_ATTR_BUS_NAME])
		data_size = size_add(data_size,
				     nla_len(attrs[DEVLINK_ATTR_BUS_NAME]) + 1);
	if (attrs[DEVLINK_ATTR_DEV_NAME])
		data_size = size_add(data_size,
				     nla_len(attrs[DEVLINK_ATTR_DEV_NAME]) + 1);

	flt = kzalloc(size_add(sizeof(*flt), data_size), GFP_KERNEL);
	if (!flt)
		return -ENOMEM;

	pos = (char *) flt->data;
	if (attrs[DEVLINK_ATTR_BUS_NAME]) {
		data_offset += nla_strscpy(pos,
					   attrs[DEVLINK_ATTR_BUS_NAME],
					   data_size) + 1;
		flt->bus_name = pos;
		pos += data_offset;
	}
	if (attrs[DEVLINK_ATTR_DEV_NAME]) {
		nla_strscpy(pos, attrs[DEVLINK_ATTR_DEV_NAME],
			    data_size - data_offset);
		flt->dev_name = pos;
	}

	if (attrs[DEVLINK_ATTR_PORT_INDEX]) {
		flt->port_index = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);
		flt->port_index_valid = true;
	}

	/* Don't attach empty filter. */
	if (!flt->bus_name && !flt->dev_name && !flt->port_index_valid) {
		kfree(flt);
		flt = NULL;
	}

	sk_priv = genl_sk_priv_get(&devlink_nl_family, NETLINK_CB(skb).sk);
	if (IS_ERR(sk_priv)) {
		kfree(flt);
		return PTR_ERR(sk_priv);
	}
	spin_lock(&sk_priv->flt_lock);
	flt = rcu_replace_pointer(sk_priv->flt, flt,
				  lockdep_is_held(&sk_priv->flt_lock));
	spin_unlock(&sk_priv->flt_lock);
	kfree_rcu(flt, rcu);
	return 0;
}

static bool devlink_obj_desc_match(const struct devlink_obj_desc *desc,
				   const struct devlink_obj_desc *flt)
{
	if (desc->bus_name && flt->bus_name &&
	    strcmp(desc->bus_name, flt->bus_name))
		return false;
	if (desc->dev_name && flt->dev_name &&
	    strcmp(desc->dev_name, flt->dev_name))
		return false;
	if (desc->port_index_valid && flt->port_index_valid &&
	    desc->port_index != flt->port_index)
		return false;
	return true;
}

int devlink_nl_notify_filter(struct sock *dsk, struct sk_buff *skb, void *data)
{
	struct devlink_obj_desc *desc = data;
	struct devlink_nl_sock_priv *sk_priv;
	struct devlink_obj_desc *flt;
	int ret = 0;

	rcu_read_lock();
	sk_priv = __genl_sk_priv_get(&devlink_nl_family, dsk);
	if (!IS_ERR_OR_NULL(sk_priv)) {
		flt = rcu_dereference(sk_priv->flt);
		if (flt)
			ret = !devlink_obj_desc_match(desc, flt);
	}
	rcu_read_unlock();
	return ret;
}

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
devlink_get_from_attrs_lock(struct net *net, struct nlattr **attrs,
			    bool dev_lock)
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
		if (strcmp(devlink->dev->bus->name, busname) == 0 &&
		    strcmp(dev_name(devlink->dev), devname) == 0) {
			devl_dev_lock(devlink, dev_lock);
			if (devl_is_registered(devlink))
				return devlink;
			devl_dev_unlock(devlink, dev_lock);
		}
		devlink_put(devlink);
	}

	return ERR_PTR(-ENODEV);
}

static int __devlink_nl_pre_doit(struct sk_buff *skb, struct genl_info *info,
				 u8 flags)
{
	bool dev_lock = flags & DEVLINK_NL_FLAG_NEED_DEV_LOCK;
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	int err;

	devlink = devlink_get_from_attrs_lock(genl_info_net(info), info->attrs,
					      dev_lock);
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
	devl_dev_unlock(devlink, dev_lock);
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

int devlink_nl_pre_doit_dev_lock(const struct genl_split_ops *ops,
				 struct sk_buff *skb, struct genl_info *info)
{
	return __devlink_nl_pre_doit(skb, info, DEVLINK_NL_FLAG_NEED_DEV_LOCK);
}

int devlink_nl_pre_doit_port_optional(const struct genl_split_ops *ops,
				      struct sk_buff *skb,
				      struct genl_info *info)
{
	return __devlink_nl_pre_doit(skb, info, DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT);
}

static void __devlink_nl_post_doit(struct sk_buff *skb, struct genl_info *info,
				   u8 flags)
{
	bool dev_lock = flags & DEVLINK_NL_FLAG_NEED_DEV_LOCK;
	struct devlink *devlink;

	devlink = info->user_ptr[0];
	devl_dev_unlock(devlink, dev_lock);
	devlink_put(devlink);
}

void devlink_nl_post_doit(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info)
{
	__devlink_nl_post_doit(skb, info, 0);
}

void
devlink_nl_post_doit_dev_lock(const struct genl_split_ops *ops,
			      struct sk_buff *skb, struct genl_info *info)
{
	__devlink_nl_post_doit(skb, info, DEVLINK_NL_FLAG_NEED_DEV_LOCK);
}

static int devlink_nl_inst_single_dumpit(struct sk_buff *msg,
					 struct netlink_callback *cb, int flags,
					 devlink_nl_dump_one_func_t *dump_one,
					 struct nlattr **attrs)
{
	struct devlink *devlink;
	int err;

	devlink = devlink_get_from_attrs_lock(sock_net(msg->sk), attrs, false);
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
	.sock_priv_size		= sizeof(struct devlink_nl_sock_priv),
	.sock_priv_init		= devlink_nl_sock_priv_init,
	.sock_priv_destroy	= devlink_nl_sock_priv_destroy,
};

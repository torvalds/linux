// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/xarray.h>
#include <net/devlink.h>
#include <net/net_shaper.h>

#include "shaper_nl_gen.h"

#include "../core/dev.h"

#define NET_SHAPER_SCOPE_SHIFT	26
#define NET_SHAPER_ID_MASK	GENMASK(NET_SHAPER_SCOPE_SHIFT - 1, 0)
#define NET_SHAPER_SCOPE_MASK	GENMASK(31, NET_SHAPER_SCOPE_SHIFT)

#define NET_SHAPER_ID_UNSPEC NET_SHAPER_ID_MASK

struct net_shaper_hierarchy {
	struct xarray shapers;
};

struct net_shaper_nl_ctx {
	struct net_shaper_binding binding;
	netdevice_tracker dev_tracker;
	unsigned long start_index;
};

static struct net_shaper_binding *net_shaper_binding_from_ctx(void *ctx)
{
	return &((struct net_shaper_nl_ctx *)ctx)->binding;
}

static struct net_shaper_hierarchy *
net_shaper_hierarchy(struct net_shaper_binding *binding)
{
	/* Pairs with WRITE_ONCE() in net_shaper_hierarchy_setup. */
	if (binding->type == NET_SHAPER_BINDING_TYPE_NETDEV)
		return READ_ONCE(binding->netdev->net_shaper_hierarchy);

	/* No other type supported yet. */
	return NULL;
}

static int net_shaper_fill_binding(struct sk_buff *msg,
				   const struct net_shaper_binding *binding,
				   u32 type)
{
	/* Should never happen, as currently only NETDEV is supported. */
	if (WARN_ON_ONCE(binding->type != NET_SHAPER_BINDING_TYPE_NETDEV))
		return -EINVAL;

	if (nla_put_u32(msg, type, binding->netdev->ifindex))
		return -EMSGSIZE;

	return 0;
}

static int net_shaper_fill_handle(struct sk_buff *msg,
				  const struct net_shaper_handle *handle,
				  u32 type)
{
	struct nlattr *handle_attr;

	if (handle->scope == NET_SHAPER_SCOPE_UNSPEC)
		return 0;

	handle_attr = nla_nest_start(msg, type);
	if (!handle_attr)
		return -EMSGSIZE;

	if (nla_put_u32(msg, NET_SHAPER_A_HANDLE_SCOPE, handle->scope) ||
	    (handle->scope >= NET_SHAPER_SCOPE_QUEUE &&
	     nla_put_u32(msg, NET_SHAPER_A_HANDLE_ID, handle->id)))
		goto handle_nest_cancel;

	nla_nest_end(msg, handle_attr);
	return 0;

handle_nest_cancel:
	nla_nest_cancel(msg, handle_attr);
	return -EMSGSIZE;
}

static int
net_shaper_fill_one(struct sk_buff *msg,
		    const struct net_shaper_binding *binding,
		    const struct net_shaper *shaper,
		    const struct genl_info *info)
{
	void *hdr;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		return -EMSGSIZE;

	if (net_shaper_fill_binding(msg, binding, NET_SHAPER_A_IFINDEX) ||
	    net_shaper_fill_handle(msg, &shaper->parent,
				   NET_SHAPER_A_PARENT) ||
	    net_shaper_fill_handle(msg, &shaper->handle,
				   NET_SHAPER_A_HANDLE) ||
	    ((shaper->bw_min || shaper->bw_max || shaper->burst) &&
	     nla_put_u32(msg, NET_SHAPER_A_METRIC, shaper->metric)) ||
	    (shaper->bw_min &&
	     nla_put_uint(msg, NET_SHAPER_A_BW_MIN, shaper->bw_min)) ||
	    (shaper->bw_max &&
	     nla_put_uint(msg, NET_SHAPER_A_BW_MAX, shaper->bw_max)) ||
	    (shaper->burst &&
	     nla_put_uint(msg, NET_SHAPER_A_BURST, shaper->burst)) ||
	    (shaper->priority &&
	     nla_put_u32(msg, NET_SHAPER_A_PRIORITY, shaper->priority)) ||
	    (shaper->weight &&
	     nla_put_u32(msg, NET_SHAPER_A_WEIGHT, shaper->weight)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/* Initialize the context fetching the relevant device and
 * acquiring a reference to it.
 */
static int net_shaper_ctx_setup(const struct genl_info *info, int type,
				struct net_shaper_nl_ctx *ctx)
{
	struct net *ns = genl_info_net(info);
	struct net_device *dev;
	int ifindex;

	if (GENL_REQ_ATTR_CHECK(info, type))
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[type]);
	dev = netdev_get_by_index(ns, ifindex, &ctx->dev_tracker, GFP_KERNEL);
	if (!dev) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[type]);
		return -ENOENT;
	}

	if (!dev->netdev_ops->net_shaper_ops) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[type]);
		netdev_put(dev, &ctx->dev_tracker);
		return -EOPNOTSUPP;
	}

	ctx->binding.type = NET_SHAPER_BINDING_TYPE_NETDEV;
	ctx->binding.netdev = dev;
	return 0;
}

static void net_shaper_ctx_cleanup(struct net_shaper_nl_ctx *ctx)
{
	if (ctx->binding.type == NET_SHAPER_BINDING_TYPE_NETDEV)
		netdev_put(ctx->binding.netdev, &ctx->dev_tracker);
}

static u32 net_shaper_handle_to_index(const struct net_shaper_handle *handle)
{
	return FIELD_PREP(NET_SHAPER_SCOPE_MASK, handle->scope) |
		FIELD_PREP(NET_SHAPER_ID_MASK, handle->id);
}

static struct net_shaper *
net_shaper_lookup(struct net_shaper_binding *binding,
		  const struct net_shaper_handle *handle)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	u32 index = net_shaper_handle_to_index(handle);

	return hierarchy ? xa_load(&hierarchy->shapers, index) : NULL;
}

static int net_shaper_parse_handle(const struct nlattr *attr,
				   const struct genl_info *info,
				   struct net_shaper_handle *handle)
{
	struct nlattr *tb[NET_SHAPER_A_HANDLE_MAX + 1];
	struct nlattr *id_attr;
	u32 id = 0;
	int ret;

	ret = nla_parse_nested(tb, NET_SHAPER_A_HANDLE_MAX, attr,
			       net_shaper_handle_nl_policy, info->extack);
	if (ret < 0)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, attr, tb,
			      NET_SHAPER_A_HANDLE_SCOPE))
		return -EINVAL;

	handle->scope = nla_get_u32(tb[NET_SHAPER_A_HANDLE_SCOPE]);

	/* The default id for NODE scope shapers is an invalid one
	 * to help the 'group' operation discriminate between new
	 * NODE shaper creation (ID_UNSPEC) and reuse of existing
	 * shaper (any other value).
	 */
	id_attr = tb[NET_SHAPER_A_HANDLE_ID];
	if (id_attr)
		id = nla_get_u32(id_attr);
	else if (handle->scope == NET_SHAPER_SCOPE_NODE)
		id = NET_SHAPER_ID_UNSPEC;

	handle->id = id;
	return 0;
}

static int net_shaper_generic_pre(struct genl_info *info, int type)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)info->ctx;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(info->ctx));

	return net_shaper_ctx_setup(info, type, ctx);
}

int net_shaper_nl_pre_doit(const struct genl_split_ops *ops,
			   struct sk_buff *skb, struct genl_info *info)
{
	return net_shaper_generic_pre(info, NET_SHAPER_A_IFINDEX);
}

static void net_shaper_generic_post(struct genl_info *info)
{
	net_shaper_ctx_cleanup((struct net_shaper_nl_ctx *)info->ctx);
}

void net_shaper_nl_post_doit(const struct genl_split_ops *ops,
			     struct sk_buff *skb, struct genl_info *info)
{
	net_shaper_generic_post(info);
}

int net_shaper_nl_pre_dumpit(struct netlink_callback *cb)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)cb->ctx;
	const struct genl_info *info = genl_info_dump(cb);

	return net_shaper_ctx_setup(info, NET_SHAPER_A_IFINDEX, ctx);
}

int net_shaper_nl_post_dumpit(struct netlink_callback *cb)
{
	net_shaper_ctx_cleanup((struct net_shaper_nl_ctx *)cb->ctx);
	return 0;
}

int net_shaper_nl_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_binding *binding;
	struct net_shaper_handle handle;
	struct net_shaper *shaper;
	struct sk_buff *msg;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_HANDLE))
		return -EINVAL;

	binding = net_shaper_binding_from_ctx(info->ctx);
	ret = net_shaper_parse_handle(info->attrs[NET_SHAPER_A_HANDLE], info,
				      &handle);
	if (ret < 0)
		return ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rcu_read_lock();
	shaper = net_shaper_lookup(binding, &handle);
	if (!shaper) {
		NL_SET_BAD_ATTR(info->extack,
				info->attrs[NET_SHAPER_A_HANDLE]);
		rcu_read_unlock();
		ret = -ENOENT;
		goto free_msg;
	}

	ret = net_shaper_fill_one(msg, binding, shaper, info);
	rcu_read_unlock();
	if (ret)
		goto free_msg;

	ret = genlmsg_reply(msg, info);
	if (ret)
		goto free_msg;

	return 0;

free_msg:
	nlmsg_free(msg);
	return ret;
}

int net_shaper_nl_get_dumpit(struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)cb->ctx;
	const struct genl_info *info = genl_info_dump(cb);
	struct net_shaper_hierarchy *hierarchy;
	struct net_shaper_binding *binding;
	struct net_shaper *shaper;
	int ret = 0;

	/* Don't error out dumps performed before any set operation. */
	binding = net_shaper_binding_from_ctx(ctx);
	hierarchy = net_shaper_hierarchy(binding);
	if (!hierarchy)
		return 0;

	rcu_read_lock();
	for (; (shaper = xa_find(&hierarchy->shapers, &ctx->start_index,
				 U32_MAX, XA_PRESENT)); ctx->start_index++) {
		ret = net_shaper_fill_one(skb, binding, shaper, info);
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}

int net_shaper_nl_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_delete_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

static void net_shaper_flush(struct net_shaper_binding *binding)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *cur;
	unsigned long index;

	if (!hierarchy)
		return;

	xa_lock(&hierarchy->shapers);
	xa_for_each(&hierarchy->shapers, index, cur) {
		__xa_erase(&hierarchy->shapers, index);
		kfree(cur);
	}
	xa_unlock(&hierarchy->shapers);
	kfree(hierarchy);
}

void net_shaper_flush_netdev(struct net_device *dev)
{
	struct net_shaper_binding binding = {
		.type = NET_SHAPER_BINDING_TYPE_NETDEV,
		.netdev = dev,
	};

	net_shaper_flush(&binding);
}

static int __init shaper_init(void)
{
	return genl_register_family(&net_shaper_nl_family);
}

subsys_initcall(shaper_init);

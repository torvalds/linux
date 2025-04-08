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

static void net_shaper_lock(struct net_shaper_binding *binding)
{
	switch (binding->type) {
	case NET_SHAPER_BINDING_TYPE_NETDEV:
		netdev_lock(binding->netdev);
		break;
	}
}

static void net_shaper_unlock(struct net_shaper_binding *binding)
{
	switch (binding->type) {
	case NET_SHAPER_BINDING_TYPE_NETDEV:
		netdev_unlock(binding->netdev);
		break;
	}
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

static const struct net_shaper_ops *
net_shaper_ops(struct net_shaper_binding *binding)
{
	if (binding->type == NET_SHAPER_BINDING_TYPE_NETDEV)
		return binding->netdev->netdev_ops->net_shaper_ops;

	/* No other type supported yet. */
	return NULL;
}

/* Count the number of [multi] attributes of the given type. */
static int net_shaper_list_len(struct genl_info *info, int type)
{
	struct nlattr *attr;
	int rem, cnt = 0;

	nla_for_each_attr_type(attr, type, genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem)
		cnt++;
	return cnt;
}

static int net_shaper_handle_size(void)
{
	return nla_total_size(nla_total_size(sizeof(u32)) +
			      nla_total_size(sizeof(u32)));
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

static void net_shaper_index_to_handle(u32 index,
				       struct net_shaper_handle *handle)
{
	handle->scope = FIELD_GET(NET_SHAPER_SCOPE_MASK, index);
	handle->id = FIELD_GET(NET_SHAPER_ID_MASK, index);
}

static void net_shaper_default_parent(const struct net_shaper_handle *handle,
				      struct net_shaper_handle *parent)
{
	switch (handle->scope) {
	case NET_SHAPER_SCOPE_UNSPEC:
	case NET_SHAPER_SCOPE_NETDEV:
	case __NET_SHAPER_SCOPE_MAX:
		parent->scope = NET_SHAPER_SCOPE_UNSPEC;
		break;

	case NET_SHAPER_SCOPE_QUEUE:
	case NET_SHAPER_SCOPE_NODE:
		parent->scope = NET_SHAPER_SCOPE_NETDEV;
		break;
	}
	parent->id = 0;
}

/*
 * MARK_0 is already in use due to XA_FLAGS_ALLOC, can't reuse such flag as
 * it's cleared by xa_store().
 */
#define NET_SHAPER_NOT_VALID XA_MARK_1

static struct net_shaper *
net_shaper_lookup(struct net_shaper_binding *binding,
		  const struct net_shaper_handle *handle)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	u32 index = net_shaper_handle_to_index(handle);

	if (!hierarchy || xa_get_mark(&hierarchy->shapers, index,
				      NET_SHAPER_NOT_VALID))
		return NULL;

	return xa_load(&hierarchy->shapers, index);
}

/* Allocate on demand the per device shaper's hierarchy container.
 * Called under the net shaper lock
 */
static struct net_shaper_hierarchy *
net_shaper_hierarchy_setup(struct net_shaper_binding *binding)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);

	if (hierarchy)
		return hierarchy;

	hierarchy = kmalloc(sizeof(*hierarchy), GFP_KERNEL);
	if (!hierarchy)
		return NULL;

	/* The flag is required for ID allocation */
	xa_init_flags(&hierarchy->shapers, XA_FLAGS_ALLOC);

	switch (binding->type) {
	case NET_SHAPER_BINDING_TYPE_NETDEV:
		/* Pairs with READ_ONCE in net_shaper_hierarchy. */
		WRITE_ONCE(binding->netdev->net_shaper_hierarchy, hierarchy);
		break;
	}
	return hierarchy;
}

/* Prepare the hierarchy container to actually insert the given shaper, doing
 * in advance the needed allocations.
 */
static int net_shaper_pre_insert(struct net_shaper_binding *binding,
				 struct net_shaper_handle *handle,
				 struct netlink_ext_ack *extack)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *prev, *cur;
	bool id_allocated = false;
	int ret, index;

	if (!hierarchy)
		return -ENOMEM;

	index = net_shaper_handle_to_index(handle);
	cur = xa_load(&hierarchy->shapers, index);
	if (cur)
		return 0;

	/* Allocated a new id, if needed. */
	if (handle->scope == NET_SHAPER_SCOPE_NODE &&
	    handle->id == NET_SHAPER_ID_UNSPEC) {
		u32 min, max;

		handle->id = NET_SHAPER_ID_MASK - 1;
		max = net_shaper_handle_to_index(handle);
		handle->id = 0;
		min = net_shaper_handle_to_index(handle);

		ret = xa_alloc(&hierarchy->shapers, &index, NULL,
			       XA_LIMIT(min, max), GFP_KERNEL);
		if (ret < 0) {
			NL_SET_ERR_MSG(extack, "Can't allocate new id for NODE shaper");
			return ret;
		}

		net_shaper_index_to_handle(index, handle);
		id_allocated = true;
	}

	cur = kzalloc(sizeof(*cur), GFP_KERNEL);
	if (!cur) {
		ret = -ENOMEM;
		goto free_id;
	}

	/* Mark 'tentative' shaper inside the hierarchy container.
	 * xa_set_mark is a no-op if the previous store fails.
	 */
	xa_lock(&hierarchy->shapers);
	prev = __xa_store(&hierarchy->shapers, index, cur, GFP_KERNEL);
	__xa_set_mark(&hierarchy->shapers, index, NET_SHAPER_NOT_VALID);
	xa_unlock(&hierarchy->shapers);
	if (xa_err(prev)) {
		NL_SET_ERR_MSG(extack, "Can't insert shaper into device store");
		kfree_rcu(cur, rcu);
		ret = xa_err(prev);
		goto free_id;
	}
	return 0;

free_id:
	if (id_allocated)
		xa_erase(&hierarchy->shapers, index);
	return ret;
}

/* Commit the tentative insert with the actual values.
 * Must be called only after a successful net_shaper_pre_insert().
 */
static void net_shaper_commit(struct net_shaper_binding *binding,
			      int nr_shapers, const struct net_shaper *shapers)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *cur;
	int index;
	int i;

	xa_lock(&hierarchy->shapers);
	for (i = 0; i < nr_shapers; ++i) {
		index = net_shaper_handle_to_index(&shapers[i].handle);

		cur = xa_load(&hierarchy->shapers, index);
		if (WARN_ON_ONCE(!cur))
			continue;

		/* Successful update: drop the tentative mark
		 * and update the hierarchy container.
		 */
		__xa_clear_mark(&hierarchy->shapers, index,
				NET_SHAPER_NOT_VALID);
		*cur = shapers[i];
	}
	xa_unlock(&hierarchy->shapers);
}

/* Rollback all the tentative inserts from the hierarchy. */
static void net_shaper_rollback(struct net_shaper_binding *binding)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *cur;
	unsigned long index;

	if (!hierarchy)
		return;

	xa_lock(&hierarchy->shapers);
	xa_for_each_marked(&hierarchy->shapers, index, cur,
			   NET_SHAPER_NOT_VALID) {
		__xa_erase(&hierarchy->shapers, index);
		kfree(cur);
	}
	xa_unlock(&hierarchy->shapers);
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

static int net_shaper_validate_caps(struct net_shaper_binding *binding,
				    struct nlattr **tb,
				    const struct genl_info *info,
				    struct net_shaper *shaper)
{
	const struct net_shaper_ops *ops = net_shaper_ops(binding);
	struct nlattr *bad = NULL;
	unsigned long caps = 0;

	ops->capabilities(binding, shaper->handle.scope, &caps);

	if (tb[NET_SHAPER_A_PRIORITY] &&
	    !(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_PRIORITY)))
		bad = tb[NET_SHAPER_A_PRIORITY];
	if (tb[NET_SHAPER_A_WEIGHT] &&
	    !(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_WEIGHT)))
		bad = tb[NET_SHAPER_A_WEIGHT];
	if (tb[NET_SHAPER_A_BW_MIN] &&
	    !(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_BW_MIN)))
		bad = tb[NET_SHAPER_A_BW_MIN];
	if (tb[NET_SHAPER_A_BW_MAX] &&
	    !(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_BW_MAX)))
		bad = tb[NET_SHAPER_A_BW_MAX];
	if (tb[NET_SHAPER_A_BURST] &&
	    !(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_BURST)))
		bad = tb[NET_SHAPER_A_BURST];

	if (!caps)
		bad = tb[NET_SHAPER_A_HANDLE];

	if (bad) {
		NL_SET_BAD_ATTR(info->extack, bad);
		return -EOPNOTSUPP;
	}

	if (shaper->handle.scope == NET_SHAPER_SCOPE_QUEUE &&
	    binding->type == NET_SHAPER_BINDING_TYPE_NETDEV &&
	    shaper->handle.id >= binding->netdev->real_num_tx_queues) {
		NL_SET_ERR_MSG_FMT(info->extack,
				   "Not existing queue id %d max %d",
				   shaper->handle.id,
				   binding->netdev->real_num_tx_queues);
		return -ENOENT;
	}

	/* The metric is really used only if there is *any* rate-related
	 * setting, either in current attributes set or in pre-existing
	 * values.
	 */
	if (shaper->burst || shaper->bw_min || shaper->bw_max) {
		u32 metric_cap = NET_SHAPER_A_CAPS_SUPPORT_METRIC_BPS +
				 shaper->metric;

		/* The metric test can fail even when the user did not
		 * specify the METRIC attribute. Pointing to rate related
		 * attribute will be confusing, as the attribute itself
		 * could be indeed supported, with a different metric.
		 * Be more specific.
		 */
		if (!(caps & BIT(metric_cap))) {
			NL_SET_ERR_MSG_FMT(info->extack, "Bad metric %d",
					   shaper->metric);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

static int net_shaper_parse_info(struct net_shaper_binding *binding,
				 struct nlattr **tb,
				 const struct genl_info *info,
				 struct net_shaper *shaper,
				 bool *exists)
{
	struct net_shaper *old;
	int ret;

	/* The shaper handle is the only mandatory attribute. */
	if (NL_REQ_ATTR_CHECK(info->extack, NULL, tb, NET_SHAPER_A_HANDLE))
		return -EINVAL;

	ret = net_shaper_parse_handle(tb[NET_SHAPER_A_HANDLE], info,
				      &shaper->handle);
	if (ret)
		return ret;

	if (shaper->handle.scope == NET_SHAPER_SCOPE_UNSPEC) {
		NL_SET_BAD_ATTR(info->extack, tb[NET_SHAPER_A_HANDLE]);
		return -EINVAL;
	}

	/* Fetch existing hierarchy, if any, so that user provide info will
	 * incrementally update the existing shaper configuration.
	 */
	old = net_shaper_lookup(binding, &shaper->handle);
	if (old)
		*shaper = *old;
	*exists = !!old;

	if (tb[NET_SHAPER_A_METRIC])
		shaper->metric = nla_get_u32(tb[NET_SHAPER_A_METRIC]);

	if (tb[NET_SHAPER_A_BW_MIN])
		shaper->bw_min = nla_get_uint(tb[NET_SHAPER_A_BW_MIN]);

	if (tb[NET_SHAPER_A_BW_MAX])
		shaper->bw_max = nla_get_uint(tb[NET_SHAPER_A_BW_MAX]);

	if (tb[NET_SHAPER_A_BURST])
		shaper->burst = nla_get_uint(tb[NET_SHAPER_A_BURST]);

	if (tb[NET_SHAPER_A_PRIORITY])
		shaper->priority = nla_get_u32(tb[NET_SHAPER_A_PRIORITY]);

	if (tb[NET_SHAPER_A_WEIGHT])
		shaper->weight = nla_get_u32(tb[NET_SHAPER_A_WEIGHT]);

	ret = net_shaper_validate_caps(binding, tb, info, shaper);
	if (ret < 0)
		return ret;

	return 0;
}

static int net_shaper_validate_nesting(struct net_shaper_binding *binding,
				       const struct net_shaper *shaper,
				       struct netlink_ext_ack *extack)
{
	const struct net_shaper_ops *ops = net_shaper_ops(binding);
	unsigned long caps = 0;

	ops->capabilities(binding, shaper->handle.scope, &caps);
	if (!(caps & BIT(NET_SHAPER_A_CAPS_SUPPORT_NESTING))) {
		NL_SET_ERR_MSG_FMT(extack,
				   "Nesting not supported for scope %d",
				   shaper->handle.scope);
		return -EOPNOTSUPP;
	}
	return 0;
}

/* Fetch the existing leaf and update it with the user-provided
 * attributes.
 */
static int net_shaper_parse_leaf(struct net_shaper_binding *binding,
				 const struct nlattr *attr,
				 const struct genl_info *info,
				 const struct net_shaper *node,
				 struct net_shaper *shaper)
{
	struct nlattr *tb[NET_SHAPER_A_WEIGHT + 1];
	bool exists;
	int ret;

	ret = nla_parse_nested(tb, NET_SHAPER_A_WEIGHT, attr,
			       net_shaper_leaf_info_nl_policy, info->extack);
	if (ret < 0)
		return ret;

	ret = net_shaper_parse_info(binding, tb, info, shaper, &exists);
	if (ret < 0)
		return ret;

	if (shaper->handle.scope != NET_SHAPER_SCOPE_QUEUE) {
		NL_SET_BAD_ATTR(info->extack, tb[NET_SHAPER_A_HANDLE]);
		return -EINVAL;
	}

	if (node->handle.scope == NET_SHAPER_SCOPE_NODE) {
		ret = net_shaper_validate_nesting(binding, shaper,
						  info->extack);
		if (ret < 0)
			return ret;
	}

	if (!exists)
		net_shaper_default_parent(&shaper->handle, &shaper->parent);
	return 0;
}

/* Alike net_parse_shaper_info(), but additionally allow the user specifying
 * the shaper's parent handle.
 */
static int net_shaper_parse_node(struct net_shaper_binding *binding,
				 struct nlattr **tb,
				 const struct genl_info *info,
				 struct net_shaper *shaper)
{
	bool exists;
	int ret;

	ret = net_shaper_parse_info(binding, tb, info, shaper, &exists);
	if (ret)
		return ret;

	if (shaper->handle.scope != NET_SHAPER_SCOPE_NODE &&
	    shaper->handle.scope != NET_SHAPER_SCOPE_NETDEV) {
		NL_SET_BAD_ATTR(info->extack, tb[NET_SHAPER_A_HANDLE]);
		return -EINVAL;
	}

	if (tb[NET_SHAPER_A_PARENT]) {
		ret = net_shaper_parse_handle(tb[NET_SHAPER_A_PARENT], info,
					      &shaper->parent);
		if (ret)
			return ret;

		if (shaper->parent.scope != NET_SHAPER_SCOPE_NODE &&
		    shaper->parent.scope != NET_SHAPER_SCOPE_NETDEV) {
			NL_SET_BAD_ATTR(info->extack, tb[NET_SHAPER_A_PARENT]);
			return -EINVAL;
		}
	}
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

int net_shaper_nl_cap_pre_doit(const struct genl_split_ops *ops,
			       struct sk_buff *skb, struct genl_info *info)
{
	return net_shaper_generic_pre(info, NET_SHAPER_A_CAPS_IFINDEX);
}

void net_shaper_nl_cap_post_doit(const struct genl_split_ops *ops,
				 struct sk_buff *skb, struct genl_info *info)
{
	net_shaper_generic_post(info);
}

int net_shaper_nl_cap_pre_dumpit(struct netlink_callback *cb)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)cb->ctx;

	return net_shaper_ctx_setup(genl_info_dump(cb),
				    NET_SHAPER_A_CAPS_IFINDEX, ctx);
}

int net_shaper_nl_cap_post_dumpit(struct netlink_callback *cb)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)cb->ctx;

	net_shaper_ctx_cleanup(ctx);
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
	struct net_shaper_hierarchy *hierarchy;
	struct net_shaper_binding *binding;
	const struct net_shaper_ops *ops;
	struct net_shaper_handle handle;
	struct net_shaper shaper = {};
	bool exists;
	int ret;

	binding = net_shaper_binding_from_ctx(info->ctx);

	net_shaper_lock(binding);
	ret = net_shaper_parse_info(binding, info->attrs, info, &shaper,
				    &exists);
	if (ret)
		goto unlock;

	if (!exists)
		net_shaper_default_parent(&shaper.handle, &shaper.parent);

	hierarchy = net_shaper_hierarchy_setup(binding);
	if (!hierarchy) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* The 'set' operation can't create node-scope shapers. */
	handle = shaper.handle;
	if (handle.scope == NET_SHAPER_SCOPE_NODE &&
	    !net_shaper_lookup(binding, &handle)) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = net_shaper_pre_insert(binding, &handle, info->extack);
	if (ret)
		goto unlock;

	ops = net_shaper_ops(binding);
	ret = ops->set(binding, &shaper, info->extack);
	if (ret) {
		net_shaper_rollback(binding);
		goto unlock;
	}

	net_shaper_commit(binding, 1, &shaper);

unlock:
	net_shaper_unlock(binding);
	return ret;
}

static int __net_shaper_delete(struct net_shaper_binding *binding,
			       struct net_shaper *shaper,
			       struct netlink_ext_ack *extack)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper_handle parent_handle, handle = shaper->handle;
	const struct net_shaper_ops *ops = net_shaper_ops(binding);
	int ret;

again:
	parent_handle = shaper->parent;

	ret = ops->delete(binding, &handle, extack);
	if (ret < 0)
		return ret;

	xa_erase(&hierarchy->shapers, net_shaper_handle_to_index(&handle));
	kfree_rcu(shaper, rcu);

	/* Eventually delete the parent, if it is left over with no leaves. */
	if (parent_handle.scope == NET_SHAPER_SCOPE_NODE) {
		shaper = net_shaper_lookup(binding, &parent_handle);
		if (shaper && !--shaper->leaves) {
			handle = parent_handle;
			goto again;
		}
	}
	return 0;
}

static int net_shaper_handle_cmp(const struct net_shaper_handle *a,
				 const struct net_shaper_handle *b)
{
	/* Must avoid holes in struct net_shaper_handle. */
	BUILD_BUG_ON(sizeof(*a) != 8);

	return memcmp(a, b, sizeof(*a));
}

static int net_shaper_parent_from_leaves(int leaves_count,
					 const struct net_shaper *leaves,
					 struct net_shaper *node,
					 struct netlink_ext_ack *extack)
{
	struct net_shaper_handle parent = leaves[0].parent;
	int i;

	for (i = 1; i < leaves_count; ++i) {
		if (net_shaper_handle_cmp(&leaves[i].parent, &parent)) {
			NL_SET_ERR_MSG_FMT(extack, "All the leaves shapers must have the same old parent");
			return -EINVAL;
		}
	}

	node->parent = parent;
	return 0;
}

static int __net_shaper_group(struct net_shaper_binding *binding,
			      bool update_node, int leaves_count,
			      struct net_shaper *leaves,
			      struct net_shaper *node,
			      struct netlink_ext_ack *extack)
{
	const struct net_shaper_ops *ops = net_shaper_ops(binding);
	struct net_shaper_handle leaf_handle;
	struct net_shaper *parent = NULL;
	bool new_node = false;
	int i, ret;

	if (node->handle.scope == NET_SHAPER_SCOPE_NODE) {
		new_node = node->handle.id == NET_SHAPER_ID_UNSPEC;

		if (!new_node && !net_shaper_lookup(binding, &node->handle)) {
			/* The related attribute is not available when
			 * reaching here from the delete() op.
			 */
			NL_SET_ERR_MSG_FMT(extack, "Node shaper %d:%d does not exists",
					   node->handle.scope, node->handle.id);
			return -ENOENT;
		}

		/* When unspecified, the node parent scope is inherited from
		 * the leaves.
		 */
		if (node->parent.scope == NET_SHAPER_SCOPE_UNSPEC) {
			ret = net_shaper_parent_from_leaves(leaves_count,
							    leaves, node,
							    extack);
			if (ret)
				return ret;
		}

	} else {
		net_shaper_default_parent(&node->handle, &node->parent);
	}

	if (node->parent.scope == NET_SHAPER_SCOPE_NODE) {
		parent = net_shaper_lookup(binding, &node->parent);
		if (!parent) {
			NL_SET_ERR_MSG_FMT(extack, "Node parent shaper %d:%d does not exists",
					   node->parent.scope, node->parent.id);
			return -ENOENT;
		}

		ret = net_shaper_validate_nesting(binding, node, extack);
		if (ret < 0)
			return ret;
	}

	if (update_node) {
		/* For newly created node scope shaper, the following will
		 * update the handle, due to id allocation.
		 */
		ret = net_shaper_pre_insert(binding, &node->handle, extack);
		if (ret)
			return ret;
	}

	for (i = 0; i < leaves_count; ++i) {
		leaf_handle = leaves[i].handle;

		ret = net_shaper_pre_insert(binding, &leaf_handle, extack);
		if (ret)
			goto rollback;

		if (!net_shaper_handle_cmp(&leaves[i].parent, &node->handle))
			continue;

		/* The leaves shapers will be nested to the node, update the
		 * linking accordingly.
		 */
		leaves[i].parent = node->handle;
		node->leaves++;
	}

	ret = ops->group(binding, leaves_count, leaves, node, extack);
	if (ret < 0)
		goto rollback;

	/* The node's parent gains a new leaf only when the node itself
	 * is created by this group operation
	 */
	if (new_node && parent)
		parent->leaves++;
	if (update_node)
		net_shaper_commit(binding, 1, node);
	net_shaper_commit(binding, leaves_count, leaves);
	return 0;

rollback:
	net_shaper_rollback(binding);
	return ret;
}

static int net_shaper_pre_del_node(struct net_shaper_binding *binding,
				   const struct net_shaper *shaper,
				   struct netlink_ext_ack *extack)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *cur, *leaves, node = {};
	int ret, leaves_count = 0;
	unsigned long index;
	bool update_node;

	if (!shaper->leaves)
		return 0;

	/* Fetch the new node information. */
	node.handle = shaper->parent;
	cur = net_shaper_lookup(binding, &node.handle);
	if (cur) {
		node = *cur;
	} else {
		/* A scope NODE shaper can be nested only to the NETDEV scope
		 * shaper without creating the latter, this check may fail only
		 * if the data is in inconsistent status.
		 */
		if (WARN_ON_ONCE(node.handle.scope != NET_SHAPER_SCOPE_NETDEV))
			return -EINVAL;
	}

	leaves = kcalloc(shaper->leaves, sizeof(struct net_shaper),
			 GFP_KERNEL);
	if (!leaves)
		return -ENOMEM;

	/* Build the leaves arrays. */
	xa_for_each(&hierarchy->shapers, index, cur) {
		if (net_shaper_handle_cmp(&cur->parent, &shaper->handle))
			continue;

		if (WARN_ON_ONCE(leaves_count == shaper->leaves)) {
			ret = -EINVAL;
			goto free;
		}

		leaves[leaves_count++] = *cur;
	}

	/* When re-linking to the netdev shaper, avoid the eventual, implicit,
	 * creation of the new node, would be surprising since the user is
	 * doing a delete operation.
	 */
	update_node = node.handle.scope != NET_SHAPER_SCOPE_NETDEV;
	ret = __net_shaper_group(binding, update_node, leaves_count,
				 leaves, &node, extack);

free:
	kfree(leaves);
	return ret;
}

int net_shaper_nl_delete_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_hierarchy *hierarchy;
	struct net_shaper_binding *binding;
	struct net_shaper_handle handle;
	struct net_shaper *shaper;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_HANDLE))
		return -EINVAL;

	binding = net_shaper_binding_from_ctx(info->ctx);

	net_shaper_lock(binding);
	ret = net_shaper_parse_handle(info->attrs[NET_SHAPER_A_HANDLE], info,
				      &handle);
	if (ret)
		goto unlock;

	hierarchy = net_shaper_hierarchy(binding);
	if (!hierarchy) {
		ret = -ENOENT;
		goto unlock;
	}

	shaper = net_shaper_lookup(binding, &handle);
	if (!shaper) {
		ret = -ENOENT;
		goto unlock;
	}

	if (handle.scope == NET_SHAPER_SCOPE_NODE) {
		ret = net_shaper_pre_del_node(binding, shaper, info->extack);
		if (ret)
			goto unlock;
	}

	ret = __net_shaper_delete(binding, shaper, info->extack);

unlock:
	net_shaper_unlock(binding);
	return ret;
}

static int net_shaper_group_send_reply(struct net_shaper_binding *binding,
				       const struct net_shaper_handle *handle,
				       struct genl_info *info,
				       struct sk_buff *msg)
{
	void *hdr;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		goto free_msg;

	if (net_shaper_fill_binding(msg, binding, NET_SHAPER_A_IFINDEX) ||
	    net_shaper_fill_handle(msg, handle, NET_SHAPER_A_HANDLE))
		goto free_msg;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

free_msg:
	/* Should never happen as msg is pre-allocated with enough space. */
	WARN_ONCE(true, "calculated message payload length (%d)",
		  net_shaper_handle_size());
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int net_shaper_nl_group_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper **old_nodes, *leaves, node = {};
	struct net_shaper_hierarchy *hierarchy;
	struct net_shaper_binding *binding;
	int i, ret, rem, leaves_count;
	int old_nodes_count = 0;
	struct sk_buff *msg;
	struct nlattr *attr;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_LEAVES))
		return -EINVAL;

	binding = net_shaper_binding_from_ctx(info->ctx);

	/* The group operation is optional. */
	if (!net_shaper_ops(binding)->group)
		return -EOPNOTSUPP;

	net_shaper_lock(binding);
	leaves_count = net_shaper_list_len(info, NET_SHAPER_A_LEAVES);
	if (!leaves_count) {
		NL_SET_BAD_ATTR(info->extack,
				info->attrs[NET_SHAPER_A_LEAVES]);
		ret = -EINVAL;
		goto unlock;
	}

	leaves = kcalloc(leaves_count, sizeof(struct net_shaper) +
			 sizeof(struct net_shaper *), GFP_KERNEL);
	if (!leaves) {
		ret = -ENOMEM;
		goto unlock;
	}
	old_nodes = (void *)&leaves[leaves_count];

	ret = net_shaper_parse_node(binding, info->attrs, info, &node);
	if (ret)
		goto free_leaves;

	i = 0;
	nla_for_each_attr_type(attr, NET_SHAPER_A_LEAVES,
			       genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem) {
		if (WARN_ON_ONCE(i >= leaves_count))
			goto free_leaves;

		ret = net_shaper_parse_leaf(binding, attr, info,
					    &node, &leaves[i]);
		if (ret)
			goto free_leaves;
		i++;
	}

	/* Prepare the msg reply in advance, to avoid device operation
	 * rollback on allocation failure.
	 */
	msg = genlmsg_new(net_shaper_handle_size(), GFP_KERNEL);
	if (!msg)
		goto free_leaves;

	hierarchy = net_shaper_hierarchy_setup(binding);
	if (!hierarchy) {
		ret = -ENOMEM;
		goto free_msg;
	}

	/* Record the node shapers that this group() operation can make
	 * childless for later cleanup.
	 */
	for (i = 0; i < leaves_count; i++) {
		if (leaves[i].parent.scope == NET_SHAPER_SCOPE_NODE &&
		    net_shaper_handle_cmp(&leaves[i].parent, &node.handle)) {
			struct net_shaper *tmp;

			tmp = net_shaper_lookup(binding, &leaves[i].parent);
			if (!tmp)
				continue;

			old_nodes[old_nodes_count++] = tmp;
		}
	}

	ret = __net_shaper_group(binding, true, leaves_count, leaves, &node,
				 info->extack);
	if (ret)
		goto free_msg;

	/* Check if we need to delete any node left alone by the new leaves
	 * linkage.
	 */
	for (i = 0; i < old_nodes_count; ++i) {
		struct net_shaper *tmp = old_nodes[i];

		if (--tmp->leaves > 0)
			continue;

		/* Errors here are not fatal: the grouping operation is
		 * completed, and user-space can still explicitly clean-up
		 * left-over nodes.
		 */
		__net_shaper_delete(binding, tmp, info->extack);
	}

	ret = net_shaper_group_send_reply(binding, &node.handle, info, msg);
	if (ret)
		GENL_SET_ERR_MSG_FMT(info, "Can't send reply");

free_leaves:
	kfree(leaves);

unlock:
	net_shaper_unlock(binding);
	return ret;

free_msg:
	kfree_skb(msg);
	goto free_leaves;
}

static int
net_shaper_cap_fill_one(struct sk_buff *msg,
			struct net_shaper_binding *binding,
			enum net_shaper_scope scope, unsigned long flags,
			const struct genl_info *info)
{
	unsigned long cur;
	void *hdr;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		return -EMSGSIZE;

	if (net_shaper_fill_binding(msg, binding, NET_SHAPER_A_CAPS_IFINDEX) ||
	    nla_put_u32(msg, NET_SHAPER_A_CAPS_SCOPE, scope))
		goto nla_put_failure;

	for (cur = NET_SHAPER_A_CAPS_SUPPORT_METRIC_BPS;
	     cur <= NET_SHAPER_A_CAPS_MAX; ++cur) {
		if (flags & BIT(cur) && nla_put_flag(msg, cur))
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int net_shaper_nl_cap_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_binding *binding;
	const struct net_shaper_ops *ops;
	enum net_shaper_scope scope;
	unsigned long flags = 0;
	struct sk_buff *msg;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_CAPS_SCOPE))
		return -EINVAL;

	binding = net_shaper_binding_from_ctx(info->ctx);
	scope = nla_get_u32(info->attrs[NET_SHAPER_A_CAPS_SCOPE]);
	ops = net_shaper_ops(binding);
	ops->capabilities(binding, scope, &flags);
	if (!flags)
		return -EOPNOTSUPP;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = net_shaper_cap_fill_one(msg, binding, scope, flags, info);
	if (ret)
		goto free_msg;

	ret =  genlmsg_reply(msg, info);
	if (ret)
		goto free_msg;
	return 0;

free_msg:
	nlmsg_free(msg);
	return ret;
}

int net_shaper_nl_cap_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct net_shaper_binding *binding;
	const struct net_shaper_ops *ops;
	enum net_shaper_scope scope;
	int ret;

	binding = net_shaper_binding_from_ctx(cb->ctx);
	ops = net_shaper_ops(binding);
	for (scope = 0; scope <= NET_SHAPER_SCOPE_MAX; ++scope) {
		unsigned long flags = 0;

		ops->capabilities(binding, scope, &flags);
		if (!flags)
			continue;

		ret = net_shaper_cap_fill_one(skb, binding, scope, flags,
					      info);
		if (ret)
			return ret;
	}

	return 0;
}

static void net_shaper_flush(struct net_shaper_binding *binding)
{
	struct net_shaper_hierarchy *hierarchy = net_shaper_hierarchy(binding);
	struct net_shaper *cur;
	unsigned long index;

	if (!hierarchy)
		return;

	net_shaper_lock(binding);
	xa_lock(&hierarchy->shapers);
	xa_for_each(&hierarchy->shapers, index, cur) {
		__xa_erase(&hierarchy->shapers, index);
		kfree(cur);
	}
	xa_unlock(&hierarchy->shapers);
	net_shaper_unlock(binding);

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

void net_shaper_set_real_num_tx_queues(struct net_device *dev,
				       unsigned int txq)
{
	struct net_shaper_hierarchy *hierarchy;
	struct net_shaper_binding binding;
	int i;

	binding.type = NET_SHAPER_BINDING_TYPE_NETDEV;
	binding.netdev = dev;
	hierarchy = net_shaper_hierarchy(&binding);
	if (!hierarchy)
		return;

	/* Only drivers implementing shapers support ensure
	 * the lock is acquired in advance.
	 */
	netdev_assert_locked(dev);

	/* Take action only when decreasing the tx queue number. */
	for (i = txq; i < dev->real_num_tx_queues; ++i) {
		struct net_shaper_handle handle, parent_handle;
		struct net_shaper *shaper;
		u32 index;

		handle.scope = NET_SHAPER_SCOPE_QUEUE;
		handle.id = i;
		shaper = net_shaper_lookup(&binding, &handle);
		if (!shaper)
			continue;

		/* Don't touch the H/W for the queue shaper, the drivers already
		 * deleted the queue and related resources.
		 */
		parent_handle = shaper->parent;
		index = net_shaper_handle_to_index(&handle);
		xa_erase(&hierarchy->shapers, index);
		kfree_rcu(shaper, rcu);

		/* The recursion on parent does the full job. */
		if (parent_handle.scope != NET_SHAPER_SCOPE_NODE)
			continue;

		shaper = net_shaper_lookup(&binding, &parent_handle);
		if (shaper && !--shaper->leaves)
			__net_shaper_delete(&binding, shaper, NULL);
	}
}

static int __init shaper_init(void)
{
	return genl_register_family(&net_shaper_nl_family);
}

subsys_initcall(shaper_init);

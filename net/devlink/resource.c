// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include "devl_internal.h"

/**
 * struct devlink_resource - devlink resource
 * @name: name of the resource
 * @id: id, per devlink instance
 * @size: size of the resource
 * @size_new: updated size of the resource, reload is needed
 * @size_valid: valid in case the total size of the resource is valid
 *              including its children
 * @parent: parent resource
 * @size_params: size parameters
 * @list: parent list
 * @resource_list: list of child resources
 * @occ_get: occupancy getter callback
 * @occ_get_priv: occupancy getter callback priv
 */
struct devlink_resource {
	const char *name;
	u64 id;
	u64 size;
	u64 size_new;
	bool size_valid;
	struct devlink_resource *parent;
	struct devlink_resource_size_params size_params;
	struct list_head list;
	struct list_head resource_list;
	devlink_resource_occ_get_t *occ_get;
	void *occ_get_priv;
};

static struct devlink_resource *
__devlink_resource_find(struct list_head *resource_list_head,
			struct devlink_resource *resource,
			u64 resource_id)
{
	struct list_head *resource_list;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = resource_list_head;

	list_for_each_entry(resource, resource_list, list) {
		struct devlink_resource *child_resource;

		if (resource->id == resource_id)
			return resource;

		child_resource = __devlink_resource_find(resource_list_head,
							 resource,
							 resource_id);
		if (child_resource)
			return child_resource;
	}
	return NULL;
}

static struct devlink_resource *
devlink_resource_find(struct devlink *devlink,
		      struct devlink_resource *resource, u64 resource_id)
{
	return __devlink_resource_find(&devlink->resource_list,
				       resource, resource_id);
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
		NL_SET_ERR_MSG(extack, "Size larger than maximum");
		err = -EINVAL;
	}

	if (size < resource->size_params.size_min) {
		NL_SET_ERR_MSG(extack, "Size smaller than minimum");
		err = -EINVAL;
	}

	div64_u64_rem(size, resource->size_params.size_granularity, &reminder);
	if (reminder) {
		NL_SET_ERR_MSG(extack, "Wrong granularity");
		err = -EINVAL;
	}

	return err;
}

int devlink_nl_resource_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	u64 resource_id;
	u64 size;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_RESOURCE_ID) ||
	    GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_RESOURCE_SIZE))
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
	if (devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_SIZE_GRAN,
			       size_params->size_granularity) ||
	    devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_SIZE_MAX,
			       size_params->size_max) ||
	    devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_SIZE_MIN,
			       size_params->size_min) ||
	    nla_put_u8(skb, DEVLINK_ATTR_RESOURCE_UNIT, size_params->unit))
		return -EMSGSIZE;
	return 0;
}

static int devlink_resource_occ_put(struct devlink_resource *resource,
				    struct sk_buff *skb)
{
	if (!resource->occ_get)
		return 0;
	return devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_OCC,
				  resource->occ_get(resource->occ_get_priv));
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
	    devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_SIZE, resource->size) ||
	    devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_ID, resource->id))
		goto nla_put_failure;
	if (resource->size != resource->size_new &&
	    devlink_nl_put_u64(skb, DEVLINK_ATTR_RESOURCE_SIZE_NEW,
			       resource->size_new))
		goto nla_put_failure;
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

static int devlink_resource_list_fill(struct sk_buff *skb,
				      struct devlink *devlink,
				      struct list_head *resource_list_head,
				      int *idx)
{
	struct devlink_resource *resource;
	int i = 0;
	int err;

	list_for_each_entry(resource, resource_list_head, list) {
		if (i < *idx) {
			i++;
			continue;
		}
		err = devlink_resource_put(devlink, skb, resource);
		if (err) {
			*idx = i;
			return err;
		}
		i++;
	}
	*idx = 0;
	return 0;
}

static int devlink_resource_fill(struct genl_info *info,
				 enum devlink_command cmd, int flags)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	struct list_head *resource_list;
	struct nlattr *resources_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	bool incomplete;
	void *hdr;
	int i;
	int err;

	resource_list = devlink_port ?
		&devlink_port->resource_list : &devlink->resource_list;
	resource = list_first_entry(resource_list,
				    struct devlink_resource, list);
start_again:
	err = devlink_nl_msg_reply_and_new(&skb, info);
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
	if (devlink_port &&
	    nla_put_u32(skb, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;

	resources_attr = nla_nest_start_noflag(skb,
					       DEVLINK_ATTR_RESOURCE_LIST);
	if (!resources_attr)
		goto nla_put_failure;

	incomplete = false;
	i = 0;
	list_for_each_entry_from(resource, resource_list, list) {
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
		err = devlink_nl_msg_reply_and_new(&skb, info);
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

int devlink_nl_resource_dump_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	struct list_head *resource_list;

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX] && !devlink_port)
		return -ENODEV;

	resource_list = devlink_port ?
		&devlink_port->resource_list : &devlink->resource_list;
	if (list_empty(resource_list))
		return -EOPNOTSUPP;

	return devlink_resource_fill(info, DEVLINK_CMD_RESOURCE_DUMP, 0);
}

static int
devlink_resource_dump_fill_one(struct sk_buff *skb, struct devlink *devlink,
			       struct devlink_port *devlink_port,
			       struct netlink_callback *cb, int flags, int *idx)
{
	struct list_head *resource_list;
	struct nlattr *resources_attr;
	int start_idx = *idx;
	void *hdr;
	int err;

	resource_list = devlink_port ?
		&devlink_port->resource_list : &devlink->resource_list;

	if (list_empty(resource_list))
		return 0;

	err = -EMSGSIZE;
	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, flags, DEVLINK_CMD_RESOURCE_DUMP);
	if (!hdr)
		return err;

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;
	if (devlink_port &&
	    nla_put_u32(skb, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;

	resources_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_RESOURCE_LIST);
	if (!resources_attr)
		goto nla_put_failure;

	err = devlink_resource_list_fill(skb, devlink, resource_list, idx);
	if (err) {
		if (*idx == start_idx)
			goto resource_list_cancel;
		nla_nest_end(skb, resources_attr);
		genlmsg_end(skb, hdr);
		return err;
	}
	nla_nest_end(skb, resources_attr);
	genlmsg_end(skb, hdr);
	return 0;

resource_list_cancel:
	nla_nest_cancel(skb, resources_attr);
nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return err;
}

static int
devlink_nl_resource_dump_one(struct sk_buff *skb, struct devlink *devlink,
			     struct netlink_callback *cb, int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	const struct genl_info *info = genl_info_dump(cb);
	struct devlink_port *devlink_port;
	struct nlattr *scope_attr = NULL;
	unsigned long port_idx;
	u32 scope = 0;
	int err;

	if (info->attrs && info->attrs[DEVLINK_ATTR_RESOURCE_SCOPE_MASK]) {
		scope_attr = info->attrs[DEVLINK_ATTR_RESOURCE_SCOPE_MASK];
		scope = nla_get_u32(scope_attr);
		if (!scope) {
			NL_SET_ERR_MSG_ATTR(info->extack, scope_attr,
					    "empty resource scope selection");
			return -EINVAL;
		}
	}

	if (!state->port_ctx.index_valid &&
	    (!scope || (scope & DEVLINK_RESOURCE_SCOPE_DEV))) {
		err = devlink_resource_dump_fill_one(skb, devlink, NULL,
						     cb, flags, &state->idx);
		if (err)
			return err;
		state->idx = 0;
	}

	if (scope && !(scope & DEVLINK_RESOURCE_SCOPE_PORT))
		goto out;
	/* Check in case port was removed between dump callbacks. */
	if (state->port_ctx.index_valid &&
	    !xa_load(&devlink->ports, state->port_ctx.index))
		state->idx = 0;
	state->port_ctx.index_valid = true;
	xa_for_each_start(&devlink->ports, port_idx, devlink_port,
			  state->port_ctx.index) {
		err = devlink_resource_dump_fill_one(skb, devlink, devlink_port,
						     cb, flags, &state->idx);
		if (err) {
			state->port_ctx.index = port_idx;
			return err;
		}
		state->idx = 0;
	}
out:
	state->port_ctx.index_valid = false;
	state->port_ctx.index = 0;
	return 0;
}

int devlink_nl_resource_dump_dumpit(struct sk_buff *skb,
				    struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_resource_dump_one);
}

int devlink_resources_validate(struct devlink *devlink,
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

static int
__devl_resource_register(struct devlink *devlink,
			 struct list_head *resource_list_head,
			 const char *resource_name, u64 resource_size,
			 u64 resource_id, u64 parent_resource_id,
			 const struct devlink_resource_size_params *params)
{
	struct devlink_resource *resource;
	struct list_head *resource_list;
	bool top_hierarchy;

	lockdep_assert_held(&devlink->lock);

	top_hierarchy = parent_resource_id == DEVLINK_RESOURCE_ID_PARENT_TOP;

	resource = __devlink_resource_find(resource_list_head, NULL,
					   resource_id);
	if (resource)
		return -EEXIST;

	resource = kzalloc_obj(*resource);
	if (!resource)
		return -ENOMEM;

	if (top_hierarchy) {
		resource_list = resource_list_head;
	} else {
		struct devlink_resource *parent_resource;

		parent_resource = __devlink_resource_find(resource_list_head,
							  NULL,
							  parent_resource_id);
		if (parent_resource) {
			resource_list = &parent_resource->resource_list;
			resource->parent = parent_resource;
		} else {
			kfree(resource);
			return -EINVAL;
		}
	}

	resource->name = resource_name;
	resource->size = resource_size;
	resource->size_new = resource_size;
	resource->id = resource_id;
	resource->size_valid = true;
	memcpy(&resource->size_params, params, sizeof(resource->size_params));
	INIT_LIST_HEAD(&resource->resource_list);
	list_add_tail(&resource->list, resource_list);

	return 0;
}

/**
 * devl_resource_register - devlink resource register
 *
 * @devlink: devlink
 * @resource_name: resource's name
 * @resource_size: resource's size
 * @resource_id: resource's id
 * @parent_resource_id: resource's parent id
 * @params: size parameters
 *
 * Generic resources should reuse the same names across drivers.
 * Please see the generic resources list at:
 * Documentation/networking/devlink/devlink-resource.rst
 *
 * Return: 0 on success, negative error code otherwise.
 */
int devl_resource_register(struct devlink *devlink, const char *resource_name,
			   u64 resource_size, u64 resource_id,
			   u64 parent_resource_id,
			   const struct devlink_resource_size_params *params)
{
	return __devl_resource_register(devlink, &devlink->resource_list,
					resource_name, resource_size,
					resource_id, parent_resource_id,
					params);
}
EXPORT_SYMBOL_GPL(devl_resource_register);

static void devlink_resource_unregister(struct devlink_resource *resource)
{
	struct devlink_resource *tmp, *child_resource;

	list_for_each_entry_safe(child_resource, tmp, &resource->resource_list,
				 list) {
		devlink_resource_unregister(child_resource);
		list_del(&child_resource->list);
		kfree(child_resource);
	}
}

static void
__devl_resources_unregister(struct devlink *devlink,
			    struct list_head *resource_list_head)
{
	struct devlink_resource *tmp, *child_resource;

	lockdep_assert_held(&devlink->lock);

	list_for_each_entry_safe(child_resource, tmp, resource_list_head,
				 list) {
		devlink_resource_unregister(child_resource);
		list_del(&child_resource->list);
		kfree(child_resource);
	}
}

/**
 * devl_resources_unregister - free all resources
 *
 * @devlink: devlink
 */
void devl_resources_unregister(struct devlink *devlink)
{
	__devl_resources_unregister(devlink, &devlink->resource_list);
}
EXPORT_SYMBOL_GPL(devl_resources_unregister);

/**
 *	devlink_resources_unregister - free all resources
 *
 *	@devlink: devlink
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_resources_unregister(struct devlink *devlink)
{
	devl_lock(devlink);
	devl_resources_unregister(devlink);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_resources_unregister);

/**
 * devl_resource_size_get - get and update size
 *
 * @devlink: devlink
 * @resource_id: the requested resource id
 * @p_resource_size: ptr to update
 */
int devl_resource_size_get(struct devlink *devlink,
			   u64 resource_id,
			   u64 *p_resource_size)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (!resource)
		return -EINVAL;
	*p_resource_size = resource->size_new;
	resource->size = resource->size_new;
	return 0;
}
EXPORT_SYMBOL_GPL(devl_resource_size_get);

/**
 * devl_resource_occ_get_register - register occupancy getter
 *
 * @devlink: devlink
 * @resource_id: resource id
 * @occ_get: occupancy getter callback
 * @occ_get_priv: occupancy getter callback priv
 */
void devl_resource_occ_get_register(struct devlink *devlink,
				    u64 resource_id,
				    devlink_resource_occ_get_t *occ_get,
				    void *occ_get_priv)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		return;
	WARN_ON(resource->occ_get);

	resource->occ_get = occ_get;
	resource->occ_get_priv = occ_get_priv;
}
EXPORT_SYMBOL_GPL(devl_resource_occ_get_register);

/**
 * devl_resource_occ_get_unregister - unregister occupancy getter
 *
 * @devlink: devlink
 * @resource_id: resource id
 */
void devl_resource_occ_get_unregister(struct devlink *devlink,
				      u64 resource_id)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		return;
	WARN_ON(!resource->occ_get);

	resource->occ_get = NULL;
	resource->occ_get_priv = NULL;
}
EXPORT_SYMBOL_GPL(devl_resource_occ_get_unregister);

/**
 * devl_port_resource_register - devlink port resource register
 *
 * @devlink_port: devlink port
 * @resource_name: resource's name
 * @resource_size: resource's size
 * @resource_id: resource's id
 * @parent_resource_id: resource's parent id
 * @params: size parameters
 *
 * Generic resources should reuse the same names across drivers.
 * Please see the generic resources list at:
 * Documentation/networking/devlink/devlink-resource.rst
 *
 * Return: 0 on success, negative error code otherwise.
 */
int
devl_port_resource_register(struct devlink_port *devlink_port,
			    const char *resource_name,
			    u64 resource_size, u64 resource_id,
			    u64 parent_resource_id,
			    const struct devlink_resource_size_params *params)
{
	return __devl_resource_register(devlink_port->devlink,
					&devlink_port->resource_list,
					resource_name, resource_size,
					resource_id, parent_resource_id,
					params);
}
EXPORT_SYMBOL_GPL(devl_port_resource_register);

/**
 * devl_port_resources_unregister - unregister all devlink port resources
 *
 * @devlink_port: devlink port
 */
void devl_port_resources_unregister(struct devlink_port *devlink_port)
{
	__devl_resources_unregister(devlink_port->devlink,
				    &devlink_port->resource_list);
}
EXPORT_SYMBOL_GPL(devl_port_resources_unregister);

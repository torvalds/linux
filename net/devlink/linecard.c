// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include "devl_internal.h"

struct devlink_linecard {
	struct list_head list;
	struct devlink *devlink;
	unsigned int index;
	const struct devlink_linecard_ops *ops;
	void *priv;
	enum devlink_linecard_state state;
	struct mutex state_lock; /* Protects state */
	const char *type;
	struct devlink_linecard_type *types;
	unsigned int types_count;
	u32 rel_index;
};

unsigned int devlink_linecard_index(struct devlink_linecard *linecard)
{
	return linecard->index;
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

	if (devlink_rel_devlink_handle_put(msg, devlink,
					   linecard->rel_index,
					   DEVLINK_ATTR_NESTED_DEVLINK,
					   NULL))
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

	if (!__devl_is_registered(devlink) || !devlink_nl_notify_need(devlink))
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

	devlink_nl_notify_send(devlink, msg);
}

void devlink_linecards_notify_register(struct devlink *devlink)
{
	struct devlink_linecard *linecard;

	list_for_each_entry(linecard, &devlink->linecard_list, list)
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
}

void devlink_linecards_notify_unregister(struct devlink *devlink)
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

int devlink_nl_linecard_set_doit(struct sk_buff *skb, struct genl_info *info)
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

static void devlink_linecard_rel_notify_cb(struct devlink *devlink,
					   u32 linecard_index)
{
	struct devlink_linecard *linecard;

	linecard = devlink_linecard_get_by_index(devlink, linecard_index);
	if (!linecard)
		return;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
}

static void devlink_linecard_rel_cleanup_cb(struct devlink *devlink,
					    u32 linecard_index, u32 rel_index)
{
	struct devlink_linecard *linecard;

	linecard = devlink_linecard_get_by_index(devlink, linecard_index);
	if (linecard && linecard->rel_index == rel_index)
		linecard->rel_index = 0;
}

/**
 *	devlink_linecard_nested_dl_set - Attach/detach nested devlink
 *					 instance to linecard.
 *
 *	@linecard: devlink linecard
 *	@nested_devlink: devlink instance to attach or NULL to detach
 */
int devlink_linecard_nested_dl_set(struct devlink_linecard *linecard,
				   struct devlink *nested_devlink)
{
	return devlink_rel_nested_in_add(&linecard->rel_index,
					 linecard->devlink->index,
					 linecard->index,
					 devlink_linecard_rel_notify_cb,
					 devlink_linecard_rel_cleanup_cb,
					 nested_devlink);
}
EXPORT_SYMBOL_GPL(devlink_linecard_nested_dl_set);

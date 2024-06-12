// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include "devl_internal.h"

static const struct devlink_param devlink_param_generic[] = {
	{
		.id = DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
		.name = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
		.name = DEVLINK_PARAM_GENERIC_MAX_MACS_NAME,
		.type = DEVLINK_PARAM_GENERIC_MAX_MACS_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
		.name = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_NAME,
		.type = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI,
		.name = DEVLINK_PARAM_GENERIC_IGNORE_ARI_NAME,
		.type = DEVLINK_PARAM_GENERIC_IGNORE_ARI_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
		.name = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_NAME,
		.type = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_RESET_DEV_ON_DRV_PROBE,
		.name = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_NAME,
		.type = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_REMOTE_DEV_RESET,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_ETH,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_ETH_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_ETH_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_RDMA,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_RDMA_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_RDMA_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_VNET,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_VNET_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_VNET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_IWARP,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_IWARP_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_IWARP_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE,
		.name = DEVLINK_PARAM_GENERIC_IO_EQ_SIZE_NAME,
		.type = DEVLINK_PARAM_GENERIC_IO_EQ_SIZE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_EVENT_EQ_SIZE,
		.name = DEVLINK_PARAM_GENERIC_EVENT_EQ_SIZE_NAME,
		.type = DEVLINK_PARAM_GENERIC_EVENT_EQ_SIZE_TYPE,
	},
};

static int devlink_param_generic_verify(const struct devlink_param *param)
{
	/* verify it match generic parameter by id and name */
	if (param->id > DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	if (strcmp(param->name, devlink_param_generic[param->id].name))
		return -ENOENT;

	WARN_ON(param->type != devlink_param_generic[param->id].type);

	return 0;
}

static int devlink_param_driver_verify(const struct devlink_param *param)
{
	int i;

	if (param->id <= DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	/* verify no such name in generic params */
	for (i = 0; i <= DEVLINK_PARAM_GENERIC_ID_MAX; i++)
		if (!strcmp(param->name, devlink_param_generic[i].name))
			return -EEXIST;

	return 0;
}

static struct devlink_param_item *
devlink_param_find_by_name(struct xarray *params, const char *param_name)
{
	struct devlink_param_item *param_item;
	unsigned long param_id;

	xa_for_each(params, param_id, param_item) {
		if (!strcmp(param_item->param->name, param_name))
			return param_item;
	}
	return NULL;
}

static struct devlink_param_item *
devlink_param_find_by_id(struct xarray *params, u32 param_id)
{
	return xa_load(params, param_id);
}

static bool
devlink_param_cmode_is_supported(const struct devlink_param *param,
				 enum devlink_param_cmode cmode)
{
	return test_bit(cmode, &param->supported_cmodes);
}

static int devlink_param_get(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx)
{
	if (!param->get)
		return -EOPNOTSUPP;
	return param->get(devlink, param->id, ctx);
}

static int devlink_param_set(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx,
			     struct netlink_ext_ack *extack)
{
	if (!param->set)
		return -EOPNOTSUPP;
	return param->set(devlink, param->id, ctx, extack);
}

static int
devlink_param_type_to_nla_type(enum devlink_param_type param_type)
{
	switch (param_type) {
	case DEVLINK_PARAM_TYPE_U8:
		return NLA_U8;
	case DEVLINK_PARAM_TYPE_U16:
		return NLA_U16;
	case DEVLINK_PARAM_TYPE_U32:
		return NLA_U32;
	case DEVLINK_PARAM_TYPE_STRING:
		return NLA_STRING;
	case DEVLINK_PARAM_TYPE_BOOL:
		return NLA_FLAG;
	default:
		return -EINVAL;
	}
}

static int
devlink_nl_param_value_fill_one(struct sk_buff *msg,
				enum devlink_param_type type,
				enum devlink_param_cmode cmode,
				union devlink_param_value val)
{
	struct nlattr *param_value_attr;

	param_value_attr = nla_nest_start_noflag(msg,
						 DEVLINK_ATTR_PARAM_VALUE);
	if (!param_value_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_CMODE, cmode))
		goto value_nest_cancel;

	switch (type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu8))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_put_u16(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu16))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_put_u32(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu32))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		if (nla_put_string(msg, DEVLINK_ATTR_PARAM_VALUE_DATA,
				   val.vstr))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (val.vbool &&
		    nla_put_flag(msg, DEVLINK_ATTR_PARAM_VALUE_DATA))
			goto value_nest_cancel;
		break;
	}

	nla_nest_end(msg, param_value_attr);
	return 0;

value_nest_cancel:
	nla_nest_cancel(msg, param_value_attr);
nla_put_failure:
	return -EMSGSIZE;
}

static int devlink_nl_param_fill(struct sk_buff *msg, struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd,
				 u32 portid, u32 seq, int flags)
{
	union devlink_param_value param_value[DEVLINK_PARAM_CMODE_MAX + 1];
	bool param_value_set[DEVLINK_PARAM_CMODE_MAX + 1] = {};
	const struct devlink_param *param = param_item->param;
	struct devlink_param_gset_ctx ctx;
	struct nlattr *param_values_list;
	struct nlattr *param_attr;
	int nla_type;
	void *hdr;
	int err;
	int i;

	/* Get value from driver part to driverinit configuration mode */
	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!devlink_param_cmode_is_supported(param, i))
			continue;
		if (i == DEVLINK_PARAM_CMODE_DRIVERINIT) {
			if (param_item->driverinit_value_new_valid)
				param_value[i] = param_item->driverinit_value_new;
			else if (param_item->driverinit_value_valid)
				param_value[i] = param_item->driverinit_value;
			else
				return -EOPNOTSUPP;
		} else {
			ctx.cmode = i;
			err = devlink_param_get(devlink, param, &ctx);
			if (err)
				return err;
			param_value[i] = ctx.val;
		}
		param_value_set[i] = true;
	}

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	if (cmd == DEVLINK_CMD_PORT_PARAM_GET ||
	    cmd == DEVLINK_CMD_PORT_PARAM_NEW ||
	    cmd == DEVLINK_CMD_PORT_PARAM_DEL)
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, port_index))
			goto genlmsg_cancel;

	param_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_PARAM);
	if (!param_attr)
		goto genlmsg_cancel;
	if (nla_put_string(msg, DEVLINK_ATTR_PARAM_NAME, param->name))
		goto param_nest_cancel;
	if (param->generic && nla_put_flag(msg, DEVLINK_ATTR_PARAM_GENERIC))
		goto param_nest_cancel;

	nla_type = devlink_param_type_to_nla_type(param->type);
	if (nla_type < 0)
		goto param_nest_cancel;
	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_TYPE, nla_type))
		goto param_nest_cancel;

	param_values_list = nla_nest_start_noflag(msg,
						  DEVLINK_ATTR_PARAM_VALUES_LIST);
	if (!param_values_list)
		goto param_nest_cancel;

	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!param_value_set[i])
			continue;
		err = devlink_nl_param_value_fill_one(msg, param->type,
						      i, param_value[i]);
		if (err)
			goto values_list_nest_cancel;
	}

	nla_nest_end(msg, param_values_list);
	nla_nest_end(msg, param_attr);
	genlmsg_end(msg, hdr);
	return 0;

values_list_nest_cancel:
	nla_nest_end(msg, param_values_list);
param_nest_cancel:
	nla_nest_cancel(msg, param_attr);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_param_notify(struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_PARAM_NEW && cmd != DEVLINK_CMD_PARAM_DEL &&
		cmd != DEVLINK_CMD_PORT_PARAM_NEW &&
		cmd != DEVLINK_CMD_PORT_PARAM_DEL);

	/* devlink_notify_register() / devlink_notify_unregister()
	 * will replay the notifications if the params are added/removed
	 * outside of the lifetime of the instance.
	 */
	if (!devl_is_registered(devlink) || !devlink_nl_notify_need(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;
	err = devlink_nl_param_fill(msg, devlink, port_index, param_item, cmd,
				    0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	devlink_nl_notify_send(devlink, msg);
}

static void devlink_params_notify(struct devlink *devlink,
				  enum devlink_command cmd)
{
	struct devlink_param_item *param_item;
	unsigned long param_id;

	xa_for_each(&devlink->params, param_id, param_item)
		devlink_param_notify(devlink, 0, param_item, cmd);
}

void devlink_params_notify_register(struct devlink *devlink)
{
	devlink_params_notify(devlink, DEVLINK_CMD_PARAM_NEW);
}

void devlink_params_notify_unregister(struct devlink *devlink)
{
	devlink_params_notify(devlink, DEVLINK_CMD_PARAM_DEL);
}

static int devlink_nl_param_get_dump_one(struct sk_buff *msg,
					 struct devlink *devlink,
					 struct netlink_callback *cb,
					 int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_param_item *param_item;
	unsigned long param_id;
	int err = 0;

	xa_for_each_start(&devlink->params, param_id, param_item, state->idx) {
		err = devlink_nl_param_fill(msg, devlink, 0, param_item,
					    DEVLINK_CMD_PARAM_GET,
					    NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq, flags);
		if (err == -EOPNOTSUPP) {
			err = 0;
		} else if (err) {
			state->idx = param_id;
			break;
		}
	}

	return err;
}

int devlink_nl_param_get_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_param_get_dump_one);
}

static int
devlink_param_type_get_from_info(struct genl_info *info,
				 enum devlink_param_type *param_type)
{
	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_TYPE))
		return -EINVAL;

	switch (nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_TYPE])) {
	case NLA_U8:
		*param_type = DEVLINK_PARAM_TYPE_U8;
		break;
	case NLA_U16:
		*param_type = DEVLINK_PARAM_TYPE_U16;
		break;
	case NLA_U32:
		*param_type = DEVLINK_PARAM_TYPE_U32;
		break;
	case NLA_STRING:
		*param_type = DEVLINK_PARAM_TYPE_STRING;
		break;
	case NLA_FLAG:
		*param_type = DEVLINK_PARAM_TYPE_BOOL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
devlink_param_value_get_from_info(const struct devlink_param *param,
				  struct genl_info *info,
				  union devlink_param_value *value)
{
	struct nlattr *param_data;
	int len;

	param_data = info->attrs[DEVLINK_ATTR_PARAM_VALUE_DATA];

	if (param->type != DEVLINK_PARAM_TYPE_BOOL && !param_data)
		return -EINVAL;

	switch (param->type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_len(param_data) != sizeof(u8))
			return -EINVAL;
		value->vu8 = nla_get_u8(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_len(param_data) != sizeof(u16))
			return -EINVAL;
		value->vu16 = nla_get_u16(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_len(param_data) != sizeof(u32))
			return -EINVAL;
		value->vu32 = nla_get_u32(param_data);
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		len = strnlen(nla_data(param_data), nla_len(param_data));
		if (len == nla_len(param_data) ||
		    len >= __DEVLINK_PARAM_MAX_STRING_VALUE)
			return -EINVAL;
		strcpy(value->vstr, nla_data(param_data));
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (param_data && nla_len(param_data))
			return -EINVAL;
		value->vbool = nla_get_flag(param_data);
		break;
	}
	return 0;
}

static struct devlink_param_item *
devlink_param_get_from_info(struct xarray *params, struct genl_info *info)
{
	char *param_name;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_NAME))
		return NULL;

	param_name = nla_data(info->attrs[DEVLINK_ATTR_PARAM_NAME]);
	return devlink_param_find_by_name(params, param_name);
}

int devlink_nl_param_get_doit(struct sk_buff *skb,
			      struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_param_item *param_item;
	struct sk_buff *msg;
	int err;

	param_item = devlink_param_get_from_info(&devlink->params, info);
	if (!param_item)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_param_fill(msg, devlink, 0, param_item,
				    DEVLINK_CMD_PARAM_GET,
				    info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __devlink_nl_cmd_param_set_doit(struct devlink *devlink,
					   unsigned int port_index,
					   struct xarray *params,
					   struct genl_info *info,
					   enum devlink_command cmd)
{
	enum devlink_param_type param_type;
	struct devlink_param_gset_ctx ctx;
	enum devlink_param_cmode cmode;
	struct devlink_param_item *param_item;
	const struct devlink_param *param;
	union devlink_param_value value;
	int err = 0;

	param_item = devlink_param_get_from_info(params, info);
	if (!param_item)
		return -EINVAL;
	param = param_item->param;
	err = devlink_param_type_get_from_info(info, &param_type);
	if (err)
		return err;
	if (param_type != param->type)
		return -EINVAL;
	err = devlink_param_value_get_from_info(param, info, &value);
	if (err)
		return err;
	if (param->validate) {
		err = param->validate(devlink, param->id, value, info->extack);
		if (err)
			return err;
	}

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_VALUE_CMODE))
		return -EINVAL;
	cmode = nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_VALUE_CMODE]);
	if (!devlink_param_cmode_is_supported(param, cmode))
		return -EOPNOTSUPP;

	if (cmode == DEVLINK_PARAM_CMODE_DRIVERINIT) {
		param_item->driverinit_value_new = value;
		param_item->driverinit_value_new_valid = true;
	} else {
		if (!param->set)
			return -EOPNOTSUPP;
		ctx.val = value;
		ctx.cmode = cmode;
		err = devlink_param_set(devlink, param, &ctx, info->extack);
		if (err)
			return err;
	}

	devlink_param_notify(devlink, port_index, param_item, cmd);
	return 0;
}

int devlink_nl_param_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	return __devlink_nl_cmd_param_set_doit(devlink, 0, &devlink->params,
					       info, DEVLINK_CMD_PARAM_NEW);
}

int devlink_nl_port_param_get_dumpit(struct sk_buff *msg,
				     struct netlink_callback *cb)
{
	NL_SET_ERR_MSG(cb->extack, "Port params are not supported");
	return msg->len;
}

int devlink_nl_port_param_get_doit(struct sk_buff *skb,
				   struct genl_info *info)
{
	NL_SET_ERR_MSG(info->extack, "Port params are not supported");
	return -EINVAL;
}

int devlink_nl_port_param_set_doit(struct sk_buff *skb,
				   struct genl_info *info)
{
	NL_SET_ERR_MSG(info->extack, "Port params are not supported");
	return -EINVAL;
}

static int devlink_param_verify(const struct devlink_param *param)
{
	if (!param || !param->name || !param->supported_cmodes)
		return -EINVAL;
	if (param->generic)
		return devlink_param_generic_verify(param);
	else
		return devlink_param_driver_verify(param);
}

static int devlink_param_register(struct devlink *devlink,
				  const struct devlink_param *param)
{
	struct devlink_param_item *param_item;
	int err;

	WARN_ON(devlink_param_verify(param));
	WARN_ON(devlink_param_find_by_name(&devlink->params, param->name));

	if (param->supported_cmodes == BIT(DEVLINK_PARAM_CMODE_DRIVERINIT))
		WARN_ON(param->get || param->set);
	else
		WARN_ON(!param->get || !param->set);

	param_item = kzalloc(sizeof(*param_item), GFP_KERNEL);
	if (!param_item)
		return -ENOMEM;

	param_item->param = param;

	err = xa_insert(&devlink->params, param->id, param_item, GFP_KERNEL);
	if (err)
		goto err_xa_insert;

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
	return 0;

err_xa_insert:
	kfree(param_item);
	return err;
}

static void devlink_param_unregister(struct devlink *devlink,
				     const struct devlink_param *param)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink->params, param->id);
	if (WARN_ON(!param_item))
		return;
	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_DEL);
	xa_erase(&devlink->params, param->id);
	kfree(param_item);
}

/**
 *	devl_params_register - register configuration parameters
 *
 *	@devlink: devlink
 *	@params: configuration parameters array
 *	@params_count: number of parameters provided
 *
 *	Register the configuration parameters supported by the driver.
 */
int devl_params_register(struct devlink *devlink,
			 const struct devlink_param *params,
			 size_t params_count)
{
	const struct devlink_param *param = params;
	int i, err;

	lockdep_assert_held(&devlink->lock);

	for (i = 0; i < params_count; i++, param++) {
		err = devlink_param_register(devlink, param);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	if (!i)
		return err;

	for (param--; i > 0; i--, param--)
		devlink_param_unregister(devlink, param);
	return err;
}
EXPORT_SYMBOL_GPL(devl_params_register);

int devlink_params_register(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count)
{
	int err;

	devl_lock(devlink);
	err = devl_params_register(devlink, params, params_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_params_register);

/**
 *	devl_params_unregister - unregister configuration parameters
 *	@devlink: devlink
 *	@params: configuration parameters to unregister
 *	@params_count: number of parameters provided
 */
void devl_params_unregister(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count)
{
	const struct devlink_param *param = params;
	int i;

	lockdep_assert_held(&devlink->lock);

	for (i = 0; i < params_count; i++, param++)
		devlink_param_unregister(devlink, param);
}
EXPORT_SYMBOL_GPL(devl_params_unregister);

void devlink_params_unregister(struct devlink *devlink,
			       const struct devlink_param *params,
			       size_t params_count)
{
	devl_lock(devlink);
	devl_params_unregister(devlink, params, params_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_params_unregister);

/**
 *	devl_param_driverinit_value_get - get configuration parameter
 *					  value for driver initializing
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@val: pointer to store the value of parameter in driverinit
 *	      configuration mode
 *
 *	This function should be used by the driver to get driverinit
 *	configuration for initialization after reload command.
 *
 *	Note that lockless call of this function relies on the
 *	driver to maintain following basic sane behavior:
 *	1) Driver ensures a call to this function cannot race with
 *	   registering/unregistering the parameter with the same parameter ID.
 *	2) Driver ensures a call to this function cannot race with
 *	   devl_param_driverinit_value_set() call with the same parameter ID.
 *	3) Driver ensures a call to this function cannot race with
 *	   reload operation.
 *	If the driver is not able to comply, it has to take the devlink->lock
 *	while calling this.
 */
int devl_param_driverinit_value_get(struct devlink *devlink, u32 param_id,
				    union devlink_param_value *val)
{
	struct devlink_param_item *param_item;

	if (WARN_ON(!devlink_reload_supported(devlink->ops)))
		return -EOPNOTSUPP;

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	if (!param_item)
		return -EINVAL;

	if (!param_item->driverinit_value_valid)
		return -EOPNOTSUPP;

	if (WARN_ON(!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT)))
		return -EOPNOTSUPP;

	*val = param_item->driverinit_value;

	return 0;
}
EXPORT_SYMBOL_GPL(devl_param_driverinit_value_get);

/**
 *	devl_param_driverinit_value_set - set value of configuration
 *					  parameter for driverinit
 *					  configuration mode
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@init_val: value of parameter to set for driverinit configuration mode
 *
 *	This function should be used by the driver to set driverinit
 *	configuration mode default value.
 */
void devl_param_driverinit_value_set(struct devlink *devlink, u32 param_id,
				     union devlink_param_value init_val)
{
	struct devlink_param_item *param_item;

	devl_assert_locked(devlink);

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	if (WARN_ON(!param_item))
		return;

	if (WARN_ON(!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT)))
		return;

	param_item->driverinit_value = init_val;
	param_item->driverinit_value_valid = true;

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devl_param_driverinit_value_set);

void devlink_params_driverinit_load_new(struct devlink *devlink)
{
	struct devlink_param_item *param_item;
	unsigned long param_id;

	xa_for_each(&devlink->params, param_id, param_item) {
		if (!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT) ||
		    !param_item->driverinit_value_new_valid)
			continue;
		param_item->driverinit_value = param_item->driverinit_value_new;
		param_item->driverinit_value_valid = true;
		param_item->driverinit_value_new_valid = false;
	}
}

/**
 *	devl_param_value_changed - notify devlink on a parameter's value
 *				   change. Should be called by the driver
 *				   right after the change.
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *
 *	This function should be used by the driver to notify devlink on value
 *	change, excluding driverinit configuration mode.
 *	For driverinit configuration mode driver should use the function
 */
void devl_param_value_changed(struct devlink *devlink, u32 param_id)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	WARN_ON(!param_item);

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devl_param_value_changed);

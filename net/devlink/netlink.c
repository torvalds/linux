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

static const struct nla_policy devlink_nl_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_UNSPEC] = { .strict_start_type =
		DEVLINK_ATTR_TRAP_POLICER_ID },
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32 },
	[DEVLINK_ATTR_PORT_TYPE] = NLA_POLICY_RANGE(NLA_U16, DEVLINK_PORT_TYPE_AUTO,
						    DEVLINK_PORT_TYPE_IB),
	[DEVLINK_ATTR_PORT_SPLIT_COUNT] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16 },
	[DEVLINK_ATTR_SB_POOL_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_SB_POOL_SIZE] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_SB_THRESHOLD] = { .type = NLA_U32 },
	[DEVLINK_ATTR_SB_TC_INDEX] = { .type = NLA_U16 },
	[DEVLINK_ATTR_ESWITCH_MODE] = NLA_POLICY_RANGE(NLA_U16, DEVLINK_ESWITCH_MODE_LEGACY,
						       DEVLINK_ESWITCH_MODE_SWITCHDEV),
	[DEVLINK_ATTR_ESWITCH_INLINE_MODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_ESWITCH_ENCAP_MODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED] = { .type = NLA_U8 },
	[DEVLINK_ATTR_RESOURCE_ID] = { .type = NLA_U64},
	[DEVLINK_ATTR_RESOURCE_SIZE] = { .type = NLA_U64},
	[DEVLINK_ATTR_PARAM_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_PARAM_TYPE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_PARAM_VALUE_CMODE] = { .type = NLA_U8 },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_REGION_CHUNK_ADDR] = { .type = NLA_U64 },
	[DEVLINK_ATTR_REGION_CHUNK_LEN] = { .type = NLA_U64 },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] = { .type = NLA_U64 },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER] = { .type = NLA_U8 },
	[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK] =
		NLA_POLICY_BITFIELD32(DEVLINK_SUPPORTED_FLASH_OVERWRITE_SECTIONS),
	[DEVLINK_ATTR_TRAP_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_TRAP_ACTION] = { .type = NLA_U8 },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_NETNS_PID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_NETNS_FD] = { .type = NLA_U32 },
	[DEVLINK_ATTR_NETNS_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP] = { .type = NLA_U8 },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .type = NLA_U32 },
	[DEVLINK_ATTR_TRAP_POLICER_RATE] = { .type = NLA_U64 },
	[DEVLINK_ATTR_TRAP_POLICER_BURST] = { .type = NLA_U64 },
	[DEVLINK_ATTR_PORT_FUNCTION] = { .type = NLA_NESTED },
	[DEVLINK_ATTR_RELOAD_ACTION] = NLA_POLICY_RANGE(NLA_U8, DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
							DEVLINK_RELOAD_ACTION_MAX),
	[DEVLINK_ATTR_RELOAD_LIMITS] = NLA_POLICY_BITFIELD32(DEVLINK_RELOAD_LIMITS_VALID_MASK),
	[DEVLINK_ATTR_PORT_FLAVOUR] = { .type = NLA_U16 },
	[DEVLINK_ATTR_PORT_PCI_PF_NUMBER] = { .type = NLA_U16 },
	[DEVLINK_ATTR_PORT_PCI_SF_NUMBER] = { .type = NLA_U32 },
	[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER] = { .type = NLA_U32 },
	[DEVLINK_ATTR_RATE_TYPE] = { .type = NLA_U16 },
	[DEVLINK_ATTR_RATE_TX_SHARE] = { .type = NLA_U64 },
	[DEVLINK_ATTR_RATE_TX_MAX] = { .type = NLA_U64 },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_LINECARD_INDEX] = { .type = NLA_U32 },
	[DEVLINK_ATTR_LINECARD_TYPE] = { .type = NLA_NUL_STRING },
	[DEVLINK_ATTR_SELFTESTS] = { .type = NLA_NESTED },
	[DEVLINK_ATTR_RATE_TX_PRIORITY] = { .type = NLA_U32 },
	[DEVLINK_ATTR_RATE_TX_WEIGHT] = { .type = NLA_U32 },
	[DEVLINK_ATTR_REGION_DIRECT] = { .type = NLA_FLAG },
};

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

static int devlink_nl_pre_doit(const struct genl_split_ops *ops,
			       struct sk_buff *skb, struct genl_info *info)
{
	struct devlink_linecard *linecard;
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	int err;

	devlink = devlink_get_from_attrs_lock(genl_info_net(info), info->attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);

	info->user_ptr[0] = devlink;
	if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (IS_ERR(devlink_port)) {
			err = PTR_ERR(devlink_port);
			goto unlock;
		}
		info->user_ptr[1] = devlink_port;
	} else if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT) {
		devlink_port = devlink_port_get_from_info(devlink, info);
		if (!IS_ERR(devlink_port))
			info->user_ptr[1] = devlink_port;
	} else if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_RATE) {
		struct devlink_rate *devlink_rate;

		devlink_rate = devlink_rate_get_from_info(devlink, info);
		if (IS_ERR(devlink_rate)) {
			err = PTR_ERR(devlink_rate);
			goto unlock;
		}
		info->user_ptr[1] = devlink_rate;
	} else if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_RATE_NODE) {
		struct devlink_rate *rate_node;

		rate_node = devlink_rate_node_get_from_info(devlink, info);
		if (IS_ERR(rate_node)) {
			err = PTR_ERR(rate_node);
			goto unlock;
		}
		info->user_ptr[1] = rate_node;
	} else if (ops->internal_flags & DEVLINK_NL_FLAG_NEED_LINECARD) {
		linecard = devlink_linecard_get_from_info(devlink, info);
		if (IS_ERR(linecard)) {
			err = PTR_ERR(linecard);
			goto unlock;
		}
		info->user_ptr[1] = linecard;
	}
	return 0;

unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

static void devlink_nl_post_doit(const struct genl_split_ops *ops,
				 struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink;

	devlink = info->user_ptr[0];
	devl_unlock(devlink);
	devlink_put(devlink);
}

static const struct devlink_cmd *devl_cmds[] = {
	[DEVLINK_CMD_GET]		= &devl_cmd_get,
	[DEVLINK_CMD_PORT_GET]		= &devl_cmd_port_get,
	[DEVLINK_CMD_SB_GET]		= &devl_cmd_sb_get,
	[DEVLINK_CMD_SB_POOL_GET]	= &devl_cmd_sb_pool_get,
	[DEVLINK_CMD_SB_PORT_POOL_GET]	= &devl_cmd_sb_port_pool_get,
	[DEVLINK_CMD_SB_TC_POOL_BIND_GET] = &devl_cmd_sb_tc_pool_bind_get,
	[DEVLINK_CMD_PARAM_GET]		= &devl_cmd_param_get,
	[DEVLINK_CMD_REGION_GET]	= &devl_cmd_region_get,
	[DEVLINK_CMD_INFO_GET]		= &devl_cmd_info_get,
	[DEVLINK_CMD_HEALTH_REPORTER_GET] = &devl_cmd_health_reporter_get,
	[DEVLINK_CMD_TRAP_GET]		= &devl_cmd_trap_get,
	[DEVLINK_CMD_TRAP_GROUP_GET]	= &devl_cmd_trap_group_get,
	[DEVLINK_CMD_TRAP_POLICER_GET]	= &devl_cmd_trap_policer_get,
	[DEVLINK_CMD_RATE_GET]		= &devl_cmd_rate_get,
	[DEVLINK_CMD_LINECARD_GET]	= &devl_cmd_linecard_get,
	[DEVLINK_CMD_SELFTESTS_GET]	= &devl_cmd_selftests_get,
};

int devlink_nl_instance_iter_dumpit(struct sk_buff *msg,
				    struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	const struct devlink_cmd *cmd;
	struct devlink *devlink;
	int err = 0;

	cmd = devl_cmds[info->op.cmd];

	while ((devlink = devlinks_xa_find_get(sock_net(msg->sk),
					       &state->instance))) {
		devl_lock(devlink);

		if (devl_is_registered(devlink))
			err = cmd->dump_one(msg, devlink, cb);
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

struct genl_family devlink_nl_family __ro_after_init = {
	.name		= DEVLINK_GENL_NAME,
	.version	= DEVLINK_GENL_VERSION,
	.maxattr	= DEVLINK_ATTR_MAX,
	.policy		= devlink_nl_policy,
	.netnsok	= true,
	.parallel_ops	= true,
	.pre_doit	= devlink_nl_pre_doit,
	.post_doit	= devlink_nl_post_doit,
	.module		= THIS_MODULE,
	.small_ops	= devlink_nl_ops,
	.n_small_ops	= ARRAY_SIZE(devlink_nl_ops),
	.resv_start_op	= DEVLINK_CMD_SELFTESTS_RUN + 1,
	.mcgrps		= devlink_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(devlink_nl_mcgrps),
};

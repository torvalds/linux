// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink_gen.h"

#include <uapi/linux/devlink.h>

/* DEVLINK_CMD_GET - do */
static const struct nla_policy devlink_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_INFO_GET - do */
static const struct nla_policy devlink_info_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* Ops table for devlink */
const struct genl_split_ops devlink_nl_ops[4] = {
	{
		.cmd		= DEVLINK_CMD_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_GET,
		.validate	= GENL_DONT_VALIDATE_DUMP,
		.dumpit		= devlink_nl_get_dumpit,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_INFO_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_info_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_info_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_INFO_GET,
		.validate	= GENL_DONT_VALIDATE_DUMP,
		.dumpit		= devlink_nl_info_get_dumpit,
		.flags		= GENL_CMD_CAP_DUMP,
	},
};

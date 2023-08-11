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

/* DEVLINK_CMD_PORT_GET - do */
static const struct nla_policy devlink_port_get_do_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_PORT_GET - dump */
static const struct nla_policy devlink_port_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SB_GET - do */
static const struct nla_policy devlink_sb_get_do_nl_policy[DEVLINK_ATTR_SB_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_SB_GET - dump */
static const struct nla_policy devlink_sb_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SB_POOL_GET - do */
static const struct nla_policy devlink_sb_pool_get_do_nl_policy[DEVLINK_ATTR_SB_POOL_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16, },
};

/* DEVLINK_CMD_SB_POOL_GET - dump */
static const struct nla_policy devlink_sb_pool_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SB_PORT_POOL_GET - do */
static const struct nla_policy devlink_sb_port_pool_get_do_nl_policy[DEVLINK_ATTR_SB_POOL_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16, },
};

/* DEVLINK_CMD_SB_PORT_POOL_GET - dump */
static const struct nla_policy devlink_sb_port_pool_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - do */
static const struct nla_policy devlink_sb_tc_pool_bind_get_do_nl_policy[DEVLINK_ATTR_SB_TC_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_TYPE] = NLA_POLICY_MAX(NLA_U8, 1),
	[DEVLINK_ATTR_SB_TC_INDEX] = { .type = NLA_U16, },
};

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - dump */
static const struct nla_policy devlink_sb_tc_pool_bind_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_PARAM_GET - do */
static const struct nla_policy devlink_param_get_do_nl_policy[DEVLINK_ATTR_PARAM_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PARAM_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_PARAM_GET - dump */
static const struct nla_policy devlink_param_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_REGION_GET - do */
static const struct nla_policy devlink_region_get_do_nl_policy[DEVLINK_ATTR_REGION_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_REGION_GET - dump */
static const struct nla_policy devlink_region_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_INFO_GET - do */
static const struct nla_policy devlink_info_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_GET - do */
static const struct nla_policy devlink_health_reporter_get_do_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_GET - dump */
static const struct nla_policy devlink_health_reporter_get_dump_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_TRAP_GET - do */
static const struct nla_policy devlink_trap_get_do_nl_policy[DEVLINK_ATTR_TRAP_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_TRAP_GET - dump */
static const struct nla_policy devlink_trap_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_TRAP_GROUP_GET - do */
static const struct nla_policy devlink_trap_group_get_do_nl_policy[DEVLINK_ATTR_TRAP_GROUP_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_TRAP_GROUP_GET - dump */
static const struct nla_policy devlink_trap_group_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_TRAP_POLICER_GET - do */
static const struct nla_policy devlink_trap_policer_get_do_nl_policy[DEVLINK_ATTR_TRAP_POLICER_ID + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_TRAP_POLICER_GET - dump */
static const struct nla_policy devlink_trap_policer_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_RATE_GET - do */
static const struct nla_policy devlink_rate_get_do_nl_policy[DEVLINK_ATTR_RATE_NODE_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_RATE_GET - dump */
static const struct nla_policy devlink_rate_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_LINECARD_GET - do */
static const struct nla_policy devlink_linecard_get_do_nl_policy[DEVLINK_ATTR_LINECARD_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_LINECARD_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_LINECARD_GET - dump */
static const struct nla_policy devlink_linecard_get_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SELFTESTS_GET - do */
static const struct nla_policy devlink_selftests_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* Ops table for devlink */
const struct genl_split_ops devlink_nl_ops[32] = {
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
		.cmd		= DEVLINK_CMD_PORT_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_GET,
		.dumpit		= devlink_nl_port_get_dumpit,
		.policy		= devlink_port_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_SB_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_sb_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_GET,
		.dumpit		= devlink_nl_sb_get_dumpit,
		.policy		= devlink_sb_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_SB_POOL_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_sb_pool_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_pool_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_POOL_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_POOL_GET,
		.dumpit		= devlink_nl_sb_pool_get_dumpit,
		.policy		= devlink_sb_pool_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_SB_PORT_POOL_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_sb_port_pool_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_port_pool_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_POOL_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_PORT_POOL_GET,
		.dumpit		= devlink_nl_sb_port_pool_get_dumpit,
		.policy		= devlink_sb_port_pool_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_SB_TC_POOL_BIND_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_sb_tc_pool_bind_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_tc_pool_bind_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_TC_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_TC_POOL_BIND_GET,
		.dumpit		= devlink_nl_sb_tc_pool_bind_get_dumpit,
		.policy		= devlink_sb_tc_pool_bind_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_PARAM_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_param_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_param_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_PARAM_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PARAM_GET,
		.dumpit		= devlink_nl_param_get_dumpit,
		.policy		= devlink_param_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_REGION_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_region_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_region_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_REGION_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_REGION_GET,
		.dumpit		= devlink_nl_region_get_dumpit,
		.policy		= devlink_region_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
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
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_GET,
		.dumpit		= devlink_nl_health_reporter_get_dumpit,
		.policy		= devlink_health_reporter_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_GET,
		.dumpit		= devlink_nl_trap_get_dumpit,
		.policy		= devlink_trap_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_GROUP_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_group_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_group_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_GROUP_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_GROUP_GET,
		.dumpit		= devlink_nl_trap_group_get_dumpit,
		.policy		= devlink_trap_group_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_POLICER_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_policer_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_policer_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_POLICER_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_TRAP_POLICER_GET,
		.dumpit		= devlink_nl_trap_policer_get_dumpit,
		.policy		= devlink_trap_policer_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_RATE_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_rate_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_rate_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_RATE_NODE_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RATE_GET,
		.dumpit		= devlink_nl_rate_get_dumpit,
		.policy		= devlink_rate_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_LINECARD_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_linecard_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_linecard_get_do_nl_policy,
		.maxattr	= DEVLINK_ATTR_LINECARD_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_LINECARD_GET,
		.dumpit		= devlink_nl_linecard_get_dumpit,
		.policy		= devlink_linecard_get_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_SELFTESTS_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_selftests_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_selftests_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SELFTESTS_GET,
		.validate	= GENL_DONT_VALIDATE_DUMP,
		.dumpit		= devlink_nl_selftests_get_dumpit,
		.flags		= GENL_CMD_CAP_DUMP,
	},
};

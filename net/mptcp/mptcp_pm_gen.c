// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/mptcp_pm.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "mptcp_pm_gen.h"

#include <uapi/linux/mptcp_pm.h>

/* Common nested types */
const struct nla_policy mptcp_pm_address_nl_policy[MPTCP_PM_ADDR_ATTR_IF_IDX + 1] = {
	[MPTCP_PM_ADDR_ATTR_FAMILY] = { .type = NLA_U16, },
	[MPTCP_PM_ADDR_ATTR_ID] = { .type = NLA_U8, },
	[MPTCP_PM_ADDR_ATTR_ADDR4] = { .type = NLA_U32, },
	[MPTCP_PM_ADDR_ATTR_ADDR6] = NLA_POLICY_EXACT_LEN(16),
	[MPTCP_PM_ADDR_ATTR_PORT] = { .type = NLA_U16, },
	[MPTCP_PM_ADDR_ATTR_FLAGS] = { .type = NLA_U32, },
	[MPTCP_PM_ADDR_ATTR_IF_IDX] = { .type = NLA_S32, },
};

/* MPTCP_PM_CMD_ADD_ADDR - do */
const struct nla_policy mptcp_pm_add_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1] = {
	[MPTCP_PM_ENDPOINT_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_DEL_ADDR - do */
const struct nla_policy mptcp_pm_del_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1] = {
	[MPTCP_PM_ENDPOINT_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_GET_ADDR - do */
const struct nla_policy mptcp_pm_get_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1] = {
	[MPTCP_PM_ENDPOINT_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_FLUSH_ADDRS - do */
const struct nla_policy mptcp_pm_flush_addrs_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1] = {
	[MPTCP_PM_ENDPOINT_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_SET_LIMITS - do */
const struct nla_policy mptcp_pm_set_limits_nl_policy[MPTCP_PM_ATTR_SUBFLOWS + 1] = {
	[MPTCP_PM_ATTR_RCV_ADD_ADDRS] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_SUBFLOWS] = { .type = NLA_U32, },
};

/* MPTCP_PM_CMD_GET_LIMITS - do */
const struct nla_policy mptcp_pm_get_limits_nl_policy[MPTCP_PM_ATTR_SUBFLOWS + 1] = {
	[MPTCP_PM_ATTR_RCV_ADD_ADDRS] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_SUBFLOWS] = { .type = NLA_U32, },
};

/* MPTCP_PM_CMD_SET_FLAGS - do */
const struct nla_policy mptcp_pm_set_flags_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1] = {
	[MPTCP_PM_ATTR_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
	[MPTCP_PM_ATTR_TOKEN] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_ADDR_REMOTE] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_ANNOUNCE - do */
const struct nla_policy mptcp_pm_announce_nl_policy[MPTCP_PM_ATTR_TOKEN + 1] = {
	[MPTCP_PM_ATTR_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
	[MPTCP_PM_ATTR_TOKEN] = { .type = NLA_U32, },
};

/* MPTCP_PM_CMD_REMOVE - do */
const struct nla_policy mptcp_pm_remove_nl_policy[MPTCP_PM_ATTR_LOC_ID + 1] = {
	[MPTCP_PM_ATTR_TOKEN] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_LOC_ID] = { .type = NLA_U8, },
};

/* MPTCP_PM_CMD_SUBFLOW_CREATE - do */
const struct nla_policy mptcp_pm_subflow_create_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1] = {
	[MPTCP_PM_ATTR_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
	[MPTCP_PM_ATTR_TOKEN] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_ADDR_REMOTE] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* MPTCP_PM_CMD_SUBFLOW_DESTROY - do */
const struct nla_policy mptcp_pm_subflow_destroy_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1] = {
	[MPTCP_PM_ATTR_ADDR] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
	[MPTCP_PM_ATTR_TOKEN] = { .type = NLA_U32, },
	[MPTCP_PM_ATTR_ADDR_REMOTE] = NLA_POLICY_NESTED(mptcp_pm_address_nl_policy),
};

/* Ops table for mptcp_pm */
const struct genl_ops mptcp_pm_nl_ops[11] = {
	{
		.cmd		= MPTCP_PM_CMD_ADD_ADDR,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_add_addr_doit,
		.policy		= mptcp_pm_add_addr_nl_policy,
		.maxattr	= MPTCP_PM_ENDPOINT_ADDR,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_DEL_ADDR,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_del_addr_doit,
		.policy		= mptcp_pm_del_addr_nl_policy,
		.maxattr	= MPTCP_PM_ENDPOINT_ADDR,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_GET_ADDR,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_get_addr_doit,
		.dumpit		= mptcp_pm_nl_get_addr_dumpit,
		.policy		= mptcp_pm_get_addr_nl_policy,
		.maxattr	= MPTCP_PM_ENDPOINT_ADDR,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_FLUSH_ADDRS,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_flush_addrs_doit,
		.policy		= mptcp_pm_flush_addrs_nl_policy,
		.maxattr	= MPTCP_PM_ENDPOINT_ADDR,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_SET_LIMITS,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_set_limits_doit,
		.policy		= mptcp_pm_set_limits_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_SUBFLOWS,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_GET_LIMITS,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_get_limits_doit,
		.policy		= mptcp_pm_get_limits_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_SUBFLOWS,
	},
	{
		.cmd		= MPTCP_PM_CMD_SET_FLAGS,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_set_flags_doit,
		.policy		= mptcp_pm_set_flags_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_ADDR_REMOTE,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_ANNOUNCE,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_announce_doit,
		.policy		= mptcp_pm_announce_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_TOKEN,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_REMOVE,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_remove_doit,
		.policy		= mptcp_pm_remove_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_LOC_ID,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_SUBFLOW_CREATE,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_subflow_create_doit,
		.policy		= mptcp_pm_subflow_create_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_ADDR_REMOTE,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd		= MPTCP_PM_CMD_SUBFLOW_DESTROY,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= mptcp_pm_nl_subflow_destroy_doit,
		.policy		= mptcp_pm_subflow_destroy_nl_policy,
		.maxattr	= MPTCP_PM_ATTR_ADDR_REMOTE,
		.flags		= GENL_UNS_ADMIN_PERM,
	},
};

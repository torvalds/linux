// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink_gen.h"

#include <uapi/linux/devlink.h>

/* Sparse enums validation callbacks */
static int
devlink_attr_param_type_validate(const struct nlattr *attr,
				 struct netlink_ext_ack *extack)
{
	switch (nla_get_u8(attr)) {
	case DEVLINK_VAR_ATTR_TYPE_U8:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_U16:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_U32:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_U64:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_STRING:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_FLAG:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_NUL_STRING:
		fallthrough;
	case DEVLINK_VAR_ATTR_TYPE_BINARY:
		return 0;
	}
	NL_SET_ERR_MSG_ATTR(extack, attr, "invalid enum value");
	return -EINVAL;
}

/* Common nested types */
const struct nla_policy devlink_dl_port_function_nl_policy[DEVLINK_PORT_FN_ATTR_CAPS + 1] = {
	[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR] = { .type = NLA_BINARY, },
	[DEVLINK_PORT_FN_ATTR_STATE] = NLA_POLICY_MAX(NLA_U8, 1),
	[DEVLINK_PORT_FN_ATTR_OPSTATE] = NLA_POLICY_MAX(NLA_U8, 1),
	[DEVLINK_PORT_FN_ATTR_CAPS] = NLA_POLICY_BITFIELD32(15),
};

const struct nla_policy devlink_dl_rate_tc_bws_nl_policy[DEVLINK_RATE_TC_ATTR_BW + 1] = {
	[DEVLINK_RATE_TC_ATTR_INDEX] = NLA_POLICY_MAX(NLA_U8, DEVLINK_RATE_TC_INDEX_MAX),
	[DEVLINK_RATE_TC_ATTR_BW] = { .type = NLA_U32, },
};

const struct nla_policy devlink_dl_selftest_id_nl_policy[DEVLINK_ATTR_SELFTEST_ID_FLASH + 1] = {
	[DEVLINK_ATTR_SELFTEST_ID_FLASH] = { .type = NLA_FLAG, },
};

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

/* DEVLINK_CMD_PORT_SET - do */
static const struct nla_policy devlink_port_set_nl_policy[DEVLINK_ATTR_PORT_FUNCTION + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_PORT_TYPE] = NLA_POLICY_MAX(NLA_U16, 3),
	[DEVLINK_ATTR_PORT_FUNCTION] = NLA_POLICY_NESTED(devlink_dl_port_function_nl_policy),
};

/* DEVLINK_CMD_PORT_NEW - do */
static const struct nla_policy devlink_port_new_nl_policy[DEVLINK_ATTR_PORT_PCI_SF_NUMBER + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_PORT_FLAVOUR] = NLA_POLICY_MAX(NLA_U16, 7),
	[DEVLINK_ATTR_PORT_PCI_PF_NUMBER] = { .type = NLA_U16, },
	[DEVLINK_ATTR_PORT_PCI_SF_NUMBER] = { .type = NLA_U32, },
	[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_PORT_DEL - do */
static const struct nla_policy devlink_port_del_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_PORT_SPLIT - do */
static const struct nla_policy devlink_port_split_nl_policy[DEVLINK_ATTR_PORT_SPLIT_COUNT + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_PORT_SPLIT_COUNT] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_PORT_UNSPLIT - do */
static const struct nla_policy devlink_port_unsplit_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_SB_POOL_SET - do */
static const struct nla_policy devlink_sb_pool_set_nl_policy[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16, },
	[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE] = NLA_POLICY_MAX(NLA_U8, 1),
	[DEVLINK_ATTR_SB_POOL_SIZE] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_SB_PORT_POOL_SET - do */
static const struct nla_policy devlink_sb_port_pool_set_nl_policy[DEVLINK_ATTR_SB_THRESHOLD + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16, },
	[DEVLINK_ATTR_SB_THRESHOLD] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_SB_TC_POOL_BIND_SET - do */
static const struct nla_policy devlink_sb_tc_pool_bind_set_nl_policy[DEVLINK_ATTR_SB_TC_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .type = NLA_U16, },
	[DEVLINK_ATTR_SB_POOL_TYPE] = NLA_POLICY_MAX(NLA_U8, 1),
	[DEVLINK_ATTR_SB_TC_INDEX] = { .type = NLA_U16, },
	[DEVLINK_ATTR_SB_THRESHOLD] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_SB_OCC_SNAPSHOT - do */
static const struct nla_policy devlink_sb_occ_snapshot_nl_policy[DEVLINK_ATTR_SB_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_SB_OCC_MAX_CLEAR - do */
static const struct nla_policy devlink_sb_occ_max_clear_nl_policy[DEVLINK_ATTR_SB_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SB_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_ESWITCH_GET - do */
static const struct nla_policy devlink_eswitch_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_ESWITCH_SET - do */
static const struct nla_policy devlink_eswitch_set_nl_policy[DEVLINK_ATTR_ESWITCH_ENCAP_MODE + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_ESWITCH_MODE] = NLA_POLICY_MAX(NLA_U16, 1),
	[DEVLINK_ATTR_ESWITCH_INLINE_MODE] = NLA_POLICY_MAX(NLA_U8, 3),
	[DEVLINK_ATTR_ESWITCH_ENCAP_MODE] = NLA_POLICY_MAX(NLA_U8, 1),
};

/* DEVLINK_CMD_DPIPE_TABLE_GET - do */
static const struct nla_policy devlink_dpipe_table_get_nl_policy[DEVLINK_ATTR_DPIPE_TABLE_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_DPIPE_ENTRIES_GET - do */
static const struct nla_policy devlink_dpipe_entries_get_nl_policy[DEVLINK_ATTR_DPIPE_TABLE_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_DPIPE_HEADERS_GET - do */
static const struct nla_policy devlink_dpipe_headers_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET - do */
static const struct nla_policy devlink_dpipe_table_counters_set_nl_policy[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED] = { .type = NLA_U8, },
};

/* DEVLINK_CMD_RESOURCE_SET - do */
static const struct nla_policy devlink_resource_set_nl_policy[DEVLINK_ATTR_RESOURCE_SIZE + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RESOURCE_ID] = { .type = NLA_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE] = { .type = NLA_U64, },
};

/* DEVLINK_CMD_RESOURCE_DUMP - do */
static const struct nla_policy devlink_resource_dump_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_RELOAD - do */
static const struct nla_policy devlink_reload_nl_policy[DEVLINK_ATTR_RELOAD_LIMITS + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RELOAD_ACTION] = NLA_POLICY_RANGE(NLA_U8, 1, 2),
	[DEVLINK_ATTR_RELOAD_LIMITS] = NLA_POLICY_BITFIELD32(6),
	[DEVLINK_ATTR_NETNS_PID] = { .type = NLA_U32, },
	[DEVLINK_ATTR_NETNS_FD] = { .type = NLA_U32, },
	[DEVLINK_ATTR_NETNS_ID] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_PARAM_SET - do */
static const struct nla_policy devlink_param_set_nl_policy[DEVLINK_ATTR_PARAM_VALUE_CMODE + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PARAM_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PARAM_TYPE] = NLA_POLICY_VALIDATE_FN(NLA_U8, &devlink_attr_param_type_validate),
	[DEVLINK_ATTR_PARAM_VALUE_CMODE] = NLA_POLICY_MAX(NLA_U8, 2),
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

/* DEVLINK_CMD_REGION_NEW - do */
static const struct nla_policy devlink_region_new_nl_policy[DEVLINK_ATTR_REGION_SNAPSHOT_ID + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_REGION_DEL - do */
static const struct nla_policy devlink_region_del_nl_policy[DEVLINK_ATTR_REGION_SNAPSHOT_ID + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_REGION_READ - dump */
static const struct nla_policy devlink_region_read_nl_policy[DEVLINK_ATTR_REGION_DIRECT + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_REGION_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .type = NLA_U32, },
	[DEVLINK_ATTR_REGION_DIRECT] = { .type = NLA_FLAG, },
	[DEVLINK_ATTR_REGION_CHUNK_ADDR] = { .type = NLA_U64, },
	[DEVLINK_ATTR_REGION_CHUNK_LEN] = { .type = NLA_U64, },
};

/* DEVLINK_CMD_PORT_PARAM_GET - do */
static const struct nla_policy devlink_port_param_get_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
};

/* DEVLINK_CMD_PORT_PARAM_SET - do */
static const struct nla_policy devlink_port_param_set_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_HEALTH_REPORTER_SET - do */
static const struct nla_policy devlink_health_reporter_set_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_BURST_PERIOD + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] = { .type = NLA_U64, },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER] = { .type = NLA_U8, },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP] = { .type = NLA_U8, },
	[DEVLINK_ATTR_HEALTH_REPORTER_BURST_PERIOD] = { .type = NLA_U64, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_RECOVER - do */
static const struct nla_policy devlink_health_reporter_recover_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE - do */
static const struct nla_policy devlink_health_reporter_diagnose_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET - dump */
static const struct nla_policy devlink_health_reporter_dump_get_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR - do */
static const struct nla_policy devlink_health_reporter_dump_clear_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_FLASH_UPDATE - do */
static const struct nla_policy devlink_flash_update_nl_policy[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK] = NLA_POLICY_BITFIELD32(3),
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

/* DEVLINK_CMD_TRAP_SET - do */
static const struct nla_policy devlink_trap_set_nl_policy[DEVLINK_ATTR_TRAP_ACTION + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_ACTION] = NLA_POLICY_MAX(NLA_U8, 2),
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

/* DEVLINK_CMD_TRAP_GROUP_SET - do */
static const struct nla_policy devlink_trap_group_set_nl_policy[DEVLINK_ATTR_TRAP_POLICER_ID + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_ACTION] = NLA_POLICY_MAX(NLA_U8, 2),
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .type = NLA_U32, },
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

/* DEVLINK_CMD_TRAP_POLICER_SET - do */
static const struct nla_policy devlink_trap_policer_set_nl_policy[DEVLINK_ATTR_TRAP_POLICER_BURST + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .type = NLA_U32, },
	[DEVLINK_ATTR_TRAP_POLICER_RATE] = { .type = NLA_U64, },
	[DEVLINK_ATTR_TRAP_POLICER_BURST] = { .type = NLA_U64, },
};

/* DEVLINK_CMD_HEALTH_REPORTER_TEST - do */
static const struct nla_policy devlink_health_reporter_test_nl_policy[DEVLINK_ATTR_HEALTH_REPORTER_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .type = NLA_NUL_STRING, },
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

/* DEVLINK_CMD_RATE_SET - do */
static const struct nla_policy devlink_rate_set_nl_policy[DEVLINK_ATTR_RATE_TC_BWS + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_TX_SHARE] = { .type = NLA_U64, },
	[DEVLINK_ATTR_RATE_TX_MAX] = { .type = NLA_U64, },
	[DEVLINK_ATTR_RATE_TX_PRIORITY] = { .type = NLA_U32, },
	[DEVLINK_ATTR_RATE_TX_WEIGHT] = { .type = NLA_U32, },
	[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_TC_BWS] = NLA_POLICY_NESTED(devlink_dl_rate_tc_bws_nl_policy),
};

/* DEVLINK_CMD_RATE_NEW - do */
static const struct nla_policy devlink_rate_new_nl_policy[DEVLINK_ATTR_RATE_TC_BWS + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_TX_SHARE] = { .type = NLA_U64, },
	[DEVLINK_ATTR_RATE_TX_MAX] = { .type = NLA_U64, },
	[DEVLINK_ATTR_RATE_TX_PRIORITY] = { .type = NLA_U32, },
	[DEVLINK_ATTR_RATE_TX_WEIGHT] = { .type = NLA_U32, },
	[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_TC_BWS] = NLA_POLICY_NESTED(devlink_dl_rate_tc_bws_nl_policy),
};

/* DEVLINK_CMD_RATE_DEL - do */
static const struct nla_policy devlink_rate_del_nl_policy[DEVLINK_ATTR_RATE_NODE_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .type = NLA_NUL_STRING, },
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

/* DEVLINK_CMD_LINECARD_SET - do */
static const struct nla_policy devlink_linecard_set_nl_policy[DEVLINK_ATTR_LINECARD_TYPE + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_LINECARD_INDEX] = { .type = NLA_U32, },
	[DEVLINK_ATTR_LINECARD_TYPE] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SELFTESTS_GET - do */
static const struct nla_policy devlink_selftests_get_nl_policy[DEVLINK_ATTR_DEV_NAME + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
};

/* DEVLINK_CMD_SELFTESTS_RUN - do */
static const struct nla_policy devlink_selftests_run_nl_policy[DEVLINK_ATTR_SELFTESTS + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_SELFTESTS] = NLA_POLICY_NESTED(devlink_dl_selftest_id_nl_policy),
};

/* DEVLINK_CMD_NOTIFY_FILTER_SET - do */
static const struct nla_policy devlink_notify_filter_set_nl_policy[DEVLINK_ATTR_PORT_INDEX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVLINK_ATTR_PORT_INDEX] = { .type = NLA_U32, },
};

/* Ops table for devlink */
const struct genl_split_ops devlink_nl_ops[74] = {
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
		.cmd		= DEVLINK_CMD_PORT_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_FUNCTION,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_NEW,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_port_new_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_new_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_PCI_SF_NUMBER,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_DEL,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_del_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_del_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_SPLIT,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_split_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_split_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_SPLIT_COUNT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_UNSPLIT,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_unsplit_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_unsplit_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_SB_POOL_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_sb_pool_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_pool_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_SB_PORT_POOL_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_sb_port_pool_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_port_pool_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_THRESHOLD,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_SB_TC_POOL_BIND_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_sb_tc_pool_bind_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_tc_pool_bind_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_TC_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_OCC_SNAPSHOT,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_sb_occ_snapshot_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_occ_snapshot_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_SB_OCC_MAX_CLEAR,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_sb_occ_max_clear_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_sb_occ_max_clear_nl_policy,
		.maxattr	= DEVLINK_ATTR_SB_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_ESWITCH_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_eswitch_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_eswitch_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_ESWITCH_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_eswitch_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_eswitch_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_ESWITCH_ENCAP_MODE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_DPIPE_TABLE_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_dpipe_table_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_dpipe_table_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DPIPE_TABLE_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_DPIPE_ENTRIES_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_dpipe_entries_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_dpipe_entries_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DPIPE_TABLE_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_DPIPE_HEADERS_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_dpipe_headers_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_dpipe_headers_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_dpipe_table_counters_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_dpipe_table_counters_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RESOURCE_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_resource_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_resource_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_RESOURCE_SIZE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RESOURCE_DUMP,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_resource_dump_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_resource_dump_nl_policy,
		.maxattr	= DEVLINK_ATTR_DEV_NAME,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RELOAD,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_dev_lock,
		.doit		= devlink_nl_reload_doit,
		.post_doit	= devlink_nl_post_doit_dev_lock,
		.policy		= devlink_reload_nl_policy,
		.maxattr	= DEVLINK_ATTR_RELOAD_LIMITS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_PARAM_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_param_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_param_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_PARAM_VALUE_CMODE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_REGION_NEW,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_region_new_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_region_new_nl_policy,
		.maxattr	= DEVLINK_ATTR_REGION_SNAPSHOT_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_REGION_DEL,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_region_del_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_region_del_nl_policy,
		.maxattr	= DEVLINK_ATTR_REGION_SNAPSHOT_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_REGION_READ,
		.validate	= GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit		= devlink_nl_region_read_dumpit,
		.policy		= devlink_region_read_nl_policy,
		.maxattr	= DEVLINK_ATTR_REGION_DIRECT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_PARAM_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_param_get_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_param_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_PARAM_GET,
		.validate	= GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit		= devlink_nl_port_param_get_dumpit,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_PORT_PARAM_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port,
		.doit		= devlink_nl_port_param_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_port_param_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_BURST_PERIOD,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_RECOVER,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_recover_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_recover_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_diagnose_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_diagnose_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET,
		.validate	= GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit		= devlink_nl_health_reporter_dump_get_dumpit,
		.policy		= devlink_health_reporter_dump_get_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_dump_clear_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_dump_clear_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_FLASH_UPDATE,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_flash_update_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_flash_update_nl_policy,
		.maxattr	= DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_TRAP_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_ACTION,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_TRAP_GROUP_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_group_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_group_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_POLICER_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_TRAP_POLICER_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_trap_policer_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_trap_policer_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_TRAP_POLICER_BURST,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_HEALTH_REPORTER_TEST,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit_port_optional,
		.doit		= devlink_nl_health_reporter_test_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_health_reporter_test_nl_policy,
		.maxattr	= DEVLINK_ATTR_HEALTH_REPORTER_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_RATE_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_rate_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_rate_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_RATE_TC_BWS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RATE_NEW,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_rate_new_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_rate_new_nl_policy,
		.maxattr	= DEVLINK_ATTR_RATE_TC_BWS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_RATE_DEL,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_rate_del_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_rate_del_nl_policy,
		.maxattr	= DEVLINK_ATTR_RATE_NODE_NAME,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
		.cmd		= DEVLINK_CMD_LINECARD_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_linecard_set_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_linecard_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_LINECARD_TYPE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
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
	{
		.cmd		= DEVLINK_CMD_SELFTESTS_RUN,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.pre_doit	= devlink_nl_pre_doit,
		.doit		= devlink_nl_selftests_run_doit,
		.post_doit	= devlink_nl_post_doit,
		.policy		= devlink_selftests_run_nl_policy,
		.maxattr	= DEVLINK_ATTR_SELFTESTS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DEVLINK_CMD_NOTIFY_FILTER_SET,
		.doit		= devlink_nl_notify_filter_set_doit,
		.policy		= devlink_notify_filter_set_nl_policy,
		.maxattr	= DEVLINK_ATTR_PORT_INDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
};

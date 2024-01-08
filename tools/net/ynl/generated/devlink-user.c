// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN user source */

#include <stdlib.h>
#include <string.h>
#include "devlink-user.h"
#include "ynl.h"
#include <linux/devlink.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

/* Enums */
static const char * const devlink_op_strmap[] = {
	[3] = "get",
	// skip "port-get", duplicate reply value
	[DEVLINK_CMD_PORT_NEW] = "port-new",
	[13] = "sb-get",
	[17] = "sb-pool-get",
	[21] = "sb-port-pool-get",
	[25] = "sb-tc-pool-bind-get",
	[DEVLINK_CMD_ESWITCH_GET] = "eswitch-get",
	[DEVLINK_CMD_DPIPE_TABLE_GET] = "dpipe-table-get",
	[DEVLINK_CMD_DPIPE_ENTRIES_GET] = "dpipe-entries-get",
	[DEVLINK_CMD_DPIPE_HEADERS_GET] = "dpipe-headers-get",
	[DEVLINK_CMD_RESOURCE_DUMP] = "resource-dump",
	[DEVLINK_CMD_RELOAD] = "reload",
	[DEVLINK_CMD_PARAM_GET] = "param-get",
	[DEVLINK_CMD_REGION_GET] = "region-get",
	[DEVLINK_CMD_REGION_NEW] = "region-new",
	[DEVLINK_CMD_REGION_READ] = "region-read",
	[DEVLINK_CMD_PORT_PARAM_GET] = "port-param-get",
	[DEVLINK_CMD_INFO_GET] = "info-get",
	[DEVLINK_CMD_HEALTH_REPORTER_GET] = "health-reporter-get",
	[DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET] = "health-reporter-dump-get",
	[63] = "trap-get",
	[67] = "trap-group-get",
	[71] = "trap-policer-get",
	[76] = "rate-get",
	[80] = "linecard-get",
	[DEVLINK_CMD_SELFTESTS_GET] = "selftests-get",
};

const char *devlink_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(devlink_op_strmap))
		return NULL;
	return devlink_op_strmap[op];
}

static const char * const devlink_sb_pool_type_strmap[] = {
	[0] = "ingress",
	[1] = "egress",
};

const char *devlink_sb_pool_type_str(enum devlink_sb_pool_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_sb_pool_type_strmap))
		return NULL;
	return devlink_sb_pool_type_strmap[value];
}

static const char * const devlink_port_type_strmap[] = {
	[0] = "notset",
	[1] = "auto",
	[2] = "eth",
	[3] = "ib",
};

const char *devlink_port_type_str(enum devlink_port_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_port_type_strmap))
		return NULL;
	return devlink_port_type_strmap[value];
}

static const char * const devlink_port_flavour_strmap[] = {
	[0] = "physical",
	[1] = "cpu",
	[2] = "dsa",
	[3] = "pci_pf",
	[4] = "pci_vf",
	[5] = "virtual",
	[6] = "unused",
	[7] = "pci_sf",
};

const char *devlink_port_flavour_str(enum devlink_port_flavour value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_port_flavour_strmap))
		return NULL;
	return devlink_port_flavour_strmap[value];
}

static const char * const devlink_port_fn_state_strmap[] = {
	[0] = "inactive",
	[1] = "active",
};

const char *devlink_port_fn_state_str(enum devlink_port_fn_state value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_port_fn_state_strmap))
		return NULL;
	return devlink_port_fn_state_strmap[value];
}

static const char * const devlink_port_fn_opstate_strmap[] = {
	[0] = "detached",
	[1] = "attached",
};

const char *devlink_port_fn_opstate_str(enum devlink_port_fn_opstate value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_port_fn_opstate_strmap))
		return NULL;
	return devlink_port_fn_opstate_strmap[value];
}

static const char * const devlink_port_fn_attr_cap_strmap[] = {
	[0] = "roce-bit",
	[1] = "migratable-bit",
	[2] = "ipsec-crypto-bit",
	[3] = "ipsec-packet-bit",
};

const char *devlink_port_fn_attr_cap_str(enum devlink_port_fn_attr_cap value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_port_fn_attr_cap_strmap))
		return NULL;
	return devlink_port_fn_attr_cap_strmap[value];
}

static const char * const devlink_sb_threshold_type_strmap[] = {
	[0] = "static",
	[1] = "dynamic",
};

const char *devlink_sb_threshold_type_str(enum devlink_sb_threshold_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_sb_threshold_type_strmap))
		return NULL;
	return devlink_sb_threshold_type_strmap[value];
}

static const char * const devlink_eswitch_mode_strmap[] = {
	[0] = "legacy",
	[1] = "switchdev",
};

const char *devlink_eswitch_mode_str(enum devlink_eswitch_mode value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_eswitch_mode_strmap))
		return NULL;
	return devlink_eswitch_mode_strmap[value];
}

static const char * const devlink_eswitch_inline_mode_strmap[] = {
	[0] = "none",
	[1] = "link",
	[2] = "network",
	[3] = "transport",
};

const char *
devlink_eswitch_inline_mode_str(enum devlink_eswitch_inline_mode value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_eswitch_inline_mode_strmap))
		return NULL;
	return devlink_eswitch_inline_mode_strmap[value];
}

static const char * const devlink_eswitch_encap_mode_strmap[] = {
	[0] = "none",
	[1] = "basic",
};

const char *
devlink_eswitch_encap_mode_str(enum devlink_eswitch_encap_mode value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_eswitch_encap_mode_strmap))
		return NULL;
	return devlink_eswitch_encap_mode_strmap[value];
}

static const char * const devlink_dpipe_match_type_strmap[] = {
	[0] = "field-exact",
};

const char *devlink_dpipe_match_type_str(enum devlink_dpipe_match_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_dpipe_match_type_strmap))
		return NULL;
	return devlink_dpipe_match_type_strmap[value];
}

static const char * const devlink_dpipe_action_type_strmap[] = {
	[0] = "field-modify",
};

const char *devlink_dpipe_action_type_str(enum devlink_dpipe_action_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_dpipe_action_type_strmap))
		return NULL;
	return devlink_dpipe_action_type_strmap[value];
}

static const char * const devlink_dpipe_field_mapping_type_strmap[] = {
	[0] = "none",
	[1] = "ifindex",
};

const char *
devlink_dpipe_field_mapping_type_str(enum devlink_dpipe_field_mapping_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_dpipe_field_mapping_type_strmap))
		return NULL;
	return devlink_dpipe_field_mapping_type_strmap[value];
}

static const char * const devlink_resource_unit_strmap[] = {
	[0] = "entry",
};

const char *devlink_resource_unit_str(enum devlink_resource_unit value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_resource_unit_strmap))
		return NULL;
	return devlink_resource_unit_strmap[value];
}

static const char * const devlink_reload_action_strmap[] = {
	[1] = "driver-reinit",
	[2] = "fw-activate",
};

const char *devlink_reload_action_str(enum devlink_reload_action value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_reload_action_strmap))
		return NULL;
	return devlink_reload_action_strmap[value];
}

static const char * const devlink_param_cmode_strmap[] = {
	[0] = "runtime",
	[1] = "driverinit",
	[2] = "permanent",
};

const char *devlink_param_cmode_str(enum devlink_param_cmode value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_param_cmode_strmap))
		return NULL;
	return devlink_param_cmode_strmap[value];
}

static const char * const devlink_flash_overwrite_strmap[] = {
	[0] = "settings-bit",
	[1] = "identifiers-bit",
};

const char *devlink_flash_overwrite_str(enum devlink_flash_overwrite value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_flash_overwrite_strmap))
		return NULL;
	return devlink_flash_overwrite_strmap[value];
}

static const char * const devlink_trap_action_strmap[] = {
	[0] = "drop",
	[1] = "trap",
	[2] = "mirror",
};

const char *devlink_trap_action_str(enum devlink_trap_action value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_trap_action_strmap))
		return NULL;
	return devlink_trap_action_strmap[value];
}

/* Policies */
struct ynl_policy_attr devlink_dl_dpipe_match_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_MATCH_TYPE] = { .name = "dpipe-match-type", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_ID] = { .name = "dpipe-header-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_GLOBAL] = { .name = "dpipe-header-global", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_HEADER_INDEX] = { .name = "dpipe-header-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_ID] = { .name = "dpipe-field-id", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_dpipe_match_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_match_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_match_value_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_MATCH] = { .name = "dpipe-match", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_match_nest, },
	[DEVLINK_ATTR_DPIPE_VALUE] = { .name = "dpipe-value", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MASK] = { .name = "dpipe-value-mask", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MAPPING] = { .name = "dpipe-value-mapping", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_dpipe_match_value_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_match_value_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_action_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ACTION_TYPE] = { .name = "dpipe-action-type", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_ID] = { .name = "dpipe-header-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_GLOBAL] = { .name = "dpipe-header-global", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_HEADER_INDEX] = { .name = "dpipe-header-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_ID] = { .name = "dpipe-field-id", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_dpipe_action_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_action_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_action_value_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ACTION] = { .name = "dpipe-action", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_action_nest, },
	[DEVLINK_ATTR_DPIPE_VALUE] = { .name = "dpipe-value", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MASK] = { .name = "dpipe-value-mask", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MAPPING] = { .name = "dpipe-value-mapping", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_dpipe_action_value_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_action_value_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_field_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_FIELD_NAME] = { .name = "dpipe-field-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_FIELD_ID] = { .name = "dpipe-field-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_BITWIDTH] = { .name = "dpipe-field-bitwidth", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_MAPPING_TYPE] = { .name = "dpipe-field-mapping-type", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_dpipe_field_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_field_policy,
};

struct ynl_policy_attr devlink_dl_resource_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RESOURCE_NAME] = { .name = "resource-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RESOURCE_ID] = { .name = "resource-id", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE] = { .name = "resource-size", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_NEW] = { .name = "resource-size-new", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_VALID] = { .name = "resource-size-valid", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RESOURCE_SIZE_MIN] = { .name = "resource-size-min", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_MAX] = { .name = "resource-size-max", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_GRAN] = { .name = "resource-size-gran", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_UNIT] = { .name = "resource-unit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RESOURCE_OCC] = { .name = "resource-occ", .type = YNL_PT_U64, },
};

struct ynl_policy_nest devlink_dl_resource_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_resource_policy,
};

struct ynl_policy_attr devlink_dl_info_version_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_INFO_VERSION_NAME] = { .name = "info-version-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_VALUE] = { .name = "info-version-value", .type = YNL_PT_NUL_STR, },
};

struct ynl_policy_nest devlink_dl_info_version_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_info_version_policy,
};

struct ynl_policy_attr devlink_dl_fmsg_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_FMSG_OBJ_NEST_START] = { .name = "fmsg-obj-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_PAIR_NEST_START] = { .name = "fmsg-pair-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_ARR_NEST_START] = { .name = "fmsg-arr-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_NEST_END] = { .name = "fmsg-nest-end", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_OBJ_NAME] = { .name = "fmsg-obj-name", .type = YNL_PT_NUL_STR, },
};

struct ynl_policy_nest devlink_dl_fmsg_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_fmsg_policy,
};

struct ynl_policy_attr devlink_dl_port_function_policy[DEVLINK_PORT_FUNCTION_ATTR_MAX + 1] = {
	[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR] = { .name = "hw-addr", .type = YNL_PT_BINARY,},
	[DEVLINK_PORT_FN_ATTR_STATE] = { .name = "state", .type = YNL_PT_U8, },
	[DEVLINK_PORT_FN_ATTR_OPSTATE] = { .name = "opstate", .type = YNL_PT_U8, },
	[DEVLINK_PORT_FN_ATTR_CAPS] = { .name = "caps", .type = YNL_PT_BITFIELD32, },
};

struct ynl_policy_nest devlink_dl_port_function_nest = {
	.max_attr = DEVLINK_PORT_FUNCTION_ATTR_MAX,
	.table = devlink_dl_port_function_policy,
};

struct ynl_policy_attr devlink_dl_reload_stats_entry_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS_LIMIT] = { .name = "reload-stats-limit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_STATS_VALUE] = { .name = "reload-stats-value", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_reload_stats_entry_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_stats_entry_policy,
};

struct ynl_policy_attr devlink_dl_reload_act_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS_ENTRY] = { .name = "reload-stats-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_entry_nest, },
};

struct ynl_policy_nest devlink_dl_reload_act_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_act_stats_policy,
};

struct ynl_policy_attr devlink_dl_selftest_id_policy[DEVLINK_ATTR_SELFTEST_ID_MAX + 1] = {
	[DEVLINK_ATTR_SELFTEST_ID_FLASH] = { .name = "flash", .type = YNL_PT_FLAG, },
};

struct ynl_policy_nest devlink_dl_selftest_id_nest = {
	.max_attr = DEVLINK_ATTR_SELFTEST_ID_MAX,
	.table = devlink_dl_selftest_id_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_table_matches_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_MATCH] = { .name = "dpipe-match", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_match_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_table_matches_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_table_matches_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_table_actions_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ACTION] = { .name = "dpipe-action", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_action_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_table_actions_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_table_actions_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_entry_match_values_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_MATCH_VALUE] = { .name = "dpipe-match-value", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_match_value_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_entry_match_values_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_entry_match_values_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_entry_action_values_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ACTION_VALUE] = { .name = "dpipe-action-value", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_action_value_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_entry_action_values_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_entry_action_values_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_header_fields_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_FIELD] = { .name = "dpipe-field", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_field_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_header_fields_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_header_fields_policy,
};

struct ynl_policy_attr devlink_dl_resource_list_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RESOURCE] = { .name = "resource", .type = YNL_PT_NEST, .nest = &devlink_dl_resource_nest, },
};

struct ynl_policy_nest devlink_dl_resource_list_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_resource_list_policy,
};

struct ynl_policy_attr devlink_dl_reload_act_info_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_ACTION] = { .name = "reload-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_ACTION_STATS] = { .name = "reload-action-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_stats_nest, },
};

struct ynl_policy_nest devlink_dl_reload_act_info_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_act_info_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_table_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .name = "dpipe-table-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_TABLE_SIZE] = { .name = "dpipe-table-size", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_TABLE_MATCHES] = { .name = "dpipe-table-matches", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_matches_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE_ACTIONS] = { .name = "dpipe-table-actions", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_actions_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED] = { .name = "dpipe-table-counters-enabled", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_ID] = { .name = "dpipe-table-resource-id", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_UNITS] = { .name = "dpipe-table-resource-units", .type = YNL_PT_U64, },
};

struct ynl_policy_nest devlink_dl_dpipe_table_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_table_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_entry_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ENTRY_INDEX] = { .name = "dpipe-entry-index", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_ENTRY_MATCH_VALUES] = { .name = "dpipe-entry-match-values", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_match_values_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY_ACTION_VALUES] = { .name = "dpipe-entry-action-values", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_action_values_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY_COUNTER] = { .name = "dpipe-entry-counter", .type = YNL_PT_U64, },
};

struct ynl_policy_nest devlink_dl_dpipe_entry_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_entry_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_header_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_HEADER_NAME] = { .name = "dpipe-header-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_HEADER_ID] = { .name = "dpipe-header-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_GLOBAL] = { .name = "dpipe-header-global", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_HEADER_FIELDS] = { .name = "dpipe-header-fields", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_header_fields_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_header_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_header_policy,
};

struct ynl_policy_attr devlink_dl_reload_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_ACTION_INFO] = { .name = "reload-action-info", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_info_nest, },
};

struct ynl_policy_nest devlink_dl_reload_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_stats_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_tables_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_TABLE] = { .name = "dpipe-table", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_tables_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_tables_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_entries_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_ENTRY] = { .name = "dpipe-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_entries_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_entries_policy,
};

struct ynl_policy_attr devlink_dl_dpipe_headers_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_DPIPE_HEADER] = { .name = "dpipe-header", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_header_nest, },
};

struct ynl_policy_nest devlink_dl_dpipe_headers_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dpipe_headers_policy,
};

struct ynl_policy_attr devlink_dl_dev_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS] = { .name = "reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_REMOTE_RELOAD_STATS] = { .name = "remote-reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
};

struct ynl_policy_nest devlink_dl_dev_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dev_stats_policy,
};

struct ynl_policy_attr devlink_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .name = "bus-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DEV_NAME] = { .name = "dev-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_PORT_INDEX] = { .name = "port-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_PORT_TYPE] = { .name = "port-type", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_PORT_SPLIT_COUNT] = { .name = "port-split-count", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .name = "sb-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .name = "sb-pool-index", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_SB_POOL_TYPE] = { .name = "sb-pool-type", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_SB_POOL_SIZE] = { .name = "sb-pool-size", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE] = { .name = "sb-pool-threshold-type", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_SB_THRESHOLD] = { .name = "sb-threshold", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_TC_INDEX] = { .name = "sb-tc-index", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_ESWITCH_MODE] = { .name = "eswitch-mode", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_ESWITCH_INLINE_MODE] = { .name = "eswitch-inline-mode", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_DPIPE_TABLES] = { .name = "dpipe-tables", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_tables_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE] = { .name = "dpipe-table", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE_NAME] = { .name = "dpipe-table-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_TABLE_SIZE] = { .name = "dpipe-table-size", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_TABLE_MATCHES] = { .name = "dpipe-table-matches", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_matches_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE_ACTIONS] = { .name = "dpipe-table-actions", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_table_actions_nest, },
	[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED] = { .name = "dpipe-table-counters-enabled", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_ENTRIES] = { .name = "dpipe-entries", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entries_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY] = { .name = "dpipe-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY_INDEX] = { .name = "dpipe-entry-index", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_ENTRY_MATCH_VALUES] = { .name = "dpipe-entry-match-values", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_match_values_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY_ACTION_VALUES] = { .name = "dpipe-entry-action-values", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_entry_action_values_nest, },
	[DEVLINK_ATTR_DPIPE_ENTRY_COUNTER] = { .name = "dpipe-entry-counter", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_MATCH] = { .name = "dpipe-match", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_match_nest, },
	[DEVLINK_ATTR_DPIPE_MATCH_VALUE] = { .name = "dpipe-match-value", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_match_value_nest, },
	[DEVLINK_ATTR_DPIPE_MATCH_TYPE] = { .name = "dpipe-match-type", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_ACTION] = { .name = "dpipe-action", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_action_nest, },
	[DEVLINK_ATTR_DPIPE_ACTION_VALUE] = { .name = "dpipe-action-value", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_action_value_nest, },
	[DEVLINK_ATTR_DPIPE_ACTION_TYPE] = { .name = "dpipe-action-type", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_VALUE] = { .name = "dpipe-value", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MASK] = { .name = "dpipe-value-mask", .type = YNL_PT_BINARY,},
	[DEVLINK_ATTR_DPIPE_VALUE_MAPPING] = { .name = "dpipe-value-mapping", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADERS] = { .name = "dpipe-headers", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_headers_nest, },
	[DEVLINK_ATTR_DPIPE_HEADER] = { .name = "dpipe-header", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_header_nest, },
	[DEVLINK_ATTR_DPIPE_HEADER_NAME] = { .name = "dpipe-header-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_HEADER_ID] = { .name = "dpipe-header-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_HEADER_FIELDS] = { .name = "dpipe-header-fields", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_header_fields_nest, },
	[DEVLINK_ATTR_DPIPE_HEADER_GLOBAL] = { .name = "dpipe-header-global", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DPIPE_HEADER_INDEX] = { .name = "dpipe-header-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD] = { .name = "dpipe-field", .type = YNL_PT_NEST, .nest = &devlink_dl_dpipe_field_nest, },
	[DEVLINK_ATTR_DPIPE_FIELD_NAME] = { .name = "dpipe-field-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DPIPE_FIELD_ID] = { .name = "dpipe-field-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_BITWIDTH] = { .name = "dpipe-field-bitwidth", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_DPIPE_FIELD_MAPPING_TYPE] = { .name = "dpipe-field-mapping-type", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[DEVLINK_ATTR_ESWITCH_ENCAP_MODE] = { .name = "eswitch-encap-mode", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RESOURCE_LIST] = { .name = "resource-list", .type = YNL_PT_NEST, .nest = &devlink_dl_resource_list_nest, },
	[DEVLINK_ATTR_RESOURCE] = { .name = "resource", .type = YNL_PT_NEST, .nest = &devlink_dl_resource_nest, },
	[DEVLINK_ATTR_RESOURCE_NAME] = { .name = "resource-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RESOURCE_ID] = { .name = "resource-id", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE] = { .name = "resource-size", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_NEW] = { .name = "resource-size-new", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_VALID] = { .name = "resource-size-valid", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RESOURCE_SIZE_MIN] = { .name = "resource-size-min", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_MAX] = { .name = "resource-size-max", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_SIZE_GRAN] = { .name = "resource-size-gran", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RESOURCE_UNIT] = { .name = "resource-unit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RESOURCE_OCC] = { .name = "resource-occ", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_ID] = { .name = "dpipe-table-resource-id", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_UNITS] = { .name = "dpipe-table-resource-units", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_PORT_FLAVOUR] = { .name = "port-flavour", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_PARAM_NAME] = { .name = "param-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_PARAM_TYPE] = { .name = "param-type", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_PARAM_VALUE_CMODE] = { .name = "param-value-cmode", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_REGION_NAME] = { .name = "region-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_REGION_SNAPSHOT_ID] = { .name = "region-snapshot-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_REGION_CHUNK_ADDR] = { .name = "region-chunk-addr", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_REGION_CHUNK_LEN] = { .name = "region-chunk-len", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_INFO_DRIVER_NAME] = { .name = "info-driver-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_SERIAL_NUMBER] = { .name = "info-serial-number", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_FIXED] = { .name = "info-version-fixed", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_RUNNING] = { .name = "info-version-running", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_STORED] = { .name = "info-version-stored", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_NAME] = { .name = "info-version-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_VALUE] = { .name = "info-version-value", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_FMSG] = { .name = "fmsg", .type = YNL_PT_NEST, .nest = &devlink_dl_fmsg_nest, },
	[DEVLINK_ATTR_FMSG_OBJ_NEST_START] = { .name = "fmsg-obj-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_PAIR_NEST_START] = { .name = "fmsg-pair-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_ARR_NEST_START] = { .name = "fmsg-arr-nest-start", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_NEST_END] = { .name = "fmsg-nest-end", .type = YNL_PT_FLAG, },
	[DEVLINK_ATTR_FMSG_OBJ_NAME] = { .name = "fmsg-obj-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .name = "health-reporter-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] = { .name = "health-reporter-graceful-period", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER] = { .name = "health-reporter-auto-recover", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME] = { .name = "flash-update-file-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT] = { .name = "flash-update-component", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_PORT_PCI_PF_NUMBER] = { .name = "port-pci-pf-number", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_TRAP_NAME] = { .name = "trap-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_TRAP_ACTION] = { .name = "trap-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .name = "trap-group-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RELOAD_FAILED] = { .name = "reload-failed", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_NETNS_FD] = { .name = "netns-fd", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_NETNS_PID] = { .name = "netns-pid", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_NETNS_ID] = { .name = "netns-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP] = { .name = "health-reporter-auto-dump", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .name = "trap-policer-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_TRAP_POLICER_RATE] = { .name = "trap-policer-rate", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_TRAP_POLICER_BURST] = { .name = "trap-policer-burst", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_PORT_FUNCTION] = { .name = "port-function", .type = YNL_PT_NEST, .nest = &devlink_dl_port_function_nest, },
	[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER] = { .name = "port-controller-number", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK] = { .name = "flash-update-overwrite-mask", .type = YNL_PT_BITFIELD32, },
	[DEVLINK_ATTR_RELOAD_ACTION] = { .name = "reload-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_ACTIONS_PERFORMED] = { .name = "reload-actions-performed", .type = YNL_PT_BITFIELD32, },
	[DEVLINK_ATTR_RELOAD_LIMITS] = { .name = "reload-limits", .type = YNL_PT_BITFIELD32, },
	[DEVLINK_ATTR_DEV_STATS] = { .name = "dev-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_dev_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS] = { .name = "reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_ENTRY] = { .name = "reload-stats-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_entry_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_LIMIT] = { .name = "reload-stats-limit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_STATS_VALUE] = { .name = "reload-stats-value", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_REMOTE_RELOAD_STATS] = { .name = "remote-reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_INFO] = { .name = "reload-action-info", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_info_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_STATS] = { .name = "reload-action-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_stats_nest, },
	[DEVLINK_ATTR_PORT_PCI_SF_NUMBER] = { .name = "port-pci-sf-number", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_RATE_TX_SHARE] = { .name = "rate-tx-share", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RATE_TX_MAX] = { .name = "rate-tx-max", .type = YNL_PT_U64, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .name = "rate-node-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] = { .name = "rate-parent-node-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_LINECARD_INDEX] = { .name = "linecard-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_LINECARD_TYPE] = { .name = "linecard-type", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_SELFTESTS] = { .name = "selftests", .type = YNL_PT_NEST, .nest = &devlink_dl_selftest_id_nest, },
	[DEVLINK_ATTR_RATE_TX_PRIORITY] = { .name = "rate-tx-priority", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_RATE_TX_WEIGHT] = { .name = "rate-tx-weight", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_REGION_DIRECT] = { .name = "region-direct", .type = YNL_PT_FLAG, },
};

struct ynl_policy_nest devlink_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_policy,
};

/* Common nested types */
void devlink_dl_dpipe_match_free(struct devlink_dl_dpipe_match *obj)
{
}

int devlink_dl_dpipe_match_parse(struct ynl_parse_arg *yarg,
				 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_match *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_MATCH_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_match_type = 1;
			dst->dpipe_match_type = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_id = 1;
			dst->dpipe_header_id = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_GLOBAL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_global = 1;
			dst->dpipe_header_global = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_index = 1;
			dst->dpipe_header_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_FIELD_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_field_id = 1;
			dst->dpipe_field_id = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void
devlink_dl_dpipe_match_value_free(struct devlink_dl_dpipe_match_value *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_match; i++)
		devlink_dl_dpipe_match_free(&obj->dpipe_match[i]);
	free(obj->dpipe_match);
	free(obj->dpipe_value);
	free(obj->dpipe_value_mask);
}

int devlink_dl_dpipe_match_value_parse(struct ynl_parse_arg *yarg,
				       const struct nlattr *nested)
{
	struct devlink_dl_dpipe_match_value *dst = yarg->data;
	unsigned int n_dpipe_match = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_match)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-match-value.dpipe-match)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_MATCH) {
			n_dpipe_match++;
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.dpipe_value_len = len;
			dst->dpipe_value = malloc(len);
			memcpy(dst->dpipe_value, mnl_attr_get_payload(attr), len);
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE_MASK) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.dpipe_value_mask_len = len;
			dst->dpipe_value_mask = malloc(len);
			memcpy(dst->dpipe_value_mask, mnl_attr_get_payload(attr), len);
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE_MAPPING) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_value_mapping = 1;
			dst->dpipe_value_mapping = mnl_attr_get_u32(attr);
		}
	}

	if (n_dpipe_match) {
		dst->dpipe_match = calloc(n_dpipe_match, sizeof(*dst->dpipe_match));
		dst->n_dpipe_match = n_dpipe_match;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_match_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_MATCH) {
				parg.data = &dst->dpipe_match[i];
				if (devlink_dl_dpipe_match_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_action_free(struct devlink_dl_dpipe_action *obj)
{
}

int devlink_dl_dpipe_action_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_dpipe_action *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ACTION_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_action_type = 1;
			dst->dpipe_action_type = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_id = 1;
			dst->dpipe_header_id = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_GLOBAL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_global = 1;
			dst->dpipe_header_global = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_index = 1;
			dst->dpipe_header_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_FIELD_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_field_id = 1;
			dst->dpipe_field_id = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void
devlink_dl_dpipe_action_value_free(struct devlink_dl_dpipe_action_value *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_action; i++)
		devlink_dl_dpipe_action_free(&obj->dpipe_action[i]);
	free(obj->dpipe_action);
	free(obj->dpipe_value);
	free(obj->dpipe_value_mask);
}

int devlink_dl_dpipe_action_value_parse(struct ynl_parse_arg *yarg,
					const struct nlattr *nested)
{
	struct devlink_dl_dpipe_action_value *dst = yarg->data;
	unsigned int n_dpipe_action = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_action)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-action-value.dpipe-action)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ACTION) {
			n_dpipe_action++;
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.dpipe_value_len = len;
			dst->dpipe_value = malloc(len);
			memcpy(dst->dpipe_value, mnl_attr_get_payload(attr), len);
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE_MASK) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.dpipe_value_mask_len = len;
			dst->dpipe_value_mask = malloc(len);
			memcpy(dst->dpipe_value_mask, mnl_attr_get_payload(attr), len);
		} else if (type == DEVLINK_ATTR_DPIPE_VALUE_MAPPING) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_value_mapping = 1;
			dst->dpipe_value_mapping = mnl_attr_get_u32(attr);
		}
	}

	if (n_dpipe_action) {
		dst->dpipe_action = calloc(n_dpipe_action, sizeof(*dst->dpipe_action));
		dst->n_dpipe_action = n_dpipe_action;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_action_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_ACTION) {
				parg.data = &dst->dpipe_action[i];
				if (devlink_dl_dpipe_action_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_field_free(struct devlink_dl_dpipe_field *obj)
{
	free(obj->dpipe_field_name);
}

int devlink_dl_dpipe_field_parse(struct ynl_parse_arg *yarg,
				 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_field *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_FIELD_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dpipe_field_name_len = len;
			dst->dpipe_field_name = malloc(len + 1);
			memcpy(dst->dpipe_field_name, mnl_attr_get_str(attr), len);
			dst->dpipe_field_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_FIELD_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_field_id = 1;
			dst->dpipe_field_id = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_FIELD_BITWIDTH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_field_bitwidth = 1;
			dst->dpipe_field_bitwidth = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_FIELD_MAPPING_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_field_mapping_type = 1;
			dst->dpipe_field_mapping_type = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void devlink_dl_resource_free(struct devlink_dl_resource *obj)
{
	free(obj->resource_name);
}

int devlink_dl_resource_parse(struct ynl_parse_arg *yarg,
			      const struct nlattr *nested)
{
	struct devlink_dl_resource *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RESOURCE_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.resource_name_len = len;
			dst->resource_name = malloc(len + 1);
			memcpy(dst->resource_name, mnl_attr_get_str(attr), len);
			dst->resource_name[len] = 0;
		} else if (type == DEVLINK_ATTR_RESOURCE_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_id = 1;
			dst->resource_id = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size = 1;
			dst->resource_size = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE_NEW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size_new = 1;
			dst->resource_size_new = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE_VALID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size_valid = 1;
			dst->resource_size_valid = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE_MIN) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size_min = 1;
			dst->resource_size_min = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size_max = 1;
			dst->resource_size_max = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_SIZE_GRAN) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_size_gran = 1;
			dst->resource_size_gran = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_UNIT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_unit = 1;
			dst->resource_unit = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RESOURCE_OCC) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_occ = 1;
			dst->resource_occ = mnl_attr_get_u64(attr);
		}
	}

	return 0;
}

void devlink_dl_info_version_free(struct devlink_dl_info_version *obj)
{
	free(obj->info_version_name);
	free(obj->info_version_value);
}

int devlink_dl_info_version_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_info_version *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_INFO_VERSION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_version_name_len = len;
			dst->info_version_name = malloc(len + 1);
			memcpy(dst->info_version_name, mnl_attr_get_str(attr), len);
			dst->info_version_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_VALUE) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_version_value_len = len;
			dst->info_version_value = malloc(len + 1);
			memcpy(dst->info_version_value, mnl_attr_get_str(attr), len);
			dst->info_version_value[len] = 0;
		}
	}

	return 0;
}

void devlink_dl_fmsg_free(struct devlink_dl_fmsg *obj)
{
	free(obj->fmsg_obj_name);
}

int devlink_dl_fmsg_parse(struct ynl_parse_arg *yarg,
			  const struct nlattr *nested)
{
	struct devlink_dl_fmsg *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_FMSG_OBJ_NEST_START) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fmsg_obj_nest_start = 1;
		} else if (type == DEVLINK_ATTR_FMSG_PAIR_NEST_START) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fmsg_pair_nest_start = 1;
		} else if (type == DEVLINK_ATTR_FMSG_ARR_NEST_START) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fmsg_arr_nest_start = 1;
		} else if (type == DEVLINK_ATTR_FMSG_NEST_END) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fmsg_nest_end = 1;
		} else if (type == DEVLINK_ATTR_FMSG_OBJ_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.fmsg_obj_name_len = len;
			dst->fmsg_obj_name = malloc(len + 1);
			memcpy(dst->fmsg_obj_name, mnl_attr_get_str(attr), len);
			dst->fmsg_obj_name[len] = 0;
		}
	}

	return 0;
}

void devlink_dl_port_function_free(struct devlink_dl_port_function *obj)
{
	free(obj->hw_addr);
}

int devlink_dl_port_function_put(struct nlmsghdr *nlh, unsigned int attr_type,
				 struct devlink_dl_port_function *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.hw_addr_len)
		mnl_attr_put(nlh, DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR, obj->_present.hw_addr_len, obj->hw_addr);
	if (obj->_present.state)
		mnl_attr_put_u8(nlh, DEVLINK_PORT_FN_ATTR_STATE, obj->state);
	if (obj->_present.opstate)
		mnl_attr_put_u8(nlh, DEVLINK_PORT_FN_ATTR_OPSTATE, obj->opstate);
	if (obj->_present.caps)
		mnl_attr_put(nlh, DEVLINK_PORT_FN_ATTR_CAPS, sizeof(struct nla_bitfield32), &obj->caps);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

void
devlink_dl_reload_stats_entry_free(struct devlink_dl_reload_stats_entry *obj)
{
}

int devlink_dl_reload_stats_entry_parse(struct ynl_parse_arg *yarg,
					const struct nlattr *nested)
{
	struct devlink_dl_reload_stats_entry *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS_LIMIT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats_limit = 1;
			dst->reload_stats_limit = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RELOAD_STATS_VALUE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats_value = 1;
			dst->reload_stats_value = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void devlink_dl_reload_act_stats_free(struct devlink_dl_reload_act_stats *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_stats_entry; i++)
		devlink_dl_reload_stats_entry_free(&obj->reload_stats_entry[i]);
	free(obj->reload_stats_entry);
}

int devlink_dl_reload_act_stats_parse(struct ynl_parse_arg *yarg,
				      const struct nlattr *nested)
{
	struct devlink_dl_reload_act_stats *dst = yarg->data;
	unsigned int n_reload_stats_entry = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_stats_entry)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-act-stats.reload-stats-entry)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS_ENTRY) {
			n_reload_stats_entry++;
		}
	}

	if (n_reload_stats_entry) {
		dst->reload_stats_entry = calloc(n_reload_stats_entry, sizeof(*dst->reload_stats_entry));
		dst->n_reload_stats_entry = n_reload_stats_entry;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_stats_entry_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_STATS_ENTRY) {
				parg.data = &dst->reload_stats_entry[i];
				if (devlink_dl_reload_stats_entry_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_selftest_id_free(struct devlink_dl_selftest_id *obj)
{
}

int devlink_dl_selftest_id_put(struct nlmsghdr *nlh, unsigned int attr_type,
			       struct devlink_dl_selftest_id *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.flash)
		mnl_attr_put(nlh, DEVLINK_ATTR_SELFTEST_ID_FLASH, 0, NULL);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

void
devlink_dl_dpipe_table_matches_free(struct devlink_dl_dpipe_table_matches *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_match; i++)
		devlink_dl_dpipe_match_free(&obj->dpipe_match[i]);
	free(obj->dpipe_match);
}

int devlink_dl_dpipe_table_matches_parse(struct ynl_parse_arg *yarg,
					 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_table_matches *dst = yarg->data;
	unsigned int n_dpipe_match = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_match)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-table-matches.dpipe-match)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_MATCH) {
			n_dpipe_match++;
		}
	}

	if (n_dpipe_match) {
		dst->dpipe_match = calloc(n_dpipe_match, sizeof(*dst->dpipe_match));
		dst->n_dpipe_match = n_dpipe_match;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_match_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_MATCH) {
				parg.data = &dst->dpipe_match[i];
				if (devlink_dl_dpipe_match_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void
devlink_dl_dpipe_table_actions_free(struct devlink_dl_dpipe_table_actions *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_action; i++)
		devlink_dl_dpipe_action_free(&obj->dpipe_action[i]);
	free(obj->dpipe_action);
}

int devlink_dl_dpipe_table_actions_parse(struct ynl_parse_arg *yarg,
					 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_table_actions *dst = yarg->data;
	unsigned int n_dpipe_action = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_action)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-table-actions.dpipe-action)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ACTION) {
			n_dpipe_action++;
		}
	}

	if (n_dpipe_action) {
		dst->dpipe_action = calloc(n_dpipe_action, sizeof(*dst->dpipe_action));
		dst->n_dpipe_action = n_dpipe_action;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_action_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_ACTION) {
				parg.data = &dst->dpipe_action[i];
				if (devlink_dl_dpipe_action_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void
devlink_dl_dpipe_entry_match_values_free(struct devlink_dl_dpipe_entry_match_values *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_match_value; i++)
		devlink_dl_dpipe_match_value_free(&obj->dpipe_match_value[i]);
	free(obj->dpipe_match_value);
}

int devlink_dl_dpipe_entry_match_values_parse(struct ynl_parse_arg *yarg,
					      const struct nlattr *nested)
{
	struct devlink_dl_dpipe_entry_match_values *dst = yarg->data;
	unsigned int n_dpipe_match_value = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_match_value)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-entry-match-values.dpipe-match-value)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_MATCH_VALUE) {
			n_dpipe_match_value++;
		}
	}

	if (n_dpipe_match_value) {
		dst->dpipe_match_value = calloc(n_dpipe_match_value, sizeof(*dst->dpipe_match_value));
		dst->n_dpipe_match_value = n_dpipe_match_value;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_match_value_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_MATCH_VALUE) {
				parg.data = &dst->dpipe_match_value[i];
				if (devlink_dl_dpipe_match_value_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void
devlink_dl_dpipe_entry_action_values_free(struct devlink_dl_dpipe_entry_action_values *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_action_value; i++)
		devlink_dl_dpipe_action_value_free(&obj->dpipe_action_value[i]);
	free(obj->dpipe_action_value);
}

int devlink_dl_dpipe_entry_action_values_parse(struct ynl_parse_arg *yarg,
					       const struct nlattr *nested)
{
	struct devlink_dl_dpipe_entry_action_values *dst = yarg->data;
	unsigned int n_dpipe_action_value = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_action_value)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-entry-action-values.dpipe-action-value)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ACTION_VALUE) {
			n_dpipe_action_value++;
		}
	}

	if (n_dpipe_action_value) {
		dst->dpipe_action_value = calloc(n_dpipe_action_value, sizeof(*dst->dpipe_action_value));
		dst->n_dpipe_action_value = n_dpipe_action_value;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_action_value_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_ACTION_VALUE) {
				parg.data = &dst->dpipe_action_value[i];
				if (devlink_dl_dpipe_action_value_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void
devlink_dl_dpipe_header_fields_free(struct devlink_dl_dpipe_header_fields *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_field; i++)
		devlink_dl_dpipe_field_free(&obj->dpipe_field[i]);
	free(obj->dpipe_field);
}

int devlink_dl_dpipe_header_fields_parse(struct ynl_parse_arg *yarg,
					 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_header_fields *dst = yarg->data;
	unsigned int n_dpipe_field = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_field)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-header-fields.dpipe-field)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_FIELD) {
			n_dpipe_field++;
		}
	}

	if (n_dpipe_field) {
		dst->dpipe_field = calloc(n_dpipe_field, sizeof(*dst->dpipe_field));
		dst->n_dpipe_field = n_dpipe_field;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_field_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_FIELD) {
				parg.data = &dst->dpipe_field[i];
				if (devlink_dl_dpipe_field_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_resource_list_free(struct devlink_dl_resource_list *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_resource; i++)
		devlink_dl_resource_free(&obj->resource[i]);
	free(obj->resource);
}

int devlink_dl_resource_list_parse(struct ynl_parse_arg *yarg,
				   const struct nlattr *nested)
{
	struct devlink_dl_resource_list *dst = yarg->data;
	unsigned int n_resource = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->resource)
		return ynl_error_parse(yarg, "attribute already present (dl-resource-list.resource)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RESOURCE) {
			n_resource++;
		}
	}

	if (n_resource) {
		dst->resource = calloc(n_resource, sizeof(*dst->resource));
		dst->n_resource = n_resource;
		i = 0;
		parg.rsp_policy = &devlink_dl_resource_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RESOURCE) {
				parg.data = &dst->resource[i];
				if (devlink_dl_resource_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_reload_act_info_free(struct devlink_dl_reload_act_info *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_action_stats; i++)
		devlink_dl_reload_act_stats_free(&obj->reload_action_stats[i]);
	free(obj->reload_action_stats);
}

int devlink_dl_reload_act_info_parse(struct ynl_parse_arg *yarg,
				     const struct nlattr *nested)
{
	struct devlink_dl_reload_act_info *dst = yarg->data;
	unsigned int n_reload_action_stats = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_action_stats)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-act-info.reload-action-stats)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_ACTION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_action = 1;
			dst->reload_action = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RELOAD_ACTION_STATS) {
			n_reload_action_stats++;
		}
	}

	if (n_reload_action_stats) {
		dst->reload_action_stats = calloc(n_reload_action_stats, sizeof(*dst->reload_action_stats));
		dst->n_reload_action_stats = n_reload_action_stats;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_act_stats_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_ACTION_STATS) {
				parg.data = &dst->reload_action_stats[i];
				if (devlink_dl_reload_act_stats_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_table_free(struct devlink_dl_dpipe_table *obj)
{
	free(obj->dpipe_table_name);
	devlink_dl_dpipe_table_matches_free(&obj->dpipe_table_matches);
	devlink_dl_dpipe_table_actions_free(&obj->dpipe_table_actions);
}

int devlink_dl_dpipe_table_parse(struct ynl_parse_arg *yarg,
				 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_table *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_TABLE_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dpipe_table_name_len = len;
			dst->dpipe_table_name = malloc(len + 1);
			memcpy(dst->dpipe_table_name, mnl_attr_get_str(attr), len);
			dst->dpipe_table_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_size = 1;
			dst->dpipe_table_size = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_MATCHES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_matches = 1;

			parg.rsp_policy = &devlink_dl_dpipe_table_matches_nest;
			parg.data = &dst->dpipe_table_matches;
			if (devlink_dl_dpipe_table_matches_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_ACTIONS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_actions = 1;

			parg.rsp_policy = &devlink_dl_dpipe_table_actions_nest;
			parg.data = &dst->dpipe_table_actions;
			if (devlink_dl_dpipe_table_actions_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_counters_enabled = 1;
			dst->dpipe_table_counters_enabled = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_resource_id = 1;
			dst->dpipe_table_resource_id = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_UNITS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_table_resource_units = 1;
			dst->dpipe_table_resource_units = mnl_attr_get_u64(attr);
		}
	}

	return 0;
}

void devlink_dl_dpipe_entry_free(struct devlink_dl_dpipe_entry *obj)
{
	devlink_dl_dpipe_entry_match_values_free(&obj->dpipe_entry_match_values);
	devlink_dl_dpipe_entry_action_values_free(&obj->dpipe_entry_action_values);
}

int devlink_dl_dpipe_entry_parse(struct ynl_parse_arg *yarg,
				 const struct nlattr *nested)
{
	struct devlink_dl_dpipe_entry *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ENTRY_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_entry_index = 1;
			dst->dpipe_entry_index = mnl_attr_get_u64(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_ENTRY_MATCH_VALUES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_entry_match_values = 1;

			parg.rsp_policy = &devlink_dl_dpipe_entry_match_values_nest;
			parg.data = &dst->dpipe_entry_match_values;
			if (devlink_dl_dpipe_entry_match_values_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_DPIPE_ENTRY_ACTION_VALUES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_entry_action_values = 1;

			parg.rsp_policy = &devlink_dl_dpipe_entry_action_values_nest;
			parg.data = &dst->dpipe_entry_action_values;
			if (devlink_dl_dpipe_entry_action_values_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_DPIPE_ENTRY_COUNTER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_entry_counter = 1;
			dst->dpipe_entry_counter = mnl_attr_get_u64(attr);
		}
	}

	return 0;
}

void devlink_dl_dpipe_header_free(struct devlink_dl_dpipe_header *obj)
{
	free(obj->dpipe_header_name);
	devlink_dl_dpipe_header_fields_free(&obj->dpipe_header_fields);
}

int devlink_dl_dpipe_header_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_dpipe_header *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_HEADER_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dpipe_header_name_len = len;
			dst->dpipe_header_name = malloc(len + 1);
			memcpy(dst->dpipe_header_name, mnl_attr_get_str(attr), len);
			dst->dpipe_header_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_id = 1;
			dst->dpipe_header_id = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_GLOBAL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_global = 1;
			dst->dpipe_header_global = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DPIPE_HEADER_FIELDS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_header_fields = 1;

			parg.rsp_policy = &devlink_dl_dpipe_header_fields_nest;
			parg.data = &dst->dpipe_header_fields;
			if (devlink_dl_dpipe_header_fields_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

void devlink_dl_reload_stats_free(struct devlink_dl_reload_stats *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_action_info; i++)
		devlink_dl_reload_act_info_free(&obj->reload_action_info[i]);
	free(obj->reload_action_info);
}

int devlink_dl_reload_stats_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_reload_stats *dst = yarg->data;
	unsigned int n_reload_action_info = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_action_info)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-stats.reload-action-info)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_ACTION_INFO) {
			n_reload_action_info++;
		}
	}

	if (n_reload_action_info) {
		dst->reload_action_info = calloc(n_reload_action_info, sizeof(*dst->reload_action_info));
		dst->n_reload_action_info = n_reload_action_info;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_act_info_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_ACTION_INFO) {
				parg.data = &dst->reload_action_info[i];
				if (devlink_dl_reload_act_info_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_tables_free(struct devlink_dl_dpipe_tables *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_table; i++)
		devlink_dl_dpipe_table_free(&obj->dpipe_table[i]);
	free(obj->dpipe_table);
}

int devlink_dl_dpipe_tables_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_dpipe_tables *dst = yarg->data;
	unsigned int n_dpipe_table = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_table)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-tables.dpipe-table)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_TABLE) {
			n_dpipe_table++;
		}
	}

	if (n_dpipe_table) {
		dst->dpipe_table = calloc(n_dpipe_table, sizeof(*dst->dpipe_table));
		dst->n_dpipe_table = n_dpipe_table;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_table_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_TABLE) {
				parg.data = &dst->dpipe_table[i];
				if (devlink_dl_dpipe_table_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_entries_free(struct devlink_dl_dpipe_entries *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_entry; i++)
		devlink_dl_dpipe_entry_free(&obj->dpipe_entry[i]);
	free(obj->dpipe_entry);
}

int devlink_dl_dpipe_entries_parse(struct ynl_parse_arg *yarg,
				   const struct nlattr *nested)
{
	struct devlink_dl_dpipe_entries *dst = yarg->data;
	unsigned int n_dpipe_entry = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_entry)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-entries.dpipe-entry)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_ENTRY) {
			n_dpipe_entry++;
		}
	}

	if (n_dpipe_entry) {
		dst->dpipe_entry = calloc(n_dpipe_entry, sizeof(*dst->dpipe_entry));
		dst->n_dpipe_entry = n_dpipe_entry;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_entry_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_ENTRY) {
				parg.data = &dst->dpipe_entry[i];
				if (devlink_dl_dpipe_entry_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dpipe_headers_free(struct devlink_dl_dpipe_headers *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_dpipe_header; i++)
		devlink_dl_dpipe_header_free(&obj->dpipe_header[i]);
	free(obj->dpipe_header);
}

int devlink_dl_dpipe_headers_parse(struct ynl_parse_arg *yarg,
				   const struct nlattr *nested)
{
	struct devlink_dl_dpipe_headers *dst = yarg->data;
	unsigned int n_dpipe_header = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->dpipe_header)
		return ynl_error_parse(yarg, "attribute already present (dl-dpipe-headers.dpipe-header)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_DPIPE_HEADER) {
			n_dpipe_header++;
		}
	}

	if (n_dpipe_header) {
		dst->dpipe_header = calloc(n_dpipe_header, sizeof(*dst->dpipe_header));
		dst->n_dpipe_header = n_dpipe_header;
		i = 0;
		parg.rsp_policy = &devlink_dl_dpipe_header_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_DPIPE_HEADER) {
				parg.data = &dst->dpipe_header[i];
				if (devlink_dl_dpipe_header_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dev_stats_free(struct devlink_dl_dev_stats *obj)
{
	devlink_dl_reload_stats_free(&obj->reload_stats);
	devlink_dl_reload_stats_free(&obj->remote_reload_stats);
}

int devlink_dl_dev_stats_parse(struct ynl_parse_arg *yarg,
			       const struct nlattr *nested)
{
	struct devlink_dl_dev_stats *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats = 1;

			parg.rsp_policy = &devlink_dl_reload_stats_nest;
			parg.data = &dst->reload_stats;
			if (devlink_dl_reload_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_REMOTE_RELOAD_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.remote_reload_stats = 1;

			parg.rsp_policy = &devlink_dl_reload_stats_nest;
			parg.data = &dst->remote_reload_stats;
			if (devlink_dl_reload_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

/* ============== DEVLINK_CMD_GET ============== */
/* DEVLINK_CMD_GET - do */
void devlink_get_req_free(struct devlink_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_get_rsp_free(struct devlink_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_dev_stats_free(&rsp->dev_stats);
	free(rsp);
}

int devlink_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_RELOAD_FAILED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_failed = 1;
			dst->reload_failed = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DEV_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dev_stats = 1;

			parg.rsp_policy = &devlink_dl_dev_stats_nest;
			parg.data = &dst->dev_stats;
			if (devlink_dl_dev_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_get_rsp *
devlink_get(struct ynl_sock *ys, struct devlink_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_get_rsp_parse;
	yrs.rsp_cmd = 3;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_GET - dump */
void devlink_get_list_free(struct devlink_get_list *rsp)
{
	struct devlink_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		devlink_dl_dev_stats_free(&rsp->obj.dev_stats);
		free(rsp);
	}
}

struct devlink_get_list *devlink_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_get_list);
	yds.cb = devlink_get_rsp_parse;
	yds.rsp_cmd = 3;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_GET ============== */
/* DEVLINK_CMD_PORT_GET - do */
void devlink_port_get_req_free(struct devlink_port_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_port_get_rsp_free(struct devlink_port_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_port_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_port_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_port_get_rsp *
devlink_port_get(struct ynl_sock *ys, struct devlink_port_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_port_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_port_get_rsp_parse;
	yrs.rsp_cmd = 7;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_port_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_PORT_GET - dump */
int devlink_port_get_rsp_dump_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_port_get_rsp_dump *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

void devlink_port_get_rsp_list_free(struct devlink_port_get_rsp_list *rsp)
{
	struct devlink_port_get_rsp_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_port_get_rsp_list *
devlink_port_get_dump(struct ynl_sock *ys,
		      struct devlink_port_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_port_get_rsp_list);
	yds.cb = devlink_port_get_rsp_dump_parse;
	yds.rsp_cmd = 7;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_PORT_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_port_get_rsp_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_SET ============== */
/* DEVLINK_CMD_PORT_SET - do */
void devlink_port_set_req_free(struct devlink_port_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	devlink_dl_port_function_free(&req->port_function);
	free(req);
}

int devlink_port_set(struct ynl_sock *ys, struct devlink_port_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.port_type)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_PORT_TYPE, req->port_type);
	if (req->_present.port_function)
		devlink_dl_port_function_put(nlh, DEVLINK_ATTR_PORT_FUNCTION, &req->port_function);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_PORT_NEW ============== */
/* DEVLINK_CMD_PORT_NEW - do */
void devlink_port_new_req_free(struct devlink_port_new_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_port_new_rsp_free(struct devlink_port_new_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_port_new_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_port_new_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_port_new_rsp *
devlink_port_new(struct ynl_sock *ys, struct devlink_port_new_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_port_new_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_NEW, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.port_flavour)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_PORT_FLAVOUR, req->port_flavour);
	if (req->_present.port_pci_pf_number)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_PORT_PCI_PF_NUMBER, req->port_pci_pf_number);
	if (req->_present.port_pci_sf_number)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_PCI_SF_NUMBER, req->port_pci_sf_number);
	if (req->_present.port_controller_number)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER, req->port_controller_number);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_port_new_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_PORT_NEW;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_port_new_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_DEL ============== */
/* DEVLINK_CMD_PORT_DEL - do */
void devlink_port_del_req_free(struct devlink_port_del_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_port_del(struct ynl_sock *ys, struct devlink_port_del_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_DEL, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_PORT_SPLIT ============== */
/* DEVLINK_CMD_PORT_SPLIT - do */
void devlink_port_split_req_free(struct devlink_port_split_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_port_split(struct ynl_sock *ys, struct devlink_port_split_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_SPLIT, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.port_split_count)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_SPLIT_COUNT, req->port_split_count);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_PORT_UNSPLIT ============== */
/* DEVLINK_CMD_PORT_UNSPLIT - do */
void devlink_port_unsplit_req_free(struct devlink_port_unsplit_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_port_unsplit(struct ynl_sock *ys,
			 struct devlink_port_unsplit_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_UNSPLIT, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SB_GET ============== */
/* DEVLINK_CMD_SB_GET - do */
void devlink_sb_get_req_free(struct devlink_sb_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_sb_get_rsp_free(struct devlink_sb_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_sb_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_get_rsp *
devlink_sb_get(struct ynl_sock *ys, struct devlink_sb_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_get_rsp_parse;
	yrs.rsp_cmd = 13;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_GET - dump */
void devlink_sb_get_list_free(struct devlink_sb_get_list *rsp)
{
	struct devlink_sb_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_get_list *
devlink_sb_get_dump(struct ynl_sock *ys, struct devlink_sb_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_get_list);
	yds.cb = devlink_sb_get_rsp_parse;
	yds.rsp_cmd = 13;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_POOL_GET ============== */
/* DEVLINK_CMD_SB_POOL_GET - do */
void devlink_sb_pool_get_req_free(struct devlink_sb_pool_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_sb_pool_get_rsp_free(struct devlink_sb_pool_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_pool_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_sb_pool_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_index = 1;
			dst->sb_pool_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_pool_get_rsp *
devlink_sb_pool_get(struct ynl_sock *ys, struct devlink_sb_pool_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_pool_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_POOL_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_pool_get_rsp_parse;
	yrs.rsp_cmd = 17;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_pool_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_POOL_GET - dump */
void devlink_sb_pool_get_list_free(struct devlink_sb_pool_get_list *rsp)
{
	struct devlink_sb_pool_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_pool_get_list *
devlink_sb_pool_get_dump(struct ynl_sock *ys,
			 struct devlink_sb_pool_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_pool_get_list);
	yds.cb = devlink_sb_pool_get_rsp_parse;
	yds.rsp_cmd = 17;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_POOL_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_pool_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_POOL_SET ============== */
/* DEVLINK_CMD_SB_POOL_SET - do */
void devlink_sb_pool_set_req_free(struct devlink_sb_pool_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_sb_pool_set(struct ynl_sock *ys,
			struct devlink_sb_pool_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_POOL_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);
	if (req->_present.sb_pool_threshold_type)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE, req->sb_pool_threshold_type);
	if (req->_present.sb_pool_size)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_POOL_SIZE, req->sb_pool_size);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SB_PORT_POOL_GET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_GET - do */
void
devlink_sb_port_pool_get_req_free(struct devlink_sb_port_pool_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_sb_port_pool_get_rsp_free(struct devlink_sb_port_pool_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_port_pool_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_sb_port_pool_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_index = 1;
			dst->sb_pool_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_port_pool_get_rsp *
devlink_sb_port_pool_get(struct ynl_sock *ys,
			 struct devlink_sb_port_pool_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_port_pool_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_PORT_POOL_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_port_pool_get_rsp_parse;
	yrs.rsp_cmd = 21;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_port_pool_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_PORT_POOL_GET - dump */
void
devlink_sb_port_pool_get_list_free(struct devlink_sb_port_pool_get_list *rsp)
{
	struct devlink_sb_port_pool_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_port_pool_get_list *
devlink_sb_port_pool_get_dump(struct ynl_sock *ys,
			      struct devlink_sb_port_pool_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_port_pool_get_list);
	yds.cb = devlink_sb_port_pool_get_rsp_parse;
	yds.rsp_cmd = 21;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_PORT_POOL_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_port_pool_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_PORT_POOL_SET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_SET - do */
void
devlink_sb_port_pool_set_req_free(struct devlink_sb_port_pool_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_sb_port_pool_set(struct ynl_sock *ys,
			     struct devlink_sb_port_pool_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_PORT_POOL_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);
	if (req->_present.sb_threshold)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_THRESHOLD, req->sb_threshold);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_GET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - do */
void
devlink_sb_tc_pool_bind_get_req_free(struct devlink_sb_tc_pool_bind_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_sb_tc_pool_bind_get_rsp_free(struct devlink_sb_tc_pool_bind_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_tc_pool_bind_get_rsp_parse(const struct nlmsghdr *nlh,
					  void *data)
{
	struct devlink_sb_tc_pool_bind_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_type = 1;
			dst->sb_pool_type = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_SB_TC_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_tc_index = 1;
			dst->sb_tc_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_tc_pool_bind_get_rsp *
devlink_sb_tc_pool_bind_get(struct ynl_sock *ys,
			    struct devlink_sb_tc_pool_bind_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_tc_pool_bind_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_TC_POOL_BIND_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_type)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_SB_POOL_TYPE, req->sb_pool_type);
	if (req->_present.sb_tc_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_TC_INDEX, req->sb_tc_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_tc_pool_bind_get_rsp_parse;
	yrs.rsp_cmd = 25;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_tc_pool_bind_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - dump */
void
devlink_sb_tc_pool_bind_get_list_free(struct devlink_sb_tc_pool_bind_get_list *rsp)
{
	struct devlink_sb_tc_pool_bind_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_tc_pool_bind_get_list *
devlink_sb_tc_pool_bind_get_dump(struct ynl_sock *ys,
				 struct devlink_sb_tc_pool_bind_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_tc_pool_bind_get_list);
	yds.cb = devlink_sb_tc_pool_bind_get_rsp_parse;
	yds.rsp_cmd = 25;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_TC_POOL_BIND_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_tc_pool_bind_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_SET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_SET - do */
void
devlink_sb_tc_pool_bind_set_req_free(struct devlink_sb_tc_pool_bind_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_sb_tc_pool_bind_set(struct ynl_sock *ys,
				struct devlink_sb_tc_pool_bind_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_TC_POOL_BIND_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);
	if (req->_present.sb_pool_type)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_SB_POOL_TYPE, req->sb_pool_type);
	if (req->_present.sb_tc_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_TC_INDEX, req->sb_tc_index);
	if (req->_present.sb_threshold)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_THRESHOLD, req->sb_threshold);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SB_OCC_SNAPSHOT ============== */
/* DEVLINK_CMD_SB_OCC_SNAPSHOT - do */
void devlink_sb_occ_snapshot_req_free(struct devlink_sb_occ_snapshot_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_sb_occ_snapshot(struct ynl_sock *ys,
			    struct devlink_sb_occ_snapshot_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_OCC_SNAPSHOT, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SB_OCC_MAX_CLEAR ============== */
/* DEVLINK_CMD_SB_OCC_MAX_CLEAR - do */
void
devlink_sb_occ_max_clear_req_free(struct devlink_sb_occ_max_clear_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_sb_occ_max_clear(struct ynl_sock *ys,
			     struct devlink_sb_occ_max_clear_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_OCC_MAX_CLEAR, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_ESWITCH_GET ============== */
/* DEVLINK_CMD_ESWITCH_GET - do */
void devlink_eswitch_get_req_free(struct devlink_eswitch_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_eswitch_get_rsp_free(struct devlink_eswitch_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_eswitch_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_eswitch_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_ESWITCH_MODE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.eswitch_mode = 1;
			dst->eswitch_mode = mnl_attr_get_u16(attr);
		} else if (type == DEVLINK_ATTR_ESWITCH_INLINE_MODE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.eswitch_inline_mode = 1;
			dst->eswitch_inline_mode = mnl_attr_get_u16(attr);
		} else if (type == DEVLINK_ATTR_ESWITCH_ENCAP_MODE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.eswitch_encap_mode = 1;
			dst->eswitch_encap_mode = mnl_attr_get_u8(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_eswitch_get_rsp *
devlink_eswitch_get(struct ynl_sock *ys, struct devlink_eswitch_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_eswitch_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_ESWITCH_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_eswitch_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_ESWITCH_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_eswitch_get_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_ESWITCH_SET ============== */
/* DEVLINK_CMD_ESWITCH_SET - do */
void devlink_eswitch_set_req_free(struct devlink_eswitch_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_eswitch_set(struct ynl_sock *ys,
			struct devlink_eswitch_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_ESWITCH_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.eswitch_mode)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_ESWITCH_MODE, req->eswitch_mode);
	if (req->_present.eswitch_inline_mode)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_ESWITCH_INLINE_MODE, req->eswitch_inline_mode);
	if (req->_present.eswitch_encap_mode)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_ESWITCH_ENCAP_MODE, req->eswitch_encap_mode);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_DPIPE_TABLE_GET ============== */
/* DEVLINK_CMD_DPIPE_TABLE_GET - do */
void devlink_dpipe_table_get_req_free(struct devlink_dpipe_table_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->dpipe_table_name);
	free(req);
}

void devlink_dpipe_table_get_rsp_free(struct devlink_dpipe_table_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_dpipe_tables_free(&rsp->dpipe_tables);
	free(rsp);
}

int devlink_dpipe_table_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_dpipe_table_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_TABLES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_tables = 1;

			parg.rsp_policy = &devlink_dl_dpipe_tables_nest;
			parg.data = &dst->dpipe_tables;
			if (devlink_dl_dpipe_tables_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_dpipe_table_get_rsp *
devlink_dpipe_table_get(struct ynl_sock *ys,
			struct devlink_dpipe_table_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_dpipe_table_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_DPIPE_TABLE_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.dpipe_table_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DPIPE_TABLE_NAME, req->dpipe_table_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_dpipe_table_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_DPIPE_TABLE_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_dpipe_table_get_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_DPIPE_ENTRIES_GET ============== */
/* DEVLINK_CMD_DPIPE_ENTRIES_GET - do */
void
devlink_dpipe_entries_get_req_free(struct devlink_dpipe_entries_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->dpipe_table_name);
	free(req);
}

void
devlink_dpipe_entries_get_rsp_free(struct devlink_dpipe_entries_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_dpipe_entries_free(&rsp->dpipe_entries);
	free(rsp);
}

int devlink_dpipe_entries_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_dpipe_entries_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_ENTRIES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_entries = 1;

			parg.rsp_policy = &devlink_dl_dpipe_entries_nest;
			parg.data = &dst->dpipe_entries;
			if (devlink_dl_dpipe_entries_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_dpipe_entries_get_rsp *
devlink_dpipe_entries_get(struct ynl_sock *ys,
			  struct devlink_dpipe_entries_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_dpipe_entries_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_DPIPE_ENTRIES_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.dpipe_table_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DPIPE_TABLE_NAME, req->dpipe_table_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_dpipe_entries_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_DPIPE_ENTRIES_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_dpipe_entries_get_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_DPIPE_HEADERS_GET ============== */
/* DEVLINK_CMD_DPIPE_HEADERS_GET - do */
void
devlink_dpipe_headers_get_req_free(struct devlink_dpipe_headers_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_dpipe_headers_get_rsp_free(struct devlink_dpipe_headers_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_dpipe_headers_free(&rsp->dpipe_headers);
	free(rsp);
}

int devlink_dpipe_headers_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_dpipe_headers_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DPIPE_HEADERS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dpipe_headers = 1;

			parg.rsp_policy = &devlink_dl_dpipe_headers_nest;
			parg.data = &dst->dpipe_headers;
			if (devlink_dl_dpipe_headers_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_dpipe_headers_get_rsp *
devlink_dpipe_headers_get(struct ynl_sock *ys,
			  struct devlink_dpipe_headers_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_dpipe_headers_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_DPIPE_HEADERS_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_dpipe_headers_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_DPIPE_HEADERS_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_dpipe_headers_get_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET ============== */
/* DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET - do */
void
devlink_dpipe_table_counters_set_req_free(struct devlink_dpipe_table_counters_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->dpipe_table_name);
	free(req);
}

int devlink_dpipe_table_counters_set(struct ynl_sock *ys,
				     struct devlink_dpipe_table_counters_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.dpipe_table_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DPIPE_TABLE_NAME, req->dpipe_table_name);
	if (req->_present.dpipe_table_counters_enabled)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED, req->dpipe_table_counters_enabled);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_RESOURCE_SET ============== */
/* DEVLINK_CMD_RESOURCE_SET - do */
void devlink_resource_set_req_free(struct devlink_resource_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_resource_set(struct ynl_sock *ys,
			 struct devlink_resource_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RESOURCE_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.resource_id)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RESOURCE_ID, req->resource_id);
	if (req->_present.resource_size)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RESOURCE_SIZE, req->resource_size);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_RESOURCE_DUMP ============== */
/* DEVLINK_CMD_RESOURCE_DUMP - do */
void devlink_resource_dump_req_free(struct devlink_resource_dump_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_resource_dump_rsp_free(struct devlink_resource_dump_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_resource_list_free(&rsp->resource_list);
	free(rsp);
}

int devlink_resource_dump_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_resource_dump_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_RESOURCE_LIST) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.resource_list = 1;

			parg.rsp_policy = &devlink_dl_resource_list_nest;
			parg.data = &dst->resource_list;
			if (devlink_dl_resource_list_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_resource_dump_rsp *
devlink_resource_dump(struct ynl_sock *ys,
		      struct devlink_resource_dump_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_resource_dump_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RESOURCE_DUMP, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_resource_dump_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_RESOURCE_DUMP;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_resource_dump_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_RELOAD ============== */
/* DEVLINK_CMD_RELOAD - do */
void devlink_reload_req_free(struct devlink_reload_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_reload_rsp_free(struct devlink_reload_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_reload_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_reload_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_RELOAD_ACTIONS_PERFORMED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_actions_performed = 1;
			memcpy(&dst->reload_actions_performed, mnl_attr_get_payload(attr), sizeof(struct nla_bitfield32));
		}
	}

	return MNL_CB_OK;
}

struct devlink_reload_rsp *
devlink_reload(struct ynl_sock *ys, struct devlink_reload_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_reload_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RELOAD, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.reload_action)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_RELOAD_ACTION, req->reload_action);
	if (req->_present.reload_limits)
		mnl_attr_put(nlh, DEVLINK_ATTR_RELOAD_LIMITS, sizeof(struct nla_bitfield32), &req->reload_limits);
	if (req->_present.netns_pid)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_NETNS_PID, req->netns_pid);
	if (req->_present.netns_fd)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_NETNS_FD, req->netns_fd);
	if (req->_present.netns_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_NETNS_ID, req->netns_id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_reload_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_RELOAD;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_reload_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_PARAM_GET ============== */
/* DEVLINK_CMD_PARAM_GET - do */
void devlink_param_get_req_free(struct devlink_param_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->param_name);
	free(req);
}

void devlink_param_get_rsp_free(struct devlink_param_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->param_name);
	free(rsp);
}

int devlink_param_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_param_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PARAM_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.param_name_len = len;
			dst->param_name = malloc(len + 1);
			memcpy(dst->param_name, mnl_attr_get_str(attr), len);
			dst->param_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_param_get_rsp *
devlink_param_get(struct ynl_sock *ys, struct devlink_param_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_param_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PARAM_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.param_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_PARAM_NAME, req->param_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_param_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_PARAM_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_param_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_PARAM_GET - dump */
void devlink_param_get_list_free(struct devlink_param_get_list *rsp)
{
	struct devlink_param_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.param_name);
		free(rsp);
	}
}

struct devlink_param_get_list *
devlink_param_get_dump(struct ynl_sock *ys,
		       struct devlink_param_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_param_get_list);
	yds.cb = devlink_param_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_PARAM_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_PARAM_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_param_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PARAM_SET ============== */
/* DEVLINK_CMD_PARAM_SET - do */
void devlink_param_set_req_free(struct devlink_param_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->param_name);
	free(req);
}

int devlink_param_set(struct ynl_sock *ys, struct devlink_param_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PARAM_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.param_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_PARAM_NAME, req->param_name);
	if (req->_present.param_type)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_PARAM_TYPE, req->param_type);
	if (req->_present.param_value_cmode)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_PARAM_VALUE_CMODE, req->param_value_cmode);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_REGION_GET ============== */
/* DEVLINK_CMD_REGION_GET - do */
void devlink_region_get_req_free(struct devlink_region_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->region_name);
	free(req);
}

void devlink_region_get_rsp_free(struct devlink_region_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->region_name);
	free(rsp);
}

int devlink_region_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_region_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_REGION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.region_name_len = len;
			dst->region_name = malloc(len + 1);
			memcpy(dst->region_name, mnl_attr_get_str(attr), len);
			dst->region_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_region_get_rsp *
devlink_region_get(struct ynl_sock *ys, struct devlink_region_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_region_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_REGION_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.region_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, req->region_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_region_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_REGION_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_region_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_REGION_GET - dump */
void devlink_region_get_list_free(struct devlink_region_get_list *rsp)
{
	struct devlink_region_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.region_name);
		free(rsp);
	}
}

struct devlink_region_get_list *
devlink_region_get_dump(struct ynl_sock *ys,
			struct devlink_region_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_region_get_list);
	yds.cb = devlink_region_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_REGION_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_REGION_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_region_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_REGION_NEW ============== */
/* DEVLINK_CMD_REGION_NEW - do */
void devlink_region_new_req_free(struct devlink_region_new_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->region_name);
	free(req);
}

void devlink_region_new_rsp_free(struct devlink_region_new_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->region_name);
	free(rsp);
}

int devlink_region_new_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_region_new_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_REGION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.region_name_len = len;
			dst->region_name = malloc(len + 1);
			memcpy(dst->region_name, mnl_attr_get_str(attr), len);
			dst->region_name[len] = 0;
		} else if (type == DEVLINK_ATTR_REGION_SNAPSHOT_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.region_snapshot_id = 1;
			dst->region_snapshot_id = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_region_new_rsp *
devlink_region_new(struct ynl_sock *ys, struct devlink_region_new_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_region_new_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_REGION_NEW, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.region_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, req->region_name);
	if (req->_present.region_snapshot_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_REGION_SNAPSHOT_ID, req->region_snapshot_id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_region_new_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_REGION_NEW;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_region_new_rsp_free(rsp);
	return NULL;
}

/* ============== DEVLINK_CMD_REGION_DEL ============== */
/* DEVLINK_CMD_REGION_DEL - do */
void devlink_region_del_req_free(struct devlink_region_del_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->region_name);
	free(req);
}

int devlink_region_del(struct ynl_sock *ys, struct devlink_region_del_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_REGION_DEL, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.region_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, req->region_name);
	if (req->_present.region_snapshot_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_REGION_SNAPSHOT_ID, req->region_snapshot_id);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_REGION_READ ============== */
/* DEVLINK_CMD_REGION_READ - dump */
int devlink_region_read_rsp_dump_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_region_read_rsp_dump *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_REGION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.region_name_len = len;
			dst->region_name = malloc(len + 1);
			memcpy(dst->region_name, mnl_attr_get_str(attr), len);
			dst->region_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

void
devlink_region_read_rsp_list_free(struct devlink_region_read_rsp_list *rsp)
{
	struct devlink_region_read_rsp_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.region_name);
		free(rsp);
	}
}

struct devlink_region_read_rsp_list *
devlink_region_read_dump(struct ynl_sock *ys,
			 struct devlink_region_read_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_region_read_rsp_list);
	yds.cb = devlink_region_read_rsp_dump_parse;
	yds.rsp_cmd = DEVLINK_CMD_REGION_READ;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_REGION_READ, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.region_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, req->region_name);
	if (req->_present.region_snapshot_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_REGION_SNAPSHOT_ID, req->region_snapshot_id);
	if (req->_present.region_direct)
		mnl_attr_put(nlh, DEVLINK_ATTR_REGION_DIRECT, 0, NULL);
	if (req->_present.region_chunk_addr)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_REGION_CHUNK_ADDR, req->region_chunk_addr);
	if (req->_present.region_chunk_len)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_REGION_CHUNK_LEN, req->region_chunk_len);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_region_read_rsp_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_PARAM_GET ============== */
/* DEVLINK_CMD_PORT_PARAM_GET - do */
void devlink_port_param_get_req_free(struct devlink_port_param_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_port_param_get_rsp_free(struct devlink_port_param_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_port_param_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_port_param_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_port_param_get_rsp *
devlink_port_param_get(struct ynl_sock *ys,
		       struct devlink_port_param_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_port_param_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_PARAM_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_port_param_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_PORT_PARAM_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_port_param_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_PORT_PARAM_GET - dump */
void devlink_port_param_get_list_free(struct devlink_port_param_get_list *rsp)
{
	struct devlink_port_param_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_port_param_get_list *
devlink_port_param_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_port_param_get_list);
	yds.cb = devlink_port_param_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_PORT_PARAM_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_PORT_PARAM_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_port_param_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_PARAM_SET ============== */
/* DEVLINK_CMD_PORT_PARAM_SET - do */
void devlink_port_param_set_req_free(struct devlink_port_param_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_port_param_set(struct ynl_sock *ys,
			   struct devlink_port_param_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_PARAM_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_INFO_GET ============== */
/* DEVLINK_CMD_INFO_GET - do */
void devlink_info_get_req_free(struct devlink_info_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_info_get_rsp_free(struct devlink_info_get_rsp *rsp)
{
	unsigned int i;

	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->info_driver_name);
	free(rsp->info_serial_number);
	for (i = 0; i < rsp->n_info_version_fixed; i++)
		devlink_dl_info_version_free(&rsp->info_version_fixed[i]);
	free(rsp->info_version_fixed);
	for (i = 0; i < rsp->n_info_version_running; i++)
		devlink_dl_info_version_free(&rsp->info_version_running[i]);
	free(rsp->info_version_running);
	for (i = 0; i < rsp->n_info_version_stored; i++)
		devlink_dl_info_version_free(&rsp->info_version_stored[i]);
	free(rsp->info_version_stored);
	free(rsp);
}

int devlink_info_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	unsigned int n_info_version_running = 0;
	unsigned int n_info_version_stored = 0;
	unsigned int n_info_version_fixed = 0;
	struct ynl_parse_arg *yarg = data;
	struct devlink_info_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	dst = yarg->data;
	parg.ys = yarg->ys;

	if (dst->info_version_fixed)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-fixed)");
	if (dst->info_version_running)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-running)");
	if (dst->info_version_stored)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-stored)");

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_DRIVER_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_driver_name_len = len;
			dst->info_driver_name = malloc(len + 1);
			memcpy(dst->info_driver_name, mnl_attr_get_str(attr), len);
			dst->info_driver_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_SERIAL_NUMBER) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_serial_number_len = len;
			dst->info_serial_number = malloc(len + 1);
			memcpy(dst->info_serial_number, mnl_attr_get_str(attr), len);
			dst->info_serial_number[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_FIXED) {
			n_info_version_fixed++;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_RUNNING) {
			n_info_version_running++;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_STORED) {
			n_info_version_stored++;
		}
	}

	if (n_info_version_fixed) {
		dst->info_version_fixed = calloc(n_info_version_fixed, sizeof(*dst->info_version_fixed));
		dst->n_info_version_fixed = n_info_version_fixed;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_FIXED) {
				parg.data = &dst->info_version_fixed[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}
	if (n_info_version_running) {
		dst->info_version_running = calloc(n_info_version_running, sizeof(*dst->info_version_running));
		dst->n_info_version_running = n_info_version_running;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_RUNNING) {
				parg.data = &dst->info_version_running[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}
	if (n_info_version_stored) {
		dst->info_version_stored = calloc(n_info_version_stored, sizeof(*dst->info_version_stored));
		dst->n_info_version_stored = n_info_version_stored;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_STORED) {
				parg.data = &dst->info_version_stored[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return MNL_CB_OK;
}

struct devlink_info_get_rsp *
devlink_info_get(struct ynl_sock *ys, struct devlink_info_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_info_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_INFO_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_info_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_INFO_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_info_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_INFO_GET - dump */
void devlink_info_get_list_free(struct devlink_info_get_list *rsp)
{
	struct devlink_info_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		unsigned int i;

		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.info_driver_name);
		free(rsp->obj.info_serial_number);
		for (i = 0; i < rsp->obj.n_info_version_fixed; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_fixed[i]);
		free(rsp->obj.info_version_fixed);
		for (i = 0; i < rsp->obj.n_info_version_running; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_running[i]);
		free(rsp->obj.info_version_running);
		for (i = 0; i < rsp->obj.n_info_version_stored; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_stored[i]);
		free(rsp->obj.info_version_stored);
		free(rsp);
	}
}

struct devlink_info_get_list *devlink_info_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_info_get_list);
	yds.cb = devlink_info_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_INFO_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_INFO_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_info_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_GET - do */
void
devlink_health_reporter_get_req_free(struct devlink_health_reporter_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

void
devlink_health_reporter_get_rsp_free(struct devlink_health_reporter_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->health_reporter_name);
	free(rsp);
}

int devlink_health_reporter_get_rsp_parse(const struct nlmsghdr *nlh,
					  void *data)
{
	struct devlink_health_reporter_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_HEALTH_REPORTER_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.health_reporter_name_len = len;
			dst->health_reporter_name = malloc(len + 1);
			memcpy(dst->health_reporter_name, mnl_attr_get_str(attr), len);
			dst->health_reporter_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_health_reporter_get_rsp *
devlink_health_reporter_get(struct ynl_sock *ys,
			    struct devlink_health_reporter_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_health_reporter_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_health_reporter_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_HEALTH_REPORTER_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_health_reporter_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_HEALTH_REPORTER_GET - dump */
void
devlink_health_reporter_get_list_free(struct devlink_health_reporter_get_list *rsp)
{
	struct devlink_health_reporter_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.health_reporter_name);
		free(rsp);
	}
}

struct devlink_health_reporter_get_list *
devlink_health_reporter_get_dump(struct ynl_sock *ys,
				 struct devlink_health_reporter_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_health_reporter_get_list);
	yds.cb = devlink_health_reporter_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_HEALTH_REPORTER_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_health_reporter_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_SET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_SET - do */
void
devlink_health_reporter_set_req_free(struct devlink_health_reporter_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

int devlink_health_reporter_set(struct ynl_sock *ys,
				struct devlink_health_reporter_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);
	if (req->_present.health_reporter_graceful_period)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD, req->health_reporter_graceful_period);
	if (req->_present.health_reporter_auto_recover)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER, req->health_reporter_auto_recover);
	if (req->_present.health_reporter_auto_dump)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP, req->health_reporter_auto_dump);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_RECOVER ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_RECOVER - do */
void
devlink_health_reporter_recover_req_free(struct devlink_health_reporter_recover_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

int devlink_health_reporter_recover(struct ynl_sock *ys,
				    struct devlink_health_reporter_recover_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_RECOVER, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE - do */
void
devlink_health_reporter_diagnose_req_free(struct devlink_health_reporter_diagnose_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

int devlink_health_reporter_diagnose(struct ynl_sock *ys,
				     struct devlink_health_reporter_diagnose_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET - dump */
int devlink_health_reporter_dump_get_rsp_dump_parse(const struct nlmsghdr *nlh,
						    void *data)
{
	struct devlink_health_reporter_dump_get_rsp_dump *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_FMSG) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fmsg = 1;

			parg.rsp_policy = &devlink_dl_fmsg_nest;
			parg.data = &dst->fmsg;
			if (devlink_dl_fmsg_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

void
devlink_health_reporter_dump_get_rsp_list_free(struct devlink_health_reporter_dump_get_rsp_list *rsp)
{
	struct devlink_health_reporter_dump_get_rsp_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		devlink_dl_fmsg_free(&rsp->obj.fmsg);
		free(rsp);
	}
}

struct devlink_health_reporter_dump_get_rsp_list *
devlink_health_reporter_dump_get_dump(struct ynl_sock *ys,
				      struct devlink_health_reporter_dump_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_health_reporter_dump_get_rsp_list);
	yds.cb = devlink_health_reporter_dump_get_rsp_dump_parse;
	yds.rsp_cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_health_reporter_dump_get_rsp_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR - do */
void
devlink_health_reporter_dump_clear_req_free(struct devlink_health_reporter_dump_clear_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

int devlink_health_reporter_dump_clear(struct ynl_sock *ys,
				       struct devlink_health_reporter_dump_clear_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_FLASH_UPDATE ============== */
/* DEVLINK_CMD_FLASH_UPDATE - do */
void devlink_flash_update_req_free(struct devlink_flash_update_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->flash_update_file_name);
	free(req->flash_update_component);
	free(req);
}

int devlink_flash_update(struct ynl_sock *ys,
			 struct devlink_flash_update_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_FLASH_UPDATE, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.flash_update_file_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME, req->flash_update_file_name);
	if (req->_present.flash_update_component_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_FLASH_UPDATE_COMPONENT, req->flash_update_component);
	if (req->_present.flash_update_overwrite_mask)
		mnl_attr_put(nlh, DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK, sizeof(struct nla_bitfield32), &req->flash_update_overwrite_mask);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_TRAP_GET ============== */
/* DEVLINK_CMD_TRAP_GET - do */
void devlink_trap_get_req_free(struct devlink_trap_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_name);
	free(req);
}

void devlink_trap_get_rsp_free(struct devlink_trap_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->trap_name);
	free(rsp);
}

int devlink_trap_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_trap_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.trap_name_len = len;
			dst->trap_name = malloc(len + 1);
			memcpy(dst->trap_name, mnl_attr_get_str(attr), len);
			dst->trap_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_get_rsp *
devlink_trap_get(struct ynl_sock *ys, struct devlink_trap_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_NAME, req->trap_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_get_rsp_parse;
	yrs.rsp_cmd = 63;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_GET - dump */
void devlink_trap_get_list_free(struct devlink_trap_get_list *rsp)
{
	struct devlink_trap_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.trap_name);
		free(rsp);
	}
}

struct devlink_trap_get_list *
devlink_trap_get_dump(struct ynl_sock *ys,
		      struct devlink_trap_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_get_list);
	yds.cb = devlink_trap_get_rsp_parse;
	yds.rsp_cmd = 63;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_SET ============== */
/* DEVLINK_CMD_TRAP_SET - do */
void devlink_trap_set_req_free(struct devlink_trap_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_name);
	free(req);
}

int devlink_trap_set(struct ynl_sock *ys, struct devlink_trap_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_NAME, req->trap_name);
	if (req->_present.trap_action)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_TRAP_ACTION, req->trap_action);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_TRAP_GROUP_GET ============== */
/* DEVLINK_CMD_TRAP_GROUP_GET - do */
void devlink_trap_group_get_req_free(struct devlink_trap_group_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_group_name);
	free(req);
}

void devlink_trap_group_get_rsp_free(struct devlink_trap_group_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->trap_group_name);
	free(rsp);
}

int devlink_trap_group_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_trap_group_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_GROUP_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.trap_group_name_len = len;
			dst->trap_group_name = malloc(len + 1);
			memcpy(dst->trap_group_name, mnl_attr_get_str(attr), len);
			dst->trap_group_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_group_get_rsp *
devlink_trap_group_get(struct ynl_sock *ys,
		       struct devlink_trap_group_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_group_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_GROUP_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_group_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_GROUP_NAME, req->trap_group_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_group_get_rsp_parse;
	yrs.rsp_cmd = 67;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_group_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_GROUP_GET - dump */
void devlink_trap_group_get_list_free(struct devlink_trap_group_get_list *rsp)
{
	struct devlink_trap_group_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.trap_group_name);
		free(rsp);
	}
}

struct devlink_trap_group_get_list *
devlink_trap_group_get_dump(struct ynl_sock *ys,
			    struct devlink_trap_group_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_group_get_list);
	yds.cb = devlink_trap_group_get_rsp_parse;
	yds.rsp_cmd = 67;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_GROUP_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_group_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_GROUP_SET ============== */
/* DEVLINK_CMD_TRAP_GROUP_SET - do */
void devlink_trap_group_set_req_free(struct devlink_trap_group_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_group_name);
	free(req);
}

int devlink_trap_group_set(struct ynl_sock *ys,
			   struct devlink_trap_group_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_GROUP_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_group_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_GROUP_NAME, req->trap_group_name);
	if (req->_present.trap_action)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_TRAP_ACTION, req->trap_action);
	if (req->_present.trap_policer_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_TRAP_POLICER_ID, req->trap_policer_id);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_TRAP_POLICER_GET ============== */
/* DEVLINK_CMD_TRAP_POLICER_GET - do */
void
devlink_trap_policer_get_req_free(struct devlink_trap_policer_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_trap_policer_get_rsp_free(struct devlink_trap_policer_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_trap_policer_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_trap_policer_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_POLICER_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.trap_policer_id = 1;
			dst->trap_policer_id = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_policer_get_rsp *
devlink_trap_policer_get(struct ynl_sock *ys,
			 struct devlink_trap_policer_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_policer_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_POLICER_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_policer_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_TRAP_POLICER_ID, req->trap_policer_id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_policer_get_rsp_parse;
	yrs.rsp_cmd = 71;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_policer_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_POLICER_GET - dump */
void
devlink_trap_policer_get_list_free(struct devlink_trap_policer_get_list *rsp)
{
	struct devlink_trap_policer_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_trap_policer_get_list *
devlink_trap_policer_get_dump(struct ynl_sock *ys,
			      struct devlink_trap_policer_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_policer_get_list);
	yds.cb = devlink_trap_policer_get_rsp_parse;
	yds.rsp_cmd = 71;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_POLICER_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_policer_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_POLICER_SET ============== */
/* DEVLINK_CMD_TRAP_POLICER_SET - do */
void
devlink_trap_policer_set_req_free(struct devlink_trap_policer_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

int devlink_trap_policer_set(struct ynl_sock *ys,
			     struct devlink_trap_policer_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_POLICER_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_policer_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_TRAP_POLICER_ID, req->trap_policer_id);
	if (req->_present.trap_policer_rate)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_TRAP_POLICER_RATE, req->trap_policer_rate);
	if (req->_present.trap_policer_burst)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_TRAP_POLICER_BURST, req->trap_policer_burst);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_TEST ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_TEST - do */
void
devlink_health_reporter_test_req_free(struct devlink_health_reporter_test_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

int devlink_health_reporter_test(struct ynl_sock *ys,
				 struct devlink_health_reporter_test_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_TEST, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_RATE_GET ============== */
/* DEVLINK_CMD_RATE_GET - do */
void devlink_rate_get_req_free(struct devlink_rate_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->rate_node_name);
	free(req);
}

void devlink_rate_get_rsp_free(struct devlink_rate_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->rate_node_name);
	free(rsp);
}

int devlink_rate_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_rate_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_RATE_NODE_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.rate_node_name_len = len;
			dst->rate_node_name = malloc(len + 1);
			memcpy(dst->rate_node_name, mnl_attr_get_str(attr), len);
			dst->rate_node_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_rate_get_rsp *
devlink_rate_get(struct ynl_sock *ys, struct devlink_rate_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_rate_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RATE_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.rate_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_NODE_NAME, req->rate_node_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_rate_get_rsp_parse;
	yrs.rsp_cmd = 76;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_rate_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_RATE_GET - dump */
void devlink_rate_get_list_free(struct devlink_rate_get_list *rsp)
{
	struct devlink_rate_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.rate_node_name);
		free(rsp);
	}
}

struct devlink_rate_get_list *
devlink_rate_get_dump(struct ynl_sock *ys,
		      struct devlink_rate_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_rate_get_list);
	yds.cb = devlink_rate_get_rsp_parse;
	yds.rsp_cmd = 76;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_RATE_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_rate_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_RATE_SET ============== */
/* DEVLINK_CMD_RATE_SET - do */
void devlink_rate_set_req_free(struct devlink_rate_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->rate_node_name);
	free(req->rate_parent_node_name);
	free(req);
}

int devlink_rate_set(struct ynl_sock *ys, struct devlink_rate_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RATE_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.rate_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_NODE_NAME, req->rate_node_name);
	if (req->_present.rate_tx_share)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RATE_TX_SHARE, req->rate_tx_share);
	if (req->_present.rate_tx_max)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RATE_TX_MAX, req->rate_tx_max);
	if (req->_present.rate_tx_priority)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_RATE_TX_PRIORITY, req->rate_tx_priority);
	if (req->_present.rate_tx_weight)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_RATE_TX_WEIGHT, req->rate_tx_weight);
	if (req->_present.rate_parent_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_PARENT_NODE_NAME, req->rate_parent_node_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_RATE_NEW ============== */
/* DEVLINK_CMD_RATE_NEW - do */
void devlink_rate_new_req_free(struct devlink_rate_new_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->rate_node_name);
	free(req->rate_parent_node_name);
	free(req);
}

int devlink_rate_new(struct ynl_sock *ys, struct devlink_rate_new_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RATE_NEW, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.rate_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_NODE_NAME, req->rate_node_name);
	if (req->_present.rate_tx_share)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RATE_TX_SHARE, req->rate_tx_share);
	if (req->_present.rate_tx_max)
		mnl_attr_put_u64(nlh, DEVLINK_ATTR_RATE_TX_MAX, req->rate_tx_max);
	if (req->_present.rate_tx_priority)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_RATE_TX_PRIORITY, req->rate_tx_priority);
	if (req->_present.rate_tx_weight)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_RATE_TX_WEIGHT, req->rate_tx_weight);
	if (req->_present.rate_parent_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_PARENT_NODE_NAME, req->rate_parent_node_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_RATE_DEL ============== */
/* DEVLINK_CMD_RATE_DEL - do */
void devlink_rate_del_req_free(struct devlink_rate_del_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->rate_node_name);
	free(req);
}

int devlink_rate_del(struct ynl_sock *ys, struct devlink_rate_del_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RATE_DEL, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.rate_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_NODE_NAME, req->rate_node_name);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_LINECARD_GET ============== */
/* DEVLINK_CMD_LINECARD_GET - do */
void devlink_linecard_get_req_free(struct devlink_linecard_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_linecard_get_rsp_free(struct devlink_linecard_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_linecard_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_linecard_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_LINECARD_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.linecard_index = 1;
			dst->linecard_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_linecard_get_rsp *
devlink_linecard_get(struct ynl_sock *ys, struct devlink_linecard_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_linecard_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_LINECARD_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.linecard_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_LINECARD_INDEX, req->linecard_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_linecard_get_rsp_parse;
	yrs.rsp_cmd = 80;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_linecard_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_LINECARD_GET - dump */
void devlink_linecard_get_list_free(struct devlink_linecard_get_list *rsp)
{
	struct devlink_linecard_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_linecard_get_list *
devlink_linecard_get_dump(struct ynl_sock *ys,
			  struct devlink_linecard_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_linecard_get_list);
	yds.cb = devlink_linecard_get_rsp_parse;
	yds.rsp_cmd = 80;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_LINECARD_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_linecard_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_LINECARD_SET ============== */
/* DEVLINK_CMD_LINECARD_SET - do */
void devlink_linecard_set_req_free(struct devlink_linecard_set_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->linecard_type);
	free(req);
}

int devlink_linecard_set(struct ynl_sock *ys,
			 struct devlink_linecard_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_LINECARD_SET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.linecard_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_LINECARD_INDEX, req->linecard_index);
	if (req->_present.linecard_type_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_LINECARD_TYPE, req->linecard_type);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== DEVLINK_CMD_SELFTESTS_GET ============== */
/* DEVLINK_CMD_SELFTESTS_GET - do */
void devlink_selftests_get_req_free(struct devlink_selftests_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_selftests_get_rsp_free(struct devlink_selftests_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_selftests_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_selftests_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_selftests_get_rsp *
devlink_selftests_get(struct ynl_sock *ys,
		      struct devlink_selftests_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_selftests_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SELFTESTS_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_selftests_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_SELFTESTS_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_selftests_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SELFTESTS_GET - dump */
void devlink_selftests_get_list_free(struct devlink_selftests_get_list *rsp)
{
	struct devlink_selftests_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_selftests_get_list *
devlink_selftests_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_selftests_get_list);
	yds.cb = devlink_selftests_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_SELFTESTS_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SELFTESTS_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_selftests_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SELFTESTS_RUN ============== */
/* DEVLINK_CMD_SELFTESTS_RUN - do */
void devlink_selftests_run_req_free(struct devlink_selftests_run_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	devlink_dl_selftest_id_free(&req->selftests);
	free(req);
}

int devlink_selftests_run(struct ynl_sock *ys,
			  struct devlink_selftests_run_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SELFTESTS_RUN, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.selftests)
		devlink_dl_selftest_id_put(nlh, DEVLINK_ATTR_SELFTESTS, &req->selftests);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

const struct ynl_family ynl_devlink_family =  {
	.name		= "devlink",
};

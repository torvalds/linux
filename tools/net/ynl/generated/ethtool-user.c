// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/ethtool.yaml */
/* YNL-GEN user source */
/* YNL-ARG --user-header linux/ethtool_netlink.h --exclude-op stats-get */

#include <stdlib.h>
#include <string.h>
#include "ethtool-user.h"
#include "ynl.h"
#include <linux/ethtool.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

#include "linux/ethtool_netlink.h"

/* Enums */
static const char * const ethtool_op_strmap[] = {
	[ETHTOOL_MSG_STRSET_GET] = "strset-get",
	[ETHTOOL_MSG_LINKINFO_GET] = "linkinfo-get",
	[3] = "linkinfo-ntf",
	[ETHTOOL_MSG_LINKMODES_GET] = "linkmodes-get",
	[5] = "linkmodes-ntf",
	[ETHTOOL_MSG_LINKSTATE_GET] = "linkstate-get",
	[ETHTOOL_MSG_DEBUG_GET] = "debug-get",
	[8] = "debug-ntf",
	[ETHTOOL_MSG_WOL_GET] = "wol-get",
	[10] = "wol-ntf",
	[ETHTOOL_MSG_FEATURES_GET] = "features-get",
	[ETHTOOL_MSG_FEATURES_SET] = "features-set",
	[13] = "features-ntf",
	[14] = "privflags-get",
	[15] = "privflags-ntf",
	[16] = "rings-get",
	[17] = "rings-ntf",
	[18] = "channels-get",
	[19] = "channels-ntf",
	[20] = "coalesce-get",
	[21] = "coalesce-ntf",
	[22] = "pause-get",
	[23] = "pause-ntf",
	[24] = "eee-get",
	[25] = "eee-ntf",
	[26] = "tsinfo-get",
	[27] = "cable-test-ntf",
	[28] = "cable-test-tdr-ntf",
	[29] = "tunnel-info-get",
	[30] = "fec-get",
	[31] = "fec-ntf",
	[32] = "module-eeprom-get",
	[34] = "phc-vclocks-get",
	[35] = "module-get",
	[36] = "module-ntf",
	[37] = "pse-get",
	[ETHTOOL_MSG_RSS_GET] = "rss-get",
	[ETHTOOL_MSG_PLCA_GET_CFG] = "plca-get-cfg",
	[40] = "plca-get-status",
	[41] = "plca-ntf",
	[ETHTOOL_MSG_MM_GET] = "mm-get",
	[43] = "mm-ntf",
};

const char *ethtool_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(ethtool_op_strmap))
		return NULL;
	return ethtool_op_strmap[op];
}

static const char * const ethtool_udp_tunnel_type_strmap[] = {
	[0] = "vxlan",
	[1] = "geneve",
	[2] = "vxlan-gpe",
};

const char *ethtool_udp_tunnel_type_str(int value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(ethtool_udp_tunnel_type_strmap))
		return NULL;
	return ethtool_udp_tunnel_type_strmap[value];
}

static const char * const ethtool_stringset_strmap[] = {
};

const char *ethtool_stringset_str(enum ethtool_stringset value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(ethtool_stringset_strmap))
		return NULL;
	return ethtool_stringset_strmap[value];
}

/* Policies */
struct ynl_policy_attr ethtool_header_policy[ETHTOOL_A_HEADER_MAX + 1] = {
	[ETHTOOL_A_HEADER_DEV_INDEX] = { .name = "dev-index", .type = YNL_PT_U32, },
	[ETHTOOL_A_HEADER_DEV_NAME] = { .name = "dev-name", .type = YNL_PT_NUL_STR, },
	[ETHTOOL_A_HEADER_FLAGS] = { .name = "flags", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_header_nest = {
	.max_attr = ETHTOOL_A_HEADER_MAX,
	.table = ethtool_header_policy,
};

struct ynl_policy_attr ethtool_pause_stat_policy[ETHTOOL_A_PAUSE_STAT_MAX + 1] = {
	[ETHTOOL_A_PAUSE_STAT_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[ETHTOOL_A_PAUSE_STAT_TX_FRAMES] = { .name = "tx-frames", .type = YNL_PT_U64, },
	[ETHTOOL_A_PAUSE_STAT_RX_FRAMES] = { .name = "rx-frames", .type = YNL_PT_U64, },
};

struct ynl_policy_nest ethtool_pause_stat_nest = {
	.max_attr = ETHTOOL_A_PAUSE_STAT_MAX,
	.table = ethtool_pause_stat_policy,
};

struct ynl_policy_attr ethtool_cable_test_tdr_cfg_policy[ETHTOOL_A_CABLE_TEST_TDR_CFG_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_FIRST] = { .name = "first", .type = YNL_PT_U32, },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_LAST] = { .name = "last", .type = YNL_PT_U32, },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP] = { .name = "step", .type = YNL_PT_U32, },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_PAIR] = { .name = "pair", .type = YNL_PT_U8, },
};

struct ynl_policy_nest ethtool_cable_test_tdr_cfg_nest = {
	.max_attr = ETHTOOL_A_CABLE_TEST_TDR_CFG_MAX,
	.table = ethtool_cable_test_tdr_cfg_policy,
};

struct ynl_policy_attr ethtool_fec_stat_policy[ETHTOOL_A_FEC_STAT_MAX + 1] = {
	[ETHTOOL_A_FEC_STAT_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[ETHTOOL_A_FEC_STAT_CORRECTED] = { .name = "corrected", .type = YNL_PT_BINARY,},
	[ETHTOOL_A_FEC_STAT_UNCORR] = { .name = "uncorr", .type = YNL_PT_BINARY,},
	[ETHTOOL_A_FEC_STAT_CORR_BITS] = { .name = "corr-bits", .type = YNL_PT_BINARY,},
};

struct ynl_policy_nest ethtool_fec_stat_nest = {
	.max_attr = ETHTOOL_A_FEC_STAT_MAX,
	.table = ethtool_fec_stat_policy,
};

struct ynl_policy_attr ethtool_mm_stat_policy[ETHTOOL_A_MM_STAT_MAX + 1] = {
	[ETHTOOL_A_MM_STAT_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[ETHTOOL_A_MM_STAT_REASSEMBLY_ERRORS] = { .name = "reassembly-errors", .type = YNL_PT_U64, },
	[ETHTOOL_A_MM_STAT_SMD_ERRORS] = { .name = "smd-errors", .type = YNL_PT_U64, },
	[ETHTOOL_A_MM_STAT_REASSEMBLY_OK] = { .name = "reassembly-ok", .type = YNL_PT_U64, },
	[ETHTOOL_A_MM_STAT_RX_FRAG_COUNT] = { .name = "rx-frag-count", .type = YNL_PT_U64, },
	[ETHTOOL_A_MM_STAT_TX_FRAG_COUNT] = { .name = "tx-frag-count", .type = YNL_PT_U64, },
	[ETHTOOL_A_MM_STAT_HOLD_COUNT] = { .name = "hold-count", .type = YNL_PT_U64, },
};

struct ynl_policy_nest ethtool_mm_stat_nest = {
	.max_attr = ETHTOOL_A_MM_STAT_MAX,
	.table = ethtool_mm_stat_policy,
};

struct ynl_policy_attr ethtool_cable_result_policy[ETHTOOL_A_CABLE_RESULT_MAX + 1] = {
	[ETHTOOL_A_CABLE_RESULT_PAIR] = { .name = "pair", .type = YNL_PT_U8, },
	[ETHTOOL_A_CABLE_RESULT_CODE] = { .name = "code", .type = YNL_PT_U8, },
};

struct ynl_policy_nest ethtool_cable_result_nest = {
	.max_attr = ETHTOOL_A_CABLE_RESULT_MAX,
	.table = ethtool_cable_result_policy,
};

struct ynl_policy_attr ethtool_cable_fault_length_policy[ETHTOOL_A_CABLE_FAULT_LENGTH_MAX + 1] = {
	[ETHTOOL_A_CABLE_FAULT_LENGTH_PAIR] = { .name = "pair", .type = YNL_PT_U8, },
	[ETHTOOL_A_CABLE_FAULT_LENGTH_CM] = { .name = "cm", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_cable_fault_length_nest = {
	.max_attr = ETHTOOL_A_CABLE_FAULT_LENGTH_MAX,
	.table = ethtool_cable_fault_length_policy,
};

struct ynl_policy_attr ethtool_bitset_bit_policy[ETHTOOL_A_BITSET_BIT_MAX + 1] = {
	[ETHTOOL_A_BITSET_BIT_INDEX] = { .name = "index", .type = YNL_PT_U32, },
	[ETHTOOL_A_BITSET_BIT_NAME] = { .name = "name", .type = YNL_PT_NUL_STR, },
	[ETHTOOL_A_BITSET_BIT_VALUE] = { .name = "value", .type = YNL_PT_FLAG, },
};

struct ynl_policy_nest ethtool_bitset_bit_nest = {
	.max_attr = ETHTOOL_A_BITSET_BIT_MAX,
	.table = ethtool_bitset_bit_policy,
};

struct ynl_policy_attr ethtool_tunnel_udp_entry_policy[ETHTOOL_A_TUNNEL_UDP_ENTRY_MAX + 1] = {
	[ETHTOOL_A_TUNNEL_UDP_ENTRY_PORT] = { .name = "port", .type = YNL_PT_U16, },
	[ETHTOOL_A_TUNNEL_UDP_ENTRY_TYPE] = { .name = "type", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_tunnel_udp_entry_nest = {
	.max_attr = ETHTOOL_A_TUNNEL_UDP_ENTRY_MAX,
	.table = ethtool_tunnel_udp_entry_policy,
};

struct ynl_policy_attr ethtool_string_policy[ETHTOOL_A_STRING_MAX + 1] = {
	[ETHTOOL_A_STRING_INDEX] = { .name = "index", .type = YNL_PT_U32, },
	[ETHTOOL_A_STRING_VALUE] = { .name = "value", .type = YNL_PT_NUL_STR, },
};

struct ynl_policy_nest ethtool_string_nest = {
	.max_attr = ETHTOOL_A_STRING_MAX,
	.table = ethtool_string_policy,
};

struct ynl_policy_attr ethtool_cable_nest_policy[ETHTOOL_A_CABLE_NEST_MAX + 1] = {
	[ETHTOOL_A_CABLE_NEST_RESULT] = { .name = "result", .type = YNL_PT_NEST, .nest = &ethtool_cable_result_nest, },
	[ETHTOOL_A_CABLE_NEST_FAULT_LENGTH] = { .name = "fault-length", .type = YNL_PT_NEST, .nest = &ethtool_cable_fault_length_nest, },
};

struct ynl_policy_nest ethtool_cable_nest_nest = {
	.max_attr = ETHTOOL_A_CABLE_NEST_MAX,
	.table = ethtool_cable_nest_policy,
};

struct ynl_policy_attr ethtool_bitset_bits_policy[ETHTOOL_A_BITSET_BITS_MAX + 1] = {
	[ETHTOOL_A_BITSET_BITS_BIT] = { .name = "bit", .type = YNL_PT_NEST, .nest = &ethtool_bitset_bit_nest, },
};

struct ynl_policy_nest ethtool_bitset_bits_nest = {
	.max_attr = ETHTOOL_A_BITSET_BITS_MAX,
	.table = ethtool_bitset_bits_policy,
};

struct ynl_policy_attr ethtool_strings_policy[ETHTOOL_A_STRINGS_MAX + 1] = {
	[ETHTOOL_A_STRINGS_STRING] = { .name = "string", .type = YNL_PT_NEST, .nest = &ethtool_string_nest, },
};

struct ynl_policy_nest ethtool_strings_nest = {
	.max_attr = ETHTOOL_A_STRINGS_MAX,
	.table = ethtool_strings_policy,
};

struct ynl_policy_attr ethtool_bitset_policy[ETHTOOL_A_BITSET_MAX + 1] = {
	[ETHTOOL_A_BITSET_NOMASK] = { .name = "nomask", .type = YNL_PT_FLAG, },
	[ETHTOOL_A_BITSET_SIZE] = { .name = "size", .type = YNL_PT_U32, },
	[ETHTOOL_A_BITSET_BITS] = { .name = "bits", .type = YNL_PT_NEST, .nest = &ethtool_bitset_bits_nest, },
};

struct ynl_policy_nest ethtool_bitset_nest = {
	.max_attr = ETHTOOL_A_BITSET_MAX,
	.table = ethtool_bitset_policy,
};

struct ynl_policy_attr ethtool_stringset_policy[ETHTOOL_A_STRINGSET_MAX + 1] = {
	[ETHTOOL_A_STRINGSET_ID] = { .name = "id", .type = YNL_PT_U32, },
	[ETHTOOL_A_STRINGSET_COUNT] = { .name = "count", .type = YNL_PT_U32, },
	[ETHTOOL_A_STRINGSET_STRINGS] = { .name = "strings", .type = YNL_PT_NEST, .nest = &ethtool_strings_nest, },
};

struct ynl_policy_nest ethtool_stringset_nest = {
	.max_attr = ETHTOOL_A_STRINGSET_MAX,
	.table = ethtool_stringset_policy,
};

struct ynl_policy_attr ethtool_tunnel_udp_table_policy[ETHTOOL_A_TUNNEL_UDP_TABLE_MAX + 1] = {
	[ETHTOOL_A_TUNNEL_UDP_TABLE_SIZE] = { .name = "size", .type = YNL_PT_U32, },
	[ETHTOOL_A_TUNNEL_UDP_TABLE_TYPES] = { .name = "types", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_TUNNEL_UDP_TABLE_ENTRY] = { .name = "entry", .type = YNL_PT_NEST, .nest = &ethtool_tunnel_udp_entry_nest, },
};

struct ynl_policy_nest ethtool_tunnel_udp_table_nest = {
	.max_attr = ETHTOOL_A_TUNNEL_UDP_TABLE_MAX,
	.table = ethtool_tunnel_udp_table_policy,
};

struct ynl_policy_attr ethtool_stringsets_policy[ETHTOOL_A_STRINGSETS_MAX + 1] = {
	[ETHTOOL_A_STRINGSETS_STRINGSET] = { .name = "stringset", .type = YNL_PT_NEST, .nest = &ethtool_stringset_nest, },
};

struct ynl_policy_nest ethtool_stringsets_nest = {
	.max_attr = ETHTOOL_A_STRINGSETS_MAX,
	.table = ethtool_stringsets_policy,
};

struct ynl_policy_attr ethtool_tunnel_udp_policy[ETHTOOL_A_TUNNEL_UDP_MAX + 1] = {
	[ETHTOOL_A_TUNNEL_UDP_TABLE] = { .name = "table", .type = YNL_PT_NEST, .nest = &ethtool_tunnel_udp_table_nest, },
};

struct ynl_policy_nest ethtool_tunnel_udp_nest = {
	.max_attr = ETHTOOL_A_TUNNEL_UDP_MAX,
	.table = ethtool_tunnel_udp_policy,
};

struct ynl_policy_attr ethtool_strset_policy[ETHTOOL_A_STRSET_MAX + 1] = {
	[ETHTOOL_A_STRSET_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_STRSET_STRINGSETS] = { .name = "stringsets", .type = YNL_PT_NEST, .nest = &ethtool_stringsets_nest, },
	[ETHTOOL_A_STRSET_COUNTS_ONLY] = { .name = "counts-only", .type = YNL_PT_FLAG, },
};

struct ynl_policy_nest ethtool_strset_nest = {
	.max_attr = ETHTOOL_A_STRSET_MAX,
	.table = ethtool_strset_policy,
};

struct ynl_policy_attr ethtool_linkinfo_policy[ETHTOOL_A_LINKINFO_MAX + 1] = {
	[ETHTOOL_A_LINKINFO_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_LINKINFO_PORT] = { .name = "port", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKINFO_PHYADDR] = { .name = "phyaddr", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKINFO_TP_MDIX] = { .name = "tp-mdix", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKINFO_TP_MDIX_CTRL] = { .name = "tp-mdix-ctrl", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKINFO_TRANSCEIVER] = { .name = "transceiver", .type = YNL_PT_U8, },
};

struct ynl_policy_nest ethtool_linkinfo_nest = {
	.max_attr = ETHTOOL_A_LINKINFO_MAX,
	.table = ethtool_linkinfo_policy,
};

struct ynl_policy_attr ethtool_linkmodes_policy[ETHTOOL_A_LINKMODES_MAX + 1] = {
	[ETHTOOL_A_LINKMODES_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_LINKMODES_AUTONEG] = { .name = "autoneg", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKMODES_OURS] = { .name = "ours", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_LINKMODES_PEER] = { .name = "peer", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_LINKMODES_SPEED] = { .name = "speed", .type = YNL_PT_U32, },
	[ETHTOOL_A_LINKMODES_DUPLEX] = { .name = "duplex", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG] = { .name = "master-slave-cfg", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKMODES_MASTER_SLAVE_STATE] = { .name = "master-slave-state", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKMODES_LANES] = { .name = "lanes", .type = YNL_PT_U32, },
	[ETHTOOL_A_LINKMODES_RATE_MATCHING] = { .name = "rate-matching", .type = YNL_PT_U8, },
};

struct ynl_policy_nest ethtool_linkmodes_nest = {
	.max_attr = ETHTOOL_A_LINKMODES_MAX,
	.table = ethtool_linkmodes_policy,
};

struct ynl_policy_attr ethtool_linkstate_policy[ETHTOOL_A_LINKSTATE_MAX + 1] = {
	[ETHTOOL_A_LINKSTATE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_LINKSTATE_LINK] = { .name = "link", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKSTATE_SQI] = { .name = "sqi", .type = YNL_PT_U32, },
	[ETHTOOL_A_LINKSTATE_SQI_MAX] = { .name = "sqi-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_LINKSTATE_EXT_STATE] = { .name = "ext-state", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKSTATE_EXT_SUBSTATE] = { .name = "ext-substate", .type = YNL_PT_U8, },
	[ETHTOOL_A_LINKSTATE_EXT_DOWN_CNT] = { .name = "ext-down-cnt", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_linkstate_nest = {
	.max_attr = ETHTOOL_A_LINKSTATE_MAX,
	.table = ethtool_linkstate_policy,
};

struct ynl_policy_attr ethtool_debug_policy[ETHTOOL_A_DEBUG_MAX + 1] = {
	[ETHTOOL_A_DEBUG_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_DEBUG_MSGMASK] = { .name = "msgmask", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
};

struct ynl_policy_nest ethtool_debug_nest = {
	.max_attr = ETHTOOL_A_DEBUG_MAX,
	.table = ethtool_debug_policy,
};

struct ynl_policy_attr ethtool_wol_policy[ETHTOOL_A_WOL_MAX + 1] = {
	[ETHTOOL_A_WOL_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_WOL_MODES] = { .name = "modes", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_WOL_SOPASS] = { .name = "sopass", .type = YNL_PT_BINARY,},
};

struct ynl_policy_nest ethtool_wol_nest = {
	.max_attr = ETHTOOL_A_WOL_MAX,
	.table = ethtool_wol_policy,
};

struct ynl_policy_attr ethtool_features_policy[ETHTOOL_A_FEATURES_MAX + 1] = {
	[ETHTOOL_A_FEATURES_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_FEATURES_HW] = { .name = "hw", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_FEATURES_WANTED] = { .name = "wanted", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_FEATURES_ACTIVE] = { .name = "active", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_FEATURES_NOCHANGE] = { .name = "nochange", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
};

struct ynl_policy_nest ethtool_features_nest = {
	.max_attr = ETHTOOL_A_FEATURES_MAX,
	.table = ethtool_features_policy,
};

struct ynl_policy_attr ethtool_privflags_policy[ETHTOOL_A_PRIVFLAGS_MAX + 1] = {
	[ETHTOOL_A_PRIVFLAGS_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_PRIVFLAGS_FLAGS] = { .name = "flags", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
};

struct ynl_policy_nest ethtool_privflags_nest = {
	.max_attr = ETHTOOL_A_PRIVFLAGS_MAX,
	.table = ethtool_privflags_policy,
};

struct ynl_policy_attr ethtool_rings_policy[ETHTOOL_A_RINGS_MAX + 1] = {
	[ETHTOOL_A_RINGS_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_RINGS_RX_MAX] = { .name = "rx-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX_MINI_MAX] = { .name = "rx-mini-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX_JUMBO_MAX] = { .name = "rx-jumbo-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_TX_MAX] = { .name = "tx-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX] = { .name = "rx", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX_MINI] = { .name = "rx-mini", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX_JUMBO] = { .name = "rx-jumbo", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_TX] = { .name = "tx", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_RX_BUF_LEN] = { .name = "rx-buf-len", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_TCP_DATA_SPLIT] = { .name = "tcp-data-split", .type = YNL_PT_U8, },
	[ETHTOOL_A_RINGS_CQE_SIZE] = { .name = "cqe-size", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_TX_PUSH] = { .name = "tx-push", .type = YNL_PT_U8, },
	[ETHTOOL_A_RINGS_RX_PUSH] = { .name = "rx-push", .type = YNL_PT_U8, },
	[ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN] = { .name = "tx-push-buf-len", .type = YNL_PT_U32, },
	[ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN_MAX] = { .name = "tx-push-buf-len-max", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_rings_nest = {
	.max_attr = ETHTOOL_A_RINGS_MAX,
	.table = ethtool_rings_policy,
};

struct ynl_policy_attr ethtool_channels_policy[ETHTOOL_A_CHANNELS_MAX + 1] = {
	[ETHTOOL_A_CHANNELS_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_CHANNELS_RX_MAX] = { .name = "rx-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_TX_MAX] = { .name = "tx-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_OTHER_MAX] = { .name = "other-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_COMBINED_MAX] = { .name = "combined-max", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_RX_COUNT] = { .name = "rx-count", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_TX_COUNT] = { .name = "tx-count", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_OTHER_COUNT] = { .name = "other-count", .type = YNL_PT_U32, },
	[ETHTOOL_A_CHANNELS_COMBINED_COUNT] = { .name = "combined-count", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_channels_nest = {
	.max_attr = ETHTOOL_A_CHANNELS_MAX,
	.table = ethtool_channels_policy,
};

struct ynl_policy_attr ethtool_coalesce_policy[ETHTOOL_A_COALESCE_MAX + 1] = {
	[ETHTOOL_A_COALESCE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_COALESCE_RX_USECS] = { .name = "rx-usecs", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES] = { .name = "rx-max-frames", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_USECS_IRQ] = { .name = "rx-usecs-irq", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ] = { .name = "rx-max-frames-irq", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_USECS] = { .name = "tx-usecs", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES] = { .name = "tx-max-frames", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_USECS_IRQ] = { .name = "tx-usecs-irq", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ] = { .name = "tx-max-frames-irq", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_STATS_BLOCK_USECS] = { .name = "stats-block-usecs", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX] = { .name = "use-adaptive-rx", .type = YNL_PT_U8, },
	[ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX] = { .name = "use-adaptive-tx", .type = YNL_PT_U8, },
	[ETHTOOL_A_COALESCE_PKT_RATE_LOW] = { .name = "pkt-rate-low", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_USECS_LOW] = { .name = "rx-usecs-low", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW] = { .name = "rx-max-frames-low", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_USECS_LOW] = { .name = "tx-usecs-low", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW] = { .name = "tx-max-frames-low", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_PKT_RATE_HIGH] = { .name = "pkt-rate-high", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_USECS_HIGH] = { .name = "rx-usecs-high", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH] = { .name = "rx-max-frames-high", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_USECS_HIGH] = { .name = "tx-usecs-high", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH] = { .name = "tx-max-frames-high", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL] = { .name = "rate-sample-interval", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_USE_CQE_MODE_TX] = { .name = "use-cqe-mode-tx", .type = YNL_PT_U8, },
	[ETHTOOL_A_COALESCE_USE_CQE_MODE_RX] = { .name = "use-cqe-mode-rx", .type = YNL_PT_U8, },
	[ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES] = { .name = "tx-aggr-max-bytes", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES] = { .name = "tx-aggr-max-frames", .type = YNL_PT_U32, },
	[ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS] = { .name = "tx-aggr-time-usecs", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_coalesce_nest = {
	.max_attr = ETHTOOL_A_COALESCE_MAX,
	.table = ethtool_coalesce_policy,
};

struct ynl_policy_attr ethtool_pause_policy[ETHTOOL_A_PAUSE_MAX + 1] = {
	[ETHTOOL_A_PAUSE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_PAUSE_AUTONEG] = { .name = "autoneg", .type = YNL_PT_U8, },
	[ETHTOOL_A_PAUSE_RX] = { .name = "rx", .type = YNL_PT_U8, },
	[ETHTOOL_A_PAUSE_TX] = { .name = "tx", .type = YNL_PT_U8, },
	[ETHTOOL_A_PAUSE_STATS] = { .name = "stats", .type = YNL_PT_NEST, .nest = &ethtool_pause_stat_nest, },
	[ETHTOOL_A_PAUSE_STATS_SRC] = { .name = "stats-src", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_pause_nest = {
	.max_attr = ETHTOOL_A_PAUSE_MAX,
	.table = ethtool_pause_policy,
};

struct ynl_policy_attr ethtool_eee_policy[ETHTOOL_A_EEE_MAX + 1] = {
	[ETHTOOL_A_EEE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_EEE_MODES_OURS] = { .name = "modes-ours", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_EEE_MODES_PEER] = { .name = "modes-peer", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_EEE_ACTIVE] = { .name = "active", .type = YNL_PT_U8, },
	[ETHTOOL_A_EEE_ENABLED] = { .name = "enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_EEE_TX_LPI_ENABLED] = { .name = "tx-lpi-enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_EEE_TX_LPI_TIMER] = { .name = "tx-lpi-timer", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_eee_nest = {
	.max_attr = ETHTOOL_A_EEE_MAX,
	.table = ethtool_eee_policy,
};

struct ynl_policy_attr ethtool_tsinfo_policy[ETHTOOL_A_TSINFO_MAX + 1] = {
	[ETHTOOL_A_TSINFO_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_TSINFO_TIMESTAMPING] = { .name = "timestamping", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_TSINFO_TX_TYPES] = { .name = "tx-types", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_TSINFO_RX_FILTERS] = { .name = "rx-filters", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_TSINFO_PHC_INDEX] = { .name = "phc-index", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_tsinfo_nest = {
	.max_attr = ETHTOOL_A_TSINFO_MAX,
	.table = ethtool_tsinfo_policy,
};

struct ynl_policy_attr ethtool_cable_test_policy[ETHTOOL_A_CABLE_TEST_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
};

struct ynl_policy_nest ethtool_cable_test_nest = {
	.max_attr = ETHTOOL_A_CABLE_TEST_MAX,
	.table = ethtool_cable_test_policy,
};

struct ynl_policy_attr ethtool_cable_test_ntf_policy[ETHTOOL_A_CABLE_TEST_NTF_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_NTF_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_CABLE_TEST_NTF_STATUS] = { .name = "status", .type = YNL_PT_U8, },
	[ETHTOOL_A_CABLE_TEST_NTF_NEST] = { .name = "nest", .type = YNL_PT_NEST, .nest = &ethtool_cable_nest_nest, },
};

struct ynl_policy_nest ethtool_cable_test_ntf_nest = {
	.max_attr = ETHTOOL_A_CABLE_TEST_NTF_MAX,
	.table = ethtool_cable_test_ntf_policy,
};

struct ynl_policy_attr ethtool_cable_test_tdr_policy[ETHTOOL_A_CABLE_TEST_TDR_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_TDR_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG] = { .name = "cfg", .type = YNL_PT_NEST, .nest = &ethtool_cable_test_tdr_cfg_nest, },
};

struct ynl_policy_nest ethtool_cable_test_tdr_nest = {
	.max_attr = ETHTOOL_A_CABLE_TEST_TDR_MAX,
	.table = ethtool_cable_test_tdr_policy,
};

struct ynl_policy_attr ethtool_cable_test_tdr_ntf_policy[ETHTOOL_A_CABLE_TEST_TDR_NTF_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_TDR_NTF_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_CABLE_TEST_TDR_NTF_STATUS] = { .name = "status", .type = YNL_PT_U8, },
	[ETHTOOL_A_CABLE_TEST_TDR_NTF_NEST] = { .name = "nest", .type = YNL_PT_NEST, .nest = &ethtool_cable_nest_nest, },
};

struct ynl_policy_nest ethtool_cable_test_tdr_ntf_nest = {
	.max_attr = ETHTOOL_A_CABLE_TEST_TDR_NTF_MAX,
	.table = ethtool_cable_test_tdr_ntf_policy,
};

struct ynl_policy_attr ethtool_tunnel_info_policy[ETHTOOL_A_TUNNEL_INFO_MAX + 1] = {
	[ETHTOOL_A_TUNNEL_INFO_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_TUNNEL_INFO_UDP_PORTS] = { .name = "udp-ports", .type = YNL_PT_NEST, .nest = &ethtool_tunnel_udp_nest, },
};

struct ynl_policy_nest ethtool_tunnel_info_nest = {
	.max_attr = ETHTOOL_A_TUNNEL_INFO_MAX,
	.table = ethtool_tunnel_info_policy,
};

struct ynl_policy_attr ethtool_fec_policy[ETHTOOL_A_FEC_MAX + 1] = {
	[ETHTOOL_A_FEC_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_FEC_MODES] = { .name = "modes", .type = YNL_PT_NEST, .nest = &ethtool_bitset_nest, },
	[ETHTOOL_A_FEC_AUTO] = { .name = "auto", .type = YNL_PT_U8, },
	[ETHTOOL_A_FEC_ACTIVE] = { .name = "active", .type = YNL_PT_U32, },
	[ETHTOOL_A_FEC_STATS] = { .name = "stats", .type = YNL_PT_NEST, .nest = &ethtool_fec_stat_nest, },
};

struct ynl_policy_nest ethtool_fec_nest = {
	.max_attr = ETHTOOL_A_FEC_MAX,
	.table = ethtool_fec_policy,
};

struct ynl_policy_attr ethtool_module_eeprom_policy[ETHTOOL_A_MODULE_EEPROM_MAX + 1] = {
	[ETHTOOL_A_MODULE_EEPROM_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_MODULE_EEPROM_OFFSET] = { .name = "offset", .type = YNL_PT_U32, },
	[ETHTOOL_A_MODULE_EEPROM_LENGTH] = { .name = "length", .type = YNL_PT_U32, },
	[ETHTOOL_A_MODULE_EEPROM_PAGE] = { .name = "page", .type = YNL_PT_U8, },
	[ETHTOOL_A_MODULE_EEPROM_BANK] = { .name = "bank", .type = YNL_PT_U8, },
	[ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS] = { .name = "i2c-address", .type = YNL_PT_U8, },
	[ETHTOOL_A_MODULE_EEPROM_DATA] = { .name = "data", .type = YNL_PT_BINARY,},
};

struct ynl_policy_nest ethtool_module_eeprom_nest = {
	.max_attr = ETHTOOL_A_MODULE_EEPROM_MAX,
	.table = ethtool_module_eeprom_policy,
};

struct ynl_policy_attr ethtool_phc_vclocks_policy[ETHTOOL_A_PHC_VCLOCKS_MAX + 1] = {
	[ETHTOOL_A_PHC_VCLOCKS_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_PHC_VCLOCKS_NUM] = { .name = "num", .type = YNL_PT_U32, },
	[ETHTOOL_A_PHC_VCLOCKS_INDEX] = { .name = "index", .type = YNL_PT_BINARY,},
};

struct ynl_policy_nest ethtool_phc_vclocks_nest = {
	.max_attr = ETHTOOL_A_PHC_VCLOCKS_MAX,
	.table = ethtool_phc_vclocks_policy,
};

struct ynl_policy_attr ethtool_module_policy[ETHTOOL_A_MODULE_MAX + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_MODULE_POWER_MODE_POLICY] = { .name = "power-mode-policy", .type = YNL_PT_U8, },
	[ETHTOOL_A_MODULE_POWER_MODE] = { .name = "power-mode", .type = YNL_PT_U8, },
};

struct ynl_policy_nest ethtool_module_nest = {
	.max_attr = ETHTOOL_A_MODULE_MAX,
	.table = ethtool_module_policy,
};

struct ynl_policy_attr ethtool_pse_policy[ETHTOOL_A_PSE_MAX + 1] = {
	[ETHTOOL_A_PSE_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_PODL_PSE_ADMIN_STATE] = { .name = "admin-state", .type = YNL_PT_U32, },
	[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL] = { .name = "admin-control", .type = YNL_PT_U32, },
	[ETHTOOL_A_PODL_PSE_PW_D_STATUS] = { .name = "pw-d-status", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_pse_nest = {
	.max_attr = ETHTOOL_A_PSE_MAX,
	.table = ethtool_pse_policy,
};

struct ynl_policy_attr ethtool_rss_policy[ETHTOOL_A_RSS_MAX + 1] = {
	[ETHTOOL_A_RSS_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_RSS_CONTEXT] = { .name = "context", .type = YNL_PT_U32, },
	[ETHTOOL_A_RSS_HFUNC] = { .name = "hfunc", .type = YNL_PT_U32, },
	[ETHTOOL_A_RSS_INDIR] = { .name = "indir", .type = YNL_PT_BINARY,},
	[ETHTOOL_A_RSS_HKEY] = { .name = "hkey", .type = YNL_PT_BINARY,},
};

struct ynl_policy_nest ethtool_rss_nest = {
	.max_attr = ETHTOOL_A_RSS_MAX,
	.table = ethtool_rss_policy,
};

struct ynl_policy_attr ethtool_plca_policy[ETHTOOL_A_PLCA_MAX + 1] = {
	[ETHTOOL_A_PLCA_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_PLCA_VERSION] = { .name = "version", .type = YNL_PT_U16, },
	[ETHTOOL_A_PLCA_ENABLED] = { .name = "enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_PLCA_STATUS] = { .name = "status", .type = YNL_PT_U8, },
	[ETHTOOL_A_PLCA_NODE_CNT] = { .name = "node-cnt", .type = YNL_PT_U32, },
	[ETHTOOL_A_PLCA_NODE_ID] = { .name = "node-id", .type = YNL_PT_U32, },
	[ETHTOOL_A_PLCA_TO_TMR] = { .name = "to-tmr", .type = YNL_PT_U32, },
	[ETHTOOL_A_PLCA_BURST_CNT] = { .name = "burst-cnt", .type = YNL_PT_U32, },
	[ETHTOOL_A_PLCA_BURST_TMR] = { .name = "burst-tmr", .type = YNL_PT_U32, },
};

struct ynl_policy_nest ethtool_plca_nest = {
	.max_attr = ETHTOOL_A_PLCA_MAX,
	.table = ethtool_plca_policy,
};

struct ynl_policy_attr ethtool_mm_policy[ETHTOOL_A_MM_MAX + 1] = {
	[ETHTOOL_A_MM_HEADER] = { .name = "header", .type = YNL_PT_NEST, .nest = &ethtool_header_nest, },
	[ETHTOOL_A_MM_PMAC_ENABLED] = { .name = "pmac-enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_MM_TX_ENABLED] = { .name = "tx-enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_MM_TX_ACTIVE] = { .name = "tx-active", .type = YNL_PT_U8, },
	[ETHTOOL_A_MM_TX_MIN_FRAG_SIZE] = { .name = "tx-min-frag-size", .type = YNL_PT_U32, },
	[ETHTOOL_A_MM_RX_MIN_FRAG_SIZE] = { .name = "rx-min-frag-size", .type = YNL_PT_U32, },
	[ETHTOOL_A_MM_VERIFY_ENABLED] = { .name = "verify-enabled", .type = YNL_PT_U8, },
	[ETHTOOL_A_MM_VERIFY_STATUS] = { .name = "verify-status", .type = YNL_PT_U8, },
	[ETHTOOL_A_MM_VERIFY_TIME] = { .name = "verify-time", .type = YNL_PT_U32, },
	[ETHTOOL_A_MM_MAX_VERIFY_TIME] = { .name = "max-verify-time", .type = YNL_PT_U32, },
	[ETHTOOL_A_MM_STATS] = { .name = "stats", .type = YNL_PT_NEST, .nest = &ethtool_mm_stat_nest, },
};

struct ynl_policy_nest ethtool_mm_nest = {
	.max_attr = ETHTOOL_A_MM_MAX,
	.table = ethtool_mm_policy,
};

/* Common nested types */
void ethtool_header_free(struct ethtool_header *obj)
{
	free(obj->dev_name);
}

int ethtool_header_put(struct nlmsghdr *nlh, unsigned int attr_type,
		       struct ethtool_header *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.dev_index)
		mnl_attr_put_u32(nlh, ETHTOOL_A_HEADER_DEV_INDEX, obj->dev_index);
	if (obj->_present.dev_name_len)
		mnl_attr_put_strz(nlh, ETHTOOL_A_HEADER_DEV_NAME, obj->dev_name);
	if (obj->_present.flags)
		mnl_attr_put_u32(nlh, ETHTOOL_A_HEADER_FLAGS, obj->flags);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_header_parse(struct ynl_parse_arg *yarg,
			 const struct nlattr *nested)
{
	struct ethtool_header *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_HEADER_DEV_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dev_index = 1;
			dst->dev_index = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_HEADER_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == ETHTOOL_A_HEADER_FLAGS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.flags = 1;
			dst->flags = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void ethtool_pause_stat_free(struct ethtool_pause_stat *obj)
{
}

int ethtool_pause_stat_put(struct nlmsghdr *nlh, unsigned int attr_type,
			   struct ethtool_pause_stat *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.tx_frames)
		mnl_attr_put_u64(nlh, ETHTOOL_A_PAUSE_STAT_TX_FRAMES, obj->tx_frames);
	if (obj->_present.rx_frames)
		mnl_attr_put_u64(nlh, ETHTOOL_A_PAUSE_STAT_RX_FRAMES, obj->rx_frames);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_pause_stat_parse(struct ynl_parse_arg *yarg,
			     const struct nlattr *nested)
{
	struct ethtool_pause_stat *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PAUSE_STAT_TX_FRAMES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_frames = 1;
			dst->tx_frames = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_PAUSE_STAT_RX_FRAMES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_frames = 1;
			dst->rx_frames = mnl_attr_get_u64(attr);
		}
	}

	return 0;
}

void ethtool_cable_test_tdr_cfg_free(struct ethtool_cable_test_tdr_cfg *obj)
{
}

void ethtool_fec_stat_free(struct ethtool_fec_stat *obj)
{
	free(obj->corrected);
	free(obj->uncorr);
	free(obj->corr_bits);
}

int ethtool_fec_stat_put(struct nlmsghdr *nlh, unsigned int attr_type,
			 struct ethtool_fec_stat *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.corrected_len)
		mnl_attr_put(nlh, ETHTOOL_A_FEC_STAT_CORRECTED, obj->_present.corrected_len, obj->corrected);
	if (obj->_present.uncorr_len)
		mnl_attr_put(nlh, ETHTOOL_A_FEC_STAT_UNCORR, obj->_present.uncorr_len, obj->uncorr);
	if (obj->_present.corr_bits_len)
		mnl_attr_put(nlh, ETHTOOL_A_FEC_STAT_CORR_BITS, obj->_present.corr_bits_len, obj->corr_bits);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_fec_stat_parse(struct ynl_parse_arg *yarg,
			   const struct nlattr *nested)
{
	struct ethtool_fec_stat *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_FEC_STAT_CORRECTED) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.corrected_len = len;
			dst->corrected = malloc(len);
			memcpy(dst->corrected, mnl_attr_get_payload(attr), len);
		} else if (type == ETHTOOL_A_FEC_STAT_UNCORR) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.uncorr_len = len;
			dst->uncorr = malloc(len);
			memcpy(dst->uncorr, mnl_attr_get_payload(attr), len);
		} else if (type == ETHTOOL_A_FEC_STAT_CORR_BITS) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.corr_bits_len = len;
			dst->corr_bits = malloc(len);
			memcpy(dst->corr_bits, mnl_attr_get_payload(attr), len);
		}
	}

	return 0;
}

void ethtool_mm_stat_free(struct ethtool_mm_stat *obj)
{
}

int ethtool_mm_stat_parse(struct ynl_parse_arg *yarg,
			  const struct nlattr *nested)
{
	struct ethtool_mm_stat *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_MM_STAT_REASSEMBLY_ERRORS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reassembly_errors = 1;
			dst->reassembly_errors = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_MM_STAT_SMD_ERRORS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.smd_errors = 1;
			dst->smd_errors = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_MM_STAT_REASSEMBLY_OK) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reassembly_ok = 1;
			dst->reassembly_ok = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_MM_STAT_RX_FRAG_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_frag_count = 1;
			dst->rx_frag_count = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_MM_STAT_TX_FRAG_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_frag_count = 1;
			dst->tx_frag_count = mnl_attr_get_u64(attr);
		} else if (type == ETHTOOL_A_MM_STAT_HOLD_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.hold_count = 1;
			dst->hold_count = mnl_attr_get_u64(attr);
		}
	}

	return 0;
}

void ethtool_cable_result_free(struct ethtool_cable_result *obj)
{
}

int ethtool_cable_result_parse(struct ynl_parse_arg *yarg,
			       const struct nlattr *nested)
{
	struct ethtool_cable_result *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CABLE_RESULT_PAIR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pair = 1;
			dst->pair = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_CABLE_RESULT_CODE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.code = 1;
			dst->code = mnl_attr_get_u8(attr);
		}
	}

	return 0;
}

void ethtool_cable_fault_length_free(struct ethtool_cable_fault_length *obj)
{
}

int ethtool_cable_fault_length_parse(struct ynl_parse_arg *yarg,
				     const struct nlattr *nested)
{
	struct ethtool_cable_fault_length *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CABLE_FAULT_LENGTH_PAIR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pair = 1;
			dst->pair = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_CABLE_FAULT_LENGTH_CM) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.cm = 1;
			dst->cm = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void ethtool_bitset_bit_free(struct ethtool_bitset_bit *obj)
{
	free(obj->name);
}

int ethtool_bitset_bit_put(struct nlmsghdr *nlh, unsigned int attr_type,
			   struct ethtool_bitset_bit *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.index)
		mnl_attr_put_u32(nlh, ETHTOOL_A_BITSET_BIT_INDEX, obj->index);
	if (obj->_present.name_len)
		mnl_attr_put_strz(nlh, ETHTOOL_A_BITSET_BIT_NAME, obj->name);
	if (obj->_present.value)
		mnl_attr_put(nlh, ETHTOOL_A_BITSET_BIT_VALUE, 0, NULL);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_bitset_bit_parse(struct ynl_parse_arg *yarg,
			     const struct nlattr *nested)
{
	struct ethtool_bitset_bit *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_BITSET_BIT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.index = 1;
			dst->index = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_BITSET_BIT_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.name_len = len;
			dst->name = malloc(len + 1);
			memcpy(dst->name, mnl_attr_get_str(attr), len);
			dst->name[len] = 0;
		} else if (type == ETHTOOL_A_BITSET_BIT_VALUE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.value = 1;
		}
	}

	return 0;
}

void ethtool_tunnel_udp_entry_free(struct ethtool_tunnel_udp_entry *obj)
{
}

int ethtool_tunnel_udp_entry_parse(struct ynl_parse_arg *yarg,
				   const struct nlattr *nested)
{
	struct ethtool_tunnel_udp_entry *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_TUNNEL_UDP_ENTRY_PORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port = 1;
			dst->port = mnl_attr_get_u16(attr);
		} else if (type == ETHTOOL_A_TUNNEL_UDP_ENTRY_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.type = 1;
			dst->type = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void ethtool_string_free(struct ethtool_string *obj)
{
	free(obj->value);
}

int ethtool_string_put(struct nlmsghdr *nlh, unsigned int attr_type,
		       struct ethtool_string *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.index)
		mnl_attr_put_u32(nlh, ETHTOOL_A_STRING_INDEX, obj->index);
	if (obj->_present.value_len)
		mnl_attr_put_strz(nlh, ETHTOOL_A_STRING_VALUE, obj->value);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_string_parse(struct ynl_parse_arg *yarg,
			 const struct nlattr *nested)
{
	struct ethtool_string *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_STRING_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.index = 1;
			dst->index = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_STRING_VALUE) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.value_len = len;
			dst->value = malloc(len + 1);
			memcpy(dst->value, mnl_attr_get_str(attr), len);
			dst->value[len] = 0;
		}
	}

	return 0;
}

void ethtool_cable_nest_free(struct ethtool_cable_nest *obj)
{
	ethtool_cable_result_free(&obj->result);
	ethtool_cable_fault_length_free(&obj->fault_length);
}

int ethtool_cable_nest_parse(struct ynl_parse_arg *yarg,
			     const struct nlattr *nested)
{
	struct ethtool_cable_nest *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CABLE_NEST_RESULT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.result = 1;

			parg.rsp_policy = &ethtool_cable_result_nest;
			parg.data = &dst->result;
			if (ethtool_cable_result_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_CABLE_NEST_FAULT_LENGTH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.fault_length = 1;

			parg.rsp_policy = &ethtool_cable_fault_length_nest;
			parg.data = &dst->fault_length;
			if (ethtool_cable_fault_length_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

void ethtool_bitset_bits_free(struct ethtool_bitset_bits *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_bit; i++)
		ethtool_bitset_bit_free(&obj->bit[i]);
	free(obj->bit);
}

int ethtool_bitset_bits_put(struct nlmsghdr *nlh, unsigned int attr_type,
			    struct ethtool_bitset_bits *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	for (unsigned int i = 0; i < obj->n_bit; i++)
		ethtool_bitset_bit_put(nlh, ETHTOOL_A_BITSET_BITS_BIT, &obj->bit[i]);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_bitset_bits_parse(struct ynl_parse_arg *yarg,
			      const struct nlattr *nested)
{
	struct ethtool_bitset_bits *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	unsigned int n_bit = 0;
	int i;

	parg.ys = yarg->ys;

	if (dst->bit)
		return ynl_error_parse(yarg, "attribute already present (bitset-bits.bit)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_BITSET_BITS_BIT) {
			n_bit++;
		}
	}

	if (n_bit) {
		dst->bit = calloc(n_bit, sizeof(*dst->bit));
		dst->n_bit = n_bit;
		i = 0;
		parg.rsp_policy = &ethtool_bitset_bit_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == ETHTOOL_A_BITSET_BITS_BIT) {
				parg.data = &dst->bit[i];
				if (ethtool_bitset_bit_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void ethtool_strings_free(struct ethtool_strings *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_string; i++)
		ethtool_string_free(&obj->string[i]);
	free(obj->string);
}

int ethtool_strings_put(struct nlmsghdr *nlh, unsigned int attr_type,
			struct ethtool_strings *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	for (unsigned int i = 0; i < obj->n_string; i++)
		ethtool_string_put(nlh, ETHTOOL_A_STRINGS_STRING, &obj->string[i]);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_strings_parse(struct ynl_parse_arg *yarg,
			  const struct nlattr *nested)
{
	struct ethtool_strings *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	unsigned int n_string = 0;
	int i;

	parg.ys = yarg->ys;

	if (dst->string)
		return ynl_error_parse(yarg, "attribute already present (strings.string)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_STRINGS_STRING) {
			n_string++;
		}
	}

	if (n_string) {
		dst->string = calloc(n_string, sizeof(*dst->string));
		dst->n_string = n_string;
		i = 0;
		parg.rsp_policy = &ethtool_string_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == ETHTOOL_A_STRINGS_STRING) {
				parg.data = &dst->string[i];
				if (ethtool_string_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void ethtool_bitset_free(struct ethtool_bitset *obj)
{
	ethtool_bitset_bits_free(&obj->bits);
}

int ethtool_bitset_put(struct nlmsghdr *nlh, unsigned int attr_type,
		       struct ethtool_bitset *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.nomask)
		mnl_attr_put(nlh, ETHTOOL_A_BITSET_NOMASK, 0, NULL);
	if (obj->_present.size)
		mnl_attr_put_u32(nlh, ETHTOOL_A_BITSET_SIZE, obj->size);
	if (obj->_present.bits)
		ethtool_bitset_bits_put(nlh, ETHTOOL_A_BITSET_BITS, &obj->bits);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_bitset_parse(struct ynl_parse_arg *yarg,
			 const struct nlattr *nested)
{
	struct ethtool_bitset *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_BITSET_NOMASK) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.nomask = 1;
		} else if (type == ETHTOOL_A_BITSET_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.size = 1;
			dst->size = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_BITSET_BITS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.bits = 1;

			parg.rsp_policy = &ethtool_bitset_bits_nest;
			parg.data = &dst->bits;
			if (ethtool_bitset_bits_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

void ethtool_stringset_free(struct ethtool_stringset_ *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_strings; i++)
		ethtool_strings_free(&obj->strings[i]);
	free(obj->strings);
}

int ethtool_stringset_put(struct nlmsghdr *nlh, unsigned int attr_type,
			  struct ethtool_stringset_ *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.id)
		mnl_attr_put_u32(nlh, ETHTOOL_A_STRINGSET_ID, obj->id);
	if (obj->_present.count)
		mnl_attr_put_u32(nlh, ETHTOOL_A_STRINGSET_COUNT, obj->count);
	for (unsigned int i = 0; i < obj->n_strings; i++)
		ethtool_strings_put(nlh, ETHTOOL_A_STRINGSET_STRINGS, &obj->strings[i]);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_stringset_parse(struct ynl_parse_arg *yarg,
			    const struct nlattr *nested)
{
	struct ethtool_stringset_ *dst = yarg->data;
	unsigned int n_strings = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->strings)
		return ynl_error_parse(yarg, "attribute already present (stringset.strings)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_STRINGSET_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.id = 1;
			dst->id = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_STRINGSET_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.count = 1;
			dst->count = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_STRINGSET_STRINGS) {
			n_strings++;
		}
	}

	if (n_strings) {
		dst->strings = calloc(n_strings, sizeof(*dst->strings));
		dst->n_strings = n_strings;
		i = 0;
		parg.rsp_policy = &ethtool_strings_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == ETHTOOL_A_STRINGSET_STRINGS) {
				parg.data = &dst->strings[i];
				if (ethtool_strings_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void ethtool_tunnel_udp_table_free(struct ethtool_tunnel_udp_table *obj)
{
	unsigned int i;

	ethtool_bitset_free(&obj->types);
	for (i = 0; i < obj->n_entry; i++)
		ethtool_tunnel_udp_entry_free(&obj->entry[i]);
	free(obj->entry);
}

int ethtool_tunnel_udp_table_parse(struct ynl_parse_arg *yarg,
				   const struct nlattr *nested)
{
	struct ethtool_tunnel_udp_table *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	unsigned int n_entry = 0;
	int i;

	parg.ys = yarg->ys;

	if (dst->entry)
		return ynl_error_parse(yarg, "attribute already present (tunnel-udp-table.entry)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_TUNNEL_UDP_TABLE_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.size = 1;
			dst->size = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_TUNNEL_UDP_TABLE_TYPES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.types = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->types;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TUNNEL_UDP_TABLE_ENTRY) {
			n_entry++;
		}
	}

	if (n_entry) {
		dst->entry = calloc(n_entry, sizeof(*dst->entry));
		dst->n_entry = n_entry;
		i = 0;
		parg.rsp_policy = &ethtool_tunnel_udp_entry_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == ETHTOOL_A_TUNNEL_UDP_TABLE_ENTRY) {
				parg.data = &dst->entry[i];
				if (ethtool_tunnel_udp_entry_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void ethtool_stringsets_free(struct ethtool_stringsets *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_stringset; i++)
		ethtool_stringset_free(&obj->stringset[i]);
	free(obj->stringset);
}

int ethtool_stringsets_put(struct nlmsghdr *nlh, unsigned int attr_type,
			   struct ethtool_stringsets *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	for (unsigned int i = 0; i < obj->n_stringset; i++)
		ethtool_stringset_put(nlh, ETHTOOL_A_STRINGSETS_STRINGSET, &obj->stringset[i]);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int ethtool_stringsets_parse(struct ynl_parse_arg *yarg,
			     const struct nlattr *nested)
{
	struct ethtool_stringsets *dst = yarg->data;
	unsigned int n_stringset = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->stringset)
		return ynl_error_parse(yarg, "attribute already present (stringsets.stringset)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_STRINGSETS_STRINGSET) {
			n_stringset++;
		}
	}

	if (n_stringset) {
		dst->stringset = calloc(n_stringset, sizeof(*dst->stringset));
		dst->n_stringset = n_stringset;
		i = 0;
		parg.rsp_policy = &ethtool_stringset_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == ETHTOOL_A_STRINGSETS_STRINGSET) {
				parg.data = &dst->stringset[i];
				if (ethtool_stringset_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void ethtool_tunnel_udp_free(struct ethtool_tunnel_udp *obj)
{
	ethtool_tunnel_udp_table_free(&obj->table);
}

int ethtool_tunnel_udp_parse(struct ynl_parse_arg *yarg,
			     const struct nlattr *nested)
{
	struct ethtool_tunnel_udp *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_TUNNEL_UDP_TABLE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.table = 1;

			parg.rsp_policy = &ethtool_tunnel_udp_table_nest;
			parg.data = &dst->table;
			if (ethtool_tunnel_udp_table_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

/* ============== ETHTOOL_MSG_STRSET_GET ============== */
/* ETHTOOL_MSG_STRSET_GET - do */
void ethtool_strset_get_req_free(struct ethtool_strset_get_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_stringsets_free(&req->stringsets);
	free(req);
}

void ethtool_strset_get_rsp_free(struct ethtool_strset_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_stringsets_free(&rsp->stringsets);
	free(rsp);
}

int ethtool_strset_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_strset_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_STRSET_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_STRSET_STRINGSETS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stringsets = 1;

			parg.rsp_policy = &ethtool_stringsets_nest;
			parg.data = &dst->stringsets;
			if (ethtool_stringsets_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_strset_get_rsp *
ethtool_strset_get(struct ynl_sock *ys, struct ethtool_strset_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_strset_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_STRSET_GET, 1);
	ys->req_policy = &ethtool_strset_nest;
	yrs.yarg.rsp_policy = &ethtool_strset_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_STRSET_HEADER, &req->header);
	if (req->_present.stringsets)
		ethtool_stringsets_put(nlh, ETHTOOL_A_STRSET_STRINGSETS, &req->stringsets);
	if (req->_present.counts_only)
		mnl_attr_put(nlh, ETHTOOL_A_STRSET_COUNTS_ONLY, 0, NULL);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_strset_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_STRSET_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_strset_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_STRSET_GET - dump */
void ethtool_strset_get_list_free(struct ethtool_strset_get_list *rsp)
{
	struct ethtool_strset_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_stringsets_free(&rsp->obj.stringsets);
		free(rsp);
	}
}

struct ethtool_strset_get_list *
ethtool_strset_get_dump(struct ynl_sock *ys,
			struct ethtool_strset_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_strset_get_list);
	yds.cb = ethtool_strset_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_STRSET_GET;
	yds.rsp_policy = &ethtool_strset_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_STRSET_GET, 1);
	ys->req_policy = &ethtool_strset_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_STRSET_HEADER, &req->header);
	if (req->_present.stringsets)
		ethtool_stringsets_put(nlh, ETHTOOL_A_STRSET_STRINGSETS, &req->stringsets);
	if (req->_present.counts_only)
		mnl_attr_put(nlh, ETHTOOL_A_STRSET_COUNTS_ONLY, 0, NULL);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_strset_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_LINKINFO_GET ============== */
/* ETHTOOL_MSG_LINKINFO_GET - do */
void ethtool_linkinfo_get_req_free(struct ethtool_linkinfo_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_linkinfo_get_rsp_free(struct ethtool_linkinfo_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_linkinfo_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_linkinfo_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_LINKINFO_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_LINKINFO_PORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port = 1;
			dst->port = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKINFO_PHYADDR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.phyaddr = 1;
			dst->phyaddr = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKINFO_TP_MDIX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tp_mdix = 1;
			dst->tp_mdix = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKINFO_TP_MDIX_CTRL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tp_mdix_ctrl = 1;
			dst->tp_mdix_ctrl = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKINFO_TRANSCEIVER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.transceiver = 1;
			dst->transceiver = mnl_attr_get_u8(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_linkinfo_get_rsp *
ethtool_linkinfo_get(struct ynl_sock *ys, struct ethtool_linkinfo_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_linkinfo_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_LINKINFO_GET, 1);
	ys->req_policy = &ethtool_linkinfo_nest;
	yrs.yarg.rsp_policy = &ethtool_linkinfo_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKINFO_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_linkinfo_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_LINKINFO_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_linkinfo_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_LINKINFO_GET - dump */
void ethtool_linkinfo_get_list_free(struct ethtool_linkinfo_get_list *rsp)
{
	struct ethtool_linkinfo_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_linkinfo_get_list *
ethtool_linkinfo_get_dump(struct ynl_sock *ys,
			  struct ethtool_linkinfo_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_linkinfo_get_list);
	yds.cb = ethtool_linkinfo_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_LINKINFO_GET;
	yds.rsp_policy = &ethtool_linkinfo_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_LINKINFO_GET, 1);
	ys->req_policy = &ethtool_linkinfo_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKINFO_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_linkinfo_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_LINKINFO_GET - notify */
void ethtool_linkinfo_get_ntf_free(struct ethtool_linkinfo_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_LINKINFO_SET ============== */
/* ETHTOOL_MSG_LINKINFO_SET - do */
void ethtool_linkinfo_set_req_free(struct ethtool_linkinfo_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_linkinfo_set(struct ynl_sock *ys,
			 struct ethtool_linkinfo_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_LINKINFO_SET, 1);
	ys->req_policy = &ethtool_linkinfo_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKINFO_HEADER, &req->header);
	if (req->_present.port)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKINFO_PORT, req->port);
	if (req->_present.phyaddr)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKINFO_PHYADDR, req->phyaddr);
	if (req->_present.tp_mdix)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKINFO_TP_MDIX, req->tp_mdix);
	if (req->_present.tp_mdix_ctrl)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKINFO_TP_MDIX_CTRL, req->tp_mdix_ctrl);
	if (req->_present.transceiver)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKINFO_TRANSCEIVER, req->transceiver);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_LINKMODES_GET ============== */
/* ETHTOOL_MSG_LINKMODES_GET - do */
void ethtool_linkmodes_get_req_free(struct ethtool_linkmodes_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_linkmodes_get_rsp_free(struct ethtool_linkmodes_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->ours);
	ethtool_bitset_free(&rsp->peer);
	free(rsp);
}

int ethtool_linkmodes_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_linkmodes_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_LINKMODES_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_LINKMODES_AUTONEG) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.autoneg = 1;
			dst->autoneg = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKMODES_OURS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ours = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->ours;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_LINKMODES_PEER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.peer = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->peer;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_LINKMODES_SPEED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.speed = 1;
			dst->speed = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_LINKMODES_DUPLEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.duplex = 1;
			dst->duplex = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.master_slave_cfg = 1;
			dst->master_slave_cfg = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKMODES_MASTER_SLAVE_STATE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.master_slave_state = 1;
			dst->master_slave_state = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKMODES_LANES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.lanes = 1;
			dst->lanes = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_LINKMODES_RATE_MATCHING) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rate_matching = 1;
			dst->rate_matching = mnl_attr_get_u8(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_linkmodes_get_rsp *
ethtool_linkmodes_get(struct ynl_sock *ys,
		      struct ethtool_linkmodes_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_linkmodes_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_LINKMODES_GET, 1);
	ys->req_policy = &ethtool_linkmodes_nest;
	yrs.yarg.rsp_policy = &ethtool_linkmodes_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKMODES_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_linkmodes_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_LINKMODES_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_linkmodes_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_LINKMODES_GET - dump */
void ethtool_linkmodes_get_list_free(struct ethtool_linkmodes_get_list *rsp)
{
	struct ethtool_linkmodes_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.ours);
		ethtool_bitset_free(&rsp->obj.peer);
		free(rsp);
	}
}

struct ethtool_linkmodes_get_list *
ethtool_linkmodes_get_dump(struct ynl_sock *ys,
			   struct ethtool_linkmodes_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_linkmodes_get_list);
	yds.cb = ethtool_linkmodes_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_LINKMODES_GET;
	yds.rsp_policy = &ethtool_linkmodes_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_LINKMODES_GET, 1);
	ys->req_policy = &ethtool_linkmodes_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKMODES_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_linkmodes_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_LINKMODES_GET - notify */
void ethtool_linkmodes_get_ntf_free(struct ethtool_linkmodes_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.ours);
	ethtool_bitset_free(&rsp->obj.peer);
	free(rsp);
}

/* ============== ETHTOOL_MSG_LINKMODES_SET ============== */
/* ETHTOOL_MSG_LINKMODES_SET - do */
void ethtool_linkmodes_set_req_free(struct ethtool_linkmodes_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->ours);
	ethtool_bitset_free(&req->peer);
	free(req);
}

int ethtool_linkmodes_set(struct ynl_sock *ys,
			  struct ethtool_linkmodes_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_LINKMODES_SET, 1);
	ys->req_policy = &ethtool_linkmodes_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKMODES_HEADER, &req->header);
	if (req->_present.autoneg)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKMODES_AUTONEG, req->autoneg);
	if (req->_present.ours)
		ethtool_bitset_put(nlh, ETHTOOL_A_LINKMODES_OURS, &req->ours);
	if (req->_present.peer)
		ethtool_bitset_put(nlh, ETHTOOL_A_LINKMODES_PEER, &req->peer);
	if (req->_present.speed)
		mnl_attr_put_u32(nlh, ETHTOOL_A_LINKMODES_SPEED, req->speed);
	if (req->_present.duplex)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKMODES_DUPLEX, req->duplex);
	if (req->_present.master_slave_cfg)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG, req->master_slave_cfg);
	if (req->_present.master_slave_state)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKMODES_MASTER_SLAVE_STATE, req->master_slave_state);
	if (req->_present.lanes)
		mnl_attr_put_u32(nlh, ETHTOOL_A_LINKMODES_LANES, req->lanes);
	if (req->_present.rate_matching)
		mnl_attr_put_u8(nlh, ETHTOOL_A_LINKMODES_RATE_MATCHING, req->rate_matching);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_LINKSTATE_GET ============== */
/* ETHTOOL_MSG_LINKSTATE_GET - do */
void ethtool_linkstate_get_req_free(struct ethtool_linkstate_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_linkstate_get_rsp_free(struct ethtool_linkstate_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_linkstate_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_linkstate_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_LINKSTATE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_LINKSTATE_LINK) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.link = 1;
			dst->link = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKSTATE_SQI) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sqi = 1;
			dst->sqi = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_LINKSTATE_SQI_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sqi_max = 1;
			dst->sqi_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_LINKSTATE_EXT_STATE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ext_state = 1;
			dst->ext_state = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKSTATE_EXT_SUBSTATE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ext_substate = 1;
			dst->ext_substate = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_LINKSTATE_EXT_DOWN_CNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ext_down_cnt = 1;
			dst->ext_down_cnt = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_linkstate_get_rsp *
ethtool_linkstate_get(struct ynl_sock *ys,
		      struct ethtool_linkstate_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_linkstate_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_LINKSTATE_GET, 1);
	ys->req_policy = &ethtool_linkstate_nest;
	yrs.yarg.rsp_policy = &ethtool_linkstate_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKSTATE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_linkstate_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_LINKSTATE_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_linkstate_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_LINKSTATE_GET - dump */
void ethtool_linkstate_get_list_free(struct ethtool_linkstate_get_list *rsp)
{
	struct ethtool_linkstate_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_linkstate_get_list *
ethtool_linkstate_get_dump(struct ynl_sock *ys,
			   struct ethtool_linkstate_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_linkstate_get_list);
	yds.cb = ethtool_linkstate_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_LINKSTATE_GET;
	yds.rsp_policy = &ethtool_linkstate_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_LINKSTATE_GET, 1);
	ys->req_policy = &ethtool_linkstate_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_LINKSTATE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_linkstate_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_DEBUG_GET ============== */
/* ETHTOOL_MSG_DEBUG_GET - do */
void ethtool_debug_get_req_free(struct ethtool_debug_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_debug_get_rsp_free(struct ethtool_debug_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->msgmask);
	free(rsp);
}

int ethtool_debug_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_debug_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_DEBUG_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_DEBUG_MSGMASK) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.msgmask = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->msgmask;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_debug_get_rsp *
ethtool_debug_get(struct ynl_sock *ys, struct ethtool_debug_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_debug_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_DEBUG_GET, 1);
	ys->req_policy = &ethtool_debug_nest;
	yrs.yarg.rsp_policy = &ethtool_debug_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_DEBUG_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_debug_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_DEBUG_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_debug_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_DEBUG_GET - dump */
void ethtool_debug_get_list_free(struct ethtool_debug_get_list *rsp)
{
	struct ethtool_debug_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.msgmask);
		free(rsp);
	}
}

struct ethtool_debug_get_list *
ethtool_debug_get_dump(struct ynl_sock *ys,
		       struct ethtool_debug_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_debug_get_list);
	yds.cb = ethtool_debug_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_DEBUG_GET;
	yds.rsp_policy = &ethtool_debug_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_DEBUG_GET, 1);
	ys->req_policy = &ethtool_debug_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_DEBUG_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_debug_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_DEBUG_GET - notify */
void ethtool_debug_get_ntf_free(struct ethtool_debug_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.msgmask);
	free(rsp);
}

/* ============== ETHTOOL_MSG_DEBUG_SET ============== */
/* ETHTOOL_MSG_DEBUG_SET - do */
void ethtool_debug_set_req_free(struct ethtool_debug_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->msgmask);
	free(req);
}

int ethtool_debug_set(struct ynl_sock *ys, struct ethtool_debug_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_DEBUG_SET, 1);
	ys->req_policy = &ethtool_debug_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_DEBUG_HEADER, &req->header);
	if (req->_present.msgmask)
		ethtool_bitset_put(nlh, ETHTOOL_A_DEBUG_MSGMASK, &req->msgmask);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_WOL_GET ============== */
/* ETHTOOL_MSG_WOL_GET - do */
void ethtool_wol_get_req_free(struct ethtool_wol_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_wol_get_rsp_free(struct ethtool_wol_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->modes);
	free(rsp->sopass);
	free(rsp);
}

int ethtool_wol_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_wol_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_WOL_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_WOL_MODES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.modes = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->modes;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_WOL_SOPASS) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.sopass_len = len;
			dst->sopass = malloc(len);
			memcpy(dst->sopass, mnl_attr_get_payload(attr), len);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_wol_get_rsp *
ethtool_wol_get(struct ynl_sock *ys, struct ethtool_wol_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_wol_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_WOL_GET, 1);
	ys->req_policy = &ethtool_wol_nest;
	yrs.yarg.rsp_policy = &ethtool_wol_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_WOL_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_wol_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_WOL_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_wol_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_WOL_GET - dump */
void ethtool_wol_get_list_free(struct ethtool_wol_get_list *rsp)
{
	struct ethtool_wol_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.modes);
		free(rsp->obj.sopass);
		free(rsp);
	}
}

struct ethtool_wol_get_list *
ethtool_wol_get_dump(struct ynl_sock *ys, struct ethtool_wol_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_wol_get_list);
	yds.cb = ethtool_wol_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_WOL_GET;
	yds.rsp_policy = &ethtool_wol_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_WOL_GET, 1);
	ys->req_policy = &ethtool_wol_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_WOL_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_wol_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_WOL_GET - notify */
void ethtool_wol_get_ntf_free(struct ethtool_wol_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.modes);
	free(rsp->obj.sopass);
	free(rsp);
}

/* ============== ETHTOOL_MSG_WOL_SET ============== */
/* ETHTOOL_MSG_WOL_SET - do */
void ethtool_wol_set_req_free(struct ethtool_wol_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->modes);
	free(req->sopass);
	free(req);
}

int ethtool_wol_set(struct ynl_sock *ys, struct ethtool_wol_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_WOL_SET, 1);
	ys->req_policy = &ethtool_wol_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_WOL_HEADER, &req->header);
	if (req->_present.modes)
		ethtool_bitset_put(nlh, ETHTOOL_A_WOL_MODES, &req->modes);
	if (req->_present.sopass_len)
		mnl_attr_put(nlh, ETHTOOL_A_WOL_SOPASS, req->_present.sopass_len, req->sopass);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_FEATURES_GET ============== */
/* ETHTOOL_MSG_FEATURES_GET - do */
void ethtool_features_get_req_free(struct ethtool_features_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_features_get_rsp_free(struct ethtool_features_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->hw);
	ethtool_bitset_free(&rsp->wanted);
	ethtool_bitset_free(&rsp->active);
	ethtool_bitset_free(&rsp->nochange);
	free(rsp);
}

int ethtool_features_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_features_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_FEATURES_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_HW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.hw = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->hw;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_WANTED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.wanted = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->wanted;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_ACTIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.active = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->active;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_NOCHANGE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.nochange = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->nochange;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_features_get_rsp *
ethtool_features_get(struct ynl_sock *ys, struct ethtool_features_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_features_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_FEATURES_GET, 1);
	ys->req_policy = &ethtool_features_nest;
	yrs.yarg.rsp_policy = &ethtool_features_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEATURES_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_features_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_FEATURES_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_features_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_FEATURES_GET - dump */
void ethtool_features_get_list_free(struct ethtool_features_get_list *rsp)
{
	struct ethtool_features_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.hw);
		ethtool_bitset_free(&rsp->obj.wanted);
		ethtool_bitset_free(&rsp->obj.active);
		ethtool_bitset_free(&rsp->obj.nochange);
		free(rsp);
	}
}

struct ethtool_features_get_list *
ethtool_features_get_dump(struct ynl_sock *ys,
			  struct ethtool_features_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_features_get_list);
	yds.cb = ethtool_features_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_FEATURES_GET;
	yds.rsp_policy = &ethtool_features_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_FEATURES_GET, 1);
	ys->req_policy = &ethtool_features_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEATURES_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_features_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_FEATURES_GET - notify */
void ethtool_features_get_ntf_free(struct ethtool_features_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.hw);
	ethtool_bitset_free(&rsp->obj.wanted);
	ethtool_bitset_free(&rsp->obj.active);
	ethtool_bitset_free(&rsp->obj.nochange);
	free(rsp);
}

/* ============== ETHTOOL_MSG_FEATURES_SET ============== */
/* ETHTOOL_MSG_FEATURES_SET - do */
void ethtool_features_set_req_free(struct ethtool_features_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->hw);
	ethtool_bitset_free(&req->wanted);
	ethtool_bitset_free(&req->active);
	ethtool_bitset_free(&req->nochange);
	free(req);
}

void ethtool_features_set_rsp_free(struct ethtool_features_set_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->hw);
	ethtool_bitset_free(&rsp->wanted);
	ethtool_bitset_free(&rsp->active);
	ethtool_bitset_free(&rsp->nochange);
	free(rsp);
}

int ethtool_features_set_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_features_set_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_FEATURES_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_HW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.hw = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->hw;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_WANTED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.wanted = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->wanted;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_ACTIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.active = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->active;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEATURES_NOCHANGE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.nochange = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->nochange;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_features_set_rsp *
ethtool_features_set(struct ynl_sock *ys, struct ethtool_features_set_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_features_set_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_FEATURES_SET, 1);
	ys->req_policy = &ethtool_features_nest;
	yrs.yarg.rsp_policy = &ethtool_features_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEATURES_HEADER, &req->header);
	if (req->_present.hw)
		ethtool_bitset_put(nlh, ETHTOOL_A_FEATURES_HW, &req->hw);
	if (req->_present.wanted)
		ethtool_bitset_put(nlh, ETHTOOL_A_FEATURES_WANTED, &req->wanted);
	if (req->_present.active)
		ethtool_bitset_put(nlh, ETHTOOL_A_FEATURES_ACTIVE, &req->active);
	if (req->_present.nochange)
		ethtool_bitset_put(nlh, ETHTOOL_A_FEATURES_NOCHANGE, &req->nochange);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_features_set_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_FEATURES_SET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_features_set_rsp_free(rsp);
	return NULL;
}

/* ============== ETHTOOL_MSG_PRIVFLAGS_GET ============== */
/* ETHTOOL_MSG_PRIVFLAGS_GET - do */
void ethtool_privflags_get_req_free(struct ethtool_privflags_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_privflags_get_rsp_free(struct ethtool_privflags_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->flags);
	free(rsp);
}

int ethtool_privflags_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_privflags_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PRIVFLAGS_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PRIVFLAGS_FLAGS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.flags = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->flags;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_privflags_get_rsp *
ethtool_privflags_get(struct ynl_sock *ys,
		      struct ethtool_privflags_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_privflags_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PRIVFLAGS_GET, 1);
	ys->req_policy = &ethtool_privflags_nest;
	yrs.yarg.rsp_policy = &ethtool_privflags_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PRIVFLAGS_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_privflags_get_rsp_parse;
	yrs.rsp_cmd = 14;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_privflags_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PRIVFLAGS_GET - dump */
void ethtool_privflags_get_list_free(struct ethtool_privflags_get_list *rsp)
{
	struct ethtool_privflags_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.flags);
		free(rsp);
	}
}

struct ethtool_privflags_get_list *
ethtool_privflags_get_dump(struct ynl_sock *ys,
			   struct ethtool_privflags_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_privflags_get_list);
	yds.cb = ethtool_privflags_get_rsp_parse;
	yds.rsp_cmd = 14;
	yds.rsp_policy = &ethtool_privflags_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PRIVFLAGS_GET, 1);
	ys->req_policy = &ethtool_privflags_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PRIVFLAGS_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_privflags_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_PRIVFLAGS_GET - notify */
void ethtool_privflags_get_ntf_free(struct ethtool_privflags_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.flags);
	free(rsp);
}

/* ============== ETHTOOL_MSG_PRIVFLAGS_SET ============== */
/* ETHTOOL_MSG_PRIVFLAGS_SET - do */
void ethtool_privflags_set_req_free(struct ethtool_privflags_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->flags);
	free(req);
}

int ethtool_privflags_set(struct ynl_sock *ys,
			  struct ethtool_privflags_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PRIVFLAGS_SET, 1);
	ys->req_policy = &ethtool_privflags_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PRIVFLAGS_HEADER, &req->header);
	if (req->_present.flags)
		ethtool_bitset_put(nlh, ETHTOOL_A_PRIVFLAGS_FLAGS, &req->flags);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_RINGS_GET ============== */
/* ETHTOOL_MSG_RINGS_GET - do */
void ethtool_rings_get_req_free(struct ethtool_rings_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_rings_get_rsp_free(struct ethtool_rings_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_rings_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_rings_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_RINGS_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_RINGS_RX_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max = 1;
			dst->rx_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_MINI_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_mini_max = 1;
			dst->rx_mini_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_JUMBO_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_jumbo_max = 1;
			dst->rx_jumbo_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_TX_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max = 1;
			dst->tx_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx = 1;
			dst->rx = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_MINI) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_mini = 1;
			dst->rx_mini = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_JUMBO) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_jumbo = 1;
			dst->rx_jumbo = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_TX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx = 1;
			dst->tx = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_BUF_LEN) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_buf_len = 1;
			dst->rx_buf_len = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_TCP_DATA_SPLIT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tcp_data_split = 1;
			dst->tcp_data_split = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_RINGS_CQE_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.cqe_size = 1;
			dst->cqe_size = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_TX_PUSH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_push = 1;
			dst->tx_push = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_RINGS_RX_PUSH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_push = 1;
			dst->rx_push = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_push_buf_len = 1;
			dst->tx_push_buf_len = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_push_buf_len_max = 1;
			dst->tx_push_buf_len_max = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_rings_get_rsp *
ethtool_rings_get(struct ynl_sock *ys, struct ethtool_rings_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_rings_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_RINGS_GET, 1);
	ys->req_policy = &ethtool_rings_nest;
	yrs.yarg.rsp_policy = &ethtool_rings_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_RINGS_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_rings_get_rsp_parse;
	yrs.rsp_cmd = 16;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_rings_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_RINGS_GET - dump */
void ethtool_rings_get_list_free(struct ethtool_rings_get_list *rsp)
{
	struct ethtool_rings_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_rings_get_list *
ethtool_rings_get_dump(struct ynl_sock *ys,
		       struct ethtool_rings_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_rings_get_list);
	yds.cb = ethtool_rings_get_rsp_parse;
	yds.rsp_cmd = 16;
	yds.rsp_policy = &ethtool_rings_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_RINGS_GET, 1);
	ys->req_policy = &ethtool_rings_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_RINGS_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_rings_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_RINGS_GET - notify */
void ethtool_rings_get_ntf_free(struct ethtool_rings_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_RINGS_SET ============== */
/* ETHTOOL_MSG_RINGS_SET - do */
void ethtool_rings_set_req_free(struct ethtool_rings_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_rings_set(struct ynl_sock *ys, struct ethtool_rings_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_RINGS_SET, 1);
	ys->req_policy = &ethtool_rings_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_RINGS_HEADER, &req->header);
	if (req->_present.rx_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_MAX, req->rx_max);
	if (req->_present.rx_mini_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_MINI_MAX, req->rx_mini_max);
	if (req->_present.rx_jumbo_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_JUMBO_MAX, req->rx_jumbo_max);
	if (req->_present.tx_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_TX_MAX, req->tx_max);
	if (req->_present.rx)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX, req->rx);
	if (req->_present.rx_mini)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_MINI, req->rx_mini);
	if (req->_present.rx_jumbo)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_JUMBO, req->rx_jumbo);
	if (req->_present.tx)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_TX, req->tx);
	if (req->_present.rx_buf_len)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_RX_BUF_LEN, req->rx_buf_len);
	if (req->_present.tcp_data_split)
		mnl_attr_put_u8(nlh, ETHTOOL_A_RINGS_TCP_DATA_SPLIT, req->tcp_data_split);
	if (req->_present.cqe_size)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_CQE_SIZE, req->cqe_size);
	if (req->_present.tx_push)
		mnl_attr_put_u8(nlh, ETHTOOL_A_RINGS_TX_PUSH, req->tx_push);
	if (req->_present.rx_push)
		mnl_attr_put_u8(nlh, ETHTOOL_A_RINGS_RX_PUSH, req->rx_push);
	if (req->_present.tx_push_buf_len)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN, req->tx_push_buf_len);
	if (req->_present.tx_push_buf_len_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_RINGS_TX_PUSH_BUF_LEN_MAX, req->tx_push_buf_len_max);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_CHANNELS_GET ============== */
/* ETHTOOL_MSG_CHANNELS_GET - do */
void ethtool_channels_get_req_free(struct ethtool_channels_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_channels_get_rsp_free(struct ethtool_channels_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_channels_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_channels_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CHANNELS_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_CHANNELS_RX_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max = 1;
			dst->rx_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_TX_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max = 1;
			dst->tx_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_OTHER_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.other_max = 1;
			dst->other_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_COMBINED_MAX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.combined_max = 1;
			dst->combined_max = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_RX_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_count = 1;
			dst->rx_count = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_TX_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_count = 1;
			dst->tx_count = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_OTHER_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.other_count = 1;
			dst->other_count = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_CHANNELS_COMBINED_COUNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.combined_count = 1;
			dst->combined_count = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_channels_get_rsp *
ethtool_channels_get(struct ynl_sock *ys, struct ethtool_channels_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_channels_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_CHANNELS_GET, 1);
	ys->req_policy = &ethtool_channels_nest;
	yrs.yarg.rsp_policy = &ethtool_channels_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_CHANNELS_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_channels_get_rsp_parse;
	yrs.rsp_cmd = 18;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_channels_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_CHANNELS_GET - dump */
void ethtool_channels_get_list_free(struct ethtool_channels_get_list *rsp)
{
	struct ethtool_channels_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_channels_get_list *
ethtool_channels_get_dump(struct ynl_sock *ys,
			  struct ethtool_channels_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_channels_get_list);
	yds.cb = ethtool_channels_get_rsp_parse;
	yds.rsp_cmd = 18;
	yds.rsp_policy = &ethtool_channels_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_CHANNELS_GET, 1);
	ys->req_policy = &ethtool_channels_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_CHANNELS_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_channels_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_CHANNELS_GET - notify */
void ethtool_channels_get_ntf_free(struct ethtool_channels_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_CHANNELS_SET ============== */
/* ETHTOOL_MSG_CHANNELS_SET - do */
void ethtool_channels_set_req_free(struct ethtool_channels_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_channels_set(struct ynl_sock *ys,
			 struct ethtool_channels_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_CHANNELS_SET, 1);
	ys->req_policy = &ethtool_channels_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_CHANNELS_HEADER, &req->header);
	if (req->_present.rx_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_RX_MAX, req->rx_max);
	if (req->_present.tx_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_TX_MAX, req->tx_max);
	if (req->_present.other_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_OTHER_MAX, req->other_max);
	if (req->_present.combined_max)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_COMBINED_MAX, req->combined_max);
	if (req->_present.rx_count)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_RX_COUNT, req->rx_count);
	if (req->_present.tx_count)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_TX_COUNT, req->tx_count);
	if (req->_present.other_count)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_OTHER_COUNT, req->other_count);
	if (req->_present.combined_count)
		mnl_attr_put_u32(nlh, ETHTOOL_A_CHANNELS_COMBINED_COUNT, req->combined_count);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_COALESCE_GET ============== */
/* ETHTOOL_MSG_COALESCE_GET - do */
void ethtool_coalesce_get_req_free(struct ethtool_coalesce_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_coalesce_get_rsp_free(struct ethtool_coalesce_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_coalesce_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_coalesce_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_COALESCE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_COALESCE_RX_USECS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_usecs = 1;
			dst->rx_usecs = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_MAX_FRAMES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max_frames = 1;
			dst->rx_max_frames = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_USECS_IRQ) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_usecs_irq = 1;
			dst->rx_usecs_irq = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max_frames_irq = 1;
			dst->rx_max_frames_irq = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_USECS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_usecs = 1;
			dst->tx_usecs = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_MAX_FRAMES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max_frames = 1;
			dst->tx_max_frames = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_USECS_IRQ) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_usecs_irq = 1;
			dst->tx_usecs_irq = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max_frames_irq = 1;
			dst->tx_max_frames_irq = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_STATS_BLOCK_USECS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stats_block_usecs = 1;
			dst->stats_block_usecs = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.use_adaptive_rx = 1;
			dst->use_adaptive_rx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.use_adaptive_tx = 1;
			dst->use_adaptive_tx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_COALESCE_PKT_RATE_LOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pkt_rate_low = 1;
			dst->pkt_rate_low = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_USECS_LOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_usecs_low = 1;
			dst->rx_usecs_low = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max_frames_low = 1;
			dst->rx_max_frames_low = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_USECS_LOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_usecs_low = 1;
			dst->tx_usecs_low = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max_frames_low = 1;
			dst->tx_max_frames_low = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_PKT_RATE_HIGH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pkt_rate_high = 1;
			dst->pkt_rate_high = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_USECS_HIGH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_usecs_high = 1;
			dst->rx_usecs_high = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_max_frames_high = 1;
			dst->rx_max_frames_high = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_USECS_HIGH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_usecs_high = 1;
			dst->tx_usecs_high = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_max_frames_high = 1;
			dst->tx_max_frames_high = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rate_sample_interval = 1;
			dst->rate_sample_interval = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_USE_CQE_MODE_TX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.use_cqe_mode_tx = 1;
			dst->use_cqe_mode_tx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_COALESCE_USE_CQE_MODE_RX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.use_cqe_mode_rx = 1;
			dst->use_cqe_mode_rx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_aggr_max_bytes = 1;
			dst->tx_aggr_max_bytes = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_aggr_max_frames = 1;
			dst->tx_aggr_max_frames = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_aggr_time_usecs = 1;
			dst->tx_aggr_time_usecs = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_coalesce_get_rsp *
ethtool_coalesce_get(struct ynl_sock *ys, struct ethtool_coalesce_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_coalesce_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_COALESCE_GET, 1);
	ys->req_policy = &ethtool_coalesce_nest;
	yrs.yarg.rsp_policy = &ethtool_coalesce_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_COALESCE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_coalesce_get_rsp_parse;
	yrs.rsp_cmd = 20;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_coalesce_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_COALESCE_GET - dump */
void ethtool_coalesce_get_list_free(struct ethtool_coalesce_get_list *rsp)
{
	struct ethtool_coalesce_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_coalesce_get_list *
ethtool_coalesce_get_dump(struct ynl_sock *ys,
			  struct ethtool_coalesce_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_coalesce_get_list);
	yds.cb = ethtool_coalesce_get_rsp_parse;
	yds.rsp_cmd = 20;
	yds.rsp_policy = &ethtool_coalesce_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_COALESCE_GET, 1);
	ys->req_policy = &ethtool_coalesce_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_COALESCE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_coalesce_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_COALESCE_GET - notify */
void ethtool_coalesce_get_ntf_free(struct ethtool_coalesce_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_COALESCE_SET ============== */
/* ETHTOOL_MSG_COALESCE_SET - do */
void ethtool_coalesce_set_req_free(struct ethtool_coalesce_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_coalesce_set(struct ynl_sock *ys,
			 struct ethtool_coalesce_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_COALESCE_SET, 1);
	ys->req_policy = &ethtool_coalesce_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_COALESCE_HEADER, &req->header);
	if (req->_present.rx_usecs)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_USECS, req->rx_usecs);
	if (req->_present.rx_max_frames)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_MAX_FRAMES, req->rx_max_frames);
	if (req->_present.rx_usecs_irq)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_USECS_IRQ, req->rx_usecs_irq);
	if (req->_present.rx_max_frames_irq)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ, req->rx_max_frames_irq);
	if (req->_present.tx_usecs)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_USECS, req->tx_usecs);
	if (req->_present.tx_max_frames)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_MAX_FRAMES, req->tx_max_frames);
	if (req->_present.tx_usecs_irq)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_USECS_IRQ, req->tx_usecs_irq);
	if (req->_present.tx_max_frames_irq)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ, req->tx_max_frames_irq);
	if (req->_present.stats_block_usecs)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_STATS_BLOCK_USECS, req->stats_block_usecs);
	if (req->_present.use_adaptive_rx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX, req->use_adaptive_rx);
	if (req->_present.use_adaptive_tx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX, req->use_adaptive_tx);
	if (req->_present.pkt_rate_low)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_PKT_RATE_LOW, req->pkt_rate_low);
	if (req->_present.rx_usecs_low)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_USECS_LOW, req->rx_usecs_low);
	if (req->_present.rx_max_frames_low)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW, req->rx_max_frames_low);
	if (req->_present.tx_usecs_low)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_USECS_LOW, req->tx_usecs_low);
	if (req->_present.tx_max_frames_low)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW, req->tx_max_frames_low);
	if (req->_present.pkt_rate_high)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_PKT_RATE_HIGH, req->pkt_rate_high);
	if (req->_present.rx_usecs_high)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_USECS_HIGH, req->rx_usecs_high);
	if (req->_present.rx_max_frames_high)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH, req->rx_max_frames_high);
	if (req->_present.tx_usecs_high)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_USECS_HIGH, req->tx_usecs_high);
	if (req->_present.tx_max_frames_high)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH, req->tx_max_frames_high);
	if (req->_present.rate_sample_interval)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL, req->rate_sample_interval);
	if (req->_present.use_cqe_mode_tx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_COALESCE_USE_CQE_MODE_TX, req->use_cqe_mode_tx);
	if (req->_present.use_cqe_mode_rx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_COALESCE_USE_CQE_MODE_RX, req->use_cqe_mode_rx);
	if (req->_present.tx_aggr_max_bytes)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES, req->tx_aggr_max_bytes);
	if (req->_present.tx_aggr_max_frames)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES, req->tx_aggr_max_frames);
	if (req->_present.tx_aggr_time_usecs)
		mnl_attr_put_u32(nlh, ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS, req->tx_aggr_time_usecs);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_PAUSE_GET ============== */
/* ETHTOOL_MSG_PAUSE_GET - do */
void ethtool_pause_get_req_free(struct ethtool_pause_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_pause_get_rsp_free(struct ethtool_pause_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_pause_stat_free(&rsp->stats);
	free(rsp);
}

int ethtool_pause_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_pause_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PAUSE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PAUSE_AUTONEG) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.autoneg = 1;
			dst->autoneg = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PAUSE_RX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx = 1;
			dst->rx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PAUSE_TX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx = 1;
			dst->tx = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PAUSE_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stats = 1;

			parg.rsp_policy = &ethtool_pause_stat_nest;
			parg.data = &dst->stats;
			if (ethtool_pause_stat_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PAUSE_STATS_SRC) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stats_src = 1;
			dst->stats_src = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_pause_get_rsp *
ethtool_pause_get(struct ynl_sock *ys, struct ethtool_pause_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_pause_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PAUSE_GET, 1);
	ys->req_policy = &ethtool_pause_nest;
	yrs.yarg.rsp_policy = &ethtool_pause_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PAUSE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_pause_get_rsp_parse;
	yrs.rsp_cmd = 22;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_pause_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PAUSE_GET - dump */
void ethtool_pause_get_list_free(struct ethtool_pause_get_list *rsp)
{
	struct ethtool_pause_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_pause_stat_free(&rsp->obj.stats);
		free(rsp);
	}
}

struct ethtool_pause_get_list *
ethtool_pause_get_dump(struct ynl_sock *ys,
		       struct ethtool_pause_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_pause_get_list);
	yds.cb = ethtool_pause_get_rsp_parse;
	yds.rsp_cmd = 22;
	yds.rsp_policy = &ethtool_pause_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PAUSE_GET, 1);
	ys->req_policy = &ethtool_pause_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PAUSE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_pause_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_PAUSE_GET - notify */
void ethtool_pause_get_ntf_free(struct ethtool_pause_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_pause_stat_free(&rsp->obj.stats);
	free(rsp);
}

/* ============== ETHTOOL_MSG_PAUSE_SET ============== */
/* ETHTOOL_MSG_PAUSE_SET - do */
void ethtool_pause_set_req_free(struct ethtool_pause_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_pause_stat_free(&req->stats);
	free(req);
}

int ethtool_pause_set(struct ynl_sock *ys, struct ethtool_pause_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PAUSE_SET, 1);
	ys->req_policy = &ethtool_pause_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PAUSE_HEADER, &req->header);
	if (req->_present.autoneg)
		mnl_attr_put_u8(nlh, ETHTOOL_A_PAUSE_AUTONEG, req->autoneg);
	if (req->_present.rx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_PAUSE_RX, req->rx);
	if (req->_present.tx)
		mnl_attr_put_u8(nlh, ETHTOOL_A_PAUSE_TX, req->tx);
	if (req->_present.stats)
		ethtool_pause_stat_put(nlh, ETHTOOL_A_PAUSE_STATS, &req->stats);
	if (req->_present.stats_src)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PAUSE_STATS_SRC, req->stats_src);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_EEE_GET ============== */
/* ETHTOOL_MSG_EEE_GET - do */
void ethtool_eee_get_req_free(struct ethtool_eee_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_eee_get_rsp_free(struct ethtool_eee_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->modes_ours);
	ethtool_bitset_free(&rsp->modes_peer);
	free(rsp);
}

int ethtool_eee_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_eee_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_EEE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_EEE_MODES_OURS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.modes_ours = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->modes_ours;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_EEE_MODES_PEER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.modes_peer = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->modes_peer;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_EEE_ACTIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.active = 1;
			dst->active = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_EEE_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.enabled = 1;
			dst->enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_EEE_TX_LPI_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_lpi_enabled = 1;
			dst->tx_lpi_enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_EEE_TX_LPI_TIMER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_lpi_timer = 1;
			dst->tx_lpi_timer = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_eee_get_rsp *
ethtool_eee_get(struct ynl_sock *ys, struct ethtool_eee_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_eee_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_EEE_GET, 1);
	ys->req_policy = &ethtool_eee_nest;
	yrs.yarg.rsp_policy = &ethtool_eee_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_EEE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_eee_get_rsp_parse;
	yrs.rsp_cmd = 24;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_eee_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_EEE_GET - dump */
void ethtool_eee_get_list_free(struct ethtool_eee_get_list *rsp)
{
	struct ethtool_eee_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.modes_ours);
		ethtool_bitset_free(&rsp->obj.modes_peer);
		free(rsp);
	}
}

struct ethtool_eee_get_list *
ethtool_eee_get_dump(struct ynl_sock *ys, struct ethtool_eee_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_eee_get_list);
	yds.cb = ethtool_eee_get_rsp_parse;
	yds.rsp_cmd = 24;
	yds.rsp_policy = &ethtool_eee_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_EEE_GET, 1);
	ys->req_policy = &ethtool_eee_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_EEE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_eee_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_EEE_GET - notify */
void ethtool_eee_get_ntf_free(struct ethtool_eee_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.modes_ours);
	ethtool_bitset_free(&rsp->obj.modes_peer);
	free(rsp);
}

/* ============== ETHTOOL_MSG_EEE_SET ============== */
/* ETHTOOL_MSG_EEE_SET - do */
void ethtool_eee_set_req_free(struct ethtool_eee_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->modes_ours);
	ethtool_bitset_free(&req->modes_peer);
	free(req);
}

int ethtool_eee_set(struct ynl_sock *ys, struct ethtool_eee_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_EEE_SET, 1);
	ys->req_policy = &ethtool_eee_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_EEE_HEADER, &req->header);
	if (req->_present.modes_ours)
		ethtool_bitset_put(nlh, ETHTOOL_A_EEE_MODES_OURS, &req->modes_ours);
	if (req->_present.modes_peer)
		ethtool_bitset_put(nlh, ETHTOOL_A_EEE_MODES_PEER, &req->modes_peer);
	if (req->_present.active)
		mnl_attr_put_u8(nlh, ETHTOOL_A_EEE_ACTIVE, req->active);
	if (req->_present.enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_EEE_ENABLED, req->enabled);
	if (req->_present.tx_lpi_enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_EEE_TX_LPI_ENABLED, req->tx_lpi_enabled);
	if (req->_present.tx_lpi_timer)
		mnl_attr_put_u32(nlh, ETHTOOL_A_EEE_TX_LPI_TIMER, req->tx_lpi_timer);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_TSINFO_GET ============== */
/* ETHTOOL_MSG_TSINFO_GET - do */
void ethtool_tsinfo_get_req_free(struct ethtool_tsinfo_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_tsinfo_get_rsp_free(struct ethtool_tsinfo_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->timestamping);
	ethtool_bitset_free(&rsp->tx_types);
	ethtool_bitset_free(&rsp->rx_filters);
	free(rsp);
}

int ethtool_tsinfo_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_tsinfo_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_TSINFO_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TSINFO_TIMESTAMPING) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.timestamping = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->timestamping;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TSINFO_TX_TYPES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_types = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->tx_types;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TSINFO_RX_FILTERS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_filters = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->rx_filters;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TSINFO_PHC_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.phc_index = 1;
			dst->phc_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_tsinfo_get_rsp *
ethtool_tsinfo_get(struct ynl_sock *ys, struct ethtool_tsinfo_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_tsinfo_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_TSINFO_GET, 1);
	ys->req_policy = &ethtool_tsinfo_nest;
	yrs.yarg.rsp_policy = &ethtool_tsinfo_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_TSINFO_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_tsinfo_get_rsp_parse;
	yrs.rsp_cmd = 26;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_tsinfo_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_TSINFO_GET - dump */
void ethtool_tsinfo_get_list_free(struct ethtool_tsinfo_get_list *rsp)
{
	struct ethtool_tsinfo_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.timestamping);
		ethtool_bitset_free(&rsp->obj.tx_types);
		ethtool_bitset_free(&rsp->obj.rx_filters);
		free(rsp);
	}
}

struct ethtool_tsinfo_get_list *
ethtool_tsinfo_get_dump(struct ynl_sock *ys,
			struct ethtool_tsinfo_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_tsinfo_get_list);
	yds.cb = ethtool_tsinfo_get_rsp_parse;
	yds.rsp_cmd = 26;
	yds.rsp_policy = &ethtool_tsinfo_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_TSINFO_GET, 1);
	ys->req_policy = &ethtool_tsinfo_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_TSINFO_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_tsinfo_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_CABLE_TEST_ACT ============== */
/* ETHTOOL_MSG_CABLE_TEST_ACT - do */
void ethtool_cable_test_act_req_free(struct ethtool_cable_test_act_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_cable_test_act(struct ynl_sock *ys,
			   struct ethtool_cable_test_act_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_CABLE_TEST_ACT, 1);
	ys->req_policy = &ethtool_cable_test_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_CABLE_TEST_HEADER, &req->header);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_CABLE_TEST_TDR_ACT ============== */
/* ETHTOOL_MSG_CABLE_TEST_TDR_ACT - do */
void
ethtool_cable_test_tdr_act_req_free(struct ethtool_cable_test_tdr_act_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_cable_test_tdr_act(struct ynl_sock *ys,
			       struct ethtool_cable_test_tdr_act_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_CABLE_TEST_TDR_ACT, 1);
	ys->req_policy = &ethtool_cable_test_tdr_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_CABLE_TEST_TDR_HEADER, &req->header);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_TUNNEL_INFO_GET ============== */
/* ETHTOOL_MSG_TUNNEL_INFO_GET - do */
void ethtool_tunnel_info_get_req_free(struct ethtool_tunnel_info_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_tunnel_info_get_rsp_free(struct ethtool_tunnel_info_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_tunnel_udp_free(&rsp->udp_ports);
	free(rsp);
}

int ethtool_tunnel_info_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_tunnel_info_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_TUNNEL_INFO_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_TUNNEL_INFO_UDP_PORTS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.udp_ports = 1;

			parg.rsp_policy = &ethtool_tunnel_udp_nest;
			parg.data = &dst->udp_ports;
			if (ethtool_tunnel_udp_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_tunnel_info_get_rsp *
ethtool_tunnel_info_get(struct ynl_sock *ys,
			struct ethtool_tunnel_info_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_tunnel_info_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_TUNNEL_INFO_GET, 1);
	ys->req_policy = &ethtool_tunnel_info_nest;
	yrs.yarg.rsp_policy = &ethtool_tunnel_info_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_TUNNEL_INFO_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_tunnel_info_get_rsp_parse;
	yrs.rsp_cmd = 29;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_tunnel_info_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_TUNNEL_INFO_GET - dump */
void
ethtool_tunnel_info_get_list_free(struct ethtool_tunnel_info_get_list *rsp)
{
	struct ethtool_tunnel_info_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_tunnel_udp_free(&rsp->obj.udp_ports);
		free(rsp);
	}
}

struct ethtool_tunnel_info_get_list *
ethtool_tunnel_info_get_dump(struct ynl_sock *ys,
			     struct ethtool_tunnel_info_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_tunnel_info_get_list);
	yds.cb = ethtool_tunnel_info_get_rsp_parse;
	yds.rsp_cmd = 29;
	yds.rsp_policy = &ethtool_tunnel_info_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_TUNNEL_INFO_GET, 1);
	ys->req_policy = &ethtool_tunnel_info_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_TUNNEL_INFO_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_tunnel_info_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_FEC_GET ============== */
/* ETHTOOL_MSG_FEC_GET - do */
void ethtool_fec_get_req_free(struct ethtool_fec_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_fec_get_rsp_free(struct ethtool_fec_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_bitset_free(&rsp->modes);
	ethtool_fec_stat_free(&rsp->stats);
	free(rsp);
}

int ethtool_fec_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_fec_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_FEC_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEC_MODES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.modes = 1;

			parg.rsp_policy = &ethtool_bitset_nest;
			parg.data = &dst->modes;
			if (ethtool_bitset_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_FEC_AUTO) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.auto_ = 1;
			dst->auto_ = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_FEC_ACTIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.active = 1;
			dst->active = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_FEC_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stats = 1;

			parg.rsp_policy = &ethtool_fec_stat_nest;
			parg.data = &dst->stats;
			if (ethtool_fec_stat_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_fec_get_rsp *
ethtool_fec_get(struct ynl_sock *ys, struct ethtool_fec_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_fec_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_FEC_GET, 1);
	ys->req_policy = &ethtool_fec_nest;
	yrs.yarg.rsp_policy = &ethtool_fec_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEC_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_fec_get_rsp_parse;
	yrs.rsp_cmd = 30;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_fec_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_FEC_GET - dump */
void ethtool_fec_get_list_free(struct ethtool_fec_get_list *rsp)
{
	struct ethtool_fec_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_bitset_free(&rsp->obj.modes);
		ethtool_fec_stat_free(&rsp->obj.stats);
		free(rsp);
	}
}

struct ethtool_fec_get_list *
ethtool_fec_get_dump(struct ynl_sock *ys, struct ethtool_fec_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_fec_get_list);
	yds.cb = ethtool_fec_get_rsp_parse;
	yds.rsp_cmd = 30;
	yds.rsp_policy = &ethtool_fec_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_FEC_GET, 1);
	ys->req_policy = &ethtool_fec_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEC_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_fec_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_FEC_GET - notify */
void ethtool_fec_get_ntf_free(struct ethtool_fec_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_bitset_free(&rsp->obj.modes);
	ethtool_fec_stat_free(&rsp->obj.stats);
	free(rsp);
}

/* ============== ETHTOOL_MSG_FEC_SET ============== */
/* ETHTOOL_MSG_FEC_SET - do */
void ethtool_fec_set_req_free(struct ethtool_fec_set_req *req)
{
	ethtool_header_free(&req->header);
	ethtool_bitset_free(&req->modes);
	ethtool_fec_stat_free(&req->stats);
	free(req);
}

int ethtool_fec_set(struct ynl_sock *ys, struct ethtool_fec_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_FEC_SET, 1);
	ys->req_policy = &ethtool_fec_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_FEC_HEADER, &req->header);
	if (req->_present.modes)
		ethtool_bitset_put(nlh, ETHTOOL_A_FEC_MODES, &req->modes);
	if (req->_present.auto_)
		mnl_attr_put_u8(nlh, ETHTOOL_A_FEC_AUTO, req->auto_);
	if (req->_present.active)
		mnl_attr_put_u32(nlh, ETHTOOL_A_FEC_ACTIVE, req->active);
	if (req->_present.stats)
		ethtool_fec_stat_put(nlh, ETHTOOL_A_FEC_STATS, &req->stats);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_MODULE_EEPROM_GET ============== */
/* ETHTOOL_MSG_MODULE_EEPROM_GET - do */
void
ethtool_module_eeprom_get_req_free(struct ethtool_module_eeprom_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void
ethtool_module_eeprom_get_rsp_free(struct ethtool_module_eeprom_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp->data);
	free(rsp);
}

int ethtool_module_eeprom_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_module_eeprom_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_MODULE_EEPROM_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_MODULE_EEPROM_OFFSET) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.offset = 1;
			dst->offset = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MODULE_EEPROM_LENGTH) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.length = 1;
			dst->length = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MODULE_EEPROM_PAGE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.page = 1;
			dst->page = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MODULE_EEPROM_BANK) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.bank = 1;
			dst->bank = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.i2c_address = 1;
			dst->i2c_address = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MODULE_EEPROM_DATA) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.data_len = len;
			dst->data = malloc(len);
			memcpy(dst->data, mnl_attr_get_payload(attr), len);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_module_eeprom_get_rsp *
ethtool_module_eeprom_get(struct ynl_sock *ys,
			  struct ethtool_module_eeprom_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_module_eeprom_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_MODULE_EEPROM_GET, 1);
	ys->req_policy = &ethtool_module_eeprom_nest;
	yrs.yarg.rsp_policy = &ethtool_module_eeprom_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MODULE_EEPROM_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_module_eeprom_get_rsp_parse;
	yrs.rsp_cmd = 32;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_module_eeprom_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_MODULE_EEPROM_GET - dump */
void
ethtool_module_eeprom_get_list_free(struct ethtool_module_eeprom_get_list *rsp)
{
	struct ethtool_module_eeprom_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp->obj.data);
		free(rsp);
	}
}

struct ethtool_module_eeprom_get_list *
ethtool_module_eeprom_get_dump(struct ynl_sock *ys,
			       struct ethtool_module_eeprom_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_module_eeprom_get_list);
	yds.cb = ethtool_module_eeprom_get_rsp_parse;
	yds.rsp_cmd = 32;
	yds.rsp_policy = &ethtool_module_eeprom_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_MODULE_EEPROM_GET, 1);
	ys->req_policy = &ethtool_module_eeprom_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MODULE_EEPROM_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_module_eeprom_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_PHC_VCLOCKS_GET ============== */
/* ETHTOOL_MSG_PHC_VCLOCKS_GET - do */
void ethtool_phc_vclocks_get_req_free(struct ethtool_phc_vclocks_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_phc_vclocks_get_rsp_free(struct ethtool_phc_vclocks_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_phc_vclocks_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_phc_vclocks_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PHC_VCLOCKS_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PHC_VCLOCKS_NUM) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.num = 1;
			dst->num = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_phc_vclocks_get_rsp *
ethtool_phc_vclocks_get(struct ynl_sock *ys,
			struct ethtool_phc_vclocks_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_phc_vclocks_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PHC_VCLOCKS_GET, 1);
	ys->req_policy = &ethtool_phc_vclocks_nest;
	yrs.yarg.rsp_policy = &ethtool_phc_vclocks_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PHC_VCLOCKS_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_phc_vclocks_get_rsp_parse;
	yrs.rsp_cmd = 34;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_phc_vclocks_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PHC_VCLOCKS_GET - dump */
void
ethtool_phc_vclocks_get_list_free(struct ethtool_phc_vclocks_get_list *rsp)
{
	struct ethtool_phc_vclocks_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_phc_vclocks_get_list *
ethtool_phc_vclocks_get_dump(struct ynl_sock *ys,
			     struct ethtool_phc_vclocks_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_phc_vclocks_get_list);
	yds.cb = ethtool_phc_vclocks_get_rsp_parse;
	yds.rsp_cmd = 34;
	yds.rsp_policy = &ethtool_phc_vclocks_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PHC_VCLOCKS_GET, 1);
	ys->req_policy = &ethtool_phc_vclocks_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PHC_VCLOCKS_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_phc_vclocks_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_MODULE_GET ============== */
/* ETHTOOL_MSG_MODULE_GET - do */
void ethtool_module_get_req_free(struct ethtool_module_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_module_get_rsp_free(struct ethtool_module_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_module_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_module_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_MODULE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_MODULE_POWER_MODE_POLICY) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.power_mode_policy = 1;
			dst->power_mode_policy = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MODULE_POWER_MODE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.power_mode = 1;
			dst->power_mode = mnl_attr_get_u8(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_module_get_rsp *
ethtool_module_get(struct ynl_sock *ys, struct ethtool_module_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_module_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_MODULE_GET, 1);
	ys->req_policy = &ethtool_module_nest;
	yrs.yarg.rsp_policy = &ethtool_module_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MODULE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_module_get_rsp_parse;
	yrs.rsp_cmd = 35;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_module_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_MODULE_GET - dump */
void ethtool_module_get_list_free(struct ethtool_module_get_list *rsp)
{
	struct ethtool_module_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_module_get_list *
ethtool_module_get_dump(struct ynl_sock *ys,
			struct ethtool_module_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_module_get_list);
	yds.cb = ethtool_module_get_rsp_parse;
	yds.rsp_cmd = 35;
	yds.rsp_policy = &ethtool_module_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_MODULE_GET, 1);
	ys->req_policy = &ethtool_module_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MODULE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_module_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_MODULE_GET - notify */
void ethtool_module_get_ntf_free(struct ethtool_module_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_MODULE_SET ============== */
/* ETHTOOL_MSG_MODULE_SET - do */
void ethtool_module_set_req_free(struct ethtool_module_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_module_set(struct ynl_sock *ys, struct ethtool_module_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_MODULE_SET, 1);
	ys->req_policy = &ethtool_module_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MODULE_HEADER, &req->header);
	if (req->_present.power_mode_policy)
		mnl_attr_put_u8(nlh, ETHTOOL_A_MODULE_POWER_MODE_POLICY, req->power_mode_policy);
	if (req->_present.power_mode)
		mnl_attr_put_u8(nlh, ETHTOOL_A_MODULE_POWER_MODE, req->power_mode);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_PSE_GET ============== */
/* ETHTOOL_MSG_PSE_GET - do */
void ethtool_pse_get_req_free(struct ethtool_pse_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_pse_get_rsp_free(struct ethtool_pse_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_pse_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_pse_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PSE_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PODL_PSE_ADMIN_STATE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.admin_state = 1;
			dst->admin_state = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PODL_PSE_ADMIN_CONTROL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.admin_control = 1;
			dst->admin_control = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PODL_PSE_PW_D_STATUS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pw_d_status = 1;
			dst->pw_d_status = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_pse_get_rsp *
ethtool_pse_get(struct ynl_sock *ys, struct ethtool_pse_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_pse_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PSE_GET, 1);
	ys->req_policy = &ethtool_pse_nest;
	yrs.yarg.rsp_policy = &ethtool_pse_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PSE_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_pse_get_rsp_parse;
	yrs.rsp_cmd = 37;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_pse_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PSE_GET - dump */
void ethtool_pse_get_list_free(struct ethtool_pse_get_list *rsp)
{
	struct ethtool_pse_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_pse_get_list *
ethtool_pse_get_dump(struct ynl_sock *ys, struct ethtool_pse_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_pse_get_list);
	yds.cb = ethtool_pse_get_rsp_parse;
	yds.rsp_cmd = 37;
	yds.rsp_policy = &ethtool_pse_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PSE_GET, 1);
	ys->req_policy = &ethtool_pse_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PSE_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_pse_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_PSE_SET ============== */
/* ETHTOOL_MSG_PSE_SET - do */
void ethtool_pse_set_req_free(struct ethtool_pse_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_pse_set(struct ynl_sock *ys, struct ethtool_pse_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PSE_SET, 1);
	ys->req_policy = &ethtool_pse_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PSE_HEADER, &req->header);
	if (req->_present.admin_state)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PODL_PSE_ADMIN_STATE, req->admin_state);
	if (req->_present.admin_control)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PODL_PSE_ADMIN_CONTROL, req->admin_control);
	if (req->_present.pw_d_status)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PODL_PSE_PW_D_STATUS, req->pw_d_status);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_RSS_GET ============== */
/* ETHTOOL_MSG_RSS_GET - do */
void ethtool_rss_get_req_free(struct ethtool_rss_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_rss_get_rsp_free(struct ethtool_rss_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp->indir);
	free(rsp->hkey);
	free(rsp);
}

int ethtool_rss_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_rss_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_RSS_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_RSS_CONTEXT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.context = 1;
			dst->context = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RSS_HFUNC) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.hfunc = 1;
			dst->hfunc = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_RSS_INDIR) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.indir_len = len;
			dst->indir = malloc(len);
			memcpy(dst->indir, mnl_attr_get_payload(attr), len);
		} else if (type == ETHTOOL_A_RSS_HKEY) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.hkey_len = len;
			dst->hkey = malloc(len);
			memcpy(dst->hkey, mnl_attr_get_payload(attr), len);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_rss_get_rsp *
ethtool_rss_get(struct ynl_sock *ys, struct ethtool_rss_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_rss_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_RSS_GET, 1);
	ys->req_policy = &ethtool_rss_nest;
	yrs.yarg.rsp_policy = &ethtool_rss_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_RSS_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_rss_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_RSS_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_rss_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_RSS_GET - dump */
void ethtool_rss_get_list_free(struct ethtool_rss_get_list *rsp)
{
	struct ethtool_rss_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp->obj.indir);
		free(rsp->obj.hkey);
		free(rsp);
	}
}

struct ethtool_rss_get_list *
ethtool_rss_get_dump(struct ynl_sock *ys, struct ethtool_rss_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_rss_get_list);
	yds.cb = ethtool_rss_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_RSS_GET;
	yds.rsp_policy = &ethtool_rss_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_RSS_GET, 1);
	ys->req_policy = &ethtool_rss_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_RSS_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_rss_get_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_PLCA_GET_CFG ============== */
/* ETHTOOL_MSG_PLCA_GET_CFG - do */
void ethtool_plca_get_cfg_req_free(struct ethtool_plca_get_cfg_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_plca_get_cfg_rsp_free(struct ethtool_plca_get_cfg_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_plca_get_cfg_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_plca_get_cfg_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PLCA_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PLCA_VERSION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.version = 1;
			dst->version = mnl_attr_get_u16(attr);
		} else if (type == ETHTOOL_A_PLCA_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.enabled = 1;
			dst->enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PLCA_STATUS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.status = 1;
			dst->status = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PLCA_NODE_CNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.node_cnt = 1;
			dst->node_cnt = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_NODE_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.node_id = 1;
			dst->node_id = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_TO_TMR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.to_tmr = 1;
			dst->to_tmr = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_BURST_CNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.burst_cnt = 1;
			dst->burst_cnt = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_BURST_TMR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.burst_tmr = 1;
			dst->burst_tmr = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_plca_get_cfg_rsp *
ethtool_plca_get_cfg(struct ynl_sock *ys, struct ethtool_plca_get_cfg_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_plca_get_cfg_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PLCA_GET_CFG, 1);
	ys->req_policy = &ethtool_plca_nest;
	yrs.yarg.rsp_policy = &ethtool_plca_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PLCA_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_plca_get_cfg_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_PLCA_GET_CFG;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_plca_get_cfg_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PLCA_GET_CFG - dump */
void ethtool_plca_get_cfg_list_free(struct ethtool_plca_get_cfg_list *rsp)
{
	struct ethtool_plca_get_cfg_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_plca_get_cfg_list *
ethtool_plca_get_cfg_dump(struct ynl_sock *ys,
			  struct ethtool_plca_get_cfg_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_plca_get_cfg_list);
	yds.cb = ethtool_plca_get_cfg_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_PLCA_GET_CFG;
	yds.rsp_policy = &ethtool_plca_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PLCA_GET_CFG, 1);
	ys->req_policy = &ethtool_plca_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PLCA_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_plca_get_cfg_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_PLCA_GET_CFG - notify */
void ethtool_plca_get_cfg_ntf_free(struct ethtool_plca_get_cfg_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ============== ETHTOOL_MSG_PLCA_SET_CFG ============== */
/* ETHTOOL_MSG_PLCA_SET_CFG - do */
void ethtool_plca_set_cfg_req_free(struct ethtool_plca_set_cfg_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_plca_set_cfg(struct ynl_sock *ys,
			 struct ethtool_plca_set_cfg_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PLCA_SET_CFG, 1);
	ys->req_policy = &ethtool_plca_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PLCA_HEADER, &req->header);
	if (req->_present.version)
		mnl_attr_put_u16(nlh, ETHTOOL_A_PLCA_VERSION, req->version);
	if (req->_present.enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_PLCA_ENABLED, req->enabled);
	if (req->_present.status)
		mnl_attr_put_u8(nlh, ETHTOOL_A_PLCA_STATUS, req->status);
	if (req->_present.node_cnt)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PLCA_NODE_CNT, req->node_cnt);
	if (req->_present.node_id)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PLCA_NODE_ID, req->node_id);
	if (req->_present.to_tmr)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PLCA_TO_TMR, req->to_tmr);
	if (req->_present.burst_cnt)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PLCA_BURST_CNT, req->burst_cnt);
	if (req->_present.burst_tmr)
		mnl_attr_put_u32(nlh, ETHTOOL_A_PLCA_BURST_TMR, req->burst_tmr);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== ETHTOOL_MSG_PLCA_GET_STATUS ============== */
/* ETHTOOL_MSG_PLCA_GET_STATUS - do */
void ethtool_plca_get_status_req_free(struct ethtool_plca_get_status_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_plca_get_status_rsp_free(struct ethtool_plca_get_status_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	free(rsp);
}

int ethtool_plca_get_status_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_plca_get_status_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_PLCA_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_PLCA_VERSION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.version = 1;
			dst->version = mnl_attr_get_u16(attr);
		} else if (type == ETHTOOL_A_PLCA_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.enabled = 1;
			dst->enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PLCA_STATUS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.status = 1;
			dst->status = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_PLCA_NODE_CNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.node_cnt = 1;
			dst->node_cnt = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_NODE_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.node_id = 1;
			dst->node_id = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_TO_TMR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.to_tmr = 1;
			dst->to_tmr = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_BURST_CNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.burst_cnt = 1;
			dst->burst_cnt = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_PLCA_BURST_TMR) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.burst_tmr = 1;
			dst->burst_tmr = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct ethtool_plca_get_status_rsp *
ethtool_plca_get_status(struct ynl_sock *ys,
			struct ethtool_plca_get_status_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_plca_get_status_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_PLCA_GET_STATUS, 1);
	ys->req_policy = &ethtool_plca_nest;
	yrs.yarg.rsp_policy = &ethtool_plca_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PLCA_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_plca_get_status_rsp_parse;
	yrs.rsp_cmd = 40;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_plca_get_status_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_PLCA_GET_STATUS - dump */
void
ethtool_plca_get_status_list_free(struct ethtool_plca_get_status_list *rsp)
{
	struct ethtool_plca_get_status_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		free(rsp);
	}
}

struct ethtool_plca_get_status_list *
ethtool_plca_get_status_dump(struct ynl_sock *ys,
			     struct ethtool_plca_get_status_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_plca_get_status_list);
	yds.cb = ethtool_plca_get_status_rsp_parse;
	yds.rsp_cmd = 40;
	yds.rsp_policy = &ethtool_plca_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_PLCA_GET_STATUS, 1);
	ys->req_policy = &ethtool_plca_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_PLCA_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_plca_get_status_list_free(yds.first);
	return NULL;
}

/* ============== ETHTOOL_MSG_MM_GET ============== */
/* ETHTOOL_MSG_MM_GET - do */
void ethtool_mm_get_req_free(struct ethtool_mm_get_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

void ethtool_mm_get_rsp_free(struct ethtool_mm_get_rsp *rsp)
{
	ethtool_header_free(&rsp->header);
	ethtool_mm_stat_free(&rsp->stats);
	free(rsp);
}

int ethtool_mm_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ethtool_mm_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_MM_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_MM_PMAC_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pmac_enabled = 1;
			dst->pmac_enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MM_TX_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_enabled = 1;
			dst->tx_enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MM_TX_ACTIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_active = 1;
			dst->tx_active = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MM_TX_MIN_FRAG_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.tx_min_frag_size = 1;
			dst->tx_min_frag_size = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MM_RX_MIN_FRAG_SIZE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.rx_min_frag_size = 1;
			dst->rx_min_frag_size = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MM_VERIFY_ENABLED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.verify_enabled = 1;
			dst->verify_enabled = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_MM_VERIFY_TIME) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.verify_time = 1;
			dst->verify_time = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MM_MAX_VERIFY_TIME) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.max_verify_time = 1;
			dst->max_verify_time = mnl_attr_get_u32(attr);
		} else if (type == ETHTOOL_A_MM_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.stats = 1;

			parg.rsp_policy = &ethtool_mm_stat_nest;
			parg.data = &dst->stats;
			if (ethtool_mm_stat_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct ethtool_mm_get_rsp *
ethtool_mm_get(struct ynl_sock *ys, struct ethtool_mm_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct ethtool_mm_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_MM_GET, 1);
	ys->req_policy = &ethtool_mm_nest;
	yrs.yarg.rsp_policy = &ethtool_mm_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MM_HEADER, &req->header);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = ethtool_mm_get_rsp_parse;
	yrs.rsp_cmd = ETHTOOL_MSG_MM_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	ethtool_mm_get_rsp_free(rsp);
	return NULL;
}

/* ETHTOOL_MSG_MM_GET - dump */
void ethtool_mm_get_list_free(struct ethtool_mm_get_list *rsp)
{
	struct ethtool_mm_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		ethtool_header_free(&rsp->obj.header);
		ethtool_mm_stat_free(&rsp->obj.stats);
		free(rsp);
	}
}

struct ethtool_mm_get_list *
ethtool_mm_get_dump(struct ynl_sock *ys, struct ethtool_mm_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct ethtool_mm_get_list);
	yds.cb = ethtool_mm_get_rsp_parse;
	yds.rsp_cmd = ETHTOOL_MSG_MM_GET;
	yds.rsp_policy = &ethtool_mm_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, ETHTOOL_MSG_MM_GET, 1);
	ys->req_policy = &ethtool_mm_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MM_HEADER, &req->header);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	ethtool_mm_get_list_free(yds.first);
	return NULL;
}

/* ETHTOOL_MSG_MM_GET - notify */
void ethtool_mm_get_ntf_free(struct ethtool_mm_get_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_mm_stat_free(&rsp->obj.stats);
	free(rsp);
}

/* ============== ETHTOOL_MSG_MM_SET ============== */
/* ETHTOOL_MSG_MM_SET - do */
void ethtool_mm_set_req_free(struct ethtool_mm_set_req *req)
{
	ethtool_header_free(&req->header);
	free(req);
}

int ethtool_mm_set(struct ynl_sock *ys, struct ethtool_mm_set_req *req)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, ETHTOOL_MSG_MM_SET, 1);
	ys->req_policy = &ethtool_mm_nest;

	if (req->_present.header)
		ethtool_header_put(nlh, ETHTOOL_A_MM_HEADER, &req->header);
	if (req->_present.verify_enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_MM_VERIFY_ENABLED, req->verify_enabled);
	if (req->_present.verify_time)
		mnl_attr_put_u32(nlh, ETHTOOL_A_MM_VERIFY_TIME, req->verify_time);
	if (req->_present.tx_enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_MM_TX_ENABLED, req->tx_enabled);
	if (req->_present.pmac_enabled)
		mnl_attr_put_u8(nlh, ETHTOOL_A_MM_PMAC_ENABLED, req->pmac_enabled);
	if (req->_present.tx_min_frag_size)
		mnl_attr_put_u32(nlh, ETHTOOL_A_MM_TX_MIN_FRAG_SIZE, req->tx_min_frag_size);

	err = ynl_exec(ys, nlh, NULL);
	if (err < 0)
		return -1;

	return 0;
}

/* ETHTOOL_MSG_CABLE_TEST_NTF - event */
int ethtool_cable_test_ntf_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ethtool_cable_test_ntf_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CABLE_TEST_NTF_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_CABLE_TEST_NTF_STATUS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.status = 1;
			dst->status = mnl_attr_get_u8(attr);
		}
	}

	return MNL_CB_OK;
}

void ethtool_cable_test_ntf_free(struct ethtool_cable_test_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	free(rsp);
}

/* ETHTOOL_MSG_CABLE_TEST_TDR_NTF - event */
int ethtool_cable_test_tdr_ntf_rsp_parse(const struct nlmsghdr *nlh,
					 void *data)
{
	struct ethtool_cable_test_tdr_ntf_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == ETHTOOL_A_CABLE_TEST_TDR_NTF_HEADER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.header = 1;

			parg.rsp_policy = &ethtool_header_nest;
			parg.data = &dst->header;
			if (ethtool_header_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == ETHTOOL_A_CABLE_TEST_TDR_NTF_STATUS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.status = 1;
			dst->status = mnl_attr_get_u8(attr);
		} else if (type == ETHTOOL_A_CABLE_TEST_TDR_NTF_NEST) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.nest = 1;

			parg.rsp_policy = &ethtool_cable_nest_nest;
			parg.data = &dst->nest;
			if (ethtool_cable_nest_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

void ethtool_cable_test_tdr_ntf_free(struct ethtool_cable_test_tdr_ntf *rsp)
{
	ethtool_header_free(&rsp->obj.header);
	ethtool_cable_nest_free(&rsp->obj.nest);
	free(rsp);
}

static const struct ynl_ntf_info ethtool_ntf_info[] =  {
	[ETHTOOL_MSG_LINKINFO_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_linkinfo_get_ntf),
		.cb		= ethtool_linkinfo_get_rsp_parse,
		.policy		= &ethtool_linkinfo_nest,
		.free		= (void *)ethtool_linkinfo_get_ntf_free,
	},
	[ETHTOOL_MSG_LINKMODES_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_linkmodes_get_ntf),
		.cb		= ethtool_linkmodes_get_rsp_parse,
		.policy		= &ethtool_linkmodes_nest,
		.free		= (void *)ethtool_linkmodes_get_ntf_free,
	},
	[ETHTOOL_MSG_DEBUG_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_debug_get_ntf),
		.cb		= ethtool_debug_get_rsp_parse,
		.policy		= &ethtool_debug_nest,
		.free		= (void *)ethtool_debug_get_ntf_free,
	},
	[ETHTOOL_MSG_WOL_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_wol_get_ntf),
		.cb		= ethtool_wol_get_rsp_parse,
		.policy		= &ethtool_wol_nest,
		.free		= (void *)ethtool_wol_get_ntf_free,
	},
	[ETHTOOL_MSG_FEATURES_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_features_get_ntf),
		.cb		= ethtool_features_get_rsp_parse,
		.policy		= &ethtool_features_nest,
		.free		= (void *)ethtool_features_get_ntf_free,
	},
	[ETHTOOL_MSG_PRIVFLAGS_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_privflags_get_ntf),
		.cb		= ethtool_privflags_get_rsp_parse,
		.policy		= &ethtool_privflags_nest,
		.free		= (void *)ethtool_privflags_get_ntf_free,
	},
	[ETHTOOL_MSG_RINGS_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_rings_get_ntf),
		.cb		= ethtool_rings_get_rsp_parse,
		.policy		= &ethtool_rings_nest,
		.free		= (void *)ethtool_rings_get_ntf_free,
	},
	[ETHTOOL_MSG_CHANNELS_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_channels_get_ntf),
		.cb		= ethtool_channels_get_rsp_parse,
		.policy		= &ethtool_channels_nest,
		.free		= (void *)ethtool_channels_get_ntf_free,
	},
	[ETHTOOL_MSG_COALESCE_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_coalesce_get_ntf),
		.cb		= ethtool_coalesce_get_rsp_parse,
		.policy		= &ethtool_coalesce_nest,
		.free		= (void *)ethtool_coalesce_get_ntf_free,
	},
	[ETHTOOL_MSG_PAUSE_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_pause_get_ntf),
		.cb		= ethtool_pause_get_rsp_parse,
		.policy		= &ethtool_pause_nest,
		.free		= (void *)ethtool_pause_get_ntf_free,
	},
	[ETHTOOL_MSG_EEE_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_eee_get_ntf),
		.cb		= ethtool_eee_get_rsp_parse,
		.policy		= &ethtool_eee_nest,
		.free		= (void *)ethtool_eee_get_ntf_free,
	},
	[ETHTOOL_MSG_CABLE_TEST_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_cable_test_ntf),
		.cb		= ethtool_cable_test_ntf_rsp_parse,
		.policy		= &ethtool_cable_test_ntf_nest,
		.free		= (void *)ethtool_cable_test_ntf_free,
	},
	[ETHTOOL_MSG_CABLE_TEST_TDR_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_cable_test_tdr_ntf),
		.cb		= ethtool_cable_test_tdr_ntf_rsp_parse,
		.policy		= &ethtool_cable_test_tdr_ntf_nest,
		.free		= (void *)ethtool_cable_test_tdr_ntf_free,
	},
	[ETHTOOL_MSG_FEC_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_fec_get_ntf),
		.cb		= ethtool_fec_get_rsp_parse,
		.policy		= &ethtool_fec_nest,
		.free		= (void *)ethtool_fec_get_ntf_free,
	},
	[ETHTOOL_MSG_MODULE_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_module_get_ntf),
		.cb		= ethtool_module_get_rsp_parse,
		.policy		= &ethtool_module_nest,
		.free		= (void *)ethtool_module_get_ntf_free,
	},
	[ETHTOOL_MSG_PLCA_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_plca_get_cfg_ntf),
		.cb		= ethtool_plca_get_cfg_rsp_parse,
		.policy		= &ethtool_plca_nest,
		.free		= (void *)ethtool_plca_get_cfg_ntf_free,
	},
	[ETHTOOL_MSG_MM_NTF] =  {
		.alloc_sz	= sizeof(struct ethtool_mm_get_ntf),
		.cb		= ethtool_mm_get_rsp_parse,
		.policy		= &ethtool_mm_nest,
		.free		= (void *)ethtool_mm_get_ntf_free,
	},
};

const struct ynl_family ynl_ethtool_family =  {
	.name		= "ethtool",
	.ntf_info	= ethtool_ntf_info,
	.ntf_info_size	= MNL_ARRAY_SIZE(ethtool_ntf_info),
};

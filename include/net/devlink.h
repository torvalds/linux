/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/devlink.h - Network physical device Netlink interface
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */
#ifndef _NET_DEVLINK_H_
#define _NET_DEVLINK_H_

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/refcount.h>
#include <net/net_namespace.h>
#include <net/flow_offload.h>
#include <uapi/linux/devlink.h>
#include <linux/xarray.h>

struct devlink_ops;

struct devlink {
	struct list_head list;
	struct list_head port_list;
	struct list_head sb_list;
	struct list_head dpipe_table_list;
	struct list_head resource_list;
	struct list_head param_list;
	struct list_head region_list;
	struct list_head reporter_list;
	struct mutex reporters_lock; /* protects reporter_list */
	struct devlink_dpipe_headers *dpipe_headers;
	struct list_head trap_list;
	struct list_head trap_group_list;
	struct list_head trap_policer_list;
	const struct devlink_ops *ops;
	struct xarray snapshot_ids;
	struct device *dev;
	possible_net_t _net;
	struct mutex lock;
	u8 reload_failed:1,
	   reload_enabled:1,
	   registered:1;
	char priv[0] __aligned(NETDEV_ALIGN);
};

struct devlink_port_phys_attrs {
	u32 port_number; /* Same value as "split group".
			  * A physical port which is visible to the user
			  * for a given port flavour.
			  */
	u32 split_subport_number;
};

struct devlink_port_pci_pf_attrs {
	u16 pf;	/* Associated PCI PF for this port. */
};

struct devlink_port_pci_vf_attrs {
	u16 pf;	/* Associated PCI PF for this port. */
	u16 vf;	/* Associated PCI VF for of the PCI PF for this port. */
};

struct devlink_port_attrs {
	u8 set:1,
	   split:1,
	   switch_port:1;
	enum devlink_port_flavour flavour;
	struct netdev_phys_item_id switch_id;
	union {
		struct devlink_port_phys_attrs phys;
		struct devlink_port_pci_pf_attrs pci_pf;
		struct devlink_port_pci_vf_attrs pci_vf;
	};
};

struct devlink_port {
	struct list_head list;
	struct list_head param_list;
	struct devlink *devlink;
	unsigned int index;
	bool registered;
	spinlock_t type_lock; /* Protects type and type_dev
			       * pointer consistency.
			       */
	enum devlink_port_type type;
	enum devlink_port_type desired_type;
	void *type_dev;
	struct devlink_port_attrs attrs;
	struct delayed_work type_warn_dw;
};

struct devlink_sb_pool_info {
	enum devlink_sb_pool_type pool_type;
	u32 size;
	enum devlink_sb_threshold_type threshold_type;
	u32 cell_size;
};

/**
 * struct devlink_dpipe_field - dpipe field object
 * @name: field name
 * @id: index inside the headers field array
 * @bitwidth: bitwidth
 * @mapping_type: mapping type
 */
struct devlink_dpipe_field {
	const char *name;
	unsigned int id;
	unsigned int bitwidth;
	enum devlink_dpipe_field_mapping_type mapping_type;
};

/**
 * struct devlink_dpipe_header - dpipe header object
 * @name: header name
 * @id: index, global/local detrmined by global bit
 * @fields: fields
 * @fields_count: number of fields
 * @global: indicates if header is shared like most protocol header
 *	    or driver specific
 */
struct devlink_dpipe_header {
	const char *name;
	unsigned int id;
	struct devlink_dpipe_field *fields;
	unsigned int fields_count;
	bool global;
};

/**
 * struct devlink_dpipe_match - represents match operation
 * @type: type of match
 * @header_index: header index (packets can have several headers of same
 *		  type like in case of tunnels)
 * @header: header
 * @fieled_id: field index
 */
struct devlink_dpipe_match {
	enum devlink_dpipe_match_type type;
	unsigned int header_index;
	struct devlink_dpipe_header *header;
	unsigned int field_id;
};

/**
 * struct devlink_dpipe_action - represents action operation
 * @type: type of action
 * @header_index: header index (packets can have several headers of same
 *		  type like in case of tunnels)
 * @header: header
 * @fieled_id: field index
 */
struct devlink_dpipe_action {
	enum devlink_dpipe_action_type type;
	unsigned int header_index;
	struct devlink_dpipe_header *header;
	unsigned int field_id;
};

/**
 * struct devlink_dpipe_value - represents value of match/action
 * @action: action
 * @match: match
 * @mapping_value: in case the field has some mapping this value
 *                 specified the mapping value
 * @mapping_valid: specify if mapping value is valid
 * @value_size: value size
 * @value: value
 * @mask: bit mask
 */
struct devlink_dpipe_value {
	union {
		struct devlink_dpipe_action *action;
		struct devlink_dpipe_match *match;
	};
	unsigned int mapping_value;
	bool mapping_valid;
	unsigned int value_size;
	void *value;
	void *mask;
};

/**
 * struct devlink_dpipe_entry - table entry object
 * @index: index of the entry in the table
 * @match_values: match values
 * @matche_values_count: count of matches tuples
 * @action_values: actions values
 * @action_values_count: count of actions values
 * @counter: value of counter
 * @counter_valid: Specify if value is valid from hardware
 */
struct devlink_dpipe_entry {
	u64 index;
	struct devlink_dpipe_value *match_values;
	unsigned int match_values_count;
	struct devlink_dpipe_value *action_values;
	unsigned int action_values_count;
	u64 counter;
	bool counter_valid;
};

/**
 * struct devlink_dpipe_dump_ctx - context provided to driver in order
 *				   to dump
 * @info: info
 * @cmd: devlink command
 * @skb: skb
 * @nest: top attribute
 * @hdr: hdr
 */
struct devlink_dpipe_dump_ctx {
	struct genl_info *info;
	enum devlink_command cmd;
	struct sk_buff *skb;
	struct nlattr *nest;
	void *hdr;
};

struct devlink_dpipe_table_ops;

/**
 * struct devlink_dpipe_table - table object
 * @priv: private
 * @name: table name
 * @counters_enabled: indicates if counters are active
 * @counter_control_extern: indicates if counter control is in dpipe or
 *			    external tool
 * @resource_valid: Indicate that the resource id is valid
 * @resource_id: relative resource this table is related to
 * @resource_units: number of resource's unit consumed per table's entry
 * @table_ops: table operations
 * @rcu: rcu
 */
struct devlink_dpipe_table {
	void *priv;
	struct list_head list;
	const char *name;
	bool counters_enabled;
	bool counter_control_extern;
	bool resource_valid;
	u64 resource_id;
	u64 resource_units;
	struct devlink_dpipe_table_ops *table_ops;
	struct rcu_head rcu;
};

/**
 * struct devlink_dpipe_table_ops - dpipe_table ops
 * @actions_dump - dumps all tables actions
 * @matches_dump - dumps all tables matches
 * @entries_dump - dumps all active entries in the table
 * @counters_set_update - when changing the counter status hardware sync
 *			  maybe needed to allocate/free counter related
 *			  resources
 * @size_get - get size
 */
struct devlink_dpipe_table_ops {
	int (*actions_dump)(void *priv, struct sk_buff *skb);
	int (*matches_dump)(void *priv, struct sk_buff *skb);
	int (*entries_dump)(void *priv, bool counters_enabled,
			    struct devlink_dpipe_dump_ctx *dump_ctx);
	int (*counters_set_update)(void *priv, bool enable);
	u64 (*size_get)(void *priv);
};

/**
 * struct devlink_dpipe_headers - dpipe headers
 * @headers - header array can be shared (global bit) or driver specific
 * @headers_count - count of headers
 */
struct devlink_dpipe_headers {
	struct devlink_dpipe_header **headers;
	unsigned int headers_count;
};

/**
 * struct devlink_resource_size_params - resource's size parameters
 * @size_min: minimum size which can be set
 * @size_max: maximum size which can be set
 * @size_granularity: size granularity
 * @size_unit: resource's basic unit
 */
struct devlink_resource_size_params {
	u64 size_min;
	u64 size_max;
	u64 size_granularity;
	enum devlink_resource_unit unit;
};

static inline void
devlink_resource_size_params_init(struct devlink_resource_size_params *size_params,
				  u64 size_min, u64 size_max,
				  u64 size_granularity,
				  enum devlink_resource_unit unit)
{
	size_params->size_min = size_min;
	size_params->size_max = size_max;
	size_params->size_granularity = size_granularity;
	size_params->unit = unit;
}

typedef u64 devlink_resource_occ_get_t(void *priv);

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

#define DEVLINK_RESOURCE_ID_PARENT_TOP 0

#define __DEVLINK_PARAM_MAX_STRING_VALUE 32
enum devlink_param_type {
	DEVLINK_PARAM_TYPE_U8,
	DEVLINK_PARAM_TYPE_U16,
	DEVLINK_PARAM_TYPE_U32,
	DEVLINK_PARAM_TYPE_STRING,
	DEVLINK_PARAM_TYPE_BOOL,
};

union devlink_param_value {
	u8 vu8;
	u16 vu16;
	u32 vu32;
	char vstr[__DEVLINK_PARAM_MAX_STRING_VALUE];
	bool vbool;
};

struct devlink_param_gset_ctx {
	union devlink_param_value val;
	enum devlink_param_cmode cmode;
};

/**
 * struct devlink_param - devlink configuration parameter data
 * @name: name of the parameter
 * @generic: indicates if the parameter is generic or driver specific
 * @type: parameter type
 * @supported_cmodes: bitmap of supported configuration modes
 * @get: get parameter value, used for runtime and permanent
 *       configuration modes
 * @set: set parameter value, used for runtime and permanent
 *       configuration modes
 * @validate: validate input value is applicable (within value range, etc.)
 *
 * This struct should be used by the driver to fill the data for
 * a parameter it registers.
 */
struct devlink_param {
	u32 id;
	const char *name;
	bool generic;
	enum devlink_param_type type;
	unsigned long supported_cmodes;
	int (*get)(struct devlink *devlink, u32 id,
		   struct devlink_param_gset_ctx *ctx);
	int (*set)(struct devlink *devlink, u32 id,
		   struct devlink_param_gset_ctx *ctx);
	int (*validate)(struct devlink *devlink, u32 id,
			union devlink_param_value val,
			struct netlink_ext_ack *extack);
};

struct devlink_param_item {
	struct list_head list;
	const struct devlink_param *param;
	union devlink_param_value driverinit_value;
	bool driverinit_value_valid;
	bool published;
};

enum devlink_param_generic_id {
	DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
	DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
	DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV,
	DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
	DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI,
	DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
	DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
	DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
	DEVLINK_PARAM_GENERIC_ID_RESET_DEV_ON_DRV_PROBE,
	DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,

	/* add new param generic ids above here*/
	__DEVLINK_PARAM_GENERIC_ID_MAX,
	DEVLINK_PARAM_GENERIC_ID_MAX = __DEVLINK_PARAM_GENERIC_ID_MAX - 1,
};

#define DEVLINK_PARAM_GENERIC_INT_ERR_RESET_NAME "internal_error_reset"
#define DEVLINK_PARAM_GENERIC_INT_ERR_RESET_TYPE DEVLINK_PARAM_TYPE_BOOL

#define DEVLINK_PARAM_GENERIC_MAX_MACS_NAME "max_macs"
#define DEVLINK_PARAM_GENERIC_MAX_MACS_TYPE DEVLINK_PARAM_TYPE_U32

#define DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_NAME "enable_sriov"
#define DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_TYPE DEVLINK_PARAM_TYPE_BOOL

#define DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_NAME "region_snapshot_enable"
#define DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_TYPE DEVLINK_PARAM_TYPE_BOOL

#define DEVLINK_PARAM_GENERIC_IGNORE_ARI_NAME "ignore_ari"
#define DEVLINK_PARAM_GENERIC_IGNORE_ARI_TYPE DEVLINK_PARAM_TYPE_BOOL

#define DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_NAME "msix_vec_per_pf_max"
#define DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_TYPE DEVLINK_PARAM_TYPE_U32

#define DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_NAME "msix_vec_per_pf_min"
#define DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_TYPE DEVLINK_PARAM_TYPE_U32

#define DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_NAME "fw_load_policy"
#define DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_TYPE DEVLINK_PARAM_TYPE_U8

#define DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_NAME \
	"reset_dev_on_drv_probe"
#define DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_TYPE DEVLINK_PARAM_TYPE_U8

#define DEVLINK_PARAM_GENERIC_ENABLE_ROCE_NAME "enable_roce"
#define DEVLINK_PARAM_GENERIC_ENABLE_ROCE_TYPE DEVLINK_PARAM_TYPE_BOOL

#define DEVLINK_PARAM_GENERIC(_id, _cmodes, _get, _set, _validate)	\
{									\
	.id = DEVLINK_PARAM_GENERIC_ID_##_id,				\
	.name = DEVLINK_PARAM_GENERIC_##_id##_NAME,			\
	.type = DEVLINK_PARAM_GENERIC_##_id##_TYPE,			\
	.generic = true,						\
	.supported_cmodes = _cmodes,					\
	.get = _get,							\
	.set = _set,							\
	.validate = _validate,						\
}

#define DEVLINK_PARAM_DRIVER(_id, _name, _type, _cmodes, _get, _set, _validate)	\
{									\
	.id = _id,							\
	.name = _name,							\
	.type = _type,							\
	.supported_cmodes = _cmodes,					\
	.get = _get,							\
	.set = _set,							\
	.validate = _validate,						\
}

/* Part number, identifier of board design */
#define DEVLINK_INFO_VERSION_GENERIC_BOARD_ID	"board.id"
/* Revision of board design */
#define DEVLINK_INFO_VERSION_GENERIC_BOARD_REV	"board.rev"
/* Maker of the board */
#define DEVLINK_INFO_VERSION_GENERIC_BOARD_MANUFACTURE	"board.manufacture"

/* Part number, identifier of asic design */
#define DEVLINK_INFO_VERSION_GENERIC_ASIC_ID	"asic.id"
/* Revision of asic design */
#define DEVLINK_INFO_VERSION_GENERIC_ASIC_REV	"asic.rev"

/* Overall FW version */
#define DEVLINK_INFO_VERSION_GENERIC_FW		"fw"
/* Control processor FW version */
#define DEVLINK_INFO_VERSION_GENERIC_FW_MGMT	"fw.mgmt"
/* FW interface specification version */
#define DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API	"fw.mgmt.api"
/* Data path microcode controlling high-speed packet processing */
#define DEVLINK_INFO_VERSION_GENERIC_FW_APP	"fw.app"
/* UNDI software version */
#define DEVLINK_INFO_VERSION_GENERIC_FW_UNDI	"fw.undi"
/* NCSI support/handler version */
#define DEVLINK_INFO_VERSION_GENERIC_FW_NCSI	"fw.ncsi"
/* FW parameter set id */
#define DEVLINK_INFO_VERSION_GENERIC_FW_PSID	"fw.psid"
/* RoCE FW version */
#define DEVLINK_INFO_VERSION_GENERIC_FW_ROCE	"fw.roce"
/* Firmware bundle identifier */
#define DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID	"fw.bundle_id"

struct devlink_region;
struct devlink_info_req;

/**
 * struct devlink_region_ops - Region operations
 * @name: region name
 * @destructor: callback used to free snapshot memory when deleting
 * @snapshot: callback to request an immediate snapshot. On success,
 *            the data variable must be updated to point to the snapshot data.
 *            The function will be called while the devlink instance lock is
 *            held.
 */
struct devlink_region_ops {
	const char *name;
	void (*destructor)(const void *data);
	int (*snapshot)(struct devlink *devlink, struct netlink_ext_ack *extack,
			u8 **data);
};

struct devlink_fmsg;
struct devlink_health_reporter;

enum devlink_health_reporter_state {
	DEVLINK_HEALTH_REPORTER_STATE_HEALTHY,
	DEVLINK_HEALTH_REPORTER_STATE_ERROR,
};

/**
 * struct devlink_health_reporter_ops - Reporter operations
 * @name: reporter name
 * @recover: callback to recover from reported error
 *           if priv_ctx is NULL, run a full recover
 * @dump: callback to dump an object
 *        if priv_ctx is NULL, run a full dump
 * @diagnose: callback to diagnose the current status
 */

struct devlink_health_reporter_ops {
	char *name;
	int (*recover)(struct devlink_health_reporter *reporter,
		       void *priv_ctx, struct netlink_ext_ack *extack);
	int (*dump)(struct devlink_health_reporter *reporter,
		    struct devlink_fmsg *fmsg, void *priv_ctx,
		    struct netlink_ext_ack *extack);
	int (*diagnose)(struct devlink_health_reporter *reporter,
			struct devlink_fmsg *fmsg,
			struct netlink_ext_ack *extack);
};

/**
 * struct devlink_trap_policer - Immutable packet trap policer attributes.
 * @id: Policer identifier.
 * @init_rate: Initial rate in packets / sec.
 * @init_burst: Initial burst size in packets.
 * @max_rate: Maximum rate.
 * @min_rate: Minimum rate.
 * @max_burst: Maximum burst size.
 * @min_burst: Minimum burst size.
 *
 * Describes immutable attributes of packet trap policers that drivers register
 * with devlink.
 */
struct devlink_trap_policer {
	u32 id;
	u64 init_rate;
	u64 init_burst;
	u64 max_rate;
	u64 min_rate;
	u64 max_burst;
	u64 min_burst;
};

/**
 * struct devlink_trap_group - Immutable packet trap group attributes.
 * @name: Trap group name.
 * @id: Trap group identifier.
 * @generic: Whether the trap group is generic or not.
 * @init_policer_id: Initial policer identifier.
 *
 * Describes immutable attributes of packet trap groups that drivers register
 * with devlink.
 */
struct devlink_trap_group {
	const char *name;
	u16 id;
	bool generic;
	u32 init_policer_id;
};

#define DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT	BIT(0)
#define DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE	BIT(1)

/**
 * struct devlink_trap - Immutable packet trap attributes.
 * @type: Trap type.
 * @init_action: Initial trap action.
 * @generic: Whether the trap is generic or not.
 * @id: Trap identifier.
 * @name: Trap name.
 * @init_group_id: Initial group identifier.
 * @metadata_cap: Metadata types that can be provided by the trap.
 *
 * Describes immutable attributes of packet traps that drivers register with
 * devlink.
 */
struct devlink_trap {
	enum devlink_trap_type type;
	enum devlink_trap_action init_action;
	bool generic;
	u16 id;
	const char *name;
	u16 init_group_id;
	u32 metadata_cap;
};

/* All traps must be documented in
 * Documentation/networking/devlink/devlink-trap.rst
 */
enum devlink_trap_generic_id {
	DEVLINK_TRAP_GENERIC_ID_SMAC_MC,
	DEVLINK_TRAP_GENERIC_ID_VLAN_TAG_MISMATCH,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_VLAN_FILTER,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_STP_FILTER,
	DEVLINK_TRAP_GENERIC_ID_EMPTY_TX_LIST,
	DEVLINK_TRAP_GENERIC_ID_PORT_LOOPBACK_FILTER,
	DEVLINK_TRAP_GENERIC_ID_BLACKHOLE_ROUTE,
	DEVLINK_TRAP_GENERIC_ID_TTL_ERROR,
	DEVLINK_TRAP_GENERIC_ID_TAIL_DROP,
	DEVLINK_TRAP_GENERIC_ID_NON_IP_PACKET,
	DEVLINK_TRAP_GENERIC_ID_UC_DIP_MC_DMAC,
	DEVLINK_TRAP_GENERIC_ID_DIP_LB,
	DEVLINK_TRAP_GENERIC_ID_SIP_MC,
	DEVLINK_TRAP_GENERIC_ID_SIP_LB,
	DEVLINK_TRAP_GENERIC_ID_CORRUPTED_IP_HDR,
	DEVLINK_TRAP_GENERIC_ID_IPV4_SIP_BC,
	DEVLINK_TRAP_GENERIC_ID_IPV6_MC_DIP_RESERVED_SCOPE,
	DEVLINK_TRAP_GENERIC_ID_IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE,
	DEVLINK_TRAP_GENERIC_ID_MTU_ERROR,
	DEVLINK_TRAP_GENERIC_ID_UNRESOLVED_NEIGH,
	DEVLINK_TRAP_GENERIC_ID_RPF,
	DEVLINK_TRAP_GENERIC_ID_REJECT_ROUTE,
	DEVLINK_TRAP_GENERIC_ID_IPV4_LPM_UNICAST_MISS,
	DEVLINK_TRAP_GENERIC_ID_IPV6_LPM_UNICAST_MISS,
	DEVLINK_TRAP_GENERIC_ID_NON_ROUTABLE,
	DEVLINK_TRAP_GENERIC_ID_DECAP_ERROR,
	DEVLINK_TRAP_GENERIC_ID_OVERLAY_SMAC_MC,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_FLOW_ACTION_DROP,
	DEVLINK_TRAP_GENERIC_ID_EGRESS_FLOW_ACTION_DROP,

	/* Add new generic trap IDs above */
	__DEVLINK_TRAP_GENERIC_ID_MAX,
	DEVLINK_TRAP_GENERIC_ID_MAX = __DEVLINK_TRAP_GENERIC_ID_MAX - 1,
};

/* All trap groups must be documented in
 * Documentation/networking/devlink/devlink-trap.rst
 */
enum devlink_trap_group_generic_id {
	DEVLINK_TRAP_GROUP_GENERIC_ID_L2_DROPS,
	DEVLINK_TRAP_GROUP_GENERIC_ID_L3_DROPS,
	DEVLINK_TRAP_GROUP_GENERIC_ID_BUFFER_DROPS,
	DEVLINK_TRAP_GROUP_GENERIC_ID_TUNNEL_DROPS,
	DEVLINK_TRAP_GROUP_GENERIC_ID_ACL_DROPS,

	/* Add new generic trap group IDs above */
	__DEVLINK_TRAP_GROUP_GENERIC_ID_MAX,
	DEVLINK_TRAP_GROUP_GENERIC_ID_MAX =
		__DEVLINK_TRAP_GROUP_GENERIC_ID_MAX - 1,
};

#define DEVLINK_TRAP_GENERIC_NAME_SMAC_MC \
	"source_mac_is_multicast"
#define DEVLINK_TRAP_GENERIC_NAME_VLAN_TAG_MISMATCH \
	"vlan_tag_mismatch"
#define DEVLINK_TRAP_GENERIC_NAME_INGRESS_VLAN_FILTER \
	"ingress_vlan_filter"
#define DEVLINK_TRAP_GENERIC_NAME_INGRESS_STP_FILTER \
	"ingress_spanning_tree_filter"
#define DEVLINK_TRAP_GENERIC_NAME_EMPTY_TX_LIST \
	"port_list_is_empty"
#define DEVLINK_TRAP_GENERIC_NAME_PORT_LOOPBACK_FILTER \
	"port_loopback_filter"
#define DEVLINK_TRAP_GENERIC_NAME_BLACKHOLE_ROUTE \
	"blackhole_route"
#define DEVLINK_TRAP_GENERIC_NAME_TTL_ERROR \
	"ttl_value_is_too_small"
#define DEVLINK_TRAP_GENERIC_NAME_TAIL_DROP \
	"tail_drop"
#define DEVLINK_TRAP_GENERIC_NAME_NON_IP_PACKET \
	"non_ip"
#define DEVLINK_TRAP_GENERIC_NAME_UC_DIP_MC_DMAC \
	"uc_dip_over_mc_dmac"
#define DEVLINK_TRAP_GENERIC_NAME_DIP_LB \
	"dip_is_loopback_address"
#define DEVLINK_TRAP_GENERIC_NAME_SIP_MC \
	"sip_is_mc"
#define DEVLINK_TRAP_GENERIC_NAME_SIP_LB \
	"sip_is_loopback_address"
#define DEVLINK_TRAP_GENERIC_NAME_CORRUPTED_IP_HDR \
	"ip_header_corrupted"
#define DEVLINK_TRAP_GENERIC_NAME_IPV4_SIP_BC \
	"ipv4_sip_is_limited_bc"
#define DEVLINK_TRAP_GENERIC_NAME_IPV6_MC_DIP_RESERVED_SCOPE \
	"ipv6_mc_dip_reserved_scope"
#define DEVLINK_TRAP_GENERIC_NAME_IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE \
	"ipv6_mc_dip_interface_local_scope"
#define DEVLINK_TRAP_GENERIC_NAME_MTU_ERROR \
	"mtu_value_is_too_small"
#define DEVLINK_TRAP_GENERIC_NAME_UNRESOLVED_NEIGH \
	"unresolved_neigh"
#define DEVLINK_TRAP_GENERIC_NAME_RPF \
	"mc_reverse_path_forwarding"
#define DEVLINK_TRAP_GENERIC_NAME_REJECT_ROUTE \
	"reject_route"
#define DEVLINK_TRAP_GENERIC_NAME_IPV4_LPM_UNICAST_MISS \
	"ipv4_lpm_miss"
#define DEVLINK_TRAP_GENERIC_NAME_IPV6_LPM_UNICAST_MISS \
	"ipv6_lpm_miss"
#define DEVLINK_TRAP_GENERIC_NAME_NON_ROUTABLE \
	"non_routable_packet"
#define DEVLINK_TRAP_GENERIC_NAME_DECAP_ERROR \
	"decap_error"
#define DEVLINK_TRAP_GENERIC_NAME_OVERLAY_SMAC_MC \
	"overlay_smac_is_mc"
#define DEVLINK_TRAP_GENERIC_NAME_INGRESS_FLOW_ACTION_DROP \
	"ingress_flow_action_drop"
#define DEVLINK_TRAP_GENERIC_NAME_EGRESS_FLOW_ACTION_DROP \
	"egress_flow_action_drop"

#define DEVLINK_TRAP_GROUP_GENERIC_NAME_L2_DROPS \
	"l2_drops"
#define DEVLINK_TRAP_GROUP_GENERIC_NAME_L3_DROPS \
	"l3_drops"
#define DEVLINK_TRAP_GROUP_GENERIC_NAME_BUFFER_DROPS \
	"buffer_drops"
#define DEVLINK_TRAP_GROUP_GENERIC_NAME_TUNNEL_DROPS \
	"tunnel_drops"
#define DEVLINK_TRAP_GROUP_GENERIC_NAME_ACL_DROPS \
	"acl_drops"

#define DEVLINK_TRAP_GENERIC(_type, _init_action, _id, _group_id,	      \
			     _metadata_cap)				      \
	{								      \
		.type = DEVLINK_TRAP_TYPE_##_type,			      \
		.init_action = DEVLINK_TRAP_ACTION_##_init_action,	      \
		.generic = true,					      \
		.id = DEVLINK_TRAP_GENERIC_ID_##_id,			      \
		.name = DEVLINK_TRAP_GENERIC_NAME_##_id,		      \
		.init_group_id = _group_id,				      \
		.metadata_cap = _metadata_cap,				      \
	}

#define DEVLINK_TRAP_DRIVER(_type, _init_action, _id, _name, _group_id,	      \
			    _metadata_cap)				      \
	{								      \
		.type = DEVLINK_TRAP_TYPE_##_type,			      \
		.init_action = DEVLINK_TRAP_ACTION_##_init_action,	      \
		.generic = false,					      \
		.id = _id,						      \
		.name = _name,						      \
		.init_group_id = _group_id,				      \
		.metadata_cap = _metadata_cap,				      \
	}

#define DEVLINK_TRAP_GROUP_GENERIC(_id, _policer_id)			      \
	{								      \
		.name = DEVLINK_TRAP_GROUP_GENERIC_NAME_##_id,		      \
		.id = DEVLINK_TRAP_GROUP_GENERIC_ID_##_id,		      \
		.generic = true,					      \
		.init_policer_id = _policer_id,				      \
	}

#define DEVLINK_TRAP_POLICER(_id, _rate, _burst, _max_rate, _min_rate,	      \
			     _max_burst, _min_burst)			      \
	{								      \
		.id = _id,						      \
		.init_rate = _rate,					      \
		.init_burst = _burst,					      \
		.max_rate = _max_rate,					      \
		.min_rate = _min_rate,					      \
		.max_burst = _max_burst,				      \
		.min_burst = _min_burst,				      \
	}

struct devlink_ops {
	int (*reload_down)(struct devlink *devlink, bool netns_change,
			   struct netlink_ext_ack *extack);
	int (*reload_up)(struct devlink *devlink,
			 struct netlink_ext_ack *extack);
	int (*port_type_set)(struct devlink_port *devlink_port,
			     enum devlink_port_type port_type);
	int (*port_split)(struct devlink *devlink, unsigned int port_index,
			  unsigned int count, struct netlink_ext_ack *extack);
	int (*port_unsplit)(struct devlink *devlink, unsigned int port_index,
			    struct netlink_ext_ack *extack);
	int (*sb_pool_get)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index,
			   struct devlink_sb_pool_info *pool_info);
	int (*sb_pool_set)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index, u32 size,
			   enum devlink_sb_threshold_type threshold_type,
			   struct netlink_ext_ack *extack);
	int (*sb_port_pool_get)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 *p_threshold);
	int (*sb_port_pool_set)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 threshold, struct netlink_ext_ack *extack);
	int (*sb_tc_pool_bind_get)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_pool_index, u32 *p_threshold);
	int (*sb_tc_pool_bind_set)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 pool_index, u32 threshold,
				   struct netlink_ext_ack *extack);
	int (*sb_occ_snapshot)(struct devlink *devlink,
			       unsigned int sb_index);
	int (*sb_occ_max_clear)(struct devlink *devlink,
				unsigned int sb_index);
	int (*sb_occ_port_pool_get)(struct devlink_port *devlink_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 *p_cur, u32 *p_max);
	int (*sb_occ_tc_port_bind_get)(struct devlink_port *devlink_port,
				       unsigned int sb_index,
				       u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u32 *p_cur, u32 *p_max);

	int (*eswitch_mode_get)(struct devlink *devlink, u16 *p_mode);
	int (*eswitch_mode_set)(struct devlink *devlink, u16 mode,
				struct netlink_ext_ack *extack);
	int (*eswitch_inline_mode_get)(struct devlink *devlink, u8 *p_inline_mode);
	int (*eswitch_inline_mode_set)(struct devlink *devlink, u8 inline_mode,
				       struct netlink_ext_ack *extack);
	int (*eswitch_encap_mode_get)(struct devlink *devlink,
				      enum devlink_eswitch_encap_mode *p_encap_mode);
	int (*eswitch_encap_mode_set)(struct devlink *devlink,
				      enum devlink_eswitch_encap_mode encap_mode,
				      struct netlink_ext_ack *extack);
	int (*info_get)(struct devlink *devlink, struct devlink_info_req *req,
			struct netlink_ext_ack *extack);
	int (*flash_update)(struct devlink *devlink, const char *file_name,
			    const char *component,
			    struct netlink_ext_ack *extack);
	/**
	 * @trap_init: Trap initialization function.
	 *
	 * Should be used by device drivers to initialize the trap in the
	 * underlying device. Drivers should also store the provided trap
	 * context, so that they could efficiently pass it to
	 * devlink_trap_report() when the trap is triggered.
	 */
	int (*trap_init)(struct devlink *devlink,
			 const struct devlink_trap *trap, void *trap_ctx);
	/**
	 * @trap_fini: Trap de-initialization function.
	 *
	 * Should be used by device drivers to de-initialize the trap in the
	 * underlying device.
	 */
	void (*trap_fini)(struct devlink *devlink,
			  const struct devlink_trap *trap, void *trap_ctx);
	/**
	 * @trap_action_set: Trap action set function.
	 */
	int (*trap_action_set)(struct devlink *devlink,
			       const struct devlink_trap *trap,
			       enum devlink_trap_action action);
	/**
	 * @trap_group_init: Trap group initialization function.
	 *
	 * Should be used by device drivers to initialize the trap group in the
	 * underlying device.
	 */
	int (*trap_group_init)(struct devlink *devlink,
			       const struct devlink_trap_group *group);
	/**
	 * @trap_group_set: Trap group parameters set function.
	 *
	 * Note: @policer can be NULL when a policer is being unbound from
	 * @group.
	 */
	int (*trap_group_set)(struct devlink *devlink,
			      const struct devlink_trap_group *group,
			      const struct devlink_trap_policer *policer);
	/**
	 * @trap_policer_init: Trap policer initialization function.
	 *
	 * Should be used by device drivers to initialize the trap policer in
	 * the underlying device.
	 */
	int (*trap_policer_init)(struct devlink *devlink,
				 const struct devlink_trap_policer *policer);
	/**
	 * @trap_policer_fini: Trap policer de-initialization function.
	 *
	 * Should be used by device drivers to de-initialize the trap policer
	 * in the underlying device.
	 */
	void (*trap_policer_fini)(struct devlink *devlink,
				  const struct devlink_trap_policer *policer);
	/**
	 * @trap_policer_set: Trap policer parameters set function.
	 */
	int (*trap_policer_set)(struct devlink *devlink,
				const struct devlink_trap_policer *policer,
				u64 rate, u64 burst,
				struct netlink_ext_ack *extack);
	/**
	 * @trap_policer_counter_get: Trap policer counter get function.
	 *
	 * Should be used by device drivers to report number of packets dropped
	 * by the policer.
	 */
	int (*trap_policer_counter_get)(struct devlink *devlink,
					const struct devlink_trap_policer *policer,
					u64 *p_drops);
};

static inline void *devlink_priv(struct devlink *devlink)
{
	BUG_ON(!devlink);
	return &devlink->priv;
}

static inline struct devlink *priv_to_devlink(void *priv)
{
	BUG_ON(!priv);
	return container_of(priv, struct devlink, priv);
}

static inline struct devlink_port *
netdev_to_devlink_port(struct net_device *dev)
{
	if (dev->netdev_ops->ndo_get_devlink_port)
		return dev->netdev_ops->ndo_get_devlink_port(dev);
	return NULL;
}

static inline struct devlink *netdev_to_devlink(struct net_device *dev)
{
	struct devlink_port *devlink_port = netdev_to_devlink_port(dev);

	if (devlink_port)
		return devlink_port->devlink;
	return NULL;
}

struct ib_device;

struct net *devlink_net(const struct devlink *devlink);
void devlink_net_set(struct devlink *devlink, struct net *net);
struct devlink *devlink_alloc(const struct devlink_ops *ops, size_t priv_size);
int devlink_register(struct devlink *devlink, struct device *dev);
void devlink_unregister(struct devlink *devlink);
void devlink_reload_enable(struct devlink *devlink);
void devlink_reload_disable(struct devlink *devlink);
void devlink_free(struct devlink *devlink);
int devlink_port_register(struct devlink *devlink,
			  struct devlink_port *devlink_port,
			  unsigned int port_index);
void devlink_port_unregister(struct devlink_port *devlink_port);
void devlink_port_type_eth_set(struct devlink_port *devlink_port,
			       struct net_device *netdev);
void devlink_port_type_ib_set(struct devlink_port *devlink_port,
			      struct ib_device *ibdev);
void devlink_port_type_clear(struct devlink_port *devlink_port);
void devlink_port_attrs_set(struct devlink_port *devlink_port,
			    enum devlink_port_flavour flavour,
			    u32 port_number, bool split,
			    u32 split_subport_number,
			    const unsigned char *switch_id,
			    unsigned char switch_id_len);
void devlink_port_attrs_pci_pf_set(struct devlink_port *devlink_port,
				   const unsigned char *switch_id,
				   unsigned char switch_id_len, u16 pf);
void devlink_port_attrs_pci_vf_set(struct devlink_port *devlink_port,
				   const unsigned char *switch_id,
				   unsigned char switch_id_len,
				   u16 pf, u16 vf);
int devlink_sb_register(struct devlink *devlink, unsigned int sb_index,
			u32 size, u16 ingress_pools_count,
			u16 egress_pools_count, u16 ingress_tc_count,
			u16 egress_tc_count);
void devlink_sb_unregister(struct devlink *devlink, unsigned int sb_index);
int devlink_dpipe_table_register(struct devlink *devlink,
				 const char *table_name,
				 struct devlink_dpipe_table_ops *table_ops,
				 void *priv, bool counter_control_extern);
void devlink_dpipe_table_unregister(struct devlink *devlink,
				    const char *table_name);
int devlink_dpipe_headers_register(struct devlink *devlink,
				   struct devlink_dpipe_headers *dpipe_headers);
void devlink_dpipe_headers_unregister(struct devlink *devlink);
bool devlink_dpipe_table_counter_enabled(struct devlink *devlink,
					 const char *table_name);
int devlink_dpipe_entry_ctx_prepare(struct devlink_dpipe_dump_ctx *dump_ctx);
int devlink_dpipe_entry_ctx_append(struct devlink_dpipe_dump_ctx *dump_ctx,
				   struct devlink_dpipe_entry *entry);
int devlink_dpipe_entry_ctx_close(struct devlink_dpipe_dump_ctx *dump_ctx);
void devlink_dpipe_entry_clear(struct devlink_dpipe_entry *entry);
int devlink_dpipe_action_put(struct sk_buff *skb,
			     struct devlink_dpipe_action *action);
int devlink_dpipe_match_put(struct sk_buff *skb,
			    struct devlink_dpipe_match *match);
extern struct devlink_dpipe_header devlink_dpipe_header_ethernet;
extern struct devlink_dpipe_header devlink_dpipe_header_ipv4;
extern struct devlink_dpipe_header devlink_dpipe_header_ipv6;

int devlink_resource_register(struct devlink *devlink,
			      const char *resource_name,
			      u64 resource_size,
			      u64 resource_id,
			      u64 parent_resource_id,
			      const struct devlink_resource_size_params *size_params);
void devlink_resources_unregister(struct devlink *devlink,
				  struct devlink_resource *resource);
int devlink_resource_size_get(struct devlink *devlink,
			      u64 resource_id,
			      u64 *p_resource_size);
int devlink_dpipe_table_resource_set(struct devlink *devlink,
				     const char *table_name, u64 resource_id,
				     u64 resource_units);
void devlink_resource_occ_get_register(struct devlink *devlink,
				       u64 resource_id,
				       devlink_resource_occ_get_t *occ_get,
				       void *occ_get_priv);
void devlink_resource_occ_get_unregister(struct devlink *devlink,
					 u64 resource_id);
int devlink_params_register(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count);
void devlink_params_unregister(struct devlink *devlink,
			       const struct devlink_param *params,
			       size_t params_count);
void devlink_params_publish(struct devlink *devlink);
void devlink_params_unpublish(struct devlink *devlink);
int devlink_port_params_register(struct devlink_port *devlink_port,
				 const struct devlink_param *params,
				 size_t params_count);
void devlink_port_params_unregister(struct devlink_port *devlink_port,
				    const struct devlink_param *params,
				    size_t params_count);
int devlink_param_driverinit_value_get(struct devlink *devlink, u32 param_id,
				       union devlink_param_value *init_val);
int devlink_param_driverinit_value_set(struct devlink *devlink, u32 param_id,
				       union devlink_param_value init_val);
int
devlink_port_param_driverinit_value_get(struct devlink_port *devlink_port,
					u32 param_id,
					union devlink_param_value *init_val);
int devlink_port_param_driverinit_value_set(struct devlink_port *devlink_port,
					    u32 param_id,
					    union devlink_param_value init_val);
void devlink_param_value_changed(struct devlink *devlink, u32 param_id);
void devlink_port_param_value_changed(struct devlink_port *devlink_port,
				      u32 param_id);
void devlink_param_value_str_fill(union devlink_param_value *dst_val,
				  const char *src);
struct devlink_region *
devlink_region_create(struct devlink *devlink,
		      const struct devlink_region_ops *ops,
		      u32 region_max_snapshots, u64 region_size);
void devlink_region_destroy(struct devlink_region *region);
int devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id);
void devlink_region_snapshot_id_put(struct devlink *devlink, u32 id);
int devlink_region_snapshot_create(struct devlink_region *region,
				   u8 *data, u32 snapshot_id);
int devlink_info_serial_number_put(struct devlink_info_req *req,
				   const char *sn);
int devlink_info_driver_name_put(struct devlink_info_req *req,
				 const char *name);
int devlink_info_version_fixed_put(struct devlink_info_req *req,
				   const char *version_name,
				   const char *version_value);
int devlink_info_version_stored_put(struct devlink_info_req *req,
				    const char *version_name,
				    const char *version_value);
int devlink_info_version_running_put(struct devlink_info_req *req,
				     const char *version_name,
				     const char *version_value);

int devlink_fmsg_obj_nest_start(struct devlink_fmsg *fmsg);
int devlink_fmsg_obj_nest_end(struct devlink_fmsg *fmsg);

int devlink_fmsg_pair_nest_start(struct devlink_fmsg *fmsg, const char *name);
int devlink_fmsg_pair_nest_end(struct devlink_fmsg *fmsg);

int devlink_fmsg_arr_pair_nest_start(struct devlink_fmsg *fmsg,
				     const char *name);
int devlink_fmsg_arr_pair_nest_end(struct devlink_fmsg *fmsg);
int devlink_fmsg_binary_pair_nest_start(struct devlink_fmsg *fmsg,
					const char *name);
int devlink_fmsg_binary_pair_nest_end(struct devlink_fmsg *fmsg);

int devlink_fmsg_bool_put(struct devlink_fmsg *fmsg, bool value);
int devlink_fmsg_u8_put(struct devlink_fmsg *fmsg, u8 value);
int devlink_fmsg_u32_put(struct devlink_fmsg *fmsg, u32 value);
int devlink_fmsg_u64_put(struct devlink_fmsg *fmsg, u64 value);
int devlink_fmsg_string_put(struct devlink_fmsg *fmsg, const char *value);
int devlink_fmsg_binary_put(struct devlink_fmsg *fmsg, const void *value,
			    u16 value_len);

int devlink_fmsg_bool_pair_put(struct devlink_fmsg *fmsg, const char *name,
			       bool value);
int devlink_fmsg_u8_pair_put(struct devlink_fmsg *fmsg, const char *name,
			     u8 value);
int devlink_fmsg_u32_pair_put(struct devlink_fmsg *fmsg, const char *name,
			      u32 value);
int devlink_fmsg_u64_pair_put(struct devlink_fmsg *fmsg, const char *name,
			      u64 value);
int devlink_fmsg_string_pair_put(struct devlink_fmsg *fmsg, const char *name,
				 const char *value);
int devlink_fmsg_binary_pair_put(struct devlink_fmsg *fmsg, const char *name,
				 const void *value, u32 value_len);

struct devlink_health_reporter *
devlink_health_reporter_create(struct devlink *devlink,
			       const struct devlink_health_reporter_ops *ops,
			       u64 graceful_period, void *priv);
void
devlink_health_reporter_destroy(struct devlink_health_reporter *reporter);

void *
devlink_health_reporter_priv(struct devlink_health_reporter *reporter);
int devlink_health_report(struct devlink_health_reporter *reporter,
			  const char *msg, void *priv_ctx);
void
devlink_health_reporter_state_update(struct devlink_health_reporter *reporter,
				     enum devlink_health_reporter_state state);
void
devlink_health_reporter_recovery_done(struct devlink_health_reporter *reporter);

bool devlink_is_reload_failed(const struct devlink *devlink);

void devlink_flash_update_begin_notify(struct devlink *devlink);
void devlink_flash_update_end_notify(struct devlink *devlink);
void devlink_flash_update_status_notify(struct devlink *devlink,
					const char *status_msg,
					const char *component,
					unsigned long done,
					unsigned long total);

int devlink_traps_register(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count, void *priv);
void devlink_traps_unregister(struct devlink *devlink,
			      const struct devlink_trap *traps,
			      size_t traps_count);
void devlink_trap_report(struct devlink *devlink, struct sk_buff *skb,
			 void *trap_ctx, struct devlink_port *in_devlink_port,
			 const struct flow_action_cookie *fa_cookie);
void *devlink_trap_ctx_priv(void *trap_ctx);
int devlink_trap_groups_register(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count);
void devlink_trap_groups_unregister(struct devlink *devlink,
				    const struct devlink_trap_group *groups,
				    size_t groups_count);
int
devlink_trap_policers_register(struct devlink *devlink,
			       const struct devlink_trap_policer *policers,
			       size_t policers_count);
void
devlink_trap_policers_unregister(struct devlink *devlink,
				 const struct devlink_trap_policer *policers,
				 size_t policers_count);

#if IS_ENABLED(CONFIG_NET_DEVLINK)

void devlink_compat_running_version(struct net_device *dev,
				    char *buf, size_t len);
int devlink_compat_flash_update(struct net_device *dev, const char *file_name);
int devlink_compat_phys_port_name_get(struct net_device *dev,
				      char *name, size_t len);
int devlink_compat_switch_id_get(struct net_device *dev,
				 struct netdev_phys_item_id *ppid);

#else

static inline void
devlink_compat_running_version(struct net_device *dev, char *buf, size_t len)
{
}

static inline int
devlink_compat_flash_update(struct net_device *dev, const char *file_name)
{
	return -EOPNOTSUPP;
}

static inline int
devlink_compat_phys_port_name_get(struct net_device *dev,
				  char *name, size_t len)
{
	return -EOPNOTSUPP;
}

static inline int
devlink_compat_switch_id_get(struct net_device *dev,
			     struct netdev_phys_item_id *ppid)
{
	return -EOPNOTSUPP;
}

#endif

#endif /* _NET_DEVLINK_H_ */

/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_DEVLINK_GEN_H
#define _LINUX_DEVLINK_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/devlink.h>

struct ynl_sock;

extern const struct ynl_family ynl_devlink_family;

/* Enums */
const char *devlink_op_str(int op);
const char *devlink_sb_pool_type_str(enum devlink_sb_pool_type value);
const char *devlink_port_type_str(enum devlink_port_type value);
const char *devlink_port_flavour_str(enum devlink_port_flavour value);
const char *devlink_port_fn_state_str(enum devlink_port_fn_state value);
const char *devlink_port_fn_opstate_str(enum devlink_port_fn_opstate value);
const char *devlink_port_fn_attr_cap_str(enum devlink_port_fn_attr_cap value);
const char *
devlink_sb_threshold_type_str(enum devlink_sb_threshold_type value);
const char *devlink_eswitch_mode_str(enum devlink_eswitch_mode value);
const char *
devlink_eswitch_inline_mode_str(enum devlink_eswitch_inline_mode value);
const char *
devlink_eswitch_encap_mode_str(enum devlink_eswitch_encap_mode value);
const char *devlink_dpipe_match_type_str(enum devlink_dpipe_match_type value);
const char *
devlink_dpipe_action_type_str(enum devlink_dpipe_action_type value);
const char *
devlink_dpipe_field_mapping_type_str(enum devlink_dpipe_field_mapping_type value);
const char *devlink_resource_unit_str(enum devlink_resource_unit value);
const char *devlink_reload_action_str(enum devlink_reload_action value);
const char *devlink_param_cmode_str(enum devlink_param_cmode value);
const char *devlink_flash_overwrite_str(enum devlink_flash_overwrite value);
const char *devlink_trap_action_str(enum devlink_trap_action value);

/* Common nested types */
struct devlink_dl_dpipe_match {
	struct {
		__u32 dpipe_match_type:1;
		__u32 dpipe_header_id:1;
		__u32 dpipe_header_global:1;
		__u32 dpipe_header_index:1;
		__u32 dpipe_field_id:1;
	} _present;

	enum devlink_dpipe_match_type dpipe_match_type;
	__u32 dpipe_header_id;
	__u8 dpipe_header_global;
	__u32 dpipe_header_index;
	__u32 dpipe_field_id;
};

struct devlink_dl_dpipe_match_value {
	struct {
		__u32 dpipe_value_len;
		__u32 dpipe_value_mask_len;
		__u32 dpipe_value_mapping:1;
	} _present;

	unsigned int n_dpipe_match;
	struct devlink_dl_dpipe_match *dpipe_match;
	void *dpipe_value;
	void *dpipe_value_mask;
	__u32 dpipe_value_mapping;
};

struct devlink_dl_dpipe_action {
	struct {
		__u32 dpipe_action_type:1;
		__u32 dpipe_header_id:1;
		__u32 dpipe_header_global:1;
		__u32 dpipe_header_index:1;
		__u32 dpipe_field_id:1;
	} _present;

	enum devlink_dpipe_action_type dpipe_action_type;
	__u32 dpipe_header_id;
	__u8 dpipe_header_global;
	__u32 dpipe_header_index;
	__u32 dpipe_field_id;
};

struct devlink_dl_dpipe_action_value {
	struct {
		__u32 dpipe_value_len;
		__u32 dpipe_value_mask_len;
		__u32 dpipe_value_mapping:1;
	} _present;

	unsigned int n_dpipe_action;
	struct devlink_dl_dpipe_action *dpipe_action;
	void *dpipe_value;
	void *dpipe_value_mask;
	__u32 dpipe_value_mapping;
};

struct devlink_dl_dpipe_field {
	struct {
		__u32 dpipe_field_name_len;
		__u32 dpipe_field_id:1;
		__u32 dpipe_field_bitwidth:1;
		__u32 dpipe_field_mapping_type:1;
	} _present;

	char *dpipe_field_name;
	__u32 dpipe_field_id;
	__u32 dpipe_field_bitwidth;
	enum devlink_dpipe_field_mapping_type dpipe_field_mapping_type;
};

struct devlink_dl_resource {
	struct {
		__u32 resource_name_len;
		__u32 resource_id:1;
		__u32 resource_size:1;
		__u32 resource_size_new:1;
		__u32 resource_size_valid:1;
		__u32 resource_size_min:1;
		__u32 resource_size_max:1;
		__u32 resource_size_gran:1;
		__u32 resource_unit:1;
		__u32 resource_occ:1;
	} _present;

	char *resource_name;
	__u64 resource_id;
	__u64 resource_size;
	__u64 resource_size_new;
	__u8 resource_size_valid;
	__u64 resource_size_min;
	__u64 resource_size_max;
	__u64 resource_size_gran;
	enum devlink_resource_unit resource_unit;
	__u64 resource_occ;
};

struct devlink_dl_info_version {
	struct {
		__u32 info_version_name_len;
		__u32 info_version_value_len;
	} _present;

	char *info_version_name;
	char *info_version_value;
};

struct devlink_dl_fmsg {
	struct {
		__u32 fmsg_obj_nest_start:1;
		__u32 fmsg_pair_nest_start:1;
		__u32 fmsg_arr_nest_start:1;
		__u32 fmsg_nest_end:1;
		__u32 fmsg_obj_name_len;
	} _present;

	char *fmsg_obj_name;
};

struct devlink_dl_port_function {
	struct {
		__u32 hw_addr_len;
		__u32 state:1;
		__u32 opstate:1;
		__u32 caps:1;
	} _present;

	void *hw_addr;
	enum devlink_port_fn_state state;
	enum devlink_port_fn_opstate opstate;
	struct nla_bitfield32 caps;
};

struct devlink_dl_reload_stats_entry {
	struct {
		__u32 reload_stats_limit:1;
		__u32 reload_stats_value:1;
	} _present;

	__u8 reload_stats_limit;
	__u32 reload_stats_value;
};

struct devlink_dl_reload_act_stats {
	unsigned int n_reload_stats_entry;
	struct devlink_dl_reload_stats_entry *reload_stats_entry;
};

struct devlink_dl_selftest_id {
	struct {
		__u32 flash:1;
	} _present;
};

struct devlink_dl_dpipe_table_matches {
	unsigned int n_dpipe_match;
	struct devlink_dl_dpipe_match *dpipe_match;
};

struct devlink_dl_dpipe_table_actions {
	unsigned int n_dpipe_action;
	struct devlink_dl_dpipe_action *dpipe_action;
};

struct devlink_dl_dpipe_entry_match_values {
	unsigned int n_dpipe_match_value;
	struct devlink_dl_dpipe_match_value *dpipe_match_value;
};

struct devlink_dl_dpipe_entry_action_values {
	unsigned int n_dpipe_action_value;
	struct devlink_dl_dpipe_action_value *dpipe_action_value;
};

struct devlink_dl_dpipe_header_fields {
	unsigned int n_dpipe_field;
	struct devlink_dl_dpipe_field *dpipe_field;
};

struct devlink_dl_resource_list {
	unsigned int n_resource;
	struct devlink_dl_resource *resource;
};

struct devlink_dl_reload_act_info {
	struct {
		__u32 reload_action:1;
	} _present;

	enum devlink_reload_action reload_action;
	unsigned int n_reload_action_stats;
	struct devlink_dl_reload_act_stats *reload_action_stats;
};

struct devlink_dl_dpipe_table {
	struct {
		__u32 dpipe_table_name_len;
		__u32 dpipe_table_size:1;
		__u32 dpipe_table_matches:1;
		__u32 dpipe_table_actions:1;
		__u32 dpipe_table_counters_enabled:1;
		__u32 dpipe_table_resource_id:1;
		__u32 dpipe_table_resource_units:1;
	} _present;

	char *dpipe_table_name;
	__u64 dpipe_table_size;
	struct devlink_dl_dpipe_table_matches dpipe_table_matches;
	struct devlink_dl_dpipe_table_actions dpipe_table_actions;
	__u8 dpipe_table_counters_enabled;
	__u64 dpipe_table_resource_id;
	__u64 dpipe_table_resource_units;
};

struct devlink_dl_dpipe_entry {
	struct {
		__u32 dpipe_entry_index:1;
		__u32 dpipe_entry_match_values:1;
		__u32 dpipe_entry_action_values:1;
		__u32 dpipe_entry_counter:1;
	} _present;

	__u64 dpipe_entry_index;
	struct devlink_dl_dpipe_entry_match_values dpipe_entry_match_values;
	struct devlink_dl_dpipe_entry_action_values dpipe_entry_action_values;
	__u64 dpipe_entry_counter;
};

struct devlink_dl_dpipe_header {
	struct {
		__u32 dpipe_header_name_len;
		__u32 dpipe_header_id:1;
		__u32 dpipe_header_global:1;
		__u32 dpipe_header_fields:1;
	} _present;

	char *dpipe_header_name;
	__u32 dpipe_header_id;
	__u8 dpipe_header_global;
	struct devlink_dl_dpipe_header_fields dpipe_header_fields;
};

struct devlink_dl_reload_stats {
	unsigned int n_reload_action_info;
	struct devlink_dl_reload_act_info *reload_action_info;
};

struct devlink_dl_dpipe_tables {
	unsigned int n_dpipe_table;
	struct devlink_dl_dpipe_table *dpipe_table;
};

struct devlink_dl_dpipe_entries {
	unsigned int n_dpipe_entry;
	struct devlink_dl_dpipe_entry *dpipe_entry;
};

struct devlink_dl_dpipe_headers {
	unsigned int n_dpipe_header;
	struct devlink_dl_dpipe_header *dpipe_header;
};

struct devlink_dl_dev_stats {
	struct {
		__u32 reload_stats:1;
		__u32 remote_reload_stats:1;
	} _present;

	struct devlink_dl_reload_stats reload_stats;
	struct devlink_dl_reload_stats remote_reload_stats;
};

/* ============== DEVLINK_CMD_GET ============== */
/* DEVLINK_CMD_GET - do */
struct devlink_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_get_req *devlink_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_get_req));
}
void devlink_get_req_free(struct devlink_get_req *req);

static inline void
devlink_get_req_set_bus_name(struct devlink_get_req *req, const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_get_req_set_dev_name(struct devlink_get_req *req, const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 reload_failed:1;
		__u32 dev_stats:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u8 reload_failed;
	struct devlink_dl_dev_stats dev_stats;
};

void devlink_get_rsp_free(struct devlink_get_rsp *rsp);

/*
 * Get devlink instances.
 */
struct devlink_get_rsp *
devlink_get(struct ynl_sock *ys, struct devlink_get_req *req);

/* DEVLINK_CMD_GET - dump */
struct devlink_get_list {
	struct devlink_get_list *next;
	struct devlink_get_rsp obj __attribute__((aligned(8)));
};

void devlink_get_list_free(struct devlink_get_list *rsp);

struct devlink_get_list *devlink_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_PORT_GET ============== */
/* DEVLINK_CMD_PORT_GET - do */
struct devlink_port_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_get_req *devlink_port_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_get_req));
}
void devlink_port_get_req_free(struct devlink_port_get_req *req);

static inline void
devlink_port_get_req_set_bus_name(struct devlink_port_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_get_req_set_dev_name(struct devlink_port_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_get_req_set_port_index(struct devlink_port_get_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

struct devlink_port_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

void devlink_port_get_rsp_free(struct devlink_port_get_rsp *rsp);

/*
 * Get devlink port instances.
 */
struct devlink_port_get_rsp *
devlink_port_get(struct ynl_sock *ys, struct devlink_port_get_req *req);

/* DEVLINK_CMD_PORT_GET - dump */
struct devlink_port_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_port_get_req_dump *
devlink_port_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_get_req_dump));
}
void devlink_port_get_req_dump_free(struct devlink_port_get_req_dump *req);

static inline void
devlink_port_get_req_dump_set_bus_name(struct devlink_port_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_get_req_dump_set_dev_name(struct devlink_port_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_port_get_rsp_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

struct devlink_port_get_rsp_list {
	struct devlink_port_get_rsp_list *next;
	struct devlink_port_get_rsp_dump obj __attribute__((aligned(8)));
};

void devlink_port_get_rsp_list_free(struct devlink_port_get_rsp_list *rsp);

struct devlink_port_get_rsp_list *
devlink_port_get_dump(struct ynl_sock *ys,
		      struct devlink_port_get_req_dump *req);

/* ============== DEVLINK_CMD_PORT_SET ============== */
/* DEVLINK_CMD_PORT_SET - do */
struct devlink_port_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 port_type:1;
		__u32 port_function:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	enum devlink_port_type port_type;
	struct devlink_dl_port_function port_function;
};

static inline struct devlink_port_set_req *devlink_port_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_set_req));
}
void devlink_port_set_req_free(struct devlink_port_set_req *req);

static inline void
devlink_port_set_req_set_bus_name(struct devlink_port_set_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_set_req_set_dev_name(struct devlink_port_set_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_set_req_set_port_index(struct devlink_port_set_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_port_set_req_set_port_type(struct devlink_port_set_req *req,
				   enum devlink_port_type port_type)
{
	req->_present.port_type = 1;
	req->port_type = port_type;
}
static inline void
devlink_port_set_req_set_port_function_hw_addr(struct devlink_port_set_req *req,
					       const void *hw_addr, size_t len)
{
	free(req->port_function.hw_addr);
	req->port_function._present.hw_addr_len = len;
	req->port_function.hw_addr = malloc(req->port_function._present.hw_addr_len);
	memcpy(req->port_function.hw_addr, hw_addr, req->port_function._present.hw_addr_len);
}
static inline void
devlink_port_set_req_set_port_function_state(struct devlink_port_set_req *req,
					     enum devlink_port_fn_state state)
{
	req->_present.port_function = 1;
	req->port_function._present.state = 1;
	req->port_function.state = state;
}
static inline void
devlink_port_set_req_set_port_function_opstate(struct devlink_port_set_req *req,
					       enum devlink_port_fn_opstate opstate)
{
	req->_present.port_function = 1;
	req->port_function._present.opstate = 1;
	req->port_function.opstate = opstate;
}
static inline void
devlink_port_set_req_set_port_function_caps(struct devlink_port_set_req *req,
					    struct nla_bitfield32 *caps)
{
	req->_present.port_function = 1;
	req->port_function._present.caps = 1;
	memcpy(&req->port_function.caps, caps, sizeof(struct nla_bitfield32));
}

/*
 * Set devlink port instances.
 */
int devlink_port_set(struct ynl_sock *ys, struct devlink_port_set_req *req);

/* ============== DEVLINK_CMD_PORT_NEW ============== */
/* DEVLINK_CMD_PORT_NEW - do */
struct devlink_port_new_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 port_flavour:1;
		__u32 port_pci_pf_number:1;
		__u32 port_pci_sf_number:1;
		__u32 port_controller_number:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	enum devlink_port_flavour port_flavour;
	__u16 port_pci_pf_number;
	__u32 port_pci_sf_number;
	__u32 port_controller_number;
};

static inline struct devlink_port_new_req *devlink_port_new_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_new_req));
}
void devlink_port_new_req_free(struct devlink_port_new_req *req);

static inline void
devlink_port_new_req_set_bus_name(struct devlink_port_new_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_new_req_set_dev_name(struct devlink_port_new_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_new_req_set_port_index(struct devlink_port_new_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_port_new_req_set_port_flavour(struct devlink_port_new_req *req,
				      enum devlink_port_flavour port_flavour)
{
	req->_present.port_flavour = 1;
	req->port_flavour = port_flavour;
}
static inline void
devlink_port_new_req_set_port_pci_pf_number(struct devlink_port_new_req *req,
					    __u16 port_pci_pf_number)
{
	req->_present.port_pci_pf_number = 1;
	req->port_pci_pf_number = port_pci_pf_number;
}
static inline void
devlink_port_new_req_set_port_pci_sf_number(struct devlink_port_new_req *req,
					    __u32 port_pci_sf_number)
{
	req->_present.port_pci_sf_number = 1;
	req->port_pci_sf_number = port_pci_sf_number;
}
static inline void
devlink_port_new_req_set_port_controller_number(struct devlink_port_new_req *req,
						__u32 port_controller_number)
{
	req->_present.port_controller_number = 1;
	req->port_controller_number = port_controller_number;
}

struct devlink_port_new_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

void devlink_port_new_rsp_free(struct devlink_port_new_rsp *rsp);

/*
 * Create devlink port instances.
 */
struct devlink_port_new_rsp *
devlink_port_new(struct ynl_sock *ys, struct devlink_port_new_req *req);

/* ============== DEVLINK_CMD_PORT_DEL ============== */
/* DEVLINK_CMD_PORT_DEL - do */
struct devlink_port_del_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_del_req *devlink_port_del_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_del_req));
}
void devlink_port_del_req_free(struct devlink_port_del_req *req);

static inline void
devlink_port_del_req_set_bus_name(struct devlink_port_del_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_del_req_set_dev_name(struct devlink_port_del_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_del_req_set_port_index(struct devlink_port_del_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

/*
 * Delete devlink port instances.
 */
int devlink_port_del(struct ynl_sock *ys, struct devlink_port_del_req *req);

/* ============== DEVLINK_CMD_PORT_SPLIT ============== */
/* DEVLINK_CMD_PORT_SPLIT - do */
struct devlink_port_split_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 port_split_count:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 port_split_count;
};

static inline struct devlink_port_split_req *devlink_port_split_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_split_req));
}
void devlink_port_split_req_free(struct devlink_port_split_req *req);

static inline void
devlink_port_split_req_set_bus_name(struct devlink_port_split_req *req,
				    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_split_req_set_dev_name(struct devlink_port_split_req *req,
				    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_split_req_set_port_index(struct devlink_port_split_req *req,
				      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_port_split_req_set_port_split_count(struct devlink_port_split_req *req,
					    __u32 port_split_count)
{
	req->_present.port_split_count = 1;
	req->port_split_count = port_split_count;
}

/*
 * Split devlink port instances.
 */
int devlink_port_split(struct ynl_sock *ys, struct devlink_port_split_req *req);

/* ============== DEVLINK_CMD_PORT_UNSPLIT ============== */
/* DEVLINK_CMD_PORT_UNSPLIT - do */
struct devlink_port_unsplit_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_unsplit_req *
devlink_port_unsplit_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_unsplit_req));
}
void devlink_port_unsplit_req_free(struct devlink_port_unsplit_req *req);

static inline void
devlink_port_unsplit_req_set_bus_name(struct devlink_port_unsplit_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_unsplit_req_set_dev_name(struct devlink_port_unsplit_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_unsplit_req_set_port_index(struct devlink_port_unsplit_req *req,
					__u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

/*
 * Unplit devlink port instances.
 */
int devlink_port_unsplit(struct ynl_sock *ys,
			 struct devlink_port_unsplit_req *req);

/* ============== DEVLINK_CMD_SB_GET ============== */
/* DEVLINK_CMD_SB_GET - do */
struct devlink_sb_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

static inline struct devlink_sb_get_req *devlink_sb_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_get_req));
}
void devlink_sb_get_req_free(struct devlink_sb_get_req *req);

static inline void
devlink_sb_get_req_set_bus_name(struct devlink_sb_get_req *req,
				const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_get_req_set_dev_name(struct devlink_sb_get_req *req,
				const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_get_req_set_sb_index(struct devlink_sb_get_req *req, __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}

struct devlink_sb_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

void devlink_sb_get_rsp_free(struct devlink_sb_get_rsp *rsp);

/*
 * Get shared buffer instances.
 */
struct devlink_sb_get_rsp *
devlink_sb_get(struct ynl_sock *ys, struct devlink_sb_get_req *req);

/* DEVLINK_CMD_SB_GET - dump */
struct devlink_sb_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_get_req_dump *
devlink_sb_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_get_req_dump));
}
void devlink_sb_get_req_dump_free(struct devlink_sb_get_req_dump *req);

static inline void
devlink_sb_get_req_dump_set_bus_name(struct devlink_sb_get_req_dump *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_get_req_dump_set_dev_name(struct devlink_sb_get_req_dump *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_get_list {
	struct devlink_sb_get_list *next;
	struct devlink_sb_get_rsp obj __attribute__((aligned(8)));
};

void devlink_sb_get_list_free(struct devlink_sb_get_list *rsp);

struct devlink_sb_get_list *
devlink_sb_get_dump(struct ynl_sock *ys, struct devlink_sb_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_POOL_GET ============== */
/* DEVLINK_CMD_SB_POOL_GET - do */
struct devlink_sb_pool_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
	__u16 sb_pool_index;
};

static inline struct devlink_sb_pool_get_req *
devlink_sb_pool_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_pool_get_req));
}
void devlink_sb_pool_get_req_free(struct devlink_sb_pool_get_req *req);

static inline void
devlink_sb_pool_get_req_set_bus_name(struct devlink_sb_pool_get_req *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_set_dev_name(struct devlink_sb_pool_get_req *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_set_sb_index(struct devlink_sb_pool_get_req *req,
				     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_pool_get_req_set_sb_pool_index(struct devlink_sb_pool_get_req *req,
					  __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}

struct devlink_sb_pool_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
	__u16 sb_pool_index;
};

void devlink_sb_pool_get_rsp_free(struct devlink_sb_pool_get_rsp *rsp);

/*
 * Get shared buffer pool instances.
 */
struct devlink_sb_pool_get_rsp *
devlink_sb_pool_get(struct ynl_sock *ys, struct devlink_sb_pool_get_req *req);

/* DEVLINK_CMD_SB_POOL_GET - dump */
struct devlink_sb_pool_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_pool_get_req_dump *
devlink_sb_pool_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_pool_get_req_dump));
}
void
devlink_sb_pool_get_req_dump_free(struct devlink_sb_pool_get_req_dump *req);

static inline void
devlink_sb_pool_get_req_dump_set_bus_name(struct devlink_sb_pool_get_req_dump *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_dump_set_dev_name(struct devlink_sb_pool_get_req_dump *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_pool_get_list {
	struct devlink_sb_pool_get_list *next;
	struct devlink_sb_pool_get_rsp obj __attribute__((aligned(8)));
};

void devlink_sb_pool_get_list_free(struct devlink_sb_pool_get_list *rsp);

struct devlink_sb_pool_get_list *
devlink_sb_pool_get_dump(struct ynl_sock *ys,
			 struct devlink_sb_pool_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_POOL_SET ============== */
/* DEVLINK_CMD_SB_POOL_SET - do */
struct devlink_sb_pool_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
		__u32 sb_pool_threshold_type:1;
		__u32 sb_pool_size:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
	__u16 sb_pool_index;
	enum devlink_sb_threshold_type sb_pool_threshold_type;
	__u32 sb_pool_size;
};

static inline struct devlink_sb_pool_set_req *
devlink_sb_pool_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_pool_set_req));
}
void devlink_sb_pool_set_req_free(struct devlink_sb_pool_set_req *req);

static inline void
devlink_sb_pool_set_req_set_bus_name(struct devlink_sb_pool_set_req *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_pool_set_req_set_dev_name(struct devlink_sb_pool_set_req *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_pool_set_req_set_sb_index(struct devlink_sb_pool_set_req *req,
				     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_pool_set_req_set_sb_pool_index(struct devlink_sb_pool_set_req *req,
					  __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}
static inline void
devlink_sb_pool_set_req_set_sb_pool_threshold_type(struct devlink_sb_pool_set_req *req,
						   enum devlink_sb_threshold_type sb_pool_threshold_type)
{
	req->_present.sb_pool_threshold_type = 1;
	req->sb_pool_threshold_type = sb_pool_threshold_type;
}
static inline void
devlink_sb_pool_set_req_set_sb_pool_size(struct devlink_sb_pool_set_req *req,
					 __u32 sb_pool_size)
{
	req->_present.sb_pool_size = 1;
	req->sb_pool_size = sb_pool_size;
}

/*
 * Set shared buffer pool instances.
 */
int devlink_sb_pool_set(struct ynl_sock *ys,
			struct devlink_sb_pool_set_req *req);

/* ============== DEVLINK_CMD_SB_PORT_POOL_GET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_GET - do */
struct devlink_sb_port_pool_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
};

static inline struct devlink_sb_port_pool_get_req *
devlink_sb_port_pool_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_port_pool_get_req));
}
void
devlink_sb_port_pool_get_req_free(struct devlink_sb_port_pool_get_req *req);

static inline void
devlink_sb_port_pool_get_req_set_bus_name(struct devlink_sb_port_pool_get_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_set_dev_name(struct devlink_sb_port_pool_get_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_set_port_index(struct devlink_sb_port_pool_get_req *req,
					    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_port_pool_get_req_set_sb_index(struct devlink_sb_port_pool_get_req *req,
					  __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_port_pool_get_req_set_sb_pool_index(struct devlink_sb_port_pool_get_req *req,
					       __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}

struct devlink_sb_port_pool_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
};

void
devlink_sb_port_pool_get_rsp_free(struct devlink_sb_port_pool_get_rsp *rsp);

/*
 * Get shared buffer port-pool combinations and threshold.
 */
struct devlink_sb_port_pool_get_rsp *
devlink_sb_port_pool_get(struct ynl_sock *ys,
			 struct devlink_sb_port_pool_get_req *req);

/* DEVLINK_CMD_SB_PORT_POOL_GET - dump */
struct devlink_sb_port_pool_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_port_pool_get_req_dump *
devlink_sb_port_pool_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_port_pool_get_req_dump));
}
void
devlink_sb_port_pool_get_req_dump_free(struct devlink_sb_port_pool_get_req_dump *req);

static inline void
devlink_sb_port_pool_get_req_dump_set_bus_name(struct devlink_sb_port_pool_get_req_dump *req,
					       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_dump_set_dev_name(struct devlink_sb_port_pool_get_req_dump *req,
					       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_port_pool_get_list {
	struct devlink_sb_port_pool_get_list *next;
	struct devlink_sb_port_pool_get_rsp obj __attribute__((aligned(8)));
};

void
devlink_sb_port_pool_get_list_free(struct devlink_sb_port_pool_get_list *rsp);

struct devlink_sb_port_pool_get_list *
devlink_sb_port_pool_get_dump(struct ynl_sock *ys,
			      struct devlink_sb_port_pool_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_PORT_POOL_SET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_SET - do */
struct devlink_sb_port_pool_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
		__u32 sb_threshold:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
	__u32 sb_threshold;
};

static inline struct devlink_sb_port_pool_set_req *
devlink_sb_port_pool_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_port_pool_set_req));
}
void
devlink_sb_port_pool_set_req_free(struct devlink_sb_port_pool_set_req *req);

static inline void
devlink_sb_port_pool_set_req_set_bus_name(struct devlink_sb_port_pool_set_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_port_pool_set_req_set_dev_name(struct devlink_sb_port_pool_set_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_port_pool_set_req_set_port_index(struct devlink_sb_port_pool_set_req *req,
					    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_port_pool_set_req_set_sb_index(struct devlink_sb_port_pool_set_req *req,
					  __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_port_pool_set_req_set_sb_pool_index(struct devlink_sb_port_pool_set_req *req,
					       __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}
static inline void
devlink_sb_port_pool_set_req_set_sb_threshold(struct devlink_sb_port_pool_set_req *req,
					      __u32 sb_threshold)
{
	req->_present.sb_threshold = 1;
	req->sb_threshold = sb_threshold;
}

/*
 * Set shared buffer port-pool combinations and threshold.
 */
int devlink_sb_port_pool_set(struct ynl_sock *ys,
			     struct devlink_sb_port_pool_set_req *req);

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_GET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - do */
struct devlink_sb_tc_pool_bind_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_type:1;
		__u32 sb_tc_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	enum devlink_sb_pool_type sb_pool_type;
	__u16 sb_tc_index;
};

static inline struct devlink_sb_tc_pool_bind_get_req *
devlink_sb_tc_pool_bind_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_tc_pool_bind_get_req));
}
void
devlink_sb_tc_pool_bind_get_req_free(struct devlink_sb_tc_pool_bind_get_req *req);

static inline void
devlink_sb_tc_pool_bind_get_req_set_bus_name(struct devlink_sb_tc_pool_bind_get_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_dev_name(struct devlink_sb_tc_pool_bind_get_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_port_index(struct devlink_sb_tc_pool_bind_get_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_index(struct devlink_sb_tc_pool_bind_get_req *req,
					     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_pool_type(struct devlink_sb_tc_pool_bind_get_req *req,
						 enum devlink_sb_pool_type sb_pool_type)
{
	req->_present.sb_pool_type = 1;
	req->sb_pool_type = sb_pool_type;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_tc_index(struct devlink_sb_tc_pool_bind_get_req *req,
						__u16 sb_tc_index)
{
	req->_present.sb_tc_index = 1;
	req->sb_tc_index = sb_tc_index;
}

struct devlink_sb_tc_pool_bind_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_type:1;
		__u32 sb_tc_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	enum devlink_sb_pool_type sb_pool_type;
	__u16 sb_tc_index;
};

void
devlink_sb_tc_pool_bind_get_rsp_free(struct devlink_sb_tc_pool_bind_get_rsp *rsp);

/*
 * Get shared buffer port-TC to pool bindings and threshold.
 */
struct devlink_sb_tc_pool_bind_get_rsp *
devlink_sb_tc_pool_bind_get(struct ynl_sock *ys,
			    struct devlink_sb_tc_pool_bind_get_req *req);

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - dump */
struct devlink_sb_tc_pool_bind_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_tc_pool_bind_get_req_dump *
devlink_sb_tc_pool_bind_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_tc_pool_bind_get_req_dump));
}
void
devlink_sb_tc_pool_bind_get_req_dump_free(struct devlink_sb_tc_pool_bind_get_req_dump *req);

static inline void
devlink_sb_tc_pool_bind_get_req_dump_set_bus_name(struct devlink_sb_tc_pool_bind_get_req_dump *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_dump_set_dev_name(struct devlink_sb_tc_pool_bind_get_req_dump *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_tc_pool_bind_get_list {
	struct devlink_sb_tc_pool_bind_get_list *next;
	struct devlink_sb_tc_pool_bind_get_rsp obj __attribute__((aligned(8)));
};

void
devlink_sb_tc_pool_bind_get_list_free(struct devlink_sb_tc_pool_bind_get_list *rsp);

struct devlink_sb_tc_pool_bind_get_list *
devlink_sb_tc_pool_bind_get_dump(struct ynl_sock *ys,
				 struct devlink_sb_tc_pool_bind_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_SET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_SET - do */
struct devlink_sb_tc_pool_bind_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
		__u32 sb_pool_type:1;
		__u32 sb_tc_index:1;
		__u32 sb_threshold:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
	enum devlink_sb_pool_type sb_pool_type;
	__u16 sb_tc_index;
	__u32 sb_threshold;
};

static inline struct devlink_sb_tc_pool_bind_set_req *
devlink_sb_tc_pool_bind_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_tc_pool_bind_set_req));
}
void
devlink_sb_tc_pool_bind_set_req_free(struct devlink_sb_tc_pool_bind_set_req *req);

static inline void
devlink_sb_tc_pool_bind_set_req_set_bus_name(struct devlink_sb_tc_pool_bind_set_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_dev_name(struct devlink_sb_tc_pool_bind_set_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_port_index(struct devlink_sb_tc_pool_bind_set_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_sb_index(struct devlink_sb_tc_pool_bind_set_req *req,
					     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_sb_pool_index(struct devlink_sb_tc_pool_bind_set_req *req,
						  __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_sb_pool_type(struct devlink_sb_tc_pool_bind_set_req *req,
						 enum devlink_sb_pool_type sb_pool_type)
{
	req->_present.sb_pool_type = 1;
	req->sb_pool_type = sb_pool_type;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_sb_tc_index(struct devlink_sb_tc_pool_bind_set_req *req,
						__u16 sb_tc_index)
{
	req->_present.sb_tc_index = 1;
	req->sb_tc_index = sb_tc_index;
}
static inline void
devlink_sb_tc_pool_bind_set_req_set_sb_threshold(struct devlink_sb_tc_pool_bind_set_req *req,
						 __u32 sb_threshold)
{
	req->_present.sb_threshold = 1;
	req->sb_threshold = sb_threshold;
}

/*
 * Set shared buffer port-TC to pool bindings and threshold.
 */
int devlink_sb_tc_pool_bind_set(struct ynl_sock *ys,
				struct devlink_sb_tc_pool_bind_set_req *req);

/* ============== DEVLINK_CMD_SB_OCC_SNAPSHOT ============== */
/* DEVLINK_CMD_SB_OCC_SNAPSHOT - do */
struct devlink_sb_occ_snapshot_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

static inline struct devlink_sb_occ_snapshot_req *
devlink_sb_occ_snapshot_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_occ_snapshot_req));
}
void devlink_sb_occ_snapshot_req_free(struct devlink_sb_occ_snapshot_req *req);

static inline void
devlink_sb_occ_snapshot_req_set_bus_name(struct devlink_sb_occ_snapshot_req *req,
					 const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_occ_snapshot_req_set_dev_name(struct devlink_sb_occ_snapshot_req *req,
					 const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_occ_snapshot_req_set_sb_index(struct devlink_sb_occ_snapshot_req *req,
					 __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}

/*
 * Take occupancy snapshot of shared buffer.
 */
int devlink_sb_occ_snapshot(struct ynl_sock *ys,
			    struct devlink_sb_occ_snapshot_req *req);

/* ============== DEVLINK_CMD_SB_OCC_MAX_CLEAR ============== */
/* DEVLINK_CMD_SB_OCC_MAX_CLEAR - do */
struct devlink_sb_occ_max_clear_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

static inline struct devlink_sb_occ_max_clear_req *
devlink_sb_occ_max_clear_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_occ_max_clear_req));
}
void
devlink_sb_occ_max_clear_req_free(struct devlink_sb_occ_max_clear_req *req);

static inline void
devlink_sb_occ_max_clear_req_set_bus_name(struct devlink_sb_occ_max_clear_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_occ_max_clear_req_set_dev_name(struct devlink_sb_occ_max_clear_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_occ_max_clear_req_set_sb_index(struct devlink_sb_occ_max_clear_req *req,
					  __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}

/*
 * Clear occupancy watermarks of shared buffer.
 */
int devlink_sb_occ_max_clear(struct ynl_sock *ys,
			     struct devlink_sb_occ_max_clear_req *req);

/* ============== DEVLINK_CMD_ESWITCH_GET ============== */
/* DEVLINK_CMD_ESWITCH_GET - do */
struct devlink_eswitch_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_eswitch_get_req *
devlink_eswitch_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_eswitch_get_req));
}
void devlink_eswitch_get_req_free(struct devlink_eswitch_get_req *req);

static inline void
devlink_eswitch_get_req_set_bus_name(struct devlink_eswitch_get_req *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_eswitch_get_req_set_dev_name(struct devlink_eswitch_get_req *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_eswitch_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 eswitch_mode:1;
		__u32 eswitch_inline_mode:1;
		__u32 eswitch_encap_mode:1;
	} _present;

	char *bus_name;
	char *dev_name;
	enum devlink_eswitch_mode eswitch_mode;
	enum devlink_eswitch_inline_mode eswitch_inline_mode;
	enum devlink_eswitch_encap_mode eswitch_encap_mode;
};

void devlink_eswitch_get_rsp_free(struct devlink_eswitch_get_rsp *rsp);

/*
 * Get eswitch attributes.
 */
struct devlink_eswitch_get_rsp *
devlink_eswitch_get(struct ynl_sock *ys, struct devlink_eswitch_get_req *req);

/* ============== DEVLINK_CMD_ESWITCH_SET ============== */
/* DEVLINK_CMD_ESWITCH_SET - do */
struct devlink_eswitch_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 eswitch_mode:1;
		__u32 eswitch_inline_mode:1;
		__u32 eswitch_encap_mode:1;
	} _present;

	char *bus_name;
	char *dev_name;
	enum devlink_eswitch_mode eswitch_mode;
	enum devlink_eswitch_inline_mode eswitch_inline_mode;
	enum devlink_eswitch_encap_mode eswitch_encap_mode;
};

static inline struct devlink_eswitch_set_req *
devlink_eswitch_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_eswitch_set_req));
}
void devlink_eswitch_set_req_free(struct devlink_eswitch_set_req *req);

static inline void
devlink_eswitch_set_req_set_bus_name(struct devlink_eswitch_set_req *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_eswitch_set_req_set_dev_name(struct devlink_eswitch_set_req *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_eswitch_set_req_set_eswitch_mode(struct devlink_eswitch_set_req *req,
					 enum devlink_eswitch_mode eswitch_mode)
{
	req->_present.eswitch_mode = 1;
	req->eswitch_mode = eswitch_mode;
}
static inline void
devlink_eswitch_set_req_set_eswitch_inline_mode(struct devlink_eswitch_set_req *req,
						enum devlink_eswitch_inline_mode eswitch_inline_mode)
{
	req->_present.eswitch_inline_mode = 1;
	req->eswitch_inline_mode = eswitch_inline_mode;
}
static inline void
devlink_eswitch_set_req_set_eswitch_encap_mode(struct devlink_eswitch_set_req *req,
					       enum devlink_eswitch_encap_mode eswitch_encap_mode)
{
	req->_present.eswitch_encap_mode = 1;
	req->eswitch_encap_mode = eswitch_encap_mode;
}

/*
 * Set eswitch attributes.
 */
int devlink_eswitch_set(struct ynl_sock *ys,
			struct devlink_eswitch_set_req *req);

/* ============== DEVLINK_CMD_DPIPE_TABLE_GET ============== */
/* DEVLINK_CMD_DPIPE_TABLE_GET - do */
struct devlink_dpipe_table_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_table_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *dpipe_table_name;
};

static inline struct devlink_dpipe_table_get_req *
devlink_dpipe_table_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_dpipe_table_get_req));
}
void devlink_dpipe_table_get_req_free(struct devlink_dpipe_table_get_req *req);

static inline void
devlink_dpipe_table_get_req_set_bus_name(struct devlink_dpipe_table_get_req *req,
					 const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_dpipe_table_get_req_set_dev_name(struct devlink_dpipe_table_get_req *req,
					 const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_dpipe_table_get_req_set_dpipe_table_name(struct devlink_dpipe_table_get_req *req,
						 const char *dpipe_table_name)
{
	free(req->dpipe_table_name);
	req->_present.dpipe_table_name_len = strlen(dpipe_table_name);
	req->dpipe_table_name = malloc(req->_present.dpipe_table_name_len + 1);
	memcpy(req->dpipe_table_name, dpipe_table_name, req->_present.dpipe_table_name_len);
	req->dpipe_table_name[req->_present.dpipe_table_name_len] = 0;
}

struct devlink_dpipe_table_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_tables:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct devlink_dl_dpipe_tables dpipe_tables;
};

void devlink_dpipe_table_get_rsp_free(struct devlink_dpipe_table_get_rsp *rsp);

/*
 * Get dpipe table attributes.
 */
struct devlink_dpipe_table_get_rsp *
devlink_dpipe_table_get(struct ynl_sock *ys,
			struct devlink_dpipe_table_get_req *req);

/* ============== DEVLINK_CMD_DPIPE_ENTRIES_GET ============== */
/* DEVLINK_CMD_DPIPE_ENTRIES_GET - do */
struct devlink_dpipe_entries_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_table_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *dpipe_table_name;
};

static inline struct devlink_dpipe_entries_get_req *
devlink_dpipe_entries_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_dpipe_entries_get_req));
}
void
devlink_dpipe_entries_get_req_free(struct devlink_dpipe_entries_get_req *req);

static inline void
devlink_dpipe_entries_get_req_set_bus_name(struct devlink_dpipe_entries_get_req *req,
					   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_dpipe_entries_get_req_set_dev_name(struct devlink_dpipe_entries_get_req *req,
					   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_dpipe_entries_get_req_set_dpipe_table_name(struct devlink_dpipe_entries_get_req *req,
						   const char *dpipe_table_name)
{
	free(req->dpipe_table_name);
	req->_present.dpipe_table_name_len = strlen(dpipe_table_name);
	req->dpipe_table_name = malloc(req->_present.dpipe_table_name_len + 1);
	memcpy(req->dpipe_table_name, dpipe_table_name, req->_present.dpipe_table_name_len);
	req->dpipe_table_name[req->_present.dpipe_table_name_len] = 0;
}

struct devlink_dpipe_entries_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_entries:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct devlink_dl_dpipe_entries dpipe_entries;
};

void
devlink_dpipe_entries_get_rsp_free(struct devlink_dpipe_entries_get_rsp *rsp);

/*
 * Get dpipe entries attributes.
 */
struct devlink_dpipe_entries_get_rsp *
devlink_dpipe_entries_get(struct ynl_sock *ys,
			  struct devlink_dpipe_entries_get_req *req);

/* ============== DEVLINK_CMD_DPIPE_HEADERS_GET ============== */
/* DEVLINK_CMD_DPIPE_HEADERS_GET - do */
struct devlink_dpipe_headers_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_dpipe_headers_get_req *
devlink_dpipe_headers_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_dpipe_headers_get_req));
}
void
devlink_dpipe_headers_get_req_free(struct devlink_dpipe_headers_get_req *req);

static inline void
devlink_dpipe_headers_get_req_set_bus_name(struct devlink_dpipe_headers_get_req *req,
					   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_dpipe_headers_get_req_set_dev_name(struct devlink_dpipe_headers_get_req *req,
					   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_dpipe_headers_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_headers:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct devlink_dl_dpipe_headers dpipe_headers;
};

void
devlink_dpipe_headers_get_rsp_free(struct devlink_dpipe_headers_get_rsp *rsp);

/*
 * Get dpipe headers attributes.
 */
struct devlink_dpipe_headers_get_rsp *
devlink_dpipe_headers_get(struct ynl_sock *ys,
			  struct devlink_dpipe_headers_get_req *req);

/* ============== DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET ============== */
/* DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET - do */
struct devlink_dpipe_table_counters_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 dpipe_table_name_len;
		__u32 dpipe_table_counters_enabled:1;
	} _present;

	char *bus_name;
	char *dev_name;
	char *dpipe_table_name;
	__u8 dpipe_table_counters_enabled;
};

static inline struct devlink_dpipe_table_counters_set_req *
devlink_dpipe_table_counters_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_dpipe_table_counters_set_req));
}
void
devlink_dpipe_table_counters_set_req_free(struct devlink_dpipe_table_counters_set_req *req);

static inline void
devlink_dpipe_table_counters_set_req_set_bus_name(struct devlink_dpipe_table_counters_set_req *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_dpipe_table_counters_set_req_set_dev_name(struct devlink_dpipe_table_counters_set_req *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_dpipe_table_counters_set_req_set_dpipe_table_name(struct devlink_dpipe_table_counters_set_req *req,
							  const char *dpipe_table_name)
{
	free(req->dpipe_table_name);
	req->_present.dpipe_table_name_len = strlen(dpipe_table_name);
	req->dpipe_table_name = malloc(req->_present.dpipe_table_name_len + 1);
	memcpy(req->dpipe_table_name, dpipe_table_name, req->_present.dpipe_table_name_len);
	req->dpipe_table_name[req->_present.dpipe_table_name_len] = 0;
}
static inline void
devlink_dpipe_table_counters_set_req_set_dpipe_table_counters_enabled(struct devlink_dpipe_table_counters_set_req *req,
								      __u8 dpipe_table_counters_enabled)
{
	req->_present.dpipe_table_counters_enabled = 1;
	req->dpipe_table_counters_enabled = dpipe_table_counters_enabled;
}

/*
 * Set dpipe counter attributes.
 */
int devlink_dpipe_table_counters_set(struct ynl_sock *ys,
				     struct devlink_dpipe_table_counters_set_req *req);

/* ============== DEVLINK_CMD_RESOURCE_SET ============== */
/* DEVLINK_CMD_RESOURCE_SET - do */
struct devlink_resource_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 resource_id:1;
		__u32 resource_size:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u64 resource_id;
	__u64 resource_size;
};

static inline struct devlink_resource_set_req *
devlink_resource_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_resource_set_req));
}
void devlink_resource_set_req_free(struct devlink_resource_set_req *req);

static inline void
devlink_resource_set_req_set_bus_name(struct devlink_resource_set_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_resource_set_req_set_dev_name(struct devlink_resource_set_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_resource_set_req_set_resource_id(struct devlink_resource_set_req *req,
					 __u64 resource_id)
{
	req->_present.resource_id = 1;
	req->resource_id = resource_id;
}
static inline void
devlink_resource_set_req_set_resource_size(struct devlink_resource_set_req *req,
					   __u64 resource_size)
{
	req->_present.resource_size = 1;
	req->resource_size = resource_size;
}

/*
 * Set resource attributes.
 */
int devlink_resource_set(struct ynl_sock *ys,
			 struct devlink_resource_set_req *req);

/* ============== DEVLINK_CMD_RESOURCE_DUMP ============== */
/* DEVLINK_CMD_RESOURCE_DUMP - do */
struct devlink_resource_dump_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_resource_dump_req *
devlink_resource_dump_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_resource_dump_req));
}
void devlink_resource_dump_req_free(struct devlink_resource_dump_req *req);

static inline void
devlink_resource_dump_req_set_bus_name(struct devlink_resource_dump_req *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_resource_dump_req_set_dev_name(struct devlink_resource_dump_req *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_resource_dump_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 resource_list:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct devlink_dl_resource_list resource_list;
};

void devlink_resource_dump_rsp_free(struct devlink_resource_dump_rsp *rsp);

/*
 * Get resource attributes.
 */
struct devlink_resource_dump_rsp *
devlink_resource_dump(struct ynl_sock *ys,
		      struct devlink_resource_dump_req *req);

/* ============== DEVLINK_CMD_RELOAD ============== */
/* DEVLINK_CMD_RELOAD - do */
struct devlink_reload_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 reload_action:1;
		__u32 reload_limits:1;
		__u32 netns_pid:1;
		__u32 netns_fd:1;
		__u32 netns_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	enum devlink_reload_action reload_action;
	struct nla_bitfield32 reload_limits;
	__u32 netns_pid;
	__u32 netns_fd;
	__u32 netns_id;
};

static inline struct devlink_reload_req *devlink_reload_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_reload_req));
}
void devlink_reload_req_free(struct devlink_reload_req *req);

static inline void
devlink_reload_req_set_bus_name(struct devlink_reload_req *req,
				const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_reload_req_set_dev_name(struct devlink_reload_req *req,
				const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_reload_req_set_reload_action(struct devlink_reload_req *req,
				     enum devlink_reload_action reload_action)
{
	req->_present.reload_action = 1;
	req->reload_action = reload_action;
}
static inline void
devlink_reload_req_set_reload_limits(struct devlink_reload_req *req,
				     struct nla_bitfield32 *reload_limits)
{
	req->_present.reload_limits = 1;
	memcpy(&req->reload_limits, reload_limits, sizeof(struct nla_bitfield32));
}
static inline void
devlink_reload_req_set_netns_pid(struct devlink_reload_req *req,
				 __u32 netns_pid)
{
	req->_present.netns_pid = 1;
	req->netns_pid = netns_pid;
}
static inline void
devlink_reload_req_set_netns_fd(struct devlink_reload_req *req, __u32 netns_fd)
{
	req->_present.netns_fd = 1;
	req->netns_fd = netns_fd;
}
static inline void
devlink_reload_req_set_netns_id(struct devlink_reload_req *req, __u32 netns_id)
{
	req->_present.netns_id = 1;
	req->netns_id = netns_id;
}

struct devlink_reload_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 reload_actions_performed:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct nla_bitfield32 reload_actions_performed;
};

void devlink_reload_rsp_free(struct devlink_reload_rsp *rsp);

/*
 * Reload devlink.
 */
struct devlink_reload_rsp *
devlink_reload(struct ynl_sock *ys, struct devlink_reload_req *req);

/* ============== DEVLINK_CMD_PARAM_GET ============== */
/* DEVLINK_CMD_PARAM_GET - do */
struct devlink_param_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 param_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *param_name;
};

static inline struct devlink_param_get_req *devlink_param_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_param_get_req));
}
void devlink_param_get_req_free(struct devlink_param_get_req *req);

static inline void
devlink_param_get_req_set_bus_name(struct devlink_param_get_req *req,
				   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_param_get_req_set_dev_name(struct devlink_param_get_req *req,
				   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_param_get_req_set_param_name(struct devlink_param_get_req *req,
				     const char *param_name)
{
	free(req->param_name);
	req->_present.param_name_len = strlen(param_name);
	req->param_name = malloc(req->_present.param_name_len + 1);
	memcpy(req->param_name, param_name, req->_present.param_name_len);
	req->param_name[req->_present.param_name_len] = 0;
}

struct devlink_param_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 param_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *param_name;
};

void devlink_param_get_rsp_free(struct devlink_param_get_rsp *rsp);

/*
 * Get param instances.
 */
struct devlink_param_get_rsp *
devlink_param_get(struct ynl_sock *ys, struct devlink_param_get_req *req);

/* DEVLINK_CMD_PARAM_GET - dump */
struct devlink_param_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_param_get_req_dump *
devlink_param_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_param_get_req_dump));
}
void devlink_param_get_req_dump_free(struct devlink_param_get_req_dump *req);

static inline void
devlink_param_get_req_dump_set_bus_name(struct devlink_param_get_req_dump *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_param_get_req_dump_set_dev_name(struct devlink_param_get_req_dump *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_param_get_list {
	struct devlink_param_get_list *next;
	struct devlink_param_get_rsp obj __attribute__((aligned(8)));
};

void devlink_param_get_list_free(struct devlink_param_get_list *rsp);

struct devlink_param_get_list *
devlink_param_get_dump(struct ynl_sock *ys,
		       struct devlink_param_get_req_dump *req);

/* ============== DEVLINK_CMD_PARAM_SET ============== */
/* DEVLINK_CMD_PARAM_SET - do */
struct devlink_param_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 param_name_len;
		__u32 param_type:1;
		__u32 param_value_cmode:1;
	} _present;

	char *bus_name;
	char *dev_name;
	char *param_name;
	__u8 param_type;
	enum devlink_param_cmode param_value_cmode;
};

static inline struct devlink_param_set_req *devlink_param_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_param_set_req));
}
void devlink_param_set_req_free(struct devlink_param_set_req *req);

static inline void
devlink_param_set_req_set_bus_name(struct devlink_param_set_req *req,
				   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_param_set_req_set_dev_name(struct devlink_param_set_req *req,
				   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_param_set_req_set_param_name(struct devlink_param_set_req *req,
				     const char *param_name)
{
	free(req->param_name);
	req->_present.param_name_len = strlen(param_name);
	req->param_name = malloc(req->_present.param_name_len + 1);
	memcpy(req->param_name, param_name, req->_present.param_name_len);
	req->param_name[req->_present.param_name_len] = 0;
}
static inline void
devlink_param_set_req_set_param_type(struct devlink_param_set_req *req,
				     __u8 param_type)
{
	req->_present.param_type = 1;
	req->param_type = param_type;
}
static inline void
devlink_param_set_req_set_param_value_cmode(struct devlink_param_set_req *req,
					    enum devlink_param_cmode param_value_cmode)
{
	req->_present.param_value_cmode = 1;
	req->param_value_cmode = param_value_cmode;
}

/*
 * Set param instances.
 */
int devlink_param_set(struct ynl_sock *ys, struct devlink_param_set_req *req);

/* ============== DEVLINK_CMD_REGION_GET ============== */
/* DEVLINK_CMD_REGION_GET - do */
struct devlink_region_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
};

static inline struct devlink_region_get_req *devlink_region_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_get_req));
}
void devlink_region_get_req_free(struct devlink_region_get_req *req);

static inline void
devlink_region_get_req_set_bus_name(struct devlink_region_get_req *req,
				    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_get_req_set_dev_name(struct devlink_region_get_req *req,
				    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_region_get_req_set_port_index(struct devlink_region_get_req *req,
				      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_region_get_req_set_region_name(struct devlink_region_get_req *req,
				       const char *region_name)
{
	free(req->region_name);
	req->_present.region_name_len = strlen(region_name);
	req->region_name = malloc(req->_present.region_name_len + 1);
	memcpy(req->region_name, region_name, req->_present.region_name_len);
	req->region_name[req->_present.region_name_len] = 0;
}

struct devlink_region_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
};

void devlink_region_get_rsp_free(struct devlink_region_get_rsp *rsp);

/*
 * Get region instances.
 */
struct devlink_region_get_rsp *
devlink_region_get(struct ynl_sock *ys, struct devlink_region_get_req *req);

/* DEVLINK_CMD_REGION_GET - dump */
struct devlink_region_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_region_get_req_dump *
devlink_region_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_get_req_dump));
}
void devlink_region_get_req_dump_free(struct devlink_region_get_req_dump *req);

static inline void
devlink_region_get_req_dump_set_bus_name(struct devlink_region_get_req_dump *req,
					 const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_get_req_dump_set_dev_name(struct devlink_region_get_req_dump *req,
					 const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_region_get_list {
	struct devlink_region_get_list *next;
	struct devlink_region_get_rsp obj __attribute__((aligned(8)));
};

void devlink_region_get_list_free(struct devlink_region_get_list *rsp);

struct devlink_region_get_list *
devlink_region_get_dump(struct ynl_sock *ys,
			struct devlink_region_get_req_dump *req);

/* ============== DEVLINK_CMD_REGION_NEW ============== */
/* DEVLINK_CMD_REGION_NEW - do */
struct devlink_region_new_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
		__u32 region_snapshot_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
	__u32 region_snapshot_id;
};

static inline struct devlink_region_new_req *devlink_region_new_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_new_req));
}
void devlink_region_new_req_free(struct devlink_region_new_req *req);

static inline void
devlink_region_new_req_set_bus_name(struct devlink_region_new_req *req,
				    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_new_req_set_dev_name(struct devlink_region_new_req *req,
				    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_region_new_req_set_port_index(struct devlink_region_new_req *req,
				      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_region_new_req_set_region_name(struct devlink_region_new_req *req,
				       const char *region_name)
{
	free(req->region_name);
	req->_present.region_name_len = strlen(region_name);
	req->region_name = malloc(req->_present.region_name_len + 1);
	memcpy(req->region_name, region_name, req->_present.region_name_len);
	req->region_name[req->_present.region_name_len] = 0;
}
static inline void
devlink_region_new_req_set_region_snapshot_id(struct devlink_region_new_req *req,
					      __u32 region_snapshot_id)
{
	req->_present.region_snapshot_id = 1;
	req->region_snapshot_id = region_snapshot_id;
}

struct devlink_region_new_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
		__u32 region_snapshot_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
	__u32 region_snapshot_id;
};

void devlink_region_new_rsp_free(struct devlink_region_new_rsp *rsp);

/*
 * Create region snapshot.
 */
struct devlink_region_new_rsp *
devlink_region_new(struct ynl_sock *ys, struct devlink_region_new_req *req);

/* ============== DEVLINK_CMD_REGION_DEL ============== */
/* DEVLINK_CMD_REGION_DEL - do */
struct devlink_region_del_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
		__u32 region_snapshot_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
	__u32 region_snapshot_id;
};

static inline struct devlink_region_del_req *devlink_region_del_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_del_req));
}
void devlink_region_del_req_free(struct devlink_region_del_req *req);

static inline void
devlink_region_del_req_set_bus_name(struct devlink_region_del_req *req,
				    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_del_req_set_dev_name(struct devlink_region_del_req *req,
				    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_region_del_req_set_port_index(struct devlink_region_del_req *req,
				      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_region_del_req_set_region_name(struct devlink_region_del_req *req,
				       const char *region_name)
{
	free(req->region_name);
	req->_present.region_name_len = strlen(region_name);
	req->region_name = malloc(req->_present.region_name_len + 1);
	memcpy(req->region_name, region_name, req->_present.region_name_len);
	req->region_name[req->_present.region_name_len] = 0;
}
static inline void
devlink_region_del_req_set_region_snapshot_id(struct devlink_region_del_req *req,
					      __u32 region_snapshot_id)
{
	req->_present.region_snapshot_id = 1;
	req->region_snapshot_id = region_snapshot_id;
}

/*
 * Delete region snapshot.
 */
int devlink_region_del(struct ynl_sock *ys, struct devlink_region_del_req *req);

/* ============== DEVLINK_CMD_REGION_READ ============== */
/* DEVLINK_CMD_REGION_READ - dump */
struct devlink_region_read_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
		__u32 region_snapshot_id:1;
		__u32 region_direct:1;
		__u32 region_chunk_addr:1;
		__u32 region_chunk_len:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
	__u32 region_snapshot_id;
	__u64 region_chunk_addr;
	__u64 region_chunk_len;
};

static inline struct devlink_region_read_req_dump *
devlink_region_read_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_read_req_dump));
}
void
devlink_region_read_req_dump_free(struct devlink_region_read_req_dump *req);

static inline void
devlink_region_read_req_dump_set_bus_name(struct devlink_region_read_req_dump *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_read_req_dump_set_dev_name(struct devlink_region_read_req_dump *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_region_read_req_dump_set_port_index(struct devlink_region_read_req_dump *req,
					    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_region_read_req_dump_set_region_name(struct devlink_region_read_req_dump *req,
					     const char *region_name)
{
	free(req->region_name);
	req->_present.region_name_len = strlen(region_name);
	req->region_name = malloc(req->_present.region_name_len + 1);
	memcpy(req->region_name, region_name, req->_present.region_name_len);
	req->region_name[req->_present.region_name_len] = 0;
}
static inline void
devlink_region_read_req_dump_set_region_snapshot_id(struct devlink_region_read_req_dump *req,
						    __u32 region_snapshot_id)
{
	req->_present.region_snapshot_id = 1;
	req->region_snapshot_id = region_snapshot_id;
}
static inline void
devlink_region_read_req_dump_set_region_direct(struct devlink_region_read_req_dump *req)
{
	req->_present.region_direct = 1;
}
static inline void
devlink_region_read_req_dump_set_region_chunk_addr(struct devlink_region_read_req_dump *req,
						   __u64 region_chunk_addr)
{
	req->_present.region_chunk_addr = 1;
	req->region_chunk_addr = region_chunk_addr;
}
static inline void
devlink_region_read_req_dump_set_region_chunk_len(struct devlink_region_read_req_dump *req,
						  __u64 region_chunk_len)
{
	req->_present.region_chunk_len = 1;
	req->region_chunk_len = region_chunk_len;
}

struct devlink_region_read_rsp_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
};

struct devlink_region_read_rsp_list {
	struct devlink_region_read_rsp_list *next;
	struct devlink_region_read_rsp_dump obj __attribute__((aligned(8)));
};

void
devlink_region_read_rsp_list_free(struct devlink_region_read_rsp_list *rsp);

struct devlink_region_read_rsp_list *
devlink_region_read_dump(struct ynl_sock *ys,
			 struct devlink_region_read_req_dump *req);

/* ============== DEVLINK_CMD_PORT_PARAM_GET ============== */
/* DEVLINK_CMD_PORT_PARAM_GET - do */
struct devlink_port_param_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_param_get_req *
devlink_port_param_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_param_get_req));
}
void devlink_port_param_get_req_free(struct devlink_port_param_get_req *req);

static inline void
devlink_port_param_get_req_set_bus_name(struct devlink_port_param_get_req *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_param_get_req_set_dev_name(struct devlink_port_param_get_req *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_param_get_req_set_port_index(struct devlink_port_param_get_req *req,
					  __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

struct devlink_port_param_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

void devlink_port_param_get_rsp_free(struct devlink_port_param_get_rsp *rsp);

/*
 * Get port param instances.
 */
struct devlink_port_param_get_rsp *
devlink_port_param_get(struct ynl_sock *ys,
		       struct devlink_port_param_get_req *req);

/* DEVLINK_CMD_PORT_PARAM_GET - dump */
struct devlink_port_param_get_list {
	struct devlink_port_param_get_list *next;
	struct devlink_port_param_get_rsp obj __attribute__((aligned(8)));
};

void devlink_port_param_get_list_free(struct devlink_port_param_get_list *rsp);

struct devlink_port_param_get_list *
devlink_port_param_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_PORT_PARAM_SET ============== */
/* DEVLINK_CMD_PORT_PARAM_SET - do */
struct devlink_port_param_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_param_set_req *
devlink_port_param_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_param_set_req));
}
void devlink_port_param_set_req_free(struct devlink_port_param_set_req *req);

static inline void
devlink_port_param_set_req_set_bus_name(struct devlink_port_param_set_req *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_param_set_req_set_dev_name(struct devlink_port_param_set_req *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_param_set_req_set_port_index(struct devlink_port_param_set_req *req,
					  __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

/*
 * Set port param instances.
 */
int devlink_port_param_set(struct ynl_sock *ys,
			   struct devlink_port_param_set_req *req);

/* ============== DEVLINK_CMD_INFO_GET ============== */
/* DEVLINK_CMD_INFO_GET - do */
struct devlink_info_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_info_get_req *devlink_info_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_info_get_req));
}
void devlink_info_get_req_free(struct devlink_info_get_req *req);

static inline void
devlink_info_get_req_set_bus_name(struct devlink_info_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_info_get_req_set_dev_name(struct devlink_info_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_info_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 info_driver_name_len;
		__u32 info_serial_number_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *info_driver_name;
	char *info_serial_number;
	unsigned int n_info_version_fixed;
	struct devlink_dl_info_version *info_version_fixed;
	unsigned int n_info_version_running;
	struct devlink_dl_info_version *info_version_running;
	unsigned int n_info_version_stored;
	struct devlink_dl_info_version *info_version_stored;
};

void devlink_info_get_rsp_free(struct devlink_info_get_rsp *rsp);

/*
 * Get device information, like driver name, hardware and firmware versions etc.
 */
struct devlink_info_get_rsp *
devlink_info_get(struct ynl_sock *ys, struct devlink_info_get_req *req);

/* DEVLINK_CMD_INFO_GET - dump */
struct devlink_info_get_list {
	struct devlink_info_get_list *next;
	struct devlink_info_get_rsp obj __attribute__((aligned(8)));
};

void devlink_info_get_list_free(struct devlink_info_get_list *rsp);

struct devlink_info_get_list *devlink_info_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_GET - do */
struct devlink_health_reporter_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_get_req *
devlink_health_reporter_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_get_req));
}
void
devlink_health_reporter_get_req_free(struct devlink_health_reporter_get_req *req);

static inline void
devlink_health_reporter_get_req_set_bus_name(struct devlink_health_reporter_get_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_set_dev_name(struct devlink_health_reporter_get_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_set_port_index(struct devlink_health_reporter_get_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_get_req_set_health_reporter_name(struct devlink_health_reporter_get_req *req,
							 const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

struct devlink_health_reporter_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

void
devlink_health_reporter_get_rsp_free(struct devlink_health_reporter_get_rsp *rsp);

/*
 * Get health reporter instances.
 */
struct devlink_health_reporter_get_rsp *
devlink_health_reporter_get(struct ynl_sock *ys,
			    struct devlink_health_reporter_get_req *req);

/* DEVLINK_CMD_HEALTH_REPORTER_GET - dump */
struct devlink_health_reporter_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_health_reporter_get_req_dump *
devlink_health_reporter_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_get_req_dump));
}
void
devlink_health_reporter_get_req_dump_free(struct devlink_health_reporter_get_req_dump *req);

static inline void
devlink_health_reporter_get_req_dump_set_bus_name(struct devlink_health_reporter_get_req_dump *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_dump_set_dev_name(struct devlink_health_reporter_get_req_dump *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_dump_set_port_index(struct devlink_health_reporter_get_req_dump *req,
						    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

struct devlink_health_reporter_get_list {
	struct devlink_health_reporter_get_list *next;
	struct devlink_health_reporter_get_rsp obj __attribute__((aligned(8)));
};

void
devlink_health_reporter_get_list_free(struct devlink_health_reporter_get_list *rsp);

struct devlink_health_reporter_get_list *
devlink_health_reporter_get_dump(struct ynl_sock *ys,
				 struct devlink_health_reporter_get_req_dump *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_SET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_SET - do */
struct devlink_health_reporter_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
		__u32 health_reporter_graceful_period:1;
		__u32 health_reporter_auto_recover:1;
		__u32 health_reporter_auto_dump:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
	__u64 health_reporter_graceful_period;
	__u8 health_reporter_auto_recover;
	__u8 health_reporter_auto_dump;
};

static inline struct devlink_health_reporter_set_req *
devlink_health_reporter_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_set_req));
}
void
devlink_health_reporter_set_req_free(struct devlink_health_reporter_set_req *req);

static inline void
devlink_health_reporter_set_req_set_bus_name(struct devlink_health_reporter_set_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_set_req_set_dev_name(struct devlink_health_reporter_set_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_set_req_set_port_index(struct devlink_health_reporter_set_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_set_req_set_health_reporter_name(struct devlink_health_reporter_set_req *req,
							 const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}
static inline void
devlink_health_reporter_set_req_set_health_reporter_graceful_period(struct devlink_health_reporter_set_req *req,
								    __u64 health_reporter_graceful_period)
{
	req->_present.health_reporter_graceful_period = 1;
	req->health_reporter_graceful_period = health_reporter_graceful_period;
}
static inline void
devlink_health_reporter_set_req_set_health_reporter_auto_recover(struct devlink_health_reporter_set_req *req,
								 __u8 health_reporter_auto_recover)
{
	req->_present.health_reporter_auto_recover = 1;
	req->health_reporter_auto_recover = health_reporter_auto_recover;
}
static inline void
devlink_health_reporter_set_req_set_health_reporter_auto_dump(struct devlink_health_reporter_set_req *req,
							      __u8 health_reporter_auto_dump)
{
	req->_present.health_reporter_auto_dump = 1;
	req->health_reporter_auto_dump = health_reporter_auto_dump;
}

/*
 * Set health reporter instances.
 */
int devlink_health_reporter_set(struct ynl_sock *ys,
				struct devlink_health_reporter_set_req *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_RECOVER ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_RECOVER - do */
struct devlink_health_reporter_recover_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_recover_req *
devlink_health_reporter_recover_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_recover_req));
}
void
devlink_health_reporter_recover_req_free(struct devlink_health_reporter_recover_req *req);

static inline void
devlink_health_reporter_recover_req_set_bus_name(struct devlink_health_reporter_recover_req *req,
						 const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_recover_req_set_dev_name(struct devlink_health_reporter_recover_req *req,
						 const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_recover_req_set_port_index(struct devlink_health_reporter_recover_req *req,
						   __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_recover_req_set_health_reporter_name(struct devlink_health_reporter_recover_req *req,
							     const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

/*
 * Recover health reporter instances.
 */
int devlink_health_reporter_recover(struct ynl_sock *ys,
				    struct devlink_health_reporter_recover_req *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE - do */
struct devlink_health_reporter_diagnose_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_diagnose_req *
devlink_health_reporter_diagnose_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_diagnose_req));
}
void
devlink_health_reporter_diagnose_req_free(struct devlink_health_reporter_diagnose_req *req);

static inline void
devlink_health_reporter_diagnose_req_set_bus_name(struct devlink_health_reporter_diagnose_req *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_diagnose_req_set_dev_name(struct devlink_health_reporter_diagnose_req *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_diagnose_req_set_port_index(struct devlink_health_reporter_diagnose_req *req,
						    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_diagnose_req_set_health_reporter_name(struct devlink_health_reporter_diagnose_req *req,
							      const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

/*
 * Diagnose health reporter instances.
 */
int devlink_health_reporter_diagnose(struct ynl_sock *ys,
				     struct devlink_health_reporter_diagnose_req *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET - dump */
struct devlink_health_reporter_dump_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_dump_get_req_dump *
devlink_health_reporter_dump_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_dump_get_req_dump));
}
void
devlink_health_reporter_dump_get_req_dump_free(struct devlink_health_reporter_dump_get_req_dump *req);

static inline void
devlink_health_reporter_dump_get_req_dump_set_bus_name(struct devlink_health_reporter_dump_get_req_dump *req,
						       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_dump_get_req_dump_set_dev_name(struct devlink_health_reporter_dump_get_req_dump *req,
						       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_dump_get_req_dump_set_port_index(struct devlink_health_reporter_dump_get_req_dump *req,
							 __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_dump_get_req_dump_set_health_reporter_name(struct devlink_health_reporter_dump_get_req_dump *req,
								   const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

struct devlink_health_reporter_dump_get_rsp_dump {
	struct {
		__u32 fmsg:1;
	} _present;

	struct devlink_dl_fmsg fmsg;
};

struct devlink_health_reporter_dump_get_rsp_list {
	struct devlink_health_reporter_dump_get_rsp_list *next;
	struct devlink_health_reporter_dump_get_rsp_dump obj __attribute__((aligned(8)));
};

void
devlink_health_reporter_dump_get_rsp_list_free(struct devlink_health_reporter_dump_get_rsp_list *rsp);

struct devlink_health_reporter_dump_get_rsp_list *
devlink_health_reporter_dump_get_dump(struct ynl_sock *ys,
				      struct devlink_health_reporter_dump_get_req_dump *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR - do */
struct devlink_health_reporter_dump_clear_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_dump_clear_req *
devlink_health_reporter_dump_clear_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_dump_clear_req));
}
void
devlink_health_reporter_dump_clear_req_free(struct devlink_health_reporter_dump_clear_req *req);

static inline void
devlink_health_reporter_dump_clear_req_set_bus_name(struct devlink_health_reporter_dump_clear_req *req,
						    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_dump_clear_req_set_dev_name(struct devlink_health_reporter_dump_clear_req *req,
						    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_dump_clear_req_set_port_index(struct devlink_health_reporter_dump_clear_req *req,
						      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_dump_clear_req_set_health_reporter_name(struct devlink_health_reporter_dump_clear_req *req,
								const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

/*
 * Clear dump of health reporter instances.
 */
int devlink_health_reporter_dump_clear(struct ynl_sock *ys,
				       struct devlink_health_reporter_dump_clear_req *req);

/* ============== DEVLINK_CMD_FLASH_UPDATE ============== */
/* DEVLINK_CMD_FLASH_UPDATE - do */
struct devlink_flash_update_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 flash_update_file_name_len;
		__u32 flash_update_component_len;
		__u32 flash_update_overwrite_mask:1;
	} _present;

	char *bus_name;
	char *dev_name;
	char *flash_update_file_name;
	char *flash_update_component;
	struct nla_bitfield32 flash_update_overwrite_mask;
};

static inline struct devlink_flash_update_req *
devlink_flash_update_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_flash_update_req));
}
void devlink_flash_update_req_free(struct devlink_flash_update_req *req);

static inline void
devlink_flash_update_req_set_bus_name(struct devlink_flash_update_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_flash_update_req_set_dev_name(struct devlink_flash_update_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_flash_update_req_set_flash_update_file_name(struct devlink_flash_update_req *req,
						    const char *flash_update_file_name)
{
	free(req->flash_update_file_name);
	req->_present.flash_update_file_name_len = strlen(flash_update_file_name);
	req->flash_update_file_name = malloc(req->_present.flash_update_file_name_len + 1);
	memcpy(req->flash_update_file_name, flash_update_file_name, req->_present.flash_update_file_name_len);
	req->flash_update_file_name[req->_present.flash_update_file_name_len] = 0;
}
static inline void
devlink_flash_update_req_set_flash_update_component(struct devlink_flash_update_req *req,
						    const char *flash_update_component)
{
	free(req->flash_update_component);
	req->_present.flash_update_component_len = strlen(flash_update_component);
	req->flash_update_component = malloc(req->_present.flash_update_component_len + 1);
	memcpy(req->flash_update_component, flash_update_component, req->_present.flash_update_component_len);
	req->flash_update_component[req->_present.flash_update_component_len] = 0;
}
static inline void
devlink_flash_update_req_set_flash_update_overwrite_mask(struct devlink_flash_update_req *req,
							 struct nla_bitfield32 *flash_update_overwrite_mask)
{
	req->_present.flash_update_overwrite_mask = 1;
	memcpy(&req->flash_update_overwrite_mask, flash_update_overwrite_mask, sizeof(struct nla_bitfield32));
}

/*
 * Flash update devlink instances.
 */
int devlink_flash_update(struct ynl_sock *ys,
			 struct devlink_flash_update_req *req);

/* ============== DEVLINK_CMD_TRAP_GET ============== */
/* DEVLINK_CMD_TRAP_GET - do */
struct devlink_trap_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_name;
};

static inline struct devlink_trap_get_req *devlink_trap_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_get_req));
}
void devlink_trap_get_req_free(struct devlink_trap_get_req *req);

static inline void
devlink_trap_get_req_set_bus_name(struct devlink_trap_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_get_req_set_dev_name(struct devlink_trap_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_get_req_set_trap_name(struct devlink_trap_get_req *req,
				   const char *trap_name)
{
	free(req->trap_name);
	req->_present.trap_name_len = strlen(trap_name);
	req->trap_name = malloc(req->_present.trap_name_len + 1);
	memcpy(req->trap_name, trap_name, req->_present.trap_name_len);
	req->trap_name[req->_present.trap_name_len] = 0;
}

struct devlink_trap_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_name;
};

void devlink_trap_get_rsp_free(struct devlink_trap_get_rsp *rsp);

/*
 * Get trap instances.
 */
struct devlink_trap_get_rsp *
devlink_trap_get(struct ynl_sock *ys, struct devlink_trap_get_req *req);

/* DEVLINK_CMD_TRAP_GET - dump */
struct devlink_trap_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_get_req_dump *
devlink_trap_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_get_req_dump));
}
void devlink_trap_get_req_dump_free(struct devlink_trap_get_req_dump *req);

static inline void
devlink_trap_get_req_dump_set_bus_name(struct devlink_trap_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_get_req_dump_set_dev_name(struct devlink_trap_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_get_list {
	struct devlink_trap_get_list *next;
	struct devlink_trap_get_rsp obj __attribute__((aligned(8)));
};

void devlink_trap_get_list_free(struct devlink_trap_get_list *rsp);

struct devlink_trap_get_list *
devlink_trap_get_dump(struct ynl_sock *ys,
		      struct devlink_trap_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_SET ============== */
/* DEVLINK_CMD_TRAP_SET - do */
struct devlink_trap_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_name_len;
		__u32 trap_action:1;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_name;
	enum devlink_trap_action trap_action;
};

static inline struct devlink_trap_set_req *devlink_trap_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_set_req));
}
void devlink_trap_set_req_free(struct devlink_trap_set_req *req);

static inline void
devlink_trap_set_req_set_bus_name(struct devlink_trap_set_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_set_req_set_dev_name(struct devlink_trap_set_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_set_req_set_trap_name(struct devlink_trap_set_req *req,
				   const char *trap_name)
{
	free(req->trap_name);
	req->_present.trap_name_len = strlen(trap_name);
	req->trap_name = malloc(req->_present.trap_name_len + 1);
	memcpy(req->trap_name, trap_name, req->_present.trap_name_len);
	req->trap_name[req->_present.trap_name_len] = 0;
}
static inline void
devlink_trap_set_req_set_trap_action(struct devlink_trap_set_req *req,
				     enum devlink_trap_action trap_action)
{
	req->_present.trap_action = 1;
	req->trap_action = trap_action;
}

/*
 * Set trap instances.
 */
int devlink_trap_set(struct ynl_sock *ys, struct devlink_trap_set_req *req);

/* ============== DEVLINK_CMD_TRAP_GROUP_GET ============== */
/* DEVLINK_CMD_TRAP_GROUP_GET - do */
struct devlink_trap_group_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_group_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_group_name;
};

static inline struct devlink_trap_group_get_req *
devlink_trap_group_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_group_get_req));
}
void devlink_trap_group_get_req_free(struct devlink_trap_group_get_req *req);

static inline void
devlink_trap_group_get_req_set_bus_name(struct devlink_trap_group_get_req *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_set_dev_name(struct devlink_trap_group_get_req *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_set_trap_group_name(struct devlink_trap_group_get_req *req,
					       const char *trap_group_name)
{
	free(req->trap_group_name);
	req->_present.trap_group_name_len = strlen(trap_group_name);
	req->trap_group_name = malloc(req->_present.trap_group_name_len + 1);
	memcpy(req->trap_group_name, trap_group_name, req->_present.trap_group_name_len);
	req->trap_group_name[req->_present.trap_group_name_len] = 0;
}

struct devlink_trap_group_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_group_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_group_name;
};

void devlink_trap_group_get_rsp_free(struct devlink_trap_group_get_rsp *rsp);

/*
 * Get trap group instances.
 */
struct devlink_trap_group_get_rsp *
devlink_trap_group_get(struct ynl_sock *ys,
		       struct devlink_trap_group_get_req *req);

/* DEVLINK_CMD_TRAP_GROUP_GET - dump */
struct devlink_trap_group_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_group_get_req_dump *
devlink_trap_group_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_group_get_req_dump));
}
void
devlink_trap_group_get_req_dump_free(struct devlink_trap_group_get_req_dump *req);

static inline void
devlink_trap_group_get_req_dump_set_bus_name(struct devlink_trap_group_get_req_dump *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_dump_set_dev_name(struct devlink_trap_group_get_req_dump *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_group_get_list {
	struct devlink_trap_group_get_list *next;
	struct devlink_trap_group_get_rsp obj __attribute__((aligned(8)));
};

void devlink_trap_group_get_list_free(struct devlink_trap_group_get_list *rsp);

struct devlink_trap_group_get_list *
devlink_trap_group_get_dump(struct ynl_sock *ys,
			    struct devlink_trap_group_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_GROUP_SET ============== */
/* DEVLINK_CMD_TRAP_GROUP_SET - do */
struct devlink_trap_group_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_group_name_len;
		__u32 trap_action:1;
		__u32 trap_policer_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_group_name;
	enum devlink_trap_action trap_action;
	__u32 trap_policer_id;
};

static inline struct devlink_trap_group_set_req *
devlink_trap_group_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_group_set_req));
}
void devlink_trap_group_set_req_free(struct devlink_trap_group_set_req *req);

static inline void
devlink_trap_group_set_req_set_bus_name(struct devlink_trap_group_set_req *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_group_set_req_set_dev_name(struct devlink_trap_group_set_req *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_group_set_req_set_trap_group_name(struct devlink_trap_group_set_req *req,
					       const char *trap_group_name)
{
	free(req->trap_group_name);
	req->_present.trap_group_name_len = strlen(trap_group_name);
	req->trap_group_name = malloc(req->_present.trap_group_name_len + 1);
	memcpy(req->trap_group_name, trap_group_name, req->_present.trap_group_name_len);
	req->trap_group_name[req->_present.trap_group_name_len] = 0;
}
static inline void
devlink_trap_group_set_req_set_trap_action(struct devlink_trap_group_set_req *req,
					   enum devlink_trap_action trap_action)
{
	req->_present.trap_action = 1;
	req->trap_action = trap_action;
}
static inline void
devlink_trap_group_set_req_set_trap_policer_id(struct devlink_trap_group_set_req *req,
					       __u32 trap_policer_id)
{
	req->_present.trap_policer_id = 1;
	req->trap_policer_id = trap_policer_id;
}

/*
 * Set trap group instances.
 */
int devlink_trap_group_set(struct ynl_sock *ys,
			   struct devlink_trap_group_set_req *req);

/* ============== DEVLINK_CMD_TRAP_POLICER_GET ============== */
/* DEVLINK_CMD_TRAP_POLICER_GET - do */
struct devlink_trap_policer_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_policer_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 trap_policer_id;
};

static inline struct devlink_trap_policer_get_req *
devlink_trap_policer_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_policer_get_req));
}
void
devlink_trap_policer_get_req_free(struct devlink_trap_policer_get_req *req);

static inline void
devlink_trap_policer_get_req_set_bus_name(struct devlink_trap_policer_get_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_set_dev_name(struct devlink_trap_policer_get_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_set_trap_policer_id(struct devlink_trap_policer_get_req *req,
						 __u32 trap_policer_id)
{
	req->_present.trap_policer_id = 1;
	req->trap_policer_id = trap_policer_id;
}

struct devlink_trap_policer_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_policer_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 trap_policer_id;
};

void
devlink_trap_policer_get_rsp_free(struct devlink_trap_policer_get_rsp *rsp);

/*
 * Get trap policer instances.
 */
struct devlink_trap_policer_get_rsp *
devlink_trap_policer_get(struct ynl_sock *ys,
			 struct devlink_trap_policer_get_req *req);

/* DEVLINK_CMD_TRAP_POLICER_GET - dump */
struct devlink_trap_policer_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_policer_get_req_dump *
devlink_trap_policer_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_policer_get_req_dump));
}
void
devlink_trap_policer_get_req_dump_free(struct devlink_trap_policer_get_req_dump *req);

static inline void
devlink_trap_policer_get_req_dump_set_bus_name(struct devlink_trap_policer_get_req_dump *req,
					       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_dump_set_dev_name(struct devlink_trap_policer_get_req_dump *req,
					       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_policer_get_list {
	struct devlink_trap_policer_get_list *next;
	struct devlink_trap_policer_get_rsp obj __attribute__((aligned(8)));
};

void
devlink_trap_policer_get_list_free(struct devlink_trap_policer_get_list *rsp);

struct devlink_trap_policer_get_list *
devlink_trap_policer_get_dump(struct ynl_sock *ys,
			      struct devlink_trap_policer_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_POLICER_SET ============== */
/* DEVLINK_CMD_TRAP_POLICER_SET - do */
struct devlink_trap_policer_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_policer_id:1;
		__u32 trap_policer_rate:1;
		__u32 trap_policer_burst:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 trap_policer_id;
	__u64 trap_policer_rate;
	__u64 trap_policer_burst;
};

static inline struct devlink_trap_policer_set_req *
devlink_trap_policer_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_policer_set_req));
}
void
devlink_trap_policer_set_req_free(struct devlink_trap_policer_set_req *req);

static inline void
devlink_trap_policer_set_req_set_bus_name(struct devlink_trap_policer_set_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_policer_set_req_set_dev_name(struct devlink_trap_policer_set_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_policer_set_req_set_trap_policer_id(struct devlink_trap_policer_set_req *req,
						 __u32 trap_policer_id)
{
	req->_present.trap_policer_id = 1;
	req->trap_policer_id = trap_policer_id;
}
static inline void
devlink_trap_policer_set_req_set_trap_policer_rate(struct devlink_trap_policer_set_req *req,
						   __u64 trap_policer_rate)
{
	req->_present.trap_policer_rate = 1;
	req->trap_policer_rate = trap_policer_rate;
}
static inline void
devlink_trap_policer_set_req_set_trap_policer_burst(struct devlink_trap_policer_set_req *req,
						    __u64 trap_policer_burst)
{
	req->_present.trap_policer_burst = 1;
	req->trap_policer_burst = trap_policer_burst;
}

/*
 * Get trap policer instances.
 */
int devlink_trap_policer_set(struct ynl_sock *ys,
			     struct devlink_trap_policer_set_req *req);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_TEST ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_TEST - do */
struct devlink_health_reporter_test_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_test_req *
devlink_health_reporter_test_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_test_req));
}
void
devlink_health_reporter_test_req_free(struct devlink_health_reporter_test_req *req);

static inline void
devlink_health_reporter_test_req_set_bus_name(struct devlink_health_reporter_test_req *req,
					      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_test_req_set_dev_name(struct devlink_health_reporter_test_req *req,
					      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_test_req_set_port_index(struct devlink_health_reporter_test_req *req,
						__u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_test_req_set_health_reporter_name(struct devlink_health_reporter_test_req *req,
							  const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

/*
 * Test health reporter instances.
 */
int devlink_health_reporter_test(struct ynl_sock *ys,
				 struct devlink_health_reporter_test_req *req);

/* ============== DEVLINK_CMD_RATE_GET ============== */
/* DEVLINK_CMD_RATE_GET - do */
struct devlink_rate_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 rate_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *rate_node_name;
};

static inline struct devlink_rate_get_req *devlink_rate_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_get_req));
}
void devlink_rate_get_req_free(struct devlink_rate_get_req *req);

static inline void
devlink_rate_get_req_set_bus_name(struct devlink_rate_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_get_req_set_dev_name(struct devlink_rate_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_rate_get_req_set_port_index(struct devlink_rate_get_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_rate_get_req_set_rate_node_name(struct devlink_rate_get_req *req,
					const char *rate_node_name)
{
	free(req->rate_node_name);
	req->_present.rate_node_name_len = strlen(rate_node_name);
	req->rate_node_name = malloc(req->_present.rate_node_name_len + 1);
	memcpy(req->rate_node_name, rate_node_name, req->_present.rate_node_name_len);
	req->rate_node_name[req->_present.rate_node_name_len] = 0;
}

struct devlink_rate_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 rate_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *rate_node_name;
};

void devlink_rate_get_rsp_free(struct devlink_rate_get_rsp *rsp);

/*
 * Get rate instances.
 */
struct devlink_rate_get_rsp *
devlink_rate_get(struct ynl_sock *ys, struct devlink_rate_get_req *req);

/* DEVLINK_CMD_RATE_GET - dump */
struct devlink_rate_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_rate_get_req_dump *
devlink_rate_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_get_req_dump));
}
void devlink_rate_get_req_dump_free(struct devlink_rate_get_req_dump *req);

static inline void
devlink_rate_get_req_dump_set_bus_name(struct devlink_rate_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_get_req_dump_set_dev_name(struct devlink_rate_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_rate_get_list {
	struct devlink_rate_get_list *next;
	struct devlink_rate_get_rsp obj __attribute__((aligned(8)));
};

void devlink_rate_get_list_free(struct devlink_rate_get_list *rsp);

struct devlink_rate_get_list *
devlink_rate_get_dump(struct ynl_sock *ys,
		      struct devlink_rate_get_req_dump *req);

/* ============== DEVLINK_CMD_RATE_SET ============== */
/* DEVLINK_CMD_RATE_SET - do */
struct devlink_rate_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 rate_node_name_len;
		__u32 rate_tx_share:1;
		__u32 rate_tx_max:1;
		__u32 rate_tx_priority:1;
		__u32 rate_tx_weight:1;
		__u32 rate_parent_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *rate_node_name;
	__u64 rate_tx_share;
	__u64 rate_tx_max;
	__u32 rate_tx_priority;
	__u32 rate_tx_weight;
	char *rate_parent_node_name;
};

static inline struct devlink_rate_set_req *devlink_rate_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_set_req));
}
void devlink_rate_set_req_free(struct devlink_rate_set_req *req);

static inline void
devlink_rate_set_req_set_bus_name(struct devlink_rate_set_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_set_req_set_dev_name(struct devlink_rate_set_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_rate_set_req_set_rate_node_name(struct devlink_rate_set_req *req,
					const char *rate_node_name)
{
	free(req->rate_node_name);
	req->_present.rate_node_name_len = strlen(rate_node_name);
	req->rate_node_name = malloc(req->_present.rate_node_name_len + 1);
	memcpy(req->rate_node_name, rate_node_name, req->_present.rate_node_name_len);
	req->rate_node_name[req->_present.rate_node_name_len] = 0;
}
static inline void
devlink_rate_set_req_set_rate_tx_share(struct devlink_rate_set_req *req,
				       __u64 rate_tx_share)
{
	req->_present.rate_tx_share = 1;
	req->rate_tx_share = rate_tx_share;
}
static inline void
devlink_rate_set_req_set_rate_tx_max(struct devlink_rate_set_req *req,
				     __u64 rate_tx_max)
{
	req->_present.rate_tx_max = 1;
	req->rate_tx_max = rate_tx_max;
}
static inline void
devlink_rate_set_req_set_rate_tx_priority(struct devlink_rate_set_req *req,
					  __u32 rate_tx_priority)
{
	req->_present.rate_tx_priority = 1;
	req->rate_tx_priority = rate_tx_priority;
}
static inline void
devlink_rate_set_req_set_rate_tx_weight(struct devlink_rate_set_req *req,
					__u32 rate_tx_weight)
{
	req->_present.rate_tx_weight = 1;
	req->rate_tx_weight = rate_tx_weight;
}
static inline void
devlink_rate_set_req_set_rate_parent_node_name(struct devlink_rate_set_req *req,
					       const char *rate_parent_node_name)
{
	free(req->rate_parent_node_name);
	req->_present.rate_parent_node_name_len = strlen(rate_parent_node_name);
	req->rate_parent_node_name = malloc(req->_present.rate_parent_node_name_len + 1);
	memcpy(req->rate_parent_node_name, rate_parent_node_name, req->_present.rate_parent_node_name_len);
	req->rate_parent_node_name[req->_present.rate_parent_node_name_len] = 0;
}

/*
 * Set rate instances.
 */
int devlink_rate_set(struct ynl_sock *ys, struct devlink_rate_set_req *req);

/* ============== DEVLINK_CMD_RATE_NEW ============== */
/* DEVLINK_CMD_RATE_NEW - do */
struct devlink_rate_new_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 rate_node_name_len;
		__u32 rate_tx_share:1;
		__u32 rate_tx_max:1;
		__u32 rate_tx_priority:1;
		__u32 rate_tx_weight:1;
		__u32 rate_parent_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *rate_node_name;
	__u64 rate_tx_share;
	__u64 rate_tx_max;
	__u32 rate_tx_priority;
	__u32 rate_tx_weight;
	char *rate_parent_node_name;
};

static inline struct devlink_rate_new_req *devlink_rate_new_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_new_req));
}
void devlink_rate_new_req_free(struct devlink_rate_new_req *req);

static inline void
devlink_rate_new_req_set_bus_name(struct devlink_rate_new_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_new_req_set_dev_name(struct devlink_rate_new_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_rate_new_req_set_rate_node_name(struct devlink_rate_new_req *req,
					const char *rate_node_name)
{
	free(req->rate_node_name);
	req->_present.rate_node_name_len = strlen(rate_node_name);
	req->rate_node_name = malloc(req->_present.rate_node_name_len + 1);
	memcpy(req->rate_node_name, rate_node_name, req->_present.rate_node_name_len);
	req->rate_node_name[req->_present.rate_node_name_len] = 0;
}
static inline void
devlink_rate_new_req_set_rate_tx_share(struct devlink_rate_new_req *req,
				       __u64 rate_tx_share)
{
	req->_present.rate_tx_share = 1;
	req->rate_tx_share = rate_tx_share;
}
static inline void
devlink_rate_new_req_set_rate_tx_max(struct devlink_rate_new_req *req,
				     __u64 rate_tx_max)
{
	req->_present.rate_tx_max = 1;
	req->rate_tx_max = rate_tx_max;
}
static inline void
devlink_rate_new_req_set_rate_tx_priority(struct devlink_rate_new_req *req,
					  __u32 rate_tx_priority)
{
	req->_present.rate_tx_priority = 1;
	req->rate_tx_priority = rate_tx_priority;
}
static inline void
devlink_rate_new_req_set_rate_tx_weight(struct devlink_rate_new_req *req,
					__u32 rate_tx_weight)
{
	req->_present.rate_tx_weight = 1;
	req->rate_tx_weight = rate_tx_weight;
}
static inline void
devlink_rate_new_req_set_rate_parent_node_name(struct devlink_rate_new_req *req,
					       const char *rate_parent_node_name)
{
	free(req->rate_parent_node_name);
	req->_present.rate_parent_node_name_len = strlen(rate_parent_node_name);
	req->rate_parent_node_name = malloc(req->_present.rate_parent_node_name_len + 1);
	memcpy(req->rate_parent_node_name, rate_parent_node_name, req->_present.rate_parent_node_name_len);
	req->rate_parent_node_name[req->_present.rate_parent_node_name_len] = 0;
}

/*
 * Create rate instances.
 */
int devlink_rate_new(struct ynl_sock *ys, struct devlink_rate_new_req *req);

/* ============== DEVLINK_CMD_RATE_DEL ============== */
/* DEVLINK_CMD_RATE_DEL - do */
struct devlink_rate_del_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 rate_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *rate_node_name;
};

static inline struct devlink_rate_del_req *devlink_rate_del_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_del_req));
}
void devlink_rate_del_req_free(struct devlink_rate_del_req *req);

static inline void
devlink_rate_del_req_set_bus_name(struct devlink_rate_del_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_del_req_set_dev_name(struct devlink_rate_del_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_rate_del_req_set_rate_node_name(struct devlink_rate_del_req *req,
					const char *rate_node_name)
{
	free(req->rate_node_name);
	req->_present.rate_node_name_len = strlen(rate_node_name);
	req->rate_node_name = malloc(req->_present.rate_node_name_len + 1);
	memcpy(req->rate_node_name, rate_node_name, req->_present.rate_node_name_len);
	req->rate_node_name[req->_present.rate_node_name_len] = 0;
}

/*
 * Delete rate instances.
 */
int devlink_rate_del(struct ynl_sock *ys, struct devlink_rate_del_req *req);

/* ============== DEVLINK_CMD_LINECARD_GET ============== */
/* DEVLINK_CMD_LINECARD_GET - do */
struct devlink_linecard_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 linecard_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 linecard_index;
};

static inline struct devlink_linecard_get_req *
devlink_linecard_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_linecard_get_req));
}
void devlink_linecard_get_req_free(struct devlink_linecard_get_req *req);

static inline void
devlink_linecard_get_req_set_bus_name(struct devlink_linecard_get_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_linecard_get_req_set_dev_name(struct devlink_linecard_get_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_linecard_get_req_set_linecard_index(struct devlink_linecard_get_req *req,
					    __u32 linecard_index)
{
	req->_present.linecard_index = 1;
	req->linecard_index = linecard_index;
}

struct devlink_linecard_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 linecard_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 linecard_index;
};

void devlink_linecard_get_rsp_free(struct devlink_linecard_get_rsp *rsp);

/*
 * Get line card instances.
 */
struct devlink_linecard_get_rsp *
devlink_linecard_get(struct ynl_sock *ys, struct devlink_linecard_get_req *req);

/* DEVLINK_CMD_LINECARD_GET - dump */
struct devlink_linecard_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_linecard_get_req_dump *
devlink_linecard_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_linecard_get_req_dump));
}
void
devlink_linecard_get_req_dump_free(struct devlink_linecard_get_req_dump *req);

static inline void
devlink_linecard_get_req_dump_set_bus_name(struct devlink_linecard_get_req_dump *req,
					   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_linecard_get_req_dump_set_dev_name(struct devlink_linecard_get_req_dump *req,
					   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_linecard_get_list {
	struct devlink_linecard_get_list *next;
	struct devlink_linecard_get_rsp obj __attribute__((aligned(8)));
};

void devlink_linecard_get_list_free(struct devlink_linecard_get_list *rsp);

struct devlink_linecard_get_list *
devlink_linecard_get_dump(struct ynl_sock *ys,
			  struct devlink_linecard_get_req_dump *req);

/* ============== DEVLINK_CMD_LINECARD_SET ============== */
/* DEVLINK_CMD_LINECARD_SET - do */
struct devlink_linecard_set_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 linecard_index:1;
		__u32 linecard_type_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 linecard_index;
	char *linecard_type;
};

static inline struct devlink_linecard_set_req *
devlink_linecard_set_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_linecard_set_req));
}
void devlink_linecard_set_req_free(struct devlink_linecard_set_req *req);

static inline void
devlink_linecard_set_req_set_bus_name(struct devlink_linecard_set_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_linecard_set_req_set_dev_name(struct devlink_linecard_set_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_linecard_set_req_set_linecard_index(struct devlink_linecard_set_req *req,
					    __u32 linecard_index)
{
	req->_present.linecard_index = 1;
	req->linecard_index = linecard_index;
}
static inline void
devlink_linecard_set_req_set_linecard_type(struct devlink_linecard_set_req *req,
					   const char *linecard_type)
{
	free(req->linecard_type);
	req->_present.linecard_type_len = strlen(linecard_type);
	req->linecard_type = malloc(req->_present.linecard_type_len + 1);
	memcpy(req->linecard_type, linecard_type, req->_present.linecard_type_len);
	req->linecard_type[req->_present.linecard_type_len] = 0;
}

/*
 * Set line card instances.
 */
int devlink_linecard_set(struct ynl_sock *ys,
			 struct devlink_linecard_set_req *req);

/* ============== DEVLINK_CMD_SELFTESTS_GET ============== */
/* DEVLINK_CMD_SELFTESTS_GET - do */
struct devlink_selftests_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_selftests_get_req *
devlink_selftests_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_selftests_get_req));
}
void devlink_selftests_get_req_free(struct devlink_selftests_get_req *req);

static inline void
devlink_selftests_get_req_set_bus_name(struct devlink_selftests_get_req *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_selftests_get_req_set_dev_name(struct devlink_selftests_get_req *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_selftests_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

void devlink_selftests_get_rsp_free(struct devlink_selftests_get_rsp *rsp);

/*
 * Get device selftest instances.
 */
struct devlink_selftests_get_rsp *
devlink_selftests_get(struct ynl_sock *ys,
		      struct devlink_selftests_get_req *req);

/* DEVLINK_CMD_SELFTESTS_GET - dump */
struct devlink_selftests_get_list {
	struct devlink_selftests_get_list *next;
	struct devlink_selftests_get_rsp obj __attribute__((aligned(8)));
};

void devlink_selftests_get_list_free(struct devlink_selftests_get_list *rsp);

struct devlink_selftests_get_list *
devlink_selftests_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_SELFTESTS_RUN ============== */
/* DEVLINK_CMD_SELFTESTS_RUN - do */
struct devlink_selftests_run_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 selftests:1;
	} _present;

	char *bus_name;
	char *dev_name;
	struct devlink_dl_selftest_id selftests;
};

static inline struct devlink_selftests_run_req *
devlink_selftests_run_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_selftests_run_req));
}
void devlink_selftests_run_req_free(struct devlink_selftests_run_req *req);

static inline void
devlink_selftests_run_req_set_bus_name(struct devlink_selftests_run_req *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_selftests_run_req_set_dev_name(struct devlink_selftests_run_req *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_selftests_run_req_set_selftests_flash(struct devlink_selftests_run_req *req)
{
	req->_present.selftests = 1;
	req->selftests._present.flash = 1;
}

/*
 * Run device selftest instances.
 */
int devlink_selftests_run(struct ynl_sock *ys,
			  struct devlink_selftests_run_req *req);

#endif /* _LINUX_DEVLINK_GEN_H */

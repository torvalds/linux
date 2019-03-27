/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_DCBX_API_H__
#define __ECORE_DCBX_API_H__

#include "ecore_status.h"

#define DCBX_CONFIG_MAX_APP_PROTOCOL	4

enum ecore_mib_read_type {
	ECORE_DCBX_OPERATIONAL_MIB,
	ECORE_DCBX_REMOTE_MIB,
	ECORE_DCBX_LOCAL_MIB,
	ECORE_DCBX_REMOTE_LLDP_MIB,
	ECORE_DCBX_LOCAL_LLDP_MIB,
	ECORE_DCBX_LLDP_TLVS
};

struct ecore_dcbx_app_data {
	bool enable;		/* DCB enabled */
	u8 update;		/* Update indication */
	u8 priority;		/* Priority */
	u8 tc;			/* Traffic Class */
	bool dscp_enable;	/* DSCP enabled */
	u8 dscp_val;		/* DSCP value */
	bool dont_add_vlan0;	/* Do not insert a vlan tag with id 0 */
};

#ifndef __EXTRACT__LINUX__IF__
enum dcbx_protocol_type {
	DCBX_PROTOCOL_ISCSI,
	DCBX_PROTOCOL_FCOE,
	DCBX_PROTOCOL_ROCE,
	DCBX_PROTOCOL_ROCE_V2,
	DCBX_PROTOCOL_ETH,
	DCBX_PROTOCOL_IWARP,
	DCBX_MAX_PROTOCOL_TYPE
};

#define ECORE_LLDP_CHASSIS_ID_STAT_LEN 4
#define ECORE_LLDP_PORT_ID_STAT_LEN 4
#define ECORE_DCBX_MAX_APP_PROTOCOL 32
#define ECORE_MAX_PFC_PRIORITIES 8
#define ECORE_DCBX_DSCP_SIZE 64

struct ecore_dcbx_lldp_remote {
	u32     peer_chassis_id[ECORE_LLDP_CHASSIS_ID_STAT_LEN];
	u32     peer_port_id[ECORE_LLDP_PORT_ID_STAT_LEN];
	bool	enable_rx;
	bool	enable_tx;
	u32     tx_interval;
	u32     max_credit;
};

struct ecore_dcbx_lldp_local {
	u32     local_chassis_id[ECORE_LLDP_CHASSIS_ID_STAT_LEN];
	u32     local_port_id[ECORE_LLDP_PORT_ID_STAT_LEN];
};

struct ecore_dcbx_app_prio {
	u8	roce;
	u8	roce_v2;
	u8	fcoe;
	u8	iscsi;
	u8	eth;
};

struct ecore_dbcx_pfc_params {
	bool	willing;
	bool	enabled;
	u8	prio[ECORE_MAX_PFC_PRIORITIES];
	u8	max_tc;
};

enum ecore_dcbx_sf_ieee_type {
	ECORE_DCBX_SF_IEEE_ETHTYPE,
	ECORE_DCBX_SF_IEEE_TCP_PORT,
	ECORE_DCBX_SF_IEEE_UDP_PORT,
	ECORE_DCBX_SF_IEEE_TCP_UDP_PORT
};

struct ecore_app_entry {
	bool ethtype;
	enum ecore_dcbx_sf_ieee_type sf_ieee;
	bool enabled;
	u8 prio;
	u16 proto_id;
	enum dcbx_protocol_type proto_type;
};

struct ecore_dcbx_params {
	struct ecore_app_entry app_entry[ECORE_DCBX_MAX_APP_PROTOCOL];
	u16	num_app_entries;
	bool	app_willing;
	bool	app_valid;
	bool	app_error;
	bool	ets_willing;
	bool	ets_enabled;
	bool	ets_cbs;
	u8	ets_pri_tc_tbl[ECORE_MAX_PFC_PRIORITIES];
	u8	ets_tc_bw_tbl[ECORE_MAX_PFC_PRIORITIES];
	u8	ets_tc_tsa_tbl[ECORE_MAX_PFC_PRIORITIES];
	struct ecore_dbcx_pfc_params pfc;
	u8	max_ets_tc;
};

struct ecore_dcbx_admin_params {
	struct ecore_dcbx_params params;
	bool valid;		/* Indicate validity of params */
};

struct ecore_dcbx_remote_params {
	struct ecore_dcbx_params params;
	bool valid;		/* Indicate validity of params */
};

struct ecore_dcbx_operational_params {
	struct ecore_dcbx_app_prio app_prio;
	struct ecore_dcbx_params params;
	bool valid;		/* Indicate validity of params */
	bool enabled;
	bool ieee;
	bool cee;
	bool local;
	u32 err;
};

struct ecore_dcbx_dscp_params {
	bool enabled;
	u8 dscp_pri_map[ECORE_DCBX_DSCP_SIZE];
};

struct ecore_dcbx_get {
	struct ecore_dcbx_operational_params operational;
	struct ecore_dcbx_lldp_remote lldp_remote;
	struct ecore_dcbx_lldp_local lldp_local;
	struct ecore_dcbx_remote_params remote;
	struct ecore_dcbx_admin_params local;
	struct ecore_dcbx_dscp_params dscp;
};
#endif

#define ECORE_DCBX_VERSION_DISABLED	0
#define ECORE_DCBX_VERSION_IEEE		1
#define ECORE_DCBX_VERSION_CEE		2
#define ECORE_DCBX_VERSION_DYNAMIC	3

struct ecore_dcbx_set {
#define ECORE_DCBX_OVERRIDE_STATE	(1 << 0)
#define ECORE_DCBX_OVERRIDE_PFC_CFG	(1 << 1)
#define ECORE_DCBX_OVERRIDE_ETS_CFG	(1 << 2)
#define ECORE_DCBX_OVERRIDE_APP_CFG	(1 << 3)
#define ECORE_DCBX_OVERRIDE_DSCP_CFG	(1 << 4)
	u32 override_flags;
	bool enabled;
	struct ecore_dcbx_admin_params config;
	u32 ver_num;
	struct ecore_dcbx_dscp_params dscp;
};

struct ecore_dcbx_results {
	bool dcbx_enabled;
	u8 pf_id;
	struct ecore_dcbx_app_data arr[DCBX_MAX_PROTOCOL_TYPE];
};

struct ecore_dcbx_app_metadata {
	enum dcbx_protocol_type id;
	char *name;
	enum ecore_pci_personality personality;
};

enum ecore_lldp_agent {
	ECORE_LLDP_NEAREST_BRIDGE = 0,
	ECORE_LLDP_NEAREST_NON_TPMR_BRIDGE,
	ECORE_LLDP_NEAREST_CUSTOMER_BRIDGE,
	ECORE_LLDP_MAX_AGENTS
};

struct ecore_lldp_config_params {
	enum ecore_lldp_agent agent;
	u8 tx_interval;
	u8 tx_hold;
	u8 tx_credit;
	bool rx_enable;
	bool tx_enable;
	u32 chassis_id_tlv[ECORE_LLDP_CHASSIS_ID_STAT_LEN];
	u32 port_id_tlv[ECORE_LLDP_PORT_ID_STAT_LEN];
};

#define ECORE_LLDP_SYS_TLV_SIZE 256
struct ecore_lldp_sys_tlvs {
	bool discard_mandatory_tlv;
	u8 buf[ECORE_LLDP_SYS_TLV_SIZE];
	u16 buf_size;
};

struct ecore_lldp_stats {
	enum ecore_lldp_agent agent;
	u32 tx_frames;
	u32 rx_frames;
	u32 rx_discards;
	u32 rx_age_outs;
};

enum _ecore_status_t ecore_dcbx_query_params(struct ecore_hwfn *p_hwfn,
					     struct ecore_dcbx_get *p_get,
					     enum ecore_mib_read_type type);

enum _ecore_status_t ecore_dcbx_get_config_params(struct ecore_hwfn *p_hwfn,
						  struct ecore_dcbx_set
						  *params);

enum _ecore_status_t ecore_dcbx_config_params(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      struct ecore_dcbx_set *params,
					      bool hw_commit);

enum _ecore_status_t ecore_lldp_register_tlv(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     enum ecore_lldp_agent agent,
					     u8 tlv_type);

enum _ecore_status_t
ecore_lldp_get_params(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		      struct ecore_lldp_config_params *p_params);

enum _ecore_status_t
ecore_lldp_set_params(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		      struct ecore_lldp_config_params *p_params);

enum _ecore_status_t
ecore_lldp_set_system_tlvs(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   struct ecore_lldp_sys_tlvs *p_params);

/* Returns priority value for a given dscp index */
enum _ecore_status_t
ecore_dcbx_get_dscp_priority(struct ecore_hwfn *p_hwfn,
			     u8 dscp_index, u8 *p_dscp_pri);

/* Sets priority value for a given dscp index */
enum _ecore_status_t
ecore_dcbx_set_dscp_priority(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			     u8 dscp_index, u8 pri_val);

enum _ecore_status_t
ecore_lldp_get_stats(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		     struct ecore_lldp_stats *p_params);

#ifndef __EXTRACT__LINUX__C__
static const struct ecore_dcbx_app_metadata ecore_dcbx_app_update[] = {
	{DCBX_PROTOCOL_ISCSI, "ISCSI", ECORE_PCI_ISCSI},
	{DCBX_PROTOCOL_FCOE, "FCOE", ECORE_PCI_FCOE},
	{DCBX_PROTOCOL_ROCE, "ROCE", ECORE_PCI_ETH_ROCE},
	{DCBX_PROTOCOL_ROCE_V2, "ROCE_V2", ECORE_PCI_ETH_ROCE},
	{DCBX_PROTOCOL_ETH, "ETH", ECORE_PCI_ETH},
	{DCBX_PROTOCOL_IWARP, "IWARP", ECORE_PCI_ETH_IWARP}
};
#endif

#endif /* __ECORE_DCBX_API_H__ */

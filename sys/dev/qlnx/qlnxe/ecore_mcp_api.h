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

#ifndef __ECORE_MCP_API_H__
#define __ECORE_MCP_API_H__

#include "ecore_status.h"

struct ecore_mcp_link_speed_params {
	bool autoneg;
	u32 advertised_speeds; /* bitmask of DRV_SPEED_CAPABILITY */
	u32 forced_speed; /* In Mb/s */
};

struct ecore_mcp_link_pause_params {
	bool autoneg;
	bool forced_rx;
	bool forced_tx;
};

enum ecore_mcp_eee_mode {
	ECORE_MCP_EEE_DISABLED,
	ECORE_MCP_EEE_ENABLED,
	ECORE_MCP_EEE_UNSUPPORTED
};

#ifndef __EXTRACT__LINUX__
struct ecore_link_eee_params {
	u32 tx_lpi_timer;
#define ECORE_EEE_1G_ADV	(1 << 0)
#define ECORE_EEE_10G_ADV	(1 << 1)
	/* Capabilities are represented using ECORE_EEE_*_ADV values */
	u8 adv_caps;
	u8 lp_adv_caps;
	bool enable;
	bool tx_lpi_enable;
};
#endif

struct ecore_mcp_link_params {
	struct ecore_mcp_link_speed_params speed;
	struct ecore_mcp_link_pause_params pause;
	u32 loopback_mode; /* in PMM_LOOPBACK values */
	struct ecore_link_eee_params eee;
};

struct ecore_mcp_link_capabilities {
	u32 speed_capabilities;
	bool default_speed_autoneg; /* In Mb/s */
	u32 default_speed; /* In Mb/s */ /* __LINUX__THROW__ */
	enum ecore_mcp_eee_mode default_eee;
	u32 eee_lpi_timer;
	u8 eee_speed_caps;
};

struct ecore_mcp_link_state {
	bool link_up;

	u32 min_pf_rate; /* In Mb/s */

	/* Actual link speed in Mb/s */
	u32 line_speed;

	/* PF max speed in MB/s, deduced from line_speed
	 * according to PF max bandwidth configuration.
	 */
	u32 speed;
	bool full_duplex;

	bool an;
	bool an_complete;
	bool parallel_detection;
	bool pfc_enabled;

#define ECORE_LINK_PARTNER_SPEED_1G_HD	(1 << 0)
#define ECORE_LINK_PARTNER_SPEED_1G_FD	(1 << 1)
#define ECORE_LINK_PARTNER_SPEED_10G	(1 << 2)
#define ECORE_LINK_PARTNER_SPEED_20G	(1 << 3)
#define ECORE_LINK_PARTNER_SPEED_25G	(1 << 4)
#define ECORE_LINK_PARTNER_SPEED_40G	(1 << 5)
#define ECORE_LINK_PARTNER_SPEED_50G	(1 << 6)
#define ECORE_LINK_PARTNER_SPEED_100G	(1 << 7)
	u32 partner_adv_speed;

	bool partner_tx_flow_ctrl_en;
	bool partner_rx_flow_ctrl_en;

#define ECORE_LINK_PARTNER_SYMMETRIC_PAUSE (1)
#define ECORE_LINK_PARTNER_ASYMMETRIC_PAUSE (2)
#define ECORE_LINK_PARTNER_BOTH_PAUSE (3)
	u8 partner_adv_pause;

	bool sfp_tx_fault;

	bool eee_active;
	u8 eee_adv_caps;
	u8 eee_lp_adv_caps;
};

struct ecore_mcp_function_info {
	u8 pause_on_host;

	enum ecore_pci_personality protocol;

	u8 bandwidth_min;
	u8 bandwidth_max;

	u8 mac[ETH_ALEN];

	u64 wwn_port;
	u64 wwn_node;

#define ECORE_MCP_VLAN_UNSET		(0xffff)
	u16 ovlan;

	u16 mtu;
};

#ifndef __EXTRACT__LINUX__
enum ecore_nvm_images {
	ECORE_NVM_IMAGE_ISCSI_CFG,
	ECORE_NVM_IMAGE_FCOE_CFG,
	ECORE_NVM_IMAGE_MDUMP,
};
#endif

struct ecore_mcp_drv_version {
	u32 version;
	u8 name[MCP_DRV_VER_STR_SIZE - 4];
};

struct ecore_mcp_lan_stats {
	u64 ucast_rx_pkts;
	u64 ucast_tx_pkts;
	u32 fcs_err;
};

#ifndef ECORE_PROTO_STATS
#define ECORE_PROTO_STATS
struct ecore_mcp_fcoe_stats {
	u64 rx_pkts;
	u64 tx_pkts;
	u32 fcs_err;
	u32 login_failure;
};

struct ecore_mcp_iscsi_stats {
	u64 rx_pdus;
	u64 tx_pdus;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct ecore_mcp_rdma_stats {
	u64 rx_pkts;
	u64 tx_pkts;
	u64 rx_bytes;
	u64 tx_byts;
};

enum ecore_mcp_protocol_type {
	ECORE_MCP_LAN_STATS,
	ECORE_MCP_FCOE_STATS,
	ECORE_MCP_ISCSI_STATS,
	ECORE_MCP_RDMA_STATS
};

union ecore_mcp_protocol_stats {
	struct ecore_mcp_lan_stats lan_stats;
	struct ecore_mcp_fcoe_stats fcoe_stats;
	struct ecore_mcp_iscsi_stats iscsi_stats;
	struct ecore_mcp_rdma_stats rdma_stats;
};
#endif

enum ecore_ov_client {
	ECORE_OV_CLIENT_DRV,
	ECORE_OV_CLIENT_USER,
	ECORE_OV_CLIENT_VENDOR_SPEC
};

enum ecore_ov_driver_state {
	ECORE_OV_DRIVER_STATE_NOT_LOADED,
	ECORE_OV_DRIVER_STATE_DISABLED,
	ECORE_OV_DRIVER_STATE_ACTIVE
};

enum ecore_ov_wol {
	ECORE_OV_WOL_DEFAULT,
	ECORE_OV_WOL_DISABLED,
	ECORE_OV_WOL_ENABLED
};

#ifndef __EXTRACT__LINUX__
#define ECORE_MAX_NPIV_ENTRIES 128
#define ECORE_WWN_SIZE 8
struct ecore_fc_npiv_tbl {
	u16 num_wwpn;
	u16 num_wwnn;
	u8 wwpn[ECORE_MAX_NPIV_ENTRIES][ECORE_WWN_SIZE];
	u8 wwnn[ECORE_MAX_NPIV_ENTRIES][ECORE_WWN_SIZE];
};

enum ecore_led_mode {
	ECORE_LED_MODE_OFF,
	ECORE_LED_MODE_ON,
	ECORE_LED_MODE_RESTORE
};
#endif

struct ecore_temperature_sensor {
	u8 sensor_location;
	u8 threshold_high;
	u8 critical;
	u8 current_temp;
};

#define ECORE_MAX_NUM_OF_SENSORS	7
struct ecore_temperature_info {
	u32 num_sensors;
	struct ecore_temperature_sensor sensors[ECORE_MAX_NUM_OF_SENSORS];
};

enum ecore_mba_img_idx {
	ECORE_MBA_LEGACY_IDX,
	ECORE_MBA_PCI3CLP_IDX,
	ECORE_MBA_PCI3_IDX,
	ECORE_MBA_FCODE_IDX,
	ECORE_EFI_X86_IDX,
	ECORE_EFI_IPF_IDX,
	ECORE_EFI_EBC_IDX,
	ECORE_EFI_X64_IDX,
	ECORE_MAX_NUM_OF_ROMIMG
};

struct ecore_mba_vers {
	u32 mba_vers[ECORE_MAX_NUM_OF_ROMIMG];
};

enum ecore_mfw_tlv_type {
	ECORE_MFW_TLV_GENERIC = 0x1, /* Core driver TLVs */
	ECORE_MFW_TLV_ETH = 0x2, /* L2 driver TLVs */
	ECORE_MFW_TLV_FCOE = 0x4, /* FCoE protocol TLVs */
	ECORE_MFW_TLV_ISCSI = 0x8, /* SCSI protocol TLVs */
	ECORE_MFW_TLV_MAX = 0x16,
};

struct ecore_mfw_tlv_generic {
	struct {
		u8 ipv4_csum_offload;
		u8 lso_supported;
		bool b_set;
	} flags;

#define ECORE_MFW_TLV_MAC_COUNT 3
	/* First entry for primary MAC, 2 secondary MACs possible */
	u8 mac[ECORE_MFW_TLV_MAC_COUNT][6];
	bool mac_set[ECORE_MFW_TLV_MAC_COUNT];

	u64 rx_frames;
	bool rx_frames_set;
	u64 rx_bytes;
	bool rx_bytes_set;
	u64 tx_frames;
	bool tx_frames_set;
	u64 tx_bytes;
	bool tx_bytes_set;
};

#ifndef __EXTRACT__LINUX__
struct ecore_mfw_tlv_eth {
	u16 lso_maxoff_size;
	bool lso_maxoff_size_set;
	u16 lso_minseg_size;
	bool lso_minseg_size_set;
	u8 prom_mode;
	bool prom_mode_set;
	u16 tx_descr_size;
	bool tx_descr_size_set;
	u16 rx_descr_size;
	bool rx_descr_size_set;
	u16 netq_count;
	bool netq_count_set;
	u32 tcp4_offloads;
	bool tcp4_offloads_set;
	u32 tcp6_offloads;
	bool tcp6_offloads_set;
	u16 tx_descr_qdepth;
	bool tx_descr_qdepth_set;
	u16 rx_descr_qdepth;
	bool rx_descr_qdepth_set;
	u8 iov_offload;
#define ECORE_MFW_TLV_IOV_OFFLOAD_NONE		(0)
#define ECORE_MFW_TLV_IOV_OFFLOAD_MULTIQUEUE	(1)
#define ECORE_MFW_TLV_IOV_OFFLOAD_VEB		(2)
#define ECORE_MFW_TLV_IOV_OFFLOAD_VEPA		(3)
	bool iov_offload_set;
	u8 txqs_empty;
	bool txqs_empty_set;
	u8 rxqs_empty;
	bool rxqs_empty_set;
	u8 num_txqs_full;
	bool num_txqs_full_set;
	u8 num_rxqs_full;
	bool num_rxqs_full_set;
};

struct ecore_mfw_tlv_time {
	bool b_set;
	u8 month;
	u8 day;
	u8 hour;
	u8 min;
	u16 msec;
	u16 usec;
};

struct ecore_mfw_tlv_fcoe {
	u8 scsi_timeout;
	bool scsi_timeout_set;
	u32 rt_tov;
	bool rt_tov_set;
	u32 ra_tov;
	bool ra_tov_set;
	u32 ed_tov;
	bool ed_tov_set;
	u32 cr_tov;
	bool cr_tov_set;
	u8 boot_type;
	bool boot_type_set;
	u8 npiv_state;
	bool npiv_state_set;
	u32 num_npiv_ids;
	bool num_npiv_ids_set;
	u8 switch_name[8];
	bool switch_name_set;
	u16 switch_portnum;
	bool switch_portnum_set;
	u8 switch_portid[3];
	bool switch_portid_set;
	u8 vendor_name[8];
	bool vendor_name_set;
	u8 switch_model[8];
	bool switch_model_set;
	u8 switch_fw_version[8];
	bool switch_fw_version_set;
	u8 qos_pri;
	bool qos_pri_set;
	u8 port_alias[3];
	bool port_alias_set;
	u8 port_state;
#define ECORE_MFW_TLV_PORT_STATE_OFFLINE	(0)
#define ECORE_MFW_TLV_PORT_STATE_LOOP		(1)
#define ECORE_MFW_TLV_PORT_STATE_P2P		(2)
#define ECORE_MFW_TLV_PORT_STATE_FABRIC		(3)
	bool port_state_set;
	u16 fip_tx_descr_size;
	bool fip_tx_descr_size_set;
	u16 fip_rx_descr_size;
	bool fip_rx_descr_size_set;
	u16 link_failures;
	bool link_failures_set;
	u8 fcoe_boot_progress;
	bool fcoe_boot_progress_set;
	u64 rx_bcast;
	bool rx_bcast_set;
	u64 tx_bcast;
	bool tx_bcast_set;
	u16 fcoe_txq_depth;
	bool fcoe_txq_depth_set;
	u16 fcoe_rxq_depth;
	bool fcoe_rxq_depth_set;
	u64 fcoe_rx_frames;
	bool fcoe_rx_frames_set;
	u64 fcoe_rx_bytes;
	bool fcoe_rx_bytes_set;
	u64 fcoe_tx_frames;
	bool fcoe_tx_frames_set;
	u64 fcoe_tx_bytes;
	bool fcoe_tx_bytes_set;
	u16 crc_count;
	bool crc_count_set;
	u32 crc_err_src_fcid[5];
	bool crc_err_src_fcid_set[5];
	struct ecore_mfw_tlv_time crc_err[5];
	u16 losync_err;
	bool losync_err_set;
	u16 losig_err;
	bool losig_err_set;
	u16 primtive_err;
	bool primtive_err_set;
	u16 disparity_err;
	bool disparity_err_set;
	u16 code_violation_err;
	bool code_violation_err_set;
	u32 flogi_param[4];
	bool flogi_param_set[4];
	struct ecore_mfw_tlv_time flogi_tstamp;
	u32 flogi_acc_param[4];
	bool flogi_acc_param_set[4];
	struct ecore_mfw_tlv_time flogi_acc_tstamp;
	u32 flogi_rjt;
	bool flogi_rjt_set;
	struct ecore_mfw_tlv_time flogi_rjt_tstamp;
	u32 fdiscs;
	bool fdiscs_set;
	u8 fdisc_acc;
	bool fdisc_acc_set;
	u8 fdisc_rjt;
	bool fdisc_rjt_set;
	u8 plogi;
	bool plogi_set;
	u8 plogi_acc;
	bool plogi_acc_set;
	u8 plogi_rjt;
	bool plogi_rjt_set;
	u32 plogi_dst_fcid[5];
	bool plogi_dst_fcid_set[5];
	struct ecore_mfw_tlv_time plogi_tstamp[5];
	u32 plogi_acc_src_fcid[5];
	bool plogi_acc_src_fcid_set[5];
	struct ecore_mfw_tlv_time plogi_acc_tstamp[5];
	u8 tx_plogos;
	bool tx_plogos_set;
	u8 plogo_acc;
	bool plogo_acc_set;
	u8 plogo_rjt;
	bool plogo_rjt_set;
	u32 plogo_src_fcid[5];
	bool plogo_src_fcid_set[5];
	struct ecore_mfw_tlv_time plogo_tstamp[5];
	u8 rx_logos;
	bool rx_logos_set;
	u8 tx_accs;
	bool tx_accs_set;
	u8 tx_prlis;
	bool tx_prlis_set;
	u8 rx_accs;
	bool rx_accs_set;
	u8 tx_abts;
	bool tx_abts_set;
	u8 rx_abts_acc;
	bool rx_abts_acc_set;
	u8 rx_abts_rjt;
	bool rx_abts_rjt_set;
	u32 abts_dst_fcid[5];
	bool abts_dst_fcid_set[5];
	struct ecore_mfw_tlv_time abts_tstamp[5];
	u8 rx_rscn;
	bool rx_rscn_set;
	u32 rx_rscn_nport[4];
	bool rx_rscn_nport_set[4];
	u8 tx_lun_rst;
	bool tx_lun_rst_set;
	u8 abort_task_sets;
	bool abort_task_sets_set;
	u8 tx_tprlos;
	bool tx_tprlos_set;
	u8 tx_nos;
	bool tx_nos_set;
	u8 rx_nos;
	bool rx_nos_set;
	u8 ols;
	bool ols_set;
	u8 lr;
	bool lr_set;
	u8 lrr;
	bool lrr_set;
	u8 tx_lip;
	bool tx_lip_set;
	u8 rx_lip;
	bool rx_lip_set;
	u8 eofa;
	bool eofa_set;
	u8 eofni;
	bool eofni_set;
	u8 scsi_chks;
	bool scsi_chks_set;
	u8 scsi_cond_met;
	bool scsi_cond_met_set;
	u8 scsi_busy;
	bool scsi_busy_set;
	u8 scsi_inter;
	bool scsi_inter_set;
	u8 scsi_inter_cond_met;
	bool scsi_inter_cond_met_set;
	u8 scsi_rsv_conflicts;
	bool scsi_rsv_conflicts_set;
	u8 scsi_tsk_full;
	bool scsi_tsk_full_set;
	u8 scsi_aca_active;
	bool scsi_aca_active_set;
	u8 scsi_tsk_abort;
	bool scsi_tsk_abort_set;
	u32 scsi_rx_chk[5];
	bool scsi_rx_chk_set[5];
	struct ecore_mfw_tlv_time scsi_chk_tstamp[5];
};

struct ecore_mfw_tlv_iscsi {
	u8 target_llmnr;
	bool target_llmnr_set;
	u8 header_digest;
	bool header_digest_set;
	u8 data_digest;
	bool data_digest_set;
	u8 auth_method;
#define ECORE_MFW_TLV_AUTH_METHOD_NONE		(1)
#define ECORE_MFW_TLV_AUTH_METHOD_CHAP		(2)
#define ECORE_MFW_TLV_AUTH_METHOD_MUTUAL_CHAP	(3)
	bool auth_method_set;
	u16 boot_taget_portal;
	bool boot_taget_portal_set;
	u16 frame_size;
	bool frame_size_set;
	u16 tx_desc_size;
	bool tx_desc_size_set;
	u16 rx_desc_size;
	bool rx_desc_size_set;
	u8 boot_progress;
	bool boot_progress_set;
	u16 tx_desc_qdepth;
	bool tx_desc_qdepth_set;
	u16 rx_desc_qdepth;
	bool rx_desc_qdepth_set;
	u64 rx_frames;
	bool rx_frames_set;
	u64 rx_bytes;
	bool rx_bytes_set;
	u64 tx_frames;
	bool tx_frames_set;
	u64 tx_bytes;
	bool tx_bytes_set;
};
#endif

union ecore_mfw_tlv_data {
	struct ecore_mfw_tlv_generic generic;
	struct ecore_mfw_tlv_eth eth;
	struct ecore_mfw_tlv_fcoe fcoe;
	struct ecore_mfw_tlv_iscsi iscsi;
};

#ifndef __EXTRACT__LINUX__
enum ecore_hw_info_change {
	ECORE_HW_INFO_CHANGE_OVLAN,
};
#endif

/**
 * @brief - returns the link params of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link params
 */
struct ecore_mcp_link_params *ecore_mcp_get_link_params(struct ecore_hwfn
							*p_hwfn);

/**
 * @brief - return the link state of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link state
 */
struct ecore_mcp_link_state *ecore_mcp_get_link_state(struct ecore_hwfn
						      *p_hwfn);

/**
 * @brief - return the link capabilities of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link capabilities
 */
struct ecore_mcp_link_capabilities
*ecore_mcp_get_link_capabilities(struct ecore_hwfn *p_hwfn);

/**
 * @brief Request the MFW to set the the link according to 'link_input'.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param b_up - raise link if `true'. Reset link if `false'.
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_mcp_set_link(struct ecore_hwfn *p_hwfn,
					struct ecore_ptt *p_ptt,
					bool b_up);

/**
 * @brief Get the management firmware version value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mfw_ver    - mfw version value
 * @param p_running_bundle_id	- image id in nvram; Optional.
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_get_mfw_ver(struct ecore_hwfn *p_hwfn,
					   struct ecore_ptt *p_ptt,
					   u32 *p_mfw_ver,
					   u32 *p_running_bundle_id);

/**
 * @brief Get the MBI version value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mbi_ver - A pointer to a variable to be filled with the MBI version.
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_get_mbi_ver(struct ecore_hwfn *p_hwfn,
					   struct ecore_ptt *p_ptt,
					   u32 *p_mbi_ver);

/**
 * @brief Get media type value of the port.
 *
 * @param p_dev      - ecore dev pointer
 * @param p_ptt
 * @param mfw_ver    - media type value
 *
 * @return enum _ecore_status_t -
 *      ECORE_SUCCESS - Operation was successful.
 *      ECORE_BUSY - Operation failed
 */
enum _ecore_status_t ecore_mcp_get_media_type(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      u32 *media_type);

/**
 * @brief Get transciever data of the port.
 *
 * @param p_dev      - ecore dev pointer
 * @param p_ptt
 * @param p_transciever_type - media type value
 *
 * @return enum _ecore_status_t -
 *      ECORE_SUCCESS - Operation was successful.
 *      ECORE_BUSY - Operation failed
 */
enum _ecore_status_t ecore_mcp_get_transceiver_data(struct ecore_hwfn *p_hwfn,
						    struct ecore_ptt *p_ptt,
						    u32 *p_tranceiver_type);

/**
 * @brief Get transciever supported speed mask.
 *
 * @param p_dev      - ecore dev pointer
 * @param p_ptt
 * @param p_speed_mask - Bit mask of all supported speeds.
 *
 * @return enum _ecore_status_t -
 *      ECORE_SUCCESS - Operation was successful.
 *      ECORE_BUSY - Operation failed
 */

enum _ecore_status_t ecore_mcp_trans_speed_mask(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u32 *p_speed_mask);

/**
 * @brief Get board configuration.
 *
 * @param p_dev      - ecore dev pointer
 * @param p_ptt
 * @param p_board_config - Board config.
 *
 * @return enum _ecore_status_t -
 *      ECORE_SUCCESS - Operation was successful.
 *      ECORE_BUSY - Operation failed
 */
enum _ecore_status_t ecore_mcp_get_board_config(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u32 *p_board_config);

/**
 * @brief - Sends a command to the MCP mailbox.
 *
 * @param p_hwfn      - hw function
 * @param p_ptt       - PTT required for register access
 * @param cmd         - command to be sent to the MCP
 * @param param       - Optional param
 * @param o_mcp_resp  - The MCP response code (exclude sequence)
 * @param o_mcp_param - Optional parameter provided by the MCP response
 *
 * @return enum _ecore_status_t -
 *      ECORE_SUCCESS - operation was successful
 *      ECORE_BUSY    - operation failed
 */
enum _ecore_status_t ecore_mcp_cmd(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt, u32 cmd, u32 param,
				   u32 *o_mcp_resp, u32 *o_mcp_param);

/**
 * @brief - drains the nig, allowing completion to pass in case of pauses.
 *          (Should be called only from sleepable context)
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_mcp_drain(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt);

#ifndef LINUX_REMOVE
/**
 * @brief - return the mcp function info of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to mcp function info
 */
const struct ecore_mcp_function_info
*ecore_mcp_get_function_info(struct ecore_hwfn *p_hwfn);
#endif

#ifndef LINUX_REMOVE
/**
 * @brief - count number of function with a matching personality on engine.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param personalities - a bitmask of ecore_pci_personality values
 *
 * @returns the count of all devices on engine whose personality match one of
 *          the bitsmasks.
 */
int ecore_mcp_get_personality_cnt(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt,
				  u32 personalities);
#endif

/**
 * @brief Get the flash size value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_flash_size  - flash size in bytes to be filled.
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_get_flash_size(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      u32 *p_flash_size);

/**
 * @brief Send driver version to MFW
 *
 * @param p_hwfn
 * @param p_ptt
 * @param version - Version value
 * @param name - Protocol driver name
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_send_drv_version(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   struct ecore_mcp_drv_version *p_ver);

/**
 * @brief Read the MFW process kill counter
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return u32
 */
u32 ecore_get_process_kill_counter(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt);

/**
 * @brief Trigger a recovery process
 *
 *  @param p_hwfn
 *  @param p_ptt
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_start_recovery_process(struct ecore_hwfn *p_hwfn,
						  struct ecore_ptt *p_ptt);

/**
 * @brief A recovery handler must call this function as its first step.
 *        It is assumed that the handler is not run from an interrupt context.
 *
 *  @param p_dev
 *  @param p_ptt
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_recovery_prolog(struct ecore_dev *p_dev);

/**
 * @brief Notify MFW about the change in base device properties
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param client - ecore client type
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_update_current_config(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt,
				   enum ecore_ov_client client);

/**
 * @brief Notify MFW about the driver state
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param drv_state - Driver state
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_update_driver_state(struct ecore_hwfn *p_hwfn,
				 struct ecore_ptt *p_ptt,
				 enum ecore_ov_driver_state drv_state);

/**
 * @brief Read NPIV settings form the MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param p_table - Array to hold the FC NPIV data. Client need allocate the
 *                   required buffer. The field 'count' specifies number of NPIV
 *                   entries. A value of 0 means the table was not populated.
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_get_fc_npiv(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			 struct ecore_fc_npiv_tbl *p_table);

/**
 * @brief Send MTU size to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mtu - MTU size
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_ov_update_mtu(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt, u16 mtu);

/**
 * @brief Send MAC address to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mac - MAC address
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_update_mac(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			u8 *mac);

/**
 * @brief Send WOL mode to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param wol - WOL mode
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_update_wol(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			enum ecore_ov_wol wol);

/**
 * @brief Set LED status
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mode - LED mode
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_set_led(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       enum ecore_led_mode mode);

/**
 * @brief Set secure mode
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_set_secure_mode(struct ecore_dev *p_dev,
						   u32 addr);

/**
 * @brief Write to phy
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *  @param cmd - nvm command
 *  @param p_buf - nvm write buffer
 *  @param len - buffer len
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_phy_write(struct ecore_dev *p_dev, u32 cmd,
					 u32 addr, u8 *p_buf, u32 len);

/**
 * @brief Write to nvm
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *  @param cmd - nvm command
 *  @param p_buf - nvm write buffer
 *  @param len - buffer len
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_write(struct ecore_dev *p_dev, u32 cmd,
					 u32 addr, u8 *p_buf, u32 len);

/**
 * @brief Put file begin
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_put_file_begin(struct ecore_dev *p_dev,
						  u32 addr);

/**
 * @brief Delete file
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_del_file(struct ecore_dev *p_dev,
					    u32 addr);

/**
 * @brief Check latest response
 *
 *  @param p_dev
 *  @param p_buf - nvm write buffer
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_resp(struct ecore_dev *p_dev, u8 *p_buf);

/**
 * @brief Read from phy
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *  @param cmd - nvm command
 *  @param p_buf - nvm read buffer
 *  @param len - buffer len
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_phy_read(struct ecore_dev *p_dev, u32 cmd,
					u32 addr, u8 *p_buf, u32 len);

/**
 * @brief Read from nvm
 *
 *  @param p_dev
 *  @param addr - nvm offset
 *  @param p_buf - nvm read buffer
 *  @param len - buffer len
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_nvm_read(struct ecore_dev *p_dev, u32 addr,
			   u8 *p_buf, u32 len);

struct ecore_nvm_image_att {
	u32 start_addr;
	u32 length;
};

/**
 * @brief Allows reading a whole nvram image
 *
 * @param p_hwfn
 * @param p_ptt
 * @param image_id - image to get attributes for
 * @param p_image_att - image attributes structure into which to fill data
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_get_nvm_image_att(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			    enum ecore_nvm_images image_id,
			    struct ecore_nvm_image_att *p_image_att);

/**
 * @brief Allows reading a whole nvram image
 *
 * @param p_hwfn
 * @param p_ptt
 * @param image_id - image requested for reading
 * @param p_buffer - allocated buffer into which to fill data
 * @param buffer_len - length of the allocated buffer.
 *
 * @return ECORE_SUCCESS iff p_buffer now contains the nvram image.
 */
enum _ecore_status_t ecore_mcp_get_nvm_image(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     enum ecore_nvm_images image_id,
					     u8 *p_buffer, u32 buffer_len);

/**
 * @brief - Sends an NVM write command request to the MFW with
 *          payload.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param cmd - Command: Either DRV_MSG_CODE_NVM_WRITE_NVRAM or
 *            DRV_MSG_CODE_NVM_PUT_FILE_DATA
 * @param param - [0:23] - Offset [24:31] - Size
 * @param o_mcp_resp - MCP response
 * @param o_mcp_param - MCP response param
 * @param i_txn_size -  Buffer size
 * @param i_buf - Pointer to the buffer
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_nvm_wr_cmd(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u32 cmd,
					  u32 param,
					  u32 *o_mcp_resp,
					  u32 *o_mcp_param,
					  u32 i_txn_size,
					  u32 *i_buf);

/**
 * @brief - Sends an NVM read command request to the MFW to get
 *        a buffer.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param cmd - Command: DRV_MSG_CODE_NVM_GET_FILE_DATA or
 *            DRV_MSG_CODE_NVM_READ_NVRAM commands
 * @param param - [0:23] - Offset [24:31] - Size
 * @param o_mcp_resp - MCP response
 * @param o_mcp_param - MCP response param
 * @param o_txn_size -  Buffer size output
 * @param o_buf - Pointer to the buffer returned by the MFW.
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_nvm_rd_cmd(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u32 cmd,
					  u32 param,
					  u32 *o_mcp_resp,
					  u32 *o_mcp_param,
					  u32 *o_txn_size,
					  u32 *o_buf);

/**
 * @brief Read from sfp
 *
 *  @param p_hwfn - hw function
 *  @param p_ptt  - PTT required for register access
 *  @param port   - transceiver port
 *  @param addr   - I2C address
 *  @param offset - offset in sfp
 *  @param len    - buffer length
 *  @param p_buf  - buffer to read into
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_phy_sfp_read(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u32 port, u32 addr, u32 offset,
					    u32 len, u8 *p_buf);

/**
 * @brief Write to sfp
 *
 *  @param p_hwfn - hw function
 *  @param p_ptt  - PTT required for register access
 *  @param port   - transceiver port
 *  @param addr   - I2C address
 *  @param offset - offset in sfp
 *  @param len    - buffer length
 *  @param p_buf  - buffer to write from
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_phy_sfp_write(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     u32 port, u32 addr, u32 offset,
					     u32 len, u8 *p_buf);

/**
 * @brief Gpio read
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *  @param gpio      - gpio number
 *  @param gpio_val  - value read from gpio
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_gpio_read(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u16 gpio, u32 *gpio_val);

/**
 * @brief Gpio write
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *  @param gpio      - gpio number
 *  @param gpio_val  - value to write to gpio
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_gpio_write(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u16 gpio, u16 gpio_val);

/**
 * @brief Gpio get information
 *
 *  @param p_hwfn          - hw function
 *  @param p_ptt           - PTT required for register access
 *  @param gpio            - gpio number
 *  @param gpio_direction  - gpio is output (0) or input (1)
 *  @param gpio_ctrl       - gpio control is uninitialized (0),
 *                         path 0 (1), path 1 (2) or shared(3)
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_gpio_info(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u16 gpio, u32 *gpio_direction,
					 u32 *gpio_ctrl);

/**
 * @brief Bist register test
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_bist_register_test(struct ecore_hwfn *p_hwfn,
						   struct ecore_ptt *p_ptt);

/**
 * @brief Bist clock test
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_bist_clock_test(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief Bist nvm test - get number of images
 *
 *  @param p_hwfn       - hw function
 *  @param p_ptt        - PTT required for register access
 *  @param num_images   - number of images if operation was
 *			  successful. 0 if not.
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_bist_nvm_test_get_num_images(struct ecore_hwfn *p_hwfn,
							    struct ecore_ptt *p_ptt,
							    u32 *num_images);

/**
 * @brief Bist nvm test - get image attributes by index
 *
 *  @param p_hwfn      - hw function
 *  @param p_ptt       - PTT required for register access
 *  @param p_image_att - Attributes of image
 *  @param image_index - Index of image to get information for
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_bist_nvm_test_get_image_att(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   struct bist_nvm_image_att *p_image_att,
							   u32 image_index);

/**
 * @brief ecore_mcp_get_temperature_info - get the status of the temperature
 *                                         sensors
 *
 *  @param p_hwfn        - hw function
 *  @param p_ptt         - PTT required for register access
 *  @param p_temp_status - A pointer to an ecore_temperature_info structure to
 *                         be filled with the temperature data
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_get_temperature_info(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,
			       struct ecore_temperature_info *p_temp_info);

/**
 * @brief Get MBA versions - get MBA sub images versions
 *
 *  @param p_hwfn      - hw function
 *  @param p_ptt       - PTT required for register access
 *  @param p_mba_vers  - MBA versions array to fill
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_get_mba_versions(
	struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt,
	struct ecore_mba_vers *p_mba_vers);

/**
 * @brief Count memory ecc events
 *
 *  @param p_hwfn      - hw function
 *  @param p_ptt       - PTT required for register access
 *  @param num_events  - number of memory ecc events
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_mem_ecc_events(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      u64 *num_events);

struct ecore_mdump_info {
	u32 reason;
	u32 version;
	u32 config;
	u32 epoch;
	u32 num_of_logs;
	u32 valid_logs;
};

/**
 * @brief - Gets the MFW crash dump configuration and logs info.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mdump_info
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t
ecore_mcp_mdump_get_info(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			 struct ecore_mdump_info *p_mdump_info);

/**
 * @brief - Clears the MFW crash dump logs.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_mdump_clear_logs(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief - Clear the mdump retained data.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_mdump_clr_retain(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief - Gets the LLDP MAC address.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param lldp_mac_addr - a buffer to be filled with the read LLDP MAC address.
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_get_lldp_mac(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u8 lldp_mac_addr[ETH_ALEN]);

/**
 * @brief - Sets the LLDP MAC address.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param lldp_mac_addr - a buffer with the LLDP MAC address to be written.
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_set_lldp_mac(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u8 lldp_mac_addr[ETH_ALEN]);

/**
 * @brief - Processes the TLV request from MFW i.e., get the required TLV info
 *          from the ecore client and send it to the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mfw_process_tlv_req(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt);

/**
 * @brief - Update fcoe vlan id value to the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vlan - fcoe vlan
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t
ecore_mcp_update_fcoe_cvid(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   u16 vlan);

/**
 * @brief - Update fabric name (wwn) value to the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param wwn - world wide name
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t
ecore_mcp_update_fcoe_fabric_name(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt, u8 *wwn);

/**
 * @brief - Return whether management firmware support smart AN
 *
 * @param p_hwfn
 *
 * @return bool - true if feature is supported.
 */
bool ecore_mcp_is_smart_an_supported(struct ecore_hwfn *p_hwfn);

/**
 * @brief - Return whether management firmware support setting of
 *          PCI relaxed ordering.
 *
 * @param p_hwfn
 *
 * @return bool - true if feature is supported.
 */
bool ecore_mcp_rlx_odr_supported(struct ecore_hwfn *p_hwfn);
#endif

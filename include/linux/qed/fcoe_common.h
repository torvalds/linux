/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef __FCOE_COMMON__
#define __FCOE_COMMON__
/*********************/
/* FCOE FW CONSTANTS */
/*********************/

#define FC_ABTS_REPLY_MAX_PAYLOAD_LEN	12
#define FCOE_MAX_SIZE_FCP_DATA_SUPER	(8600)

struct fcoe_abts_pkt {
	__le32 abts_rsp_fc_payload_lo;
	__le16 abts_rsp_rx_id;
	u8 abts_rsp_rctl;
	u8 reserved2;
};

/* FCoE additional WQE (Sq/XferQ) information */
union fcoe_additional_info_union {
	__le32 previous_tid;
	__le32 parent_tid;
	__le32 burst_length;
	__le32 seq_rec_updated_offset;
};

struct fcoe_exp_ro {
	__le32 data_offset;
	__le32 reserved;
};

union fcoe_cleanup_addr_exp_ro_union {
	struct regpair abts_rsp_fc_payload_hi;
	struct fcoe_exp_ro exp_ro;
};

/* FCoE Ramrod Command IDs */
enum fcoe_completion_status {
	FCOE_COMPLETION_STATUS_SUCCESS,
	FCOE_COMPLETION_STATUS_FCOE_VER_ERR,
	FCOE_COMPLETION_STATUS_SRC_MAC_ADD_ARR_ERR,
	MAX_FCOE_COMPLETION_STATUS
};

struct fc_addr_nw {
	u8 addr_lo;
	u8 addr_mid;
	u8 addr_hi;
};

/* FCoE connection offload */
struct fcoe_conn_offload_ramrod_data {
	struct regpair sq_pbl_addr;
	struct regpair sq_curr_page_addr;
	struct regpair sq_next_page_addr;
	struct regpair xferq_pbl_addr;
	struct regpair xferq_curr_page_addr;
	struct regpair xferq_next_page_addr;
	struct regpair respq_pbl_addr;
	struct regpair respq_curr_page_addr;
	struct regpair respq_next_page_addr;
	__le16 dst_mac_addr_lo;
	__le16 dst_mac_addr_mid;
	__le16 dst_mac_addr_hi;
	__le16 src_mac_addr_lo;
	__le16 src_mac_addr_mid;
	__le16 src_mac_addr_hi;
	__le16 tx_max_fc_pay_len;
	__le16 e_d_tov_timer_val;
	__le16 rx_max_fc_pay_len;
	__le16 vlan_tag;
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_VLAN_ID_MASK              0xFFF
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_VLAN_ID_SHIFT             0
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_CFI_MASK                  0x1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_CFI_SHIFT                 12
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_PRIORITY_MASK             0x7
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_PRIORITY_SHIFT            13
	__le16 physical_q0;
	__le16 rec_rr_tov_timer_val;
	struct fc_addr_nw s_id;
	u8 max_conc_seqs_c3;
	struct fc_addr_nw d_id;
	u8 flags;
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_CONT_INCR_SEQ_CNT_MASK  0x1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_CONT_INCR_SEQ_CNT_SHIFT 0
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_CONF_REQ_MASK           0x1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_CONF_REQ_SHIFT          1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_REC_VALID_MASK          0x1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_REC_VALID_SHIFT         2
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_VLAN_FLAG_MASK          0x1
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_B_VLAN_FLAG_SHIFT         3
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_MODE_MASK                 0x3
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_MODE_SHIFT                4
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_RESERVED0_MASK            0x3
#define FCOE_CONN_OFFLOAD_RAMROD_DATA_RESERVED0_SHIFT           6
	__le16 conn_id;
	u8 def_q_idx;
	u8 reserved[5];
};

/* FCoE terminate connection request */
struct fcoe_conn_terminate_ramrod_data {
	struct regpair terminate_params_addr;
};

struct fcoe_fast_sgl_ctx {
	struct regpair sgl_start_addr;
	__le32 sgl_byte_offset;
	__le16 task_reuse_cnt;
	__le16 init_offset_in_first_sge;
};

struct fcoe_slow_sgl_ctx {
	struct regpair base_sgl_addr;
	__le16 curr_sge_off;
	__le16 remainder_num_sges;
	__le16 curr_sgl_index;
	__le16 reserved;
};

struct fcoe_sge {
	struct regpair sge_addr;
	__le16 size;
	__le16 reserved0;
	u8 reserved1[3];
	u8 is_valid_sge;
};

union fcoe_data_desc_ctx {
	struct fcoe_fast_sgl_ctx fast;
	struct fcoe_slow_sgl_ctx slow;
	struct fcoe_sge single_sge;
};

union fcoe_dix_desc_ctx {
	struct fcoe_slow_sgl_ctx dix_sgl;
	struct fcoe_sge cached_dix_sge;
};

struct fcoe_fcp_cmd_payload {
	__le32 opaque[8];
};

struct fcoe_fcp_rsp_payload {
	__le32 opaque[6];
};

struct fcoe_fcp_xfer_payload {
	__le32 opaque[3];
};

/* FCoE firmware function init */
struct fcoe_init_func_ramrod_data {
	struct scsi_init_func_params func_params;
	struct scsi_init_func_queues q_params;
	__le16 mtu;
	__le16 sq_num_pages_in_pbl;
	__le32 reserved;
};

/* FCoE: Mode of the connection: Target or Initiator or both */
enum fcoe_mode_type {
	FCOE_INITIATOR_MODE = 0x0,
	FCOE_TARGET_MODE = 0x1,
	FCOE_BOTH_OR_NOT_CHOSEN = 0x3,
	MAX_FCOE_MODE_TYPE
};

struct fcoe_mstorm_fcoe_task_st_ctx_fp {
	__le16 flags;
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_FP_RSRV0_MASK                 0x7FFF
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_FP_RSRV0_SHIFT                0
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_FP_MP_INCLUDE_FC_HEADER_MASK  0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_FP_MP_INCLUDE_FC_HEADER_SHIFT 15
	__le16 difDataResidue;
	__le16 parent_id;
	__le16 single_sge_saved_offset;
	__le32 data_2_trns_rem;
	__le32 offset_in_io;
	union fcoe_dix_desc_ctx dix_desc;
	union fcoe_data_desc_ctx data_desc;
};

struct fcoe_mstorm_fcoe_task_st_ctx_non_fp {
	__le16 flags;
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_HOST_INTERFACE_MASK            0x3
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_HOST_INTERFACE_SHIFT           0
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIF_TO_PEER_MASK               0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIF_TO_PEER_SHIFT              2
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_VALIDATE_DIX_APP_TAG_MASK      0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_VALIDATE_DIX_APP_TAG_SHIFT     3
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_INTERVAL_SIZE_LOG_MASK         0xF
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_INTERVAL_SIZE_LOG_SHIFT        4
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIX_BLOCK_SIZE_MASK            0x3
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIX_BLOCK_SIZE_SHIFT           8
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RESERVED_MASK                  0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RESERVED_SHIFT                 10
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_HAS_FIRST_PACKET_ARRIVED_MASK  0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_HAS_FIRST_PACKET_ARRIVED_SHIFT 11
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_VALIDATE_DIX_REF_TAG_MASK      0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_VALIDATE_DIX_REF_TAG_SHIFT     12
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIX_CACHED_SGE_FLG_MASK        0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIX_CACHED_SGE_FLG_SHIFT       13
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_OFFSET_IN_IO_VALID_MASK        0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_OFFSET_IN_IO_VALID_SHIFT       14
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIF_SUPPORTED_MASK             0x1
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_DIF_SUPPORTED_SHIFT            15
	u8 tx_rx_sgl_mode;
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_TX_SGL_MODE_MASK               0x7
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_TX_SGL_MODE_SHIFT              0
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RX_SGL_MODE_MASK               0x7
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RX_SGL_MODE_SHIFT              3
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RSRV1_MASK                     0x3
#define FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RSRV1_SHIFT                    6
	u8 rsrv2;
	__le32 num_prm_zero_read;
	struct regpair rsp_buf_addr;
};

struct fcoe_rx_stat {
	struct regpair fcoe_rx_byte_cnt;
	struct regpair fcoe_rx_data_pkt_cnt;
	struct regpair fcoe_rx_xfer_pkt_cnt;
	struct regpair fcoe_rx_other_pkt_cnt;
	__le32 fcoe_silent_drop_pkt_cmdq_full_cnt;
	__le32 fcoe_silent_drop_pkt_rq_full_cnt;
	__le32 fcoe_silent_drop_pkt_crc_error_cnt;
	__le32 fcoe_silent_drop_pkt_task_invalid_cnt;
	__le32 fcoe_silent_drop_total_pkt_cnt;
	__le32 rsrv;
};

enum fcoe_sgl_mode {
	FCOE_SLOW_SGL,
	FCOE_SINGLE_FAST_SGE,
	FCOE_2_FAST_SGE,
	FCOE_3_FAST_SGE,
	FCOE_4_FAST_SGE,
	FCOE_MUL_FAST_SGES,
	MAX_FCOE_SGL_MODE
};

struct fcoe_stat_ramrod_data {
	struct regpair stat_params_addr;
};

struct protection_info_ctx {
	__le16 flags;
#define PROTECTION_INFO_CTX_HOST_INTERFACE_MASK        0x3
#define PROTECTION_INFO_CTX_HOST_INTERFACE_SHIFT       0
#define PROTECTION_INFO_CTX_DIF_TO_PEER_MASK           0x1
#define PROTECTION_INFO_CTX_DIF_TO_PEER_SHIFT          2
#define PROTECTION_INFO_CTX_VALIDATE_DIX_APP_TAG_MASK  0x1
#define PROTECTION_INFO_CTX_VALIDATE_DIX_APP_TAG_SHIFT 3
#define PROTECTION_INFO_CTX_INTERVAL_SIZE_LOG_MASK     0xF
#define PROTECTION_INFO_CTX_INTERVAL_SIZE_LOG_SHIFT    4
#define PROTECTION_INFO_CTX_VALIDATE_DIX_REF_TAG_MASK  0x1
#define PROTECTION_INFO_CTX_VALIDATE_DIX_REF_TAG_SHIFT 8
#define PROTECTION_INFO_CTX_RESERVED0_MASK             0x7F
#define PROTECTION_INFO_CTX_RESERVED0_SHIFT            9
	u8 dix_block_size;
	u8 dst_size;
};

union protection_info_union_ctx {
	struct protection_info_ctx info;
	__le32 value;
};

struct fcp_rsp_payload_padded {
	struct fcoe_fcp_rsp_payload rsp_payload;
	__le32 reserved[2];
};

struct fcp_xfer_payload_padded {
	struct fcoe_fcp_xfer_payload xfer_payload;
	__le32 reserved[5];
};

struct fcoe_tx_data_params {
	__le32 data_offset;
	__le32 offset_in_io;
	u8 flags;
#define FCOE_TX_DATA_PARAMS_OFFSET_IN_IO_VALID_MASK  0x1
#define FCOE_TX_DATA_PARAMS_OFFSET_IN_IO_VALID_SHIFT 0
#define FCOE_TX_DATA_PARAMS_DROP_DATA_MASK           0x1
#define FCOE_TX_DATA_PARAMS_DROP_DATA_SHIFT          1
#define FCOE_TX_DATA_PARAMS_AFTER_SEQ_REC_MASK       0x1
#define FCOE_TX_DATA_PARAMS_AFTER_SEQ_REC_SHIFT      2
#define FCOE_TX_DATA_PARAMS_RESERVED0_MASK           0x1F
#define FCOE_TX_DATA_PARAMS_RESERVED0_SHIFT          3
	u8 dif_residual;
	__le16 seq_cnt;
	__le16 single_sge_saved_offset;
	__le16 next_dif_offset;
	__le16 seq_id;
	__le16 reserved3;
};

struct fcoe_tx_mid_path_params {
	__le32 parameter;
	u8 r_ctl;
	u8 type;
	u8 cs_ctl;
	u8 df_ctl;
	__le16 rx_id;
	__le16 ox_id;
};

struct fcoe_tx_params {
	struct fcoe_tx_data_params data;
	struct fcoe_tx_mid_path_params mid_path;
};

union fcoe_tx_info_union_ctx {
	struct fcoe_fcp_cmd_payload fcp_cmd_payload;
	struct fcp_rsp_payload_padded fcp_rsp_payload;
	struct fcp_xfer_payload_padded fcp_xfer_payload;
	struct fcoe_tx_params tx_params;
};

struct ystorm_fcoe_task_st_ctx {
	u8 task_type;
	u8 sgl_mode;
#define YSTORM_FCOE_TASK_ST_CTX_TX_SGL_MODE_MASK  0x7
#define YSTORM_FCOE_TASK_ST_CTX_TX_SGL_MODE_SHIFT 0
#define YSTORM_FCOE_TASK_ST_CTX_RSRV_MASK         0x1F
#define YSTORM_FCOE_TASK_ST_CTX_RSRV_SHIFT        3
	u8 cached_dix_sge;
	u8 expect_first_xfer;
	__le32 num_pbf_zero_write;
	union protection_info_union_ctx protection_info_union;
	__le32 data_2_trns_rem;
	union fcoe_tx_info_union_ctx tx_info_union;
	union fcoe_dix_desc_ctx dix_desc;
	union fcoe_data_desc_ctx data_desc;
	__le16 ox_id;
	__le16 rx_id;
	__le32 task_rety_identifier;
	__le32 reserved1[2];
};

struct ystorm_fcoe_task_ag_ctx {
	u8 byte0;
	u8 byte1;
	__le16 word0;
	u8 flags0;
#define YSTORM_FCOE_TASK_AG_CTX_NIBBLE0_MASK     0xF
#define YSTORM_FCOE_TASK_AG_CTX_NIBBLE0_SHIFT    0
#define YSTORM_FCOE_TASK_AG_CTX_BIT0_MASK        0x1
#define YSTORM_FCOE_TASK_AG_CTX_BIT0_SHIFT       4
#define YSTORM_FCOE_TASK_AG_CTX_BIT1_MASK        0x1
#define YSTORM_FCOE_TASK_AG_CTX_BIT1_SHIFT       5
#define YSTORM_FCOE_TASK_AG_CTX_BIT2_MASK        0x1
#define YSTORM_FCOE_TASK_AG_CTX_BIT2_SHIFT       6
#define YSTORM_FCOE_TASK_AG_CTX_BIT3_MASK        0x1
#define YSTORM_FCOE_TASK_AG_CTX_BIT3_SHIFT       7
	u8 flags1;
#define YSTORM_FCOE_TASK_AG_CTX_CF0_MASK         0x3
#define YSTORM_FCOE_TASK_AG_CTX_CF0_SHIFT        0
#define YSTORM_FCOE_TASK_AG_CTX_CF1_MASK         0x3
#define YSTORM_FCOE_TASK_AG_CTX_CF1_SHIFT        2
#define YSTORM_FCOE_TASK_AG_CTX_CF2SPECIAL_MASK  0x3
#define YSTORM_FCOE_TASK_AG_CTX_CF2SPECIAL_SHIFT 4
#define YSTORM_FCOE_TASK_AG_CTX_CF0EN_MASK       0x1
#define YSTORM_FCOE_TASK_AG_CTX_CF0EN_SHIFT      6
#define YSTORM_FCOE_TASK_AG_CTX_CF1EN_MASK       0x1
#define YSTORM_FCOE_TASK_AG_CTX_CF1EN_SHIFT      7
	u8 flags2;
#define YSTORM_FCOE_TASK_AG_CTX_BIT4_MASK        0x1
#define YSTORM_FCOE_TASK_AG_CTX_BIT4_SHIFT       0
#define YSTORM_FCOE_TASK_AG_CTX_RULE0EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE0EN_SHIFT    1
#define YSTORM_FCOE_TASK_AG_CTX_RULE1EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE1EN_SHIFT    2
#define YSTORM_FCOE_TASK_AG_CTX_RULE2EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE2EN_SHIFT    3
#define YSTORM_FCOE_TASK_AG_CTX_RULE3EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE3EN_SHIFT    4
#define YSTORM_FCOE_TASK_AG_CTX_RULE4EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE4EN_SHIFT    5
#define YSTORM_FCOE_TASK_AG_CTX_RULE5EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE5EN_SHIFT    6
#define YSTORM_FCOE_TASK_AG_CTX_RULE6EN_MASK     0x1
#define YSTORM_FCOE_TASK_AG_CTX_RULE6EN_SHIFT    7
	u8 byte2;
	__le32 reg0;
	u8 byte3;
	u8 byte4;
	__le16 rx_id;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le16 word5;
	__le32 reg1;
	__le32 reg2;
};

struct tstorm_fcoe_task_ag_ctx {
	u8 reserved;
	u8 byte1;
	__le16 icid;
	u8 flags0;
#define TSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_MASK     0xF
#define TSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_SHIFT    0
#define TSTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_MASK        0x1
#define TSTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_SHIFT       4
#define TSTORM_FCOE_TASK_AG_CTX_BIT1_MASK                0x1
#define TSTORM_FCOE_TASK_AG_CTX_BIT1_SHIFT               5
#define TSTORM_FCOE_TASK_AG_CTX_WAIT_ABTS_RSP_F_MASK     0x1
#define TSTORM_FCOE_TASK_AG_CTX_WAIT_ABTS_RSP_F_SHIFT    6
#define TSTORM_FCOE_TASK_AG_CTX_VALID_MASK               0x1
#define TSTORM_FCOE_TASK_AG_CTX_VALID_SHIFT              7
	u8 flags1;
#define TSTORM_FCOE_TASK_AG_CTX_FALSE_RR_TOV_MASK        0x1
#define TSTORM_FCOE_TASK_AG_CTX_FALSE_RR_TOV_SHIFT       0
#define TSTORM_FCOE_TASK_AG_CTX_BIT5_MASK                0x1
#define TSTORM_FCOE_TASK_AG_CTX_BIT5_SHIFT               1
#define TSTORM_FCOE_TASK_AG_CTX_REC_RR_TOV_CF_MASK       0x3
#define TSTORM_FCOE_TASK_AG_CTX_REC_RR_TOV_CF_SHIFT      2
#define TSTORM_FCOE_TASK_AG_CTX_ED_TOV_CF_MASK           0x3
#define TSTORM_FCOE_TASK_AG_CTX_ED_TOV_CF_SHIFT          4
#define TSTORM_FCOE_TASK_AG_CTX_CF2_MASK                 0x3
#define TSTORM_FCOE_TASK_AG_CTX_CF2_SHIFT                6
	u8 flags2;
#define TSTORM_FCOE_TASK_AG_CTX_TIMER_STOP_ALL_MASK      0x3
#define TSTORM_FCOE_TASK_AG_CTX_TIMER_STOP_ALL_SHIFT     0
#define TSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_MASK       0x3
#define TSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_SHIFT      2
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_INIT_CF_MASK         0x3
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_INIT_CF_SHIFT        4
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_RECOVERY_CF_MASK     0x3
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_RECOVERY_CF_SHIFT    6
	u8 flags3;
#define TSTORM_FCOE_TASK_AG_CTX_UNSOL_COMP_CF_MASK       0x3
#define TSTORM_FCOE_TASK_AG_CTX_UNSOL_COMP_CF_SHIFT      0
#define TSTORM_FCOE_TASK_AG_CTX_REC_RR_TOV_CF_EN_MASK    0x1
#define TSTORM_FCOE_TASK_AG_CTX_REC_RR_TOV_CF_EN_SHIFT   2
#define TSTORM_FCOE_TASK_AG_CTX_ED_TOV_CF_EN_MASK        0x1
#define TSTORM_FCOE_TASK_AG_CTX_ED_TOV_CF_EN_SHIFT       3
#define TSTORM_FCOE_TASK_AG_CTX_CF2EN_MASK               0x1
#define TSTORM_FCOE_TASK_AG_CTX_CF2EN_SHIFT              4
#define TSTORM_FCOE_TASK_AG_CTX_TIMER_STOP_ALL_EN_MASK   0x1
#define TSTORM_FCOE_TASK_AG_CTX_TIMER_STOP_ALL_EN_SHIFT  5
#define TSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_EN_MASK    0x1
#define TSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_EN_SHIFT   6
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_INIT_CF_EN_MASK      0x1
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_INIT_CF_EN_SHIFT     7
	u8 flags4;
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_RECOVERY_CF_EN_MASK  0x1
#define TSTORM_FCOE_TASK_AG_CTX_SEQ_RECOVERY_CF_EN_SHIFT 0
#define TSTORM_FCOE_TASK_AG_CTX_UNSOL_COMP_CF_EN_MASK    0x1
#define TSTORM_FCOE_TASK_AG_CTX_UNSOL_COMP_CF_EN_SHIFT   1
#define TSTORM_FCOE_TASK_AG_CTX_RULE0EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE0EN_SHIFT            2
#define TSTORM_FCOE_TASK_AG_CTX_RULE1EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE1EN_SHIFT            3
#define TSTORM_FCOE_TASK_AG_CTX_RULE2EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE2EN_SHIFT            4
#define TSTORM_FCOE_TASK_AG_CTX_RULE3EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE3EN_SHIFT            5
#define TSTORM_FCOE_TASK_AG_CTX_RULE4EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE4EN_SHIFT            6
#define TSTORM_FCOE_TASK_AG_CTX_RULE5EN_MASK             0x1
#define TSTORM_FCOE_TASK_AG_CTX_RULE5EN_SHIFT            7
	u8 cleanup_state;
	__le16 last_sent_tid;
	__le32 rec_rr_tov_exp_timeout;
	u8 byte3;
	u8 byte4;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 data_offset_end_of_seq;
	__le32 data_offset_next;
};

struct fcoe_tstorm_fcoe_task_st_ctx_read_write {
	union fcoe_cleanup_addr_exp_ro_union cleanup_addr_exp_ro_union;
	__le16 flags;
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_RX_SGL_MODE_MASK       0x7
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_RX_SGL_MODE_SHIFT      0
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_EXP_FIRST_FRAME_MASK   0x1
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_EXP_FIRST_FRAME_SHIFT  3
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SEQ_ACTIVE_MASK        0x1
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SEQ_ACTIVE_SHIFT       4
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SEQ_TIMEOUT_MASK       0x1
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SEQ_TIMEOUT_SHIFT      5
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SINGLE_PKT_IN_EX_MASK  0x1
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_SINGLE_PKT_IN_EX_SHIFT 6
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_OOO_RX_SEQ_STAT_MASK   0x1
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_OOO_RX_SEQ_STAT_SHIFT  7
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_CQ_ADD_ADV_MASK        0x3
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_CQ_ADD_ADV_SHIFT       8
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_RSRV1_MASK             0x3F
#define FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_RSRV1_SHIFT            10
	__le16 seq_cnt;
	u8 seq_id;
	u8 ooo_rx_seq_id;
	__le16 rx_id;
	struct fcoe_abts_pkt abts_data;
	__le32 e_d_tov_exp_timeout_val;
	__le16 ooo_rx_seq_cnt;
	__le16 reserved1;
};

struct fcoe_tstorm_fcoe_task_st_ctx_read_only {
	u8 task_type;
	u8 dev_type;
	u8 conf_supported;
	u8 glbl_q_num;
	__le32 cid;
	__le32 fcp_cmd_trns_size;
	__le32 rsrv;
};

struct tstorm_fcoe_task_st_ctx {
	struct fcoe_tstorm_fcoe_task_st_ctx_read_write read_write;
	struct fcoe_tstorm_fcoe_task_st_ctx_read_only read_only;
};

struct mstorm_fcoe_task_ag_ctx {
	u8 byte0;
	u8 byte1;
	__le16 icid;
	u8 flags0;
#define MSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_MASK    0xF
#define MSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_SHIFT   0
#define MSTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_MASK       0x1
#define MSTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_SHIFT      4
#define MSTORM_FCOE_TASK_AG_CTX_CQE_PLACED_MASK         0x1
#define MSTORM_FCOE_TASK_AG_CTX_CQE_PLACED_SHIFT        5
#define MSTORM_FCOE_TASK_AG_CTX_BIT2_MASK               0x1
#define MSTORM_FCOE_TASK_AG_CTX_BIT2_SHIFT              6
#define MSTORM_FCOE_TASK_AG_CTX_BIT3_MASK               0x1
#define MSTORM_FCOE_TASK_AG_CTX_BIT3_SHIFT              7
	u8 flags1;
#define MSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_MASK      0x3
#define MSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_SHIFT     0
#define MSTORM_FCOE_TASK_AG_CTX_CF1_MASK                0x3
#define MSTORM_FCOE_TASK_AG_CTX_CF1_SHIFT               2
#define MSTORM_FCOE_TASK_AG_CTX_CF2_MASK                0x3
#define MSTORM_FCOE_TASK_AG_CTX_CF2_SHIFT               4
#define MSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_EN_MASK   0x1
#define MSTORM_FCOE_TASK_AG_CTX_EX_CLEANUP_CF_EN_SHIFT  6
#define MSTORM_FCOE_TASK_AG_CTX_CF1EN_MASK              0x1
#define MSTORM_FCOE_TASK_AG_CTX_CF1EN_SHIFT             7
	u8 flags2;
#define MSTORM_FCOE_TASK_AG_CTX_CF2EN_MASK              0x1
#define MSTORM_FCOE_TASK_AG_CTX_CF2EN_SHIFT             0
#define MSTORM_FCOE_TASK_AG_CTX_RULE0EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE0EN_SHIFT           1
#define MSTORM_FCOE_TASK_AG_CTX_RULE1EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE1EN_SHIFT           2
#define MSTORM_FCOE_TASK_AG_CTX_RULE2EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE2EN_SHIFT           3
#define MSTORM_FCOE_TASK_AG_CTX_RULE3EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE3EN_SHIFT           4
#define MSTORM_FCOE_TASK_AG_CTX_RULE4EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE4EN_SHIFT           5
#define MSTORM_FCOE_TASK_AG_CTX_XFER_PLACEMENT_EN_MASK  0x1
#define MSTORM_FCOE_TASK_AG_CTX_XFER_PLACEMENT_EN_SHIFT 6
#define MSTORM_FCOE_TASK_AG_CTX_RULE6EN_MASK            0x1
#define MSTORM_FCOE_TASK_AG_CTX_RULE6EN_SHIFT           7
	u8 cleanup_state;
	__le32 received_bytes;
	u8 byte3;
	u8 glbl_q_num;
	__le16 word1;
	__le16 tid_to_xfer;
	__le16 word3;
	__le16 word4;
	__le16 word5;
	__le32 expected_bytes;
	__le32 reg2;
};

struct mstorm_fcoe_task_st_ctx {
	struct fcoe_mstorm_fcoe_task_st_ctx_non_fp non_fp;
	struct fcoe_mstorm_fcoe_task_st_ctx_fp fp;
};

struct ustorm_fcoe_task_ag_ctx {
	u8 reserved;
	u8 byte1;
	__le16 icid;
	u8 flags0;
#define USTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_MASK  0xF
#define USTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define USTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_MASK     0x1
#define USTORM_FCOE_TASK_AG_CTX_EXIST_IN_QM0_SHIFT    4
#define USTORM_FCOE_TASK_AG_CTX_BIT1_MASK             0x1
#define USTORM_FCOE_TASK_AG_CTX_BIT1_SHIFT            5
#define USTORM_FCOE_TASK_AG_CTX_CF0_MASK              0x3
#define USTORM_FCOE_TASK_AG_CTX_CF0_SHIFT             6
	u8 flags1;
#define USTORM_FCOE_TASK_AG_CTX_CF1_MASK              0x3
#define USTORM_FCOE_TASK_AG_CTX_CF1_SHIFT             0
#define USTORM_FCOE_TASK_AG_CTX_CF2_MASK              0x3
#define USTORM_FCOE_TASK_AG_CTX_CF2_SHIFT             2
#define USTORM_FCOE_TASK_AG_CTX_CF3_MASK              0x3
#define USTORM_FCOE_TASK_AG_CTX_CF3_SHIFT             4
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_CF_MASK     0x3
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_CF_SHIFT    6
	u8 flags2;
#define USTORM_FCOE_TASK_AG_CTX_CF0EN_MASK            0x1
#define USTORM_FCOE_TASK_AG_CTX_CF0EN_SHIFT           0
#define USTORM_FCOE_TASK_AG_CTX_CF1EN_MASK            0x1
#define USTORM_FCOE_TASK_AG_CTX_CF1EN_SHIFT           1
#define USTORM_FCOE_TASK_AG_CTX_CF2EN_MASK            0x1
#define USTORM_FCOE_TASK_AG_CTX_CF2EN_SHIFT           2
#define USTORM_FCOE_TASK_AG_CTX_CF3EN_MASK            0x1
#define USTORM_FCOE_TASK_AG_CTX_CF3EN_SHIFT           3
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK  0x1
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT 4
#define USTORM_FCOE_TASK_AG_CTX_RULE0EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE0EN_SHIFT         5
#define USTORM_FCOE_TASK_AG_CTX_RULE1EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE1EN_SHIFT         6
#define USTORM_FCOE_TASK_AG_CTX_RULE2EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE2EN_SHIFT         7
	u8 flags3;
#define USTORM_FCOE_TASK_AG_CTX_RULE3EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE3EN_SHIFT         0
#define USTORM_FCOE_TASK_AG_CTX_RULE4EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE4EN_SHIFT         1
#define USTORM_FCOE_TASK_AG_CTX_RULE5EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE5EN_SHIFT         2
#define USTORM_FCOE_TASK_AG_CTX_RULE6EN_MASK          0x1
#define USTORM_FCOE_TASK_AG_CTX_RULE6EN_SHIFT         3
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_TYPE_MASK   0xF
#define USTORM_FCOE_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT  4
	__le32 dif_err_intervals;
	__le32 dif_error_1st_interval;
	__le32 global_cq_num;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
};

struct fcoe_task_context {
	struct ystorm_fcoe_task_st_ctx ystorm_st_context;
	struct tdif_task_context tdif_context;
	struct ystorm_fcoe_task_ag_ctx ystorm_ag_context;
	struct tstorm_fcoe_task_ag_ctx tstorm_ag_context;
	struct timers_context timer_context;
	struct tstorm_fcoe_task_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
	struct mstorm_fcoe_task_ag_ctx mstorm_ag_context;
	struct mstorm_fcoe_task_st_ctx mstorm_st_context;
	struct ustorm_fcoe_task_ag_ctx ustorm_ag_context;
	struct rdif_task_context rdif_context;
};

struct fcoe_tx_stat {
	struct regpair fcoe_tx_byte_cnt;
	struct regpair fcoe_tx_data_pkt_cnt;
	struct regpair fcoe_tx_xfer_pkt_cnt;
	struct regpair fcoe_tx_other_pkt_cnt;
};

struct fcoe_wqe {
	__le16 task_id;
	__le16 flags;
#define FCOE_WQE_REQ_TYPE_MASK        0xF
#define FCOE_WQE_REQ_TYPE_SHIFT       0
#define FCOE_WQE_SGL_MODE_MASK        0x7
#define FCOE_WQE_SGL_MODE_SHIFT       4
#define FCOE_WQE_CONTINUATION_MASK    0x1
#define FCOE_WQE_CONTINUATION_SHIFT   7
#define FCOE_WQE_INVALIDATE_PTU_MASK  0x1
#define FCOE_WQE_INVALIDATE_PTU_SHIFT 8
#define FCOE_WQE_SUPER_IO_MASK        0x1
#define FCOE_WQE_SUPER_IO_SHIFT       9
#define FCOE_WQE_SEND_AUTO_RSP_MASK   0x1
#define FCOE_WQE_SEND_AUTO_RSP_SHIFT  10
#define FCOE_WQE_RESERVED0_MASK       0x1F
#define FCOE_WQE_RESERVED0_SHIFT      11
	union fcoe_additional_info_union additional_info_union;
};

struct xfrqe_prot_flags {
	u8 flags;
#define XFRQE_PROT_FLAGS_PROT_INTERVAL_SIZE_LOG_MASK  0xF
#define XFRQE_PROT_FLAGS_PROT_INTERVAL_SIZE_LOG_SHIFT 0
#define XFRQE_PROT_FLAGS_DIF_TO_PEER_MASK             0x1
#define XFRQE_PROT_FLAGS_DIF_TO_PEER_SHIFT            4
#define XFRQE_PROT_FLAGS_HOST_INTERFACE_MASK          0x3
#define XFRQE_PROT_FLAGS_HOST_INTERFACE_SHIFT         5
#define XFRQE_PROT_FLAGS_RESERVED_MASK                0x1
#define XFRQE_PROT_FLAGS_RESERVED_SHIFT               7
};

struct fcoe_db_data {
	u8 params;
#define FCOE_DB_DATA_DEST_MASK         0x3
#define FCOE_DB_DATA_DEST_SHIFT        0
#define FCOE_DB_DATA_AGG_CMD_MASK      0x3
#define FCOE_DB_DATA_AGG_CMD_SHIFT     2
#define FCOE_DB_DATA_BYPASS_EN_MASK    0x1
#define FCOE_DB_DATA_BYPASS_EN_SHIFT   4
#define FCOE_DB_DATA_RESERVED_MASK     0x1
#define FCOE_DB_DATA_RESERVED_SHIFT    5
#define FCOE_DB_DATA_AGG_VAL_SEL_MASK  0x3
#define FCOE_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8 agg_flags;
	__le16 sq_prod;
};
#endif /* __FCOE_COMMON__ */

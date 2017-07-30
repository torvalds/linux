/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __ISCSI_COMMON__
#define __ISCSI_COMMON__
/**********************/
/* ISCSI FW CONSTANTS */
/**********************/

/* iSCSI HSI constants */
#define ISCSI_DEFAULT_MTU       (1500)

/* KWQ (kernel work queue) layer codes */
#define ISCSI_SLOW_PATH_LAYER_CODE   (6)

/* iSCSI parameter defaults */
#define ISCSI_DEFAULT_HEADER_DIGEST         (0)
#define ISCSI_DEFAULT_DATA_DIGEST           (0)
#define ISCSI_DEFAULT_INITIAL_R2T           (1)
#define ISCSI_DEFAULT_IMMEDIATE_DATA        (1)
#define ISCSI_DEFAULT_MAX_PDU_LENGTH        (0x2000)
#define ISCSI_DEFAULT_FIRST_BURST_LENGTH    (0x10000)
#define ISCSI_DEFAULT_MAX_BURST_LENGTH      (0x40000)
#define ISCSI_DEFAULT_MAX_OUTSTANDING_R2T   (1)

/* iSCSI parameter limits */
#define ISCSI_MIN_VAL_MAX_PDU_LENGTH        (0x200)
#define ISCSI_MAX_VAL_MAX_PDU_LENGTH        (0xffffff)
#define ISCSI_MIN_VAL_BURST_LENGTH          (0x200)
#define ISCSI_MAX_VAL_BURST_LENGTH          (0xffffff)
#define ISCSI_MIN_VAL_MAX_OUTSTANDING_R2T   (1)
#define ISCSI_MAX_VAL_MAX_OUTSTANDING_R2T   (0xff)

#define ISCSI_AHS_CNTL_SIZE 4

#define ISCSI_WQE_NUM_SGES_SLOWIO           (0xf)

/* iSCSI reserved params */
#define ISCSI_ITT_ALL_ONES	(0xffffffff)
#define ISCSI_TTT_ALL_ONES	(0xffffffff)

#define ISCSI_OPTION_1_OFF_CHIP_TCP 1
#define ISCSI_OPTION_2_ON_CHIP_TCP 2

#define ISCSI_INITIATOR_MODE 0
#define ISCSI_TARGET_MODE 1

/* iSCSI request op codes */
#define ISCSI_OPCODE_NOP_OUT		(0)
#define ISCSI_OPCODE_SCSI_CMD		(1)
#define ISCSI_OPCODE_TMF_REQUEST	(2)
#define ISCSI_OPCODE_LOGIN_REQUEST	(3)
#define ISCSI_OPCODE_TEXT_REQUEST	(4)
#define ISCSI_OPCODE_DATA_OUT		(5)
#define ISCSI_OPCODE_LOGOUT_REQUEST	(6)

/* iSCSI response/messages op codes */
#define ISCSI_OPCODE_NOP_IN             (0x20)
#define ISCSI_OPCODE_SCSI_RESPONSE      (0x21)
#define ISCSI_OPCODE_TMF_RESPONSE       (0x22)
#define ISCSI_OPCODE_LOGIN_RESPONSE     (0x23)
#define ISCSI_OPCODE_TEXT_RESPONSE      (0x24)
#define ISCSI_OPCODE_DATA_IN            (0x25)
#define ISCSI_OPCODE_LOGOUT_RESPONSE    (0x26)
#define ISCSI_OPCODE_R2T                (0x31)
#define ISCSI_OPCODE_ASYNC_MSG          (0x32)
#define ISCSI_OPCODE_REJECT             (0x3f)

/* iSCSI stages */
#define ISCSI_STAGE_SECURITY_NEGOTIATION            (0)
#define ISCSI_STAGE_LOGIN_OPERATIONAL_NEGOTIATION   (1)
#define ISCSI_STAGE_FULL_FEATURE_PHASE              (3)

/* iSCSI CQE errors */
#define CQE_ERROR_BITMAP_DATA_DIGEST          (0x08)
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN  (0x10)
#define CQE_ERROR_BITMAP_DATA_TRUNCATED       (0x20)

struct cqe_error_bitmap {
	u8 cqe_error_status_bits;
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_MASK         0x7
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_SHIFT        0
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_MASK      0x1
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_SHIFT     3
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_MASK  0x1
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_SHIFT 4
#define CQE_ERROR_BITMAP_DATA_TRUNCATED_ERR_MASK   0x1
#define CQE_ERROR_BITMAP_DATA_TRUNCATED_ERR_SHIFT  5
#define CQE_ERROR_BITMAP_UNDER_RUN_ERR_MASK        0x1
#define CQE_ERROR_BITMAP_UNDER_RUN_ERR_SHIFT       6
#define CQE_ERROR_BITMAP_RESERVED2_MASK            0x1
#define CQE_ERROR_BITMAP_RESERVED2_SHIFT           7
};

union cqe_error_status {
	u8 error_status;
	struct cqe_error_bitmap error_bits;
};

struct data_hdr {
	__le32 data[12];
};

struct iscsi_async_msg_hdr {
	__le16 reserved0;
	u8 flags_attr;
#define ISCSI_ASYNC_MSG_HDR_RSRV_MASK           0x7F
#define ISCSI_ASYNC_MSG_HDR_RSRV_SHIFT          0
#define ISCSI_ASYNC_MSG_HDR_CONST1_MASK         0x1
#define ISCSI_ASYNC_MSG_HDR_CONST1_SHIFT        7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_ASYNC_MSG_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_ASYNC_MSG_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_ASYNC_MSG_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_ASYNC_MSG_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 all_ones;
	__le32 reserved1;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le16 param1_rsrv;
	u8 async_vcode;
	u8 async_event;
	__le16 param3_rsrv;
	__le16 param2_rsrv;
	__le32 reserved7;
};

struct iscsi_cmd_hdr {
	__le16 reserved1;
	u8 flags_attr;
#define ISCSI_CMD_HDR_ATTR_MASK		0x7
#define ISCSI_CMD_HDR_ATTR_SHIFT	0
#define ISCSI_CMD_HDR_RSRV_MASK		0x3
#define ISCSI_CMD_HDR_RSRV_SHIFT	3
#define ISCSI_CMD_HDR_WRITE_MASK	0x1
#define ISCSI_CMD_HDR_WRITE_SHIFT	5
#define ISCSI_CMD_HDR_READ_MASK		0x1
#define ISCSI_CMD_HDR_READ_SHIFT	6
#define ISCSI_CMD_HDR_FINAL_MASK	0x1
#define ISCSI_CMD_HDR_FINAL_SHIFT	7
	u8 hdr_first_byte;
#define ISCSI_CMD_HDR_OPCODE_MASK	0x3F
#define ISCSI_CMD_HDR_OPCODE_SHIFT	0
#define ISCSI_CMD_HDR_IMM_MASK		0x1
#define ISCSI_CMD_HDR_IMM_SHIFT		6
#define ISCSI_CMD_HDR_RSRV1_MASK	0x1
#define ISCSI_CMD_HDR_RSRV1_SHIFT	7
	__le32 hdr_second_dword;
#define ISCSI_CMD_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_CMD_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_CMD_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_CMD_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 expected_transfer_length;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 cdb[4];
};

struct iscsi_common_hdr {
	u8 hdr_status;
	u8 hdr_response;
	u8 hdr_flags;
	u8 hdr_first_byte;
#define ISCSI_COMMON_HDR_OPCODE_MASK         0x3F
#define ISCSI_COMMON_HDR_OPCODE_SHIFT        0
#define ISCSI_COMMON_HDR_IMM_MASK            0x1
#define ISCSI_COMMON_HDR_IMM_SHIFT           6
#define ISCSI_COMMON_HDR_RSRV_MASK           0x1
#define ISCSI_COMMON_HDR_RSRV_SHIFT          7
	__le32 hdr_second_dword;
#define ISCSI_COMMON_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_COMMON_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_COMMON_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_COMMON_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun_reserved;
	__le32 itt;
	__le32 ttt;
	__le32 cmdstat_sn;
	__le32 exp_statcmd_sn;
	__le32 max_cmd_sn;
	__le32 data[3];
};

struct iscsi_conn_offload_params {
	struct regpair sq_pbl_addr;
	struct regpair r2tq_pbl_addr;
	struct regpair xhq_pbl_addr;
	struct regpair uhq_pbl_addr;
	__le32 initial_ack;
	__le16 physical_q0;
	__le16 physical_q1;
	u8 flags;
#define ISCSI_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_MASK  0x1
#define ISCSI_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_SHIFT 0
#define ISCSI_CONN_OFFLOAD_PARAMS_TARGET_MODE_MASK     0x1
#define ISCSI_CONN_OFFLOAD_PARAMS_TARGET_MODE_SHIFT    1
#define ISCSI_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_MASK	0x1
#define ISCSI_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_SHIFT	2
#define ISCSI_CONN_OFFLOAD_PARAMS_RESERVED1_MASK	0x1F
#define ISCSI_CONN_OFFLOAD_PARAMS_RESERVED1_SHIFT	3
	u8 pbl_page_size_log;
	u8 pbe_page_size_log;
	u8 default_cq;
	__le32 stat_sn;
};

struct iscsi_slow_path_hdr {
	u8 op_code;
	u8 flags;
#define ISCSI_SLOW_PATH_HDR_RESERVED0_MASK   0xF
#define ISCSI_SLOW_PATH_HDR_RESERVED0_SHIFT  0
#define ISCSI_SLOW_PATH_HDR_LAYER_CODE_MASK  0x7
#define ISCSI_SLOW_PATH_HDR_LAYER_CODE_SHIFT 4
#define ISCSI_SLOW_PATH_HDR_RESERVED1_MASK   0x1
#define ISCSI_SLOW_PATH_HDR_RESERVED1_SHIFT  7
};

struct iscsi_conn_update_ramrod_params {
	struct iscsi_slow_path_hdr hdr;
	__le16 conn_id;
	__le32 fw_cid;
	u8 flags;
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_HD_EN_MASK           0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_HD_EN_SHIFT          0
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DD_EN_MASK           0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DD_EN_SHIFT          1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_INITIAL_R2T_MASK     0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_INITIAL_R2T_SHIFT    2
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_IMMEDIATE_DATA_MASK  0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_IMMEDIATE_DATA_SHIFT 3
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_BLOCK_SIZE_MASK  0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_BLOCK_SIZE_SHIFT 4
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_HOST_EN_MASK  0x1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_HOST_EN_SHIFT 5
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_RESERVED1_MASK       0x3
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_RESERVED1_SHIFT      6
	u8 reserved0[3];
	__le32 max_seq_size;
	__le32 max_send_pdu_length;
	__le32 max_recv_pdu_length;
	__le32 first_seq_length;
	__le32 exp_stat_sn;
};

struct iscsi_ext_cdb_cmd_hdr {
	__le16 reserved1;
	u8 flags_attr;
#define ISCSI_EXT_CDB_CMD_HDR_ATTR_MASK          0x7
#define ISCSI_EXT_CDB_CMD_HDR_ATTR_SHIFT         0
#define ISCSI_EXT_CDB_CMD_HDR_RSRV_MASK          0x3
#define ISCSI_EXT_CDB_CMD_HDR_RSRV_SHIFT         3
#define ISCSI_EXT_CDB_CMD_HDR_WRITE_MASK         0x1
#define ISCSI_EXT_CDB_CMD_HDR_WRITE_SHIFT        5
#define ISCSI_EXT_CDB_CMD_HDR_READ_MASK          0x1
#define ISCSI_EXT_CDB_CMD_HDR_READ_SHIFT         6
#define ISCSI_EXT_CDB_CMD_HDR_FINAL_MASK         0x1
#define ISCSI_EXT_CDB_CMD_HDR_FINAL_SHIFT        7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_EXT_CDB_CMD_HDR_DATA_SEG_LEN_MASK  0xFFFFFF
#define ISCSI_EXT_CDB_CMD_HDR_DATA_SEG_LEN_SHIFT 0
#define ISCSI_EXT_CDB_CMD_HDR_CDB_SIZE_MASK      0xFF
#define ISCSI_EXT_CDB_CMD_HDR_CDB_SIZE_SHIFT     24
	struct regpair lun;
	__le32 itt;
	__le32 expected_transfer_length;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	struct scsi_sge cdb_sge;
};

struct iscsi_login_req_hdr {
	u8 version_min;
	u8 version_max;
	u8 flags_attr;
#define ISCSI_LOGIN_REQ_HDR_NSG_MASK            0x3
#define ISCSI_LOGIN_REQ_HDR_NSG_SHIFT           0
#define ISCSI_LOGIN_REQ_HDR_CSG_MASK            0x3
#define ISCSI_LOGIN_REQ_HDR_CSG_SHIFT           2
#define ISCSI_LOGIN_REQ_HDR_RSRV_MASK           0x3
#define ISCSI_LOGIN_REQ_HDR_RSRV_SHIFT          4
#define ISCSI_LOGIN_REQ_HDR_C_MASK              0x1
#define ISCSI_LOGIN_REQ_HDR_C_SHIFT             6
#define ISCSI_LOGIN_REQ_HDR_T_MASK              0x1
#define ISCSI_LOGIN_REQ_HDR_T_SHIFT             7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_LOGIN_REQ_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_LOGIN_REQ_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGIN_REQ_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_LOGIN_REQ_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 isid_tabc;
	__le16 tsih;
	__le16 isid_d;
	__le32 itt;
	__le16 reserved1;
	__le16 cid;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 reserved2[4];
};

struct iscsi_logout_req_hdr {
	__le16 reserved0;
	u8 reason_code;
	u8 opcode;
	__le32 reserved1;
	__le32 reserved2[2];
	__le32 itt;
	__le16 reserved3;
	__le16 cid;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 reserved4[4];
};

struct iscsi_data_out_hdr {
	__le16 reserved1;
	u8 flags_attr;
#define ISCSI_DATA_OUT_HDR_RSRV_MASK   0x7F
#define ISCSI_DATA_OUT_HDR_RSRV_SHIFT  0
#define ISCSI_DATA_OUT_HDR_FINAL_MASK  0x1
#define ISCSI_DATA_OUT_HDR_FINAL_SHIFT 7
	u8 opcode;
	__le32 reserved2;
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 reserved3;
	__le32 exp_stat_sn;
	__le32 reserved4;
	__le32 data_sn;
	__le32 buffer_offset;
	__le32 reserved5;
};

struct iscsi_data_in_hdr {
	u8 status_rsvd;
	u8 reserved1;
	u8 flags;
#define ISCSI_DATA_IN_HDR_STATUS_MASK     0x1
#define ISCSI_DATA_IN_HDR_STATUS_SHIFT    0
#define ISCSI_DATA_IN_HDR_UNDERFLOW_MASK  0x1
#define ISCSI_DATA_IN_HDR_UNDERFLOW_SHIFT 1
#define ISCSI_DATA_IN_HDR_OVERFLOW_MASK   0x1
#define ISCSI_DATA_IN_HDR_OVERFLOW_SHIFT  2
#define ISCSI_DATA_IN_HDR_RSRV_MASK       0x7
#define ISCSI_DATA_IN_HDR_RSRV_SHIFT      3
#define ISCSI_DATA_IN_HDR_ACK_MASK        0x1
#define ISCSI_DATA_IN_HDR_ACK_SHIFT       6
#define ISCSI_DATA_IN_HDR_FINAL_MASK      0x1
#define ISCSI_DATA_IN_HDR_FINAL_SHIFT     7
	u8 opcode;
	__le32 reserved2;
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 data_sn;
	__le32 buffer_offset;
	__le32 residual_count;
};

struct iscsi_r2t_hdr {
	u8 reserved0[3];
	u8 opcode;
	__le32 reserved2;
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 r2t_sn;
	__le32 buffer_offset;
	__le32 desired_data_trns_len;
};

struct iscsi_nop_out_hdr {
	__le16 reserved1;
	u8 flags_attr;
#define ISCSI_NOP_OUT_HDR_RSRV_MASK    0x7F
#define ISCSI_NOP_OUT_HDR_RSRV_SHIFT   0
#define ISCSI_NOP_OUT_HDR_CONST1_MASK  0x1
#define ISCSI_NOP_OUT_HDR_CONST1_SHIFT 7
	u8 opcode;
	__le32 reserved2;
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 reserved3;
	__le32 reserved4;
	__le32 reserved5;
	__le32 reserved6;
};

struct iscsi_nop_in_hdr {
	__le16 reserved0;
	u8 flags_attr;
#define ISCSI_NOP_IN_HDR_RSRV_MASK           0x7F
#define ISCSI_NOP_IN_HDR_RSRV_SHIFT          0
#define ISCSI_NOP_IN_HDR_CONST1_MASK         0x1
#define ISCSI_NOP_IN_HDR_CONST1_SHIFT        7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_NOP_IN_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_NOP_IN_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_NOP_IN_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_NOP_IN_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 reserved5;
	__le32 reserved6;
	__le32 reserved7;
};

struct iscsi_login_response_hdr {
	u8 version_active;
	u8 version_max;
	u8 flags_attr;
#define ISCSI_LOGIN_RESPONSE_HDR_NSG_MASK            0x3
#define ISCSI_LOGIN_RESPONSE_HDR_NSG_SHIFT           0
#define ISCSI_LOGIN_RESPONSE_HDR_CSG_MASK            0x3
#define ISCSI_LOGIN_RESPONSE_HDR_CSG_SHIFT           2
#define ISCSI_LOGIN_RESPONSE_HDR_RSRV_MASK           0x3
#define ISCSI_LOGIN_RESPONSE_HDR_RSRV_SHIFT          4
#define ISCSI_LOGIN_RESPONSE_HDR_C_MASK              0x1
#define ISCSI_LOGIN_RESPONSE_HDR_C_SHIFT             6
#define ISCSI_LOGIN_RESPONSE_HDR_T_MASK              0x1
#define ISCSI_LOGIN_RESPONSE_HDR_T_SHIFT             7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGIN_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_LOGIN_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 isid_tabc;
	__le16 tsih;
	__le16 isid_d;
	__le32 itt;
	__le32 reserved1;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le16 reserved2;
	u8 status_detail;
	u8 status_class;
	__le32 reserved4[2];
};

struct iscsi_logout_response_hdr {
	u8 reserved1;
	u8 response;
	u8 flags;
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_LOGOUT_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_LOGOUT_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGOUT_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_LOGOUT_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 reserved2[2];
	__le32 itt;
	__le32 reserved3;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 reserved4;
	__le16 time_2_retain;
	__le16 time_2_wait;
	__le32 reserved5[1];
};

struct iscsi_text_request_hdr {
	__le16 reserved0;
	u8 flags_attr;
#define ISCSI_TEXT_REQUEST_HDR_RSRV_MASK           0x3F
#define ISCSI_TEXT_REQUEST_HDR_RSRV_SHIFT          0
#define ISCSI_TEXT_REQUEST_HDR_C_MASK              0x1
#define ISCSI_TEXT_REQUEST_HDR_C_SHIFT             6
#define ISCSI_TEXT_REQUEST_HDR_F_MASK              0x1
#define ISCSI_TEXT_REQUEST_HDR_F_SHIFT             7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_TEXT_REQUEST_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_TEXT_REQUEST_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TEXT_REQUEST_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_TEXT_REQUEST_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 reserved4[4];
};

struct iscsi_text_response_hdr {
	__le16 reserved1;
	u8 flags;
#define ISCSI_TEXT_RESPONSE_HDR_RSRV_MASK           0x3F
#define ISCSI_TEXT_RESPONSE_HDR_RSRV_SHIFT          0
#define ISCSI_TEXT_RESPONSE_HDR_C_MASK              0x1
#define ISCSI_TEXT_RESPONSE_HDR_C_SHIFT             6
#define ISCSI_TEXT_RESPONSE_HDR_F_MASK              0x1
#define ISCSI_TEXT_RESPONSE_HDR_F_SHIFT             7
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TEXT_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_TEXT_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 ttt;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 reserved4[3];
};

struct iscsi_tmf_request_hdr {
	__le16 reserved0;
	u8 function;
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_TMF_REQUEST_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_TMF_REQUEST_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TMF_REQUEST_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_TMF_REQUEST_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 rtt;
	__le32 cmd_sn;
	__le32 exp_stat_sn;
	__le32 ref_cmd_sn;
	__le32 exp_data_sn;
	__le32 reserved4[2];
};

struct iscsi_tmf_response_hdr {
	u8 reserved2;
	u8 hdr_response;
	u8 hdr_flags;
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_TMF_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_TMF_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TMF_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_TMF_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair reserved0;
	__le32 itt;
	__le32 reserved1;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 reserved4[3];
};

struct iscsi_response_hdr {
	u8 hdr_status;
	u8 hdr_response;
	u8 hdr_flags;
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun;
	__le32 itt;
	__le32 snack_tag;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 exp_data_sn;
	__le32 bi_residual_count;
	__le32 residual_count;
};

struct iscsi_reject_hdr {
	u8 reserved4;
	u8 hdr_reason;
	u8 hdr_flags;
	u8 opcode;
	__le32 hdr_second_dword;
#define ISCSI_REJECT_HDR_DATA_SEG_LEN_MASK   0xFFFFFF
#define ISCSI_REJECT_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_REJECT_HDR_TOTAL_AHS_LEN_MASK  0xFF
#define ISCSI_REJECT_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair reserved0;
	__le32 all_ones;
	__le32 reserved2;
	__le32 stat_sn;
	__le32 exp_cmd_sn;
	__le32 max_cmd_sn;
	__le32 data_sn;
	__le32 reserved3[2];
};

union iscsi_task_hdr {
	struct iscsi_common_hdr common;
	struct data_hdr data;
	struct iscsi_cmd_hdr cmd;
	struct iscsi_ext_cdb_cmd_hdr ext_cdb_cmd;
	struct iscsi_login_req_hdr login_req;
	struct iscsi_logout_req_hdr logout_req;
	struct iscsi_data_out_hdr data_out;
	struct iscsi_data_in_hdr data_in;
	struct iscsi_r2t_hdr r2t;
	struct iscsi_nop_out_hdr nop_out;
	struct iscsi_nop_in_hdr nop_in;
	struct iscsi_login_response_hdr login_response;
	struct iscsi_logout_response_hdr logout_response;
	struct iscsi_text_request_hdr text_request;
	struct iscsi_text_response_hdr text_response;
	struct iscsi_tmf_request_hdr tmf_request;
	struct iscsi_tmf_response_hdr tmf_response;
	struct iscsi_response_hdr response;
	struct iscsi_reject_hdr reject;
	struct iscsi_async_msg_hdr async_msg;
};

struct iscsi_cqe_common {
	__le16 conn_id;
	u8 cqe_type;
	union cqe_error_status error_bitmap;
	__le32 reserved[3];
	union iscsi_task_hdr iscsi_hdr;
};

struct iscsi_cqe_solicited {
	__le16 conn_id;
	u8 cqe_type;
	union cqe_error_status error_bitmap;
	__le16 itid;
	u8 task_type;
	u8 fw_dbg_field;
	u8 caused_conn_err;
	u8 reserved0[3];
	__le32 reserved1[1];
	union iscsi_task_hdr iscsi_hdr;
};

struct iscsi_cqe_unsolicited {
	__le16 conn_id;
	u8 cqe_type;
	union cqe_error_status error_bitmap;
	__le16 reserved0;
	u8 reserved1;
	u8 unsol_cqe_type;
	struct regpair rqe_opaque;
	union iscsi_task_hdr iscsi_hdr;
};

union iscsi_cqe {
	struct iscsi_cqe_common cqe_common;
	struct iscsi_cqe_solicited cqe_solicited;
	struct iscsi_cqe_unsolicited cqe_unsolicited;
};

enum iscsi_cqes_type {
	ISCSI_CQE_TYPE_SOLICITED = 1,
	ISCSI_CQE_TYPE_UNSOLICITED,
	ISCSI_CQE_TYPE_SOLICITED_WITH_SENSE,
	ISCSI_CQE_TYPE_TASK_CLEANUP,
	ISCSI_CQE_TYPE_DUMMY,
	MAX_ISCSI_CQES_TYPE
};

enum iscsi_cqe_unsolicited_type {
	ISCSI_CQE_UNSOLICITED_NONE,
	ISCSI_CQE_UNSOLICITED_SINGLE,
	ISCSI_CQE_UNSOLICITED_FIRST,
	ISCSI_CQE_UNSOLICITED_MIDDLE,
	ISCSI_CQE_UNSOLICITED_LAST,
	MAX_ISCSI_CQE_UNSOLICITED_TYPE
};


struct iscsi_debug_modes {
	u8 flags;
#define ISCSI_DEBUG_MODES_ASSERT_IF_RX_CONN_ERROR_MASK         0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RX_CONN_ERROR_SHIFT        0
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_RESET_MASK            0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_RESET_SHIFT           1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_FIN_MASK              0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_FIN_SHIFT             2
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_CLEANUP_MASK          0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_CLEANUP_SHIFT         3
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_REJECT_OR_ASYNC_MASK  0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_REJECT_OR_ASYNC_SHIFT 4
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_NOP_MASK              0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_NOP_SHIFT             5
#define ISCSI_DEBUG_MODES_ASSERT_IF_DATA_DIGEST_ERROR_MASK     0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_DATA_DIGEST_ERROR_SHIFT    6
#define ISCSI_DEBUG_MODES_ASSERT_IF_DIF_ERROR_MASK             0x1
#define ISCSI_DEBUG_MODES_ASSERT_IF_DIF_ERROR_SHIFT            7
};

struct iscsi_dif_flags {
	u8 flags;
#define ISCSI_DIF_FLAGS_PROT_INTERVAL_SIZE_LOG_MASK  0xF
#define ISCSI_DIF_FLAGS_PROT_INTERVAL_SIZE_LOG_SHIFT 0
#define ISCSI_DIF_FLAGS_DIF_TO_PEER_MASK             0x1
#define ISCSI_DIF_FLAGS_DIF_TO_PEER_SHIFT            4
#define ISCSI_DIF_FLAGS_HOST_INTERFACE_MASK          0x7
#define ISCSI_DIF_FLAGS_HOST_INTERFACE_SHIFT         5
};

enum iscsi_eqe_opcode {
	ISCSI_EVENT_TYPE_INIT_FUNC = 0,
	ISCSI_EVENT_TYPE_DESTROY_FUNC,
	ISCSI_EVENT_TYPE_OFFLOAD_CONN,
	ISCSI_EVENT_TYPE_UPDATE_CONN,
	ISCSI_EVENT_TYPE_CLEAR_SQ,
	ISCSI_EVENT_TYPE_TERMINATE_CONN,
	ISCSI_EVENT_TYPE_MAC_UPDATE_CONN,
	ISCSI_EVENT_TYPE_ASYN_CONNECT_COMPLETE,
	ISCSI_EVENT_TYPE_ASYN_TERMINATE_DONE,
	RESERVED9,
	ISCSI_EVENT_TYPE_START_OF_ERROR_TYPES = 10,
	ISCSI_EVENT_TYPE_ASYN_ABORT_RCVD,
	ISCSI_EVENT_TYPE_ASYN_CLOSE_RCVD,
	ISCSI_EVENT_TYPE_ASYN_SYN_RCVD,
	ISCSI_EVENT_TYPE_ASYN_MAX_RT_TIME,
	ISCSI_EVENT_TYPE_ASYN_MAX_RT_CNT,
	ISCSI_EVENT_TYPE_ASYN_MAX_KA_PROBES_CNT,
	ISCSI_EVENT_TYPE_ASYN_FIN_WAIT2,
	ISCSI_EVENT_TYPE_ISCSI_CONN_ERROR,
	ISCSI_EVENT_TYPE_TCP_CONN_ERROR,
	MAX_ISCSI_EQE_OPCODE
};

enum iscsi_error_types {
	ISCSI_STATUS_NONE = 0,
	ISCSI_CQE_ERROR_UNSOLICITED_RCV_ON_INVALID_CONN = 1,
	ISCSI_CONN_ERROR_TASK_CID_MISMATCH,
	ISCSI_CONN_ERROR_TASK_NOT_VALID,
	ISCSI_CONN_ERROR_RQ_RING_IS_FULL,
	ISCSI_CONN_ERROR_CMDQ_RING_IS_FULL,
	ISCSI_CONN_ERROR_HQE_CACHING_FAILED,
	ISCSI_CONN_ERROR_HEADER_DIGEST_ERROR,
	ISCSI_CONN_ERROR_LOCAL_COMPLETION_ERROR,
	ISCSI_CONN_ERROR_DATA_OVERRUN,
	ISCSI_CONN_ERROR_OUT_OF_SGES_ERROR,
	ISCSI_CONN_ERROR_IP_OPTIONS_ERROR,
	ISCSI_CONN_ERROR_PRS_ERRORS,
	ISCSI_CONN_ERROR_CONNECT_INVALID_TCP_OPTION,
	ISCSI_CONN_ERROR_TCP_IP_FRAGMENT_ERROR,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_AHS_LEN,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_AHS_TYPE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_ITT_OUT_OF_RANGE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_TTT_OUT_OF_RANGE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SEG_LEN_EXCEEDS_PDU_SIZE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_OPCODE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_OPCODE_BEFORE_UPDATE,
	ISCSI_CONN_ERROR_UNVALID_NOPIN_DSL,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_CARRIES_NO_DATA,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SN,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_IN_TTT,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_OUT_ITT,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_TTT,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_BUFFER_OFFSET,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_BUFFER_OFFSET_OOO,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_SN,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_0,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_1,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_2,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_LUN,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_F_BIT_ZERO,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_F_BIT_ZERO_S_BIT_ONE,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_EXP_STAT_SN,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DSL_NOT_ZERO,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_DSL,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SEG_LEN_TOO_BIG,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_OUTSTANDING_R2T_COUNT,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DIF_TX,
	ISCSI_CONN_ERROR_SENSE_DATA_LENGTH,
	ISCSI_CONN_ERROR_DATA_PLACEMENT_ERROR,
	ISCSI_CONN_ERROR_INVALID_ITT,
	ISCSI_ERROR_UNKNOWN,
	MAX_ISCSI_ERROR_TYPES
};


enum iscsi_ramrod_cmd_id {
	ISCSI_RAMROD_CMD_ID_UNUSED = 0,
	ISCSI_RAMROD_CMD_ID_INIT_FUNC = 1,
	ISCSI_RAMROD_CMD_ID_DESTROY_FUNC = 2,
	ISCSI_RAMROD_CMD_ID_OFFLOAD_CONN = 3,
	ISCSI_RAMROD_CMD_ID_UPDATE_CONN = 4,
	ISCSI_RAMROD_CMD_ID_TERMINATION_CONN = 5,
	ISCSI_RAMROD_CMD_ID_CLEAR_SQ = 6,
	ISCSI_RAMROD_CMD_ID_MAC_UPDATE = 7,
	MAX_ISCSI_RAMROD_CMD_ID
};

struct iscsi_reg1 {
	__le32 reg1_map;
#define ISCSI_REG1_NUM_SGES_MASK   0xF
#define ISCSI_REG1_NUM_SGES_SHIFT  0
#define ISCSI_REG1_RESERVED1_MASK  0xFFFFFFF
#define ISCSI_REG1_RESERVED1_SHIFT 4
};

union iscsi_seq_num {
	__le16 data_sn;
	__le16 r2t_sn;
};

struct iscsi_spe_conn_mac_update {
	struct iscsi_slow_path_hdr hdr;
	__le16 conn_id;
	__le32 fw_cid;
	__le16 remote_mac_addr_lo;
	__le16 remote_mac_addr_mid;
	__le16 remote_mac_addr_hi;
	u8 reserved0[2];
};

struct iscsi_spe_conn_offload {
	struct iscsi_slow_path_hdr hdr;
	__le16 conn_id;
	__le32 fw_cid;
	struct iscsi_conn_offload_params iscsi;
	struct tcp_offload_params tcp;
};

struct iscsi_spe_conn_offload_option2 {
	struct iscsi_slow_path_hdr hdr;
	__le16 conn_id;
	__le32 fw_cid;
	struct iscsi_conn_offload_params iscsi;
	struct tcp_offload_params_opt2 tcp;
};

struct iscsi_spe_conn_termination {
	struct iscsi_slow_path_hdr hdr;
	__le16 conn_id;
	__le32 fw_cid;
	u8 abortive;
	u8 reserved0[7];
	struct regpair queue_cnts_addr;
	struct regpair query_params_addr;
};

struct iscsi_spe_func_dstry {
	struct iscsi_slow_path_hdr hdr;
	__le16 reserved0;
	__le32 reserved1;
};

struct iscsi_spe_func_init {
	struct iscsi_slow_path_hdr hdr;
	__le16 half_way_close_timeout;
	u8 num_sq_pages_in_ring;
	u8 num_r2tq_pages_in_ring;
	u8 num_uhq_pages_in_ring;
	u8 ll2_rx_queue_id;
	u8 ooo_enable;
	struct iscsi_debug_modes debug_mode;
	__le16 reserved1;
	__le32 reserved2;
	__le32 reserved3;
	__le32 reserved4;
	struct scsi_init_func_params func_params;
	struct scsi_init_func_queues q_params;
};

struct ystorm_iscsi_task_state {
	struct scsi_cached_sges data_desc;
	struct scsi_sgl_params sgl_params;
	__le32 exp_r2t_sn;
	__le32 buffer_offset;
	union iscsi_seq_num seq_num;
	struct iscsi_dif_flags dif_flags;
	u8 flags;
#define YSTORM_ISCSI_TASK_STATE_LOCAL_COMP_MASK  0x1
#define YSTORM_ISCSI_TASK_STATE_LOCAL_COMP_SHIFT 0
#define YSTORM_ISCSI_TASK_STATE_SLOW_IO_MASK     0x1
#define YSTORM_ISCSI_TASK_STATE_SLOW_IO_SHIFT    1
#define YSTORM_ISCSI_TASK_STATE_RESERVED0_MASK   0x3F
#define YSTORM_ISCSI_TASK_STATE_RESERVED0_SHIFT  2
};

struct ystorm_iscsi_task_rxmit_opt {
	__le32 fast_rxmit_sge_offset;
	__le32 scan_start_buffer_offset;
	__le32 fast_rxmit_buffer_offset;
	u8 scan_start_sgl_index;
	u8 fast_rxmit_sgl_index;
	__le16 reserved;
};

struct ystorm_iscsi_task_st_ctx {
	struct ystorm_iscsi_task_state state;
	struct ystorm_iscsi_task_rxmit_opt rxmit_opt;
	union iscsi_task_hdr pdu_hdr;
};

struct ystorm_iscsi_task_ag_ctx {
	u8 reserved;
	u8 byte1;
	__le16 word0;
	u8 flags0;
#define YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK     0xF
#define YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT    0
#define YSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK        0x1
#define YSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT       4
#define YSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK        0x1
#define YSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT       5
#define YSTORM_ISCSI_TASK_AG_CTX_VALID_MASK       0x1
#define YSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT      6
#define YSTORM_ISCSI_TASK_AG_CTX_BIT3_MASK        0x1
#define YSTORM_ISCSI_TASK_AG_CTX_BIT3_SHIFT       7
	u8 flags1;
#define YSTORM_ISCSI_TASK_AG_CTX_CF0_MASK         0x3
#define YSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT        0
#define YSTORM_ISCSI_TASK_AG_CTX_CF1_MASK         0x3
#define YSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT        2
#define YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_MASK  0x3
#define YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_SHIFT 4
#define YSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK       0x1
#define YSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT      6
#define YSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK       0x1
#define YSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT      7
	u8 flags2;
#define YSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK        0x1
#define YSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT       0
#define YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT    1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT    2
#define YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT    3
#define YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT    4
#define YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT    5
#define YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT    6
#define YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK     0x1
#define YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT    7
	u8 byte2;
	__le32 TTT;
	u8 byte3;
	u8 byte4;
	__le16 word1;
};

struct mstorm_iscsi_task_ag_ctx {
	u8 cdu_validation;
	u8 byte1;
	__le16 task_cid;
	u8 flags0;
#define MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK     0xF
#define MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT    0
#define MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK        0x1
#define MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT       4
#define MSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                0x1
#define MSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT               5
#define MSTORM_ISCSI_TASK_AG_CTX_VALID_MASK               0x1
#define MSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT              6
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_MASK   0x1
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_SHIFT  7
	u8 flags1;
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_MASK     0x3
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_SHIFT    0
#define MSTORM_ISCSI_TASK_AG_CTX_CF1_MASK                 0x3
#define MSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT                2
#define MSTORM_ISCSI_TASK_AG_CTX_CF2_MASK                 0x3
#define MSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT                4
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_MASK  0x1
#define MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_SHIFT 6
#define MSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK               0x1
#define MSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT              7
	u8 flags2;
#define MSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK               0x1
#define MSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT              0
#define MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT            1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT            2
#define MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT            3
#define MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT            4
#define MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT            5
#define MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT            6
#define MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK             0x1
#define MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT            7
	u8 byte2;
	__le32 reg0;
	u8 byte3;
	u8 byte4;
	__le16 word1;
};

struct ustorm_iscsi_task_ag_ctx {
	u8 reserved;
	u8 state;
	__le16 icid;
	u8 flags0;
#define USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK        0xF
#define USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT       0
#define USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK           0x1
#define USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT          4
#define USTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                   0x1
#define USTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT                  5
#define USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_MASK          0x3
#define USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_SHIFT         6
	u8 flags1;
#define USTORM_ISCSI_TASK_AG_CTX_RESERVED1_MASK              0x3
#define USTORM_ISCSI_TASK_AG_CTX_RESERVED1_SHIFT             0
#define USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_MASK               0x3
#define USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_SHIFT              2
#define USTORM_ISCSI_TASK_AG_CTX_CF3_MASK                    0x3
#define USTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT                   4
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_MASK           0x3
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_SHIFT          6
	u8 flags2;
#define USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_MASK       0x1
#define USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_SHIFT      0
#define USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_MASK     0x1
#define USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_SHIFT    1
#define USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_MASK            0x1
#define USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_SHIFT           2
#define USTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK                  0x1
#define USTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT                 3
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK        0x1
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT       4
#define USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_MASK  0x1
#define USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_SHIFT 5
#define USTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK                0x1
#define USTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT               6
#define USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_MASK    0x1
#define USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_SHIFT   7
	u8 flags3;
#define USTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK                0x1
#define USTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT               0
#define USTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK                0x1
#define USTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT               1
#define USTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK                0x1
#define USTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT               2
#define USTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK                0x1
#define USTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT               3
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_MASK         0xF
#define USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT        4
	__le32 dif_err_intervals;
	__le32 dif_error_1st_interval;
	__le32 rcv_cont_len;
	__le32 exp_cont_len;
	__le32 total_data_acked;
	__le32 exp_data_acked;
	u8 next_tid_valid;
	u8 byte3;
	__le16 word1;
	__le16 next_tid;
	__le16 word3;
	__le32 hdr_residual_count;
	__le32 exp_r2t_sn;
};

struct mstorm_iscsi_task_st_ctx {
	struct scsi_cached_sges data_desc;
	struct scsi_sgl_params sgl_params;
	__le32 rem_task_size;
	__le32 data_buffer_offset;
	u8 task_type;
	struct iscsi_dif_flags dif_flags;
	u8 reserved0[2];
	struct regpair sense_db;
	__le32 expected_itt;
	__le32 reserved1;
};

struct ustorm_iscsi_task_st_ctx {
	__le32 rem_rcv_len;
	__le32 exp_data_transfer_len;
	__le32 exp_data_sn;
	struct regpair lun;
	struct iscsi_reg1 reg1;
	u8 flags2;
#define USTORM_ISCSI_TASK_ST_CTX_AHS_EXIST_MASK             0x1
#define USTORM_ISCSI_TASK_ST_CTX_AHS_EXIST_SHIFT            0
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED1_MASK             0x7F
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED1_SHIFT            1
	struct iscsi_dif_flags dif_flags;
	__le16 reserved3;
	__le32 reserved4;
	__le32 reserved5;
	__le32 reserved6;
	__le32 reserved7;
	u8 task_type;
	u8 error_flags;
#define USTORM_ISCSI_TASK_ST_CTX_DATA_DIGEST_ERROR_MASK     0x1
#define USTORM_ISCSI_TASK_ST_CTX_DATA_DIGEST_ERROR_SHIFT    0
#define USTORM_ISCSI_TASK_ST_CTX_DATA_TRUNCATED_ERROR_MASK  0x1
#define USTORM_ISCSI_TASK_ST_CTX_DATA_TRUNCATED_ERROR_SHIFT 1
#define USTORM_ISCSI_TASK_ST_CTX_UNDER_RUN_ERROR_MASK       0x1
#define USTORM_ISCSI_TASK_ST_CTX_UNDER_RUN_ERROR_SHIFT      2
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED8_MASK             0x1F
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED8_SHIFT            3
	u8 flags;
#define USTORM_ISCSI_TASK_ST_CTX_CQE_WRITE_MASK             0x3
#define USTORM_ISCSI_TASK_ST_CTX_CQE_WRITE_SHIFT            0
#define USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP_MASK            0x1
#define USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP_SHIFT           2
#define USTORM_ISCSI_TASK_ST_CTX_Q0_R2TQE_WRITE_MASK        0x1
#define USTORM_ISCSI_TASK_ST_CTX_Q0_R2TQE_WRITE_SHIFT       3
#define USTORM_ISCSI_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_MASK  0x1
#define USTORM_ISCSI_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_SHIFT 4
#define USTORM_ISCSI_TASK_ST_CTX_HQ_SCANNED_DONE_MASK        0x1
#define USTORM_ISCSI_TASK_ST_CTX_HQ_SCANNED_DONE_SHIFT       5
#define USTORM_ISCSI_TASK_ST_CTX_R2T2RECV_DONE_MASK         0x1
#define USTORM_ISCSI_TASK_ST_CTX_R2T2RECV_DONE_SHIFT        6
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED0_MASK             0x1
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED0_SHIFT            7
	u8 cq_rss_number;
};

struct iscsi_task_context {
	struct ystorm_iscsi_task_st_ctx ystorm_st_context;
	struct ystorm_iscsi_task_ag_ctx ystorm_ag_context;
	struct regpair ystorm_ag_padding[2];
	struct tdif_task_context tdif_context;
	struct mstorm_iscsi_task_ag_ctx mstorm_ag_context;
	struct regpair mstorm_ag_padding[2];
	struct ustorm_iscsi_task_ag_ctx ustorm_ag_context;
	struct mstorm_iscsi_task_st_ctx mstorm_st_context;
	struct ustorm_iscsi_task_st_ctx ustorm_st_context;
	struct rdif_task_context rdif_context;
};

enum iscsi_task_type {
	ISCSI_TASK_TYPE_INITIATOR_WRITE,
	ISCSI_TASK_TYPE_INITIATOR_READ,
	ISCSI_TASK_TYPE_MIDPATH,
	ISCSI_TASK_TYPE_UNSOLIC,
	ISCSI_TASK_TYPE_EXCHCLEANUP,
	ISCSI_TASK_TYPE_IRRELEVANT,
	ISCSI_TASK_TYPE_TARGET_WRITE,
	ISCSI_TASK_TYPE_TARGET_READ,
	ISCSI_TASK_TYPE_TARGET_RESPONSE,
	ISCSI_TASK_TYPE_LOGIN_RESPONSE,
	MAX_ISCSI_TASK_TYPE
};

union iscsi_ttt_txlen_union {
	__le32 desired_tx_len;
	__le32 ttt;
};

struct iscsi_uhqe {
	__le32 reg1;
#define ISCSI_UHQE_PDU_PAYLOAD_LEN_MASK     0xFFFFF
#define ISCSI_UHQE_PDU_PAYLOAD_LEN_SHIFT    0
#define ISCSI_UHQE_LOCAL_COMP_MASK          0x1
#define ISCSI_UHQE_LOCAL_COMP_SHIFT         20
#define ISCSI_UHQE_TOGGLE_BIT_MASK          0x1
#define ISCSI_UHQE_TOGGLE_BIT_SHIFT         21
#define ISCSI_UHQE_PURE_PAYLOAD_MASK        0x1
#define ISCSI_UHQE_PURE_PAYLOAD_SHIFT       22
#define ISCSI_UHQE_LOGIN_RESPONSE_PDU_MASK  0x1
#define ISCSI_UHQE_LOGIN_RESPONSE_PDU_SHIFT 23
#define ISCSI_UHQE_TASK_ID_HI_MASK          0xFF
#define ISCSI_UHQE_TASK_ID_HI_SHIFT         24
	__le32 reg2;
#define ISCSI_UHQE_BUFFER_OFFSET_MASK       0xFFFFFF
#define ISCSI_UHQE_BUFFER_OFFSET_SHIFT      0
#define ISCSI_UHQE_TASK_ID_LO_MASK          0xFF
#define ISCSI_UHQE_TASK_ID_LO_SHIFT         24
};


struct iscsi_wqe {
	__le16 task_id;
	u8 flags;
#define ISCSI_WQE_WQE_TYPE_MASK        0x7
#define ISCSI_WQE_WQE_TYPE_SHIFT       0
#define ISCSI_WQE_NUM_SGES_MASK  0xF
#define ISCSI_WQE_NUM_SGES_SHIFT 3
#define ISCSI_WQE_RESPONSE_MASK        0x1
#define ISCSI_WQE_RESPONSE_SHIFT       7
	struct iscsi_dif_flags prot_flags;
	__le32 contlen_cdbsize;
#define ISCSI_WQE_CONT_LEN_MASK  0xFFFFFF
#define ISCSI_WQE_CONT_LEN_SHIFT 0
#define ISCSI_WQE_CDB_SIZE_MASK  0xFF
#define ISCSI_WQE_CDB_SIZE_SHIFT 24
};

enum iscsi_wqe_type {
	ISCSI_WQE_TYPE_NORMAL,
	ISCSI_WQE_TYPE_TASK_CLEANUP,
	ISCSI_WQE_TYPE_MIDDLE_PATH,
	ISCSI_WQE_TYPE_LOGIN,
	ISCSI_WQE_TYPE_FIRST_R2T_CONT,
	ISCSI_WQE_TYPE_NONFIRST_R2T_CONT,
	ISCSI_WQE_TYPE_RESPONSE,
	MAX_ISCSI_WQE_TYPE
};

struct iscsi_xhqe {
	union iscsi_ttt_txlen_union ttt_or_txlen;
	__le32 exp_stat_sn;
	struct iscsi_dif_flags prot_flags;
	u8 total_ahs_length;
	u8 opcode;
	u8 flags;
#define ISCSI_XHQE_FINAL_MASK       0x1
#define ISCSI_XHQE_FINAL_SHIFT      0
#define ISCSI_XHQE_STATUS_BIT_MASK  0x1
#define ISCSI_XHQE_STATUS_BIT_SHIFT 1
#define ISCSI_XHQE_NUM_SGES_MASK    0xF
#define ISCSI_XHQE_NUM_SGES_SHIFT   2
#define ISCSI_XHQE_RESERVED0_MASK   0x3
#define ISCSI_XHQE_RESERVED0_SHIFT  6
	union iscsi_seq_num seq_num;
	__le16 reserved1;
};

struct mstorm_iscsi_stats_drv {
	struct regpair iscsi_rx_dropped_pdus_task_not_valid;
};

struct pstorm_iscsi_stats_drv {
	struct regpair iscsi_tx_bytes_cnt;
	struct regpair iscsi_tx_packet_cnt;
};

struct tstorm_iscsi_stats_drv {
	struct regpair iscsi_rx_bytes_cnt;
	struct regpair iscsi_rx_packet_cnt;
	struct regpair iscsi_rx_new_ooo_isle_events_cnt;
	__le32 iscsi_cmdq_threshold_cnt;
	__le32 iscsi_rq_threshold_cnt;
	__le32 iscsi_immq_threshold_cnt;
};

struct ustorm_iscsi_stats_drv {
	struct regpair iscsi_rx_data_pdu_cnt;
	struct regpair iscsi_rx_r2t_pdu_cnt;
	struct regpair iscsi_rx_total_pdu_cnt;
};

struct xstorm_iscsi_stats_drv {
	struct regpair iscsi_tx_go_to_slow_start_event_cnt;
	struct regpair iscsi_tx_fast_retransmit_event_cnt;
};

struct ystorm_iscsi_stats_drv {
	struct regpair iscsi_tx_data_pdu_cnt;
	struct regpair iscsi_tx_r2t_pdu_cnt;
	struct regpair iscsi_tx_total_pdu_cnt;
};

struct tstorm_iscsi_task_ag_ctx {
	u8 byte0;
	u8 byte1;
	__le16 word0;
	u8 flags0;
#define TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK  0xF
#define TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT 0
#define TSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT    4
#define TSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT    5
#define TSTORM_ISCSI_TASK_AG_CTX_BIT2_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT2_SHIFT    6
#define TSTORM_ISCSI_TASK_AG_CTX_BIT3_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT3_SHIFT    7
	u8 flags1;
#define TSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT    0
#define TSTORM_ISCSI_TASK_AG_CTX_BIT5_MASK     0x1
#define TSTORM_ISCSI_TASK_AG_CTX_BIT5_SHIFT    1
#define TSTORM_ISCSI_TASK_AG_CTX_CF0_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT     2
#define TSTORM_ISCSI_TASK_AG_CTX_CF1_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT     4
#define TSTORM_ISCSI_TASK_AG_CTX_CF2_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT     6
	u8 flags2;
#define TSTORM_ISCSI_TASK_AG_CTX_CF3_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT     0
#define TSTORM_ISCSI_TASK_AG_CTX_CF4_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF4_SHIFT     2
#define TSTORM_ISCSI_TASK_AG_CTX_CF5_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF5_SHIFT     4
#define TSTORM_ISCSI_TASK_AG_CTX_CF6_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF6_SHIFT     6
	u8 flags3;
#define TSTORM_ISCSI_TASK_AG_CTX_CF7_MASK      0x3
#define TSTORM_ISCSI_TASK_AG_CTX_CF7_SHIFT     0
#define TSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT   2
#define TSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT   3
#define TSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT   4
#define TSTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT   5
#define TSTORM_ISCSI_TASK_AG_CTX_CF4EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF4EN_SHIFT   6
#define TSTORM_ISCSI_TASK_AG_CTX_CF5EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF5EN_SHIFT   7
	u8 flags4;
#define TSTORM_ISCSI_TASK_AG_CTX_CF6EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF6EN_SHIFT   0
#define TSTORM_ISCSI_TASK_AG_CTX_CF7EN_MASK    0x1
#define TSTORM_ISCSI_TASK_AG_CTX_CF7EN_SHIFT   1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT 2
#define TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT 3
#define TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT 4
#define TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT 5
#define TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT 6
#define TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK  0x1
#define TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT 7
	u8 byte2;
	__le16 word1;
	__le32 reg0;
	u8 byte3;
	u8 byte4;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg1;
	__le32 reg2;
};
struct iscsi_db_data {
	u8 params;
#define ISCSI_DB_DATA_DEST_MASK         0x3
#define ISCSI_DB_DATA_DEST_SHIFT        0
#define ISCSI_DB_DATA_AGG_CMD_MASK      0x3
#define ISCSI_DB_DATA_AGG_CMD_SHIFT     2
#define ISCSI_DB_DATA_BYPASS_EN_MASK    0x1
#define ISCSI_DB_DATA_BYPASS_EN_SHIFT   4
#define ISCSI_DB_DATA_RESERVED_MASK     0x1
#define ISCSI_DB_DATA_RESERVED_SHIFT    5
#define ISCSI_DB_DATA_AGG_VAL_SEL_MASK  0x3
#define ISCSI_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8 agg_flags;
	__le16 sq_prod;
};

#endif /* __ISCSI_COMMON__ */

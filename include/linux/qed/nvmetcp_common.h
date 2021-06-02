/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef __NVMETCP_COMMON__
#define __NVMETCP_COMMON__

#include "tcp_common.h"
#include <linux/nvme-tcp.h>

#define NVMETCP_SLOW_PATH_LAYER_CODE (6)
#define NVMETCP_WQE_NUM_SGES_SLOWIO (0xf)

/* NVMeTCP firmware function init parameters */
struct nvmetcp_spe_func_init {
	__le16 half_way_close_timeout;
	u8 num_sq_pages_in_ring;
	u8 num_r2tq_pages_in_ring;
	u8 num_uhq_pages_in_ring;
	u8 ll2_rx_queue_id;
	u8 flags;
#define NVMETCP_SPE_FUNC_INIT_COUNTERS_EN_MASK 0x1
#define NVMETCP_SPE_FUNC_INIT_COUNTERS_EN_SHIFT 0
#define NVMETCP_SPE_FUNC_INIT_NVMETCP_MODE_MASK 0x1
#define NVMETCP_SPE_FUNC_INIT_NVMETCP_MODE_SHIFT 1
#define NVMETCP_SPE_FUNC_INIT_RESERVED0_MASK 0x3F
#define NVMETCP_SPE_FUNC_INIT_RESERVED0_SHIFT 2
	u8 debug_flags;
	__le16 reserved1;
	u8 params;
#define NVMETCP_SPE_FUNC_INIT_MAX_SYN_RT_MASK	0xF
#define NVMETCP_SPE_FUNC_INIT_MAX_SYN_RT_SHIFT	0
#define NVMETCP_SPE_FUNC_INIT_RESERVED1_MASK	0xF
#define NVMETCP_SPE_FUNC_INIT_RESERVED1_SHIFT	4
	u8 reserved2[5];
	struct scsi_init_func_params func_params;
	struct scsi_init_func_queues q_params;
};

/* NVMeTCP init params passed by driver to FW in NVMeTCP init ramrod. */
struct nvmetcp_init_ramrod_params {
	struct nvmetcp_spe_func_init nvmetcp_init_spe;
	struct tcp_init_params tcp_init;
};

/* NVMeTCP Ramrod Command IDs */
enum nvmetcp_ramrod_cmd_id {
	NVMETCP_RAMROD_CMD_ID_UNUSED = 0,
	NVMETCP_RAMROD_CMD_ID_INIT_FUNC = 1,
	NVMETCP_RAMROD_CMD_ID_DESTROY_FUNC = 2,
	NVMETCP_RAMROD_CMD_ID_OFFLOAD_CONN = 3,
	NVMETCP_RAMROD_CMD_ID_UPDATE_CONN = 4,
	NVMETCP_RAMROD_CMD_ID_TERMINATION_CONN = 5,
	NVMETCP_RAMROD_CMD_ID_CLEAR_SQ = 6,
	MAX_NVMETCP_RAMROD_CMD_ID
};

struct nvmetcp_glbl_queue_entry {
	struct regpair cq_pbl_addr;
	struct regpair reserved;
};

/* NVMeTCP conn level EQEs */
enum nvmetcp_eqe_opcode {
	NVMETCP_EVENT_TYPE_INIT_FUNC = 0, /* Response after init Ramrod */
	NVMETCP_EVENT_TYPE_DESTROY_FUNC, /* Response after destroy Ramrod */
	NVMETCP_EVENT_TYPE_OFFLOAD_CONN,/* Response after option 2 offload Ramrod */
	NVMETCP_EVENT_TYPE_UPDATE_CONN, /* Response after update Ramrod */
	NVMETCP_EVENT_TYPE_CLEAR_SQ, /* Response after clear sq Ramrod */
	NVMETCP_EVENT_TYPE_TERMINATE_CONN, /* Response after termination Ramrod */
	NVMETCP_EVENT_TYPE_RESERVED0,
	NVMETCP_EVENT_TYPE_RESERVED1,
	NVMETCP_EVENT_TYPE_ASYN_CONNECT_COMPLETE, /* Connect completed (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_TERMINATE_DONE, /* Termination completed (A-syn EQE) */
	NVMETCP_EVENT_TYPE_START_OF_ERROR_TYPES = 10, /* Separate EQs from err EQs */
	NVMETCP_EVENT_TYPE_ASYN_ABORT_RCVD, /* TCP RST packet receive (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_CLOSE_RCVD, /* TCP FIN packet receive (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_SYN_RCVD, /* TCP SYN+ACK packet receive (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_MAX_RT_TIME, /* TCP max retransmit time (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_MAX_RT_CNT, /* TCP max retransmit count (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_MAX_KA_PROBES_CNT, /* TCP ka probes count (A-syn EQE) */
	NVMETCP_EVENT_TYPE_ASYN_FIN_WAIT2, /* TCP fin wait 2 (A-syn EQE) */
	NVMETCP_EVENT_TYPE_NVMETCP_CONN_ERROR, /* NVMeTCP error response (A-syn EQE) */
	NVMETCP_EVENT_TYPE_TCP_CONN_ERROR, /* NVMeTCP error - tcp error (A-syn EQE) */
	MAX_NVMETCP_EQE_OPCODE
};

struct nvmetcp_conn_offload_section {
	struct regpair cccid_itid_table_addr; /* CCCID to iTID table address */
	__le16 cccid_max_range; /* CCCID max value - used for validation */
	__le16 reserved[3];
};

/* NVMe TCP connection offload params passed by driver to FW in NVMeTCP offload ramrod */
struct nvmetcp_conn_offload_params {
	struct regpair sq_pbl_addr;
	struct regpair r2tq_pbl_addr;
	struct regpair xhq_pbl_addr;
	struct regpair uhq_pbl_addr;
	__le16 physical_q0;
	__le16 physical_q1;
	u8 flags;
#define NVMETCP_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_MASK 0x1
#define NVMETCP_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_SHIFT 0
#define NVMETCP_CONN_OFFLOAD_PARAMS_TARGET_MODE_MASK 0x1
#define NVMETCP_CONN_OFFLOAD_PARAMS_TARGET_MODE_SHIFT 1
#define NVMETCP_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_MASK 0x1
#define NVMETCP_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_SHIFT 2
#define NVMETCP_CONN_OFFLOAD_PARAMS_NVMETCP_MODE_MASK 0x1
#define NVMETCP_CONN_OFFLOAD_PARAMS_NVMETCP_MODE_SHIFT 3
#define NVMETCP_CONN_OFFLOAD_PARAMS_RESERVED1_MASK 0xF
#define NVMETCP_CONN_OFFLOAD_PARAMS_RESERVED1_SHIFT 4
	u8 default_cq;
	__le16 reserved0;
	__le32 reserved1;
	__le32 initial_ack;

	struct nvmetcp_conn_offload_section nvmetcp; /* NVMe/TCP section */
};

/* NVMe TCP and TCP connection offload params passed by driver to FW in NVMeTCP offload ramrod. */
struct nvmetcp_spe_conn_offload {
	__le16 reserved;
	__le16 conn_id;
	__le32 fw_cid;
	struct nvmetcp_conn_offload_params nvmetcp;
	struct tcp_offload_params_opt2 tcp;
};

/* NVMeTCP connection update params passed by driver to FW in NVMETCP update ramrod. */
struct nvmetcp_conn_update_ramrod_params {
	__le16 reserved0;
	__le16 conn_id;
	__le32 reserved1;
	u8 flags;
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_HD_EN_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_HD_EN_SHIFT 0
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_DD_EN_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_DD_EN_SHIFT 1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED0_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED0_SHIFT 2
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED1_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED1_DATA_SHIFT 3
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED2_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED2_SHIFT 4
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED3_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED3_SHIFT 5
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED4_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED4_SHIFT 6
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED5_MASK 0x1
#define NVMETCP_CONN_UPDATE_RAMROD_PARAMS_RESERVED5_SHIFT 7
	u8 reserved3[3];
	__le32 max_seq_size;
	__le32 max_send_pdu_length;
	__le32 max_recv_pdu_length;
	__le32 first_seq_length;
	__le32 reserved4[5];
};

/* NVMeTCP connection termination request */
struct nvmetcp_spe_conn_termination {
	__le16 reserved0;
	__le16 conn_id;
	__le32 reserved1;
	u8 abortive;
	u8 reserved2[7];
	struct regpair reserved3;
	struct regpair reserved4;
};

struct nvmetcp_dif_flags {
	u8 flags;
};

enum nvmetcp_wqe_type {
	NVMETCP_WQE_TYPE_NORMAL,
	NVMETCP_WQE_TYPE_TASK_CLEANUP,
	NVMETCP_WQE_TYPE_MIDDLE_PATH,
	NVMETCP_WQE_TYPE_IC,
	MAX_NVMETCP_WQE_TYPE
};

struct nvmetcp_wqe {
	__le16 task_id;
	u8 flags;
#define NVMETCP_WQE_WQE_TYPE_MASK 0x7 /* [use nvmetcp_wqe_type] */
#define NVMETCP_WQE_WQE_TYPE_SHIFT 0
#define NVMETCP_WQE_NUM_SGES_MASK 0xF
#define NVMETCP_WQE_NUM_SGES_SHIFT 3
#define NVMETCP_WQE_RESPONSE_MASK 0x1
#define NVMETCP_WQE_RESPONSE_SHIFT 7
	struct nvmetcp_dif_flags prot_flags;
	__le32 contlen_cdbsize;
#define NVMETCP_WQE_CONT_LEN_MASK 0xFFFFFF
#define NVMETCP_WQE_CONT_LEN_SHIFT 0
#define NVMETCP_WQE_CDB_SIZE_OR_NVMETCP_CMD_MASK 0xFF
#define NVMETCP_WQE_CDB_SIZE_OR_NVMETCP_CMD_SHIFT 24
};

struct nvmetcp_host_cccid_itid_entry {
	__le16 itid;
};

struct nvmetcp_connect_done_results {
	__le16 icid;
	__le16 conn_id;
	struct tcp_ulp_connect_done_params params;
};

struct nvmetcp_eqe_data {
	__le16 icid;
	__le16 conn_id;
	__le16 reserved;
	u8 error_code;
	u8 error_pdu_opcode_reserved;
#define NVMETCP_EQE_DATA_ERROR_PDU_OPCODE_MASK 0x3F
#define NVMETCP_EQE_DATA_ERROR_PDU_OPCODE_SHIFT  0
#define NVMETCP_EQE_DATA_ERROR_PDU_OPCODE_VALID_MASK  0x1
#define NVMETCP_EQE_DATA_ERROR_PDU_OPCODE_VALID_SHIFT  6
#define NVMETCP_EQE_DATA_RESERVED0_MASK 0x1
#define NVMETCP_EQE_DATA_RESERVED0_SHIFT 7
};

enum nvmetcp_task_type {
	NVMETCP_TASK_TYPE_HOST_WRITE,
	NVMETCP_TASK_TYPE_HOST_READ,
	NVMETCP_TASK_TYPE_INIT_CONN_REQUEST,
	NVMETCP_TASK_TYPE_RESERVED0,
	NVMETCP_TASK_TYPE_CLEANUP,
	NVMETCP_TASK_TYPE_HOST_READ_NO_CQE,
	MAX_NVMETCP_TASK_TYPE
};

struct nvmetcp_db_data {
	u8 params;
#define NVMETCP_DB_DATA_DEST_MASK 0x3 /* destination of doorbell (use enum db_dest) */
#define NVMETCP_DB_DATA_DEST_SHIFT 0
#define NVMETCP_DB_DATA_AGG_CMD_MASK 0x3 /* aggregative command to CM (use enum db_agg_cmd_sel) */
#define NVMETCP_DB_DATA_AGG_CMD_SHIFT 2
#define NVMETCP_DB_DATA_BYPASS_EN_MASK 0x1 /* enable QM bypass */
#define NVMETCP_DB_DATA_BYPASS_EN_SHIFT 4
#define NVMETCP_DB_DATA_RESERVED_MASK 0x1
#define NVMETCP_DB_DATA_RESERVED_SHIFT 5
#define NVMETCP_DB_DATA_AGG_VAL_SEL_MASK 0x3 /* aggregative value selection */
#define NVMETCP_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8 agg_flags; /* bit for every DQ counter flags in CM context that DQ can increment */
	__le16 sq_prod;
};

struct nvmetcp_fw_nvmf_cqe {
	__le32 reserved[4];
};

struct nvmetcp_icresp_mdata {
	u8  digest;
	u8  cpda;
	__le16  pfv;
	__le32 maxdata;
	__le16 rsvd[4];
};

union nvmetcp_fw_cqe_data {
	struct nvmetcp_fw_nvmf_cqe nvme_cqe;
	struct nvmetcp_icresp_mdata icresp_mdata;
};

struct nvmetcp_fw_cqe {
	__le16 conn_id;
	u8 cqe_type;
	u8 cqe_error_status_bits;
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_MASK 0x7
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_SHIFT 0
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_MASK 0x1
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_SHIFT 3
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_MASK 0x1
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_SHIFT 4
	__le16 itid;
	u8 task_type;
	u8 fw_dbg_field;
	u8 caused_conn_err;
	u8 reserved0[3];
	__le32 reserved1;
	union nvmetcp_fw_cqe_data cqe_data;
	struct regpair task_opaque;
	__le32 reserved[6];
};

enum nvmetcp_fw_cqes_type {
	NVMETCP_FW_CQE_TYPE_NORMAL = 1,
	NVMETCP_FW_CQE_TYPE_RESERVED0,
	NVMETCP_FW_CQE_TYPE_RESERVED1,
	NVMETCP_FW_CQE_TYPE_CLEANUP,
	NVMETCP_FW_CQE_TYPE_DUMMY,
	MAX_NVMETCP_FW_CQES_TYPE
};

struct ystorm_nvmetcp_task_state {
	struct scsi_cached_sges data_desc;
	struct scsi_sgl_params sgl_params;
	__le32 resrved0;
	__le32 buffer_offset;
	__le16 cccid;
	struct nvmetcp_dif_flags dif_flags;
	u8 flags;
#define YSTORM_NVMETCP_TASK_STATE_LOCAL_COMP_MASK 0x1
#define YSTORM_NVMETCP_TASK_STATE_LOCAL_COMP_SHIFT 0
#define YSTORM_NVMETCP_TASK_STATE_SLOW_IO_MASK 0x1
#define YSTORM_NVMETCP_TASK_STATE_SLOW_IO_SHIFT 1
#define YSTORM_NVMETCP_TASK_STATE_SET_DIF_OFFSET_MASK 0x1
#define YSTORM_NVMETCP_TASK_STATE_SET_DIF_OFFSET_SHIFT 2
#define YSTORM_NVMETCP_TASK_STATE_SEND_W_RSP_MASK 0x1
#define YSTORM_NVMETCP_TASK_STATE_SEND_W_RSP_SHIFT 3
};

struct ystorm_nvmetcp_task_rxmit_opt {
	__le32 reserved[4];
};

struct nvmetcp_task_hdr {
	__le32 reg[18];
};

struct nvmetcp_task_hdr_aligned {
	struct nvmetcp_task_hdr task_hdr;
	__le32 reserved[2];	/* HSI_COMMENT: Align to QREG */
};

struct e5_tdif_task_context {
	__le32 reserved[16];
};

struct e5_rdif_task_context {
	__le32 reserved[12];
};

struct ystorm_nvmetcp_task_st_ctx {
	struct ystorm_nvmetcp_task_state state;
	struct ystorm_nvmetcp_task_rxmit_opt rxmit_opt;
	struct nvmetcp_task_hdr_aligned pdu_hdr;
};

struct mstorm_nvmetcp_task_st_ctx {
	struct scsi_cached_sges data_desc;
	struct scsi_sgl_params sgl_params;
	__le32 rem_task_size;
	__le32 data_buffer_offset;
	u8 task_type;
	struct nvmetcp_dif_flags dif_flags;
	__le16 dif_task_icid;
	struct regpair reserved0;
	__le32 expected_itt;
	__le32 reserved1;
};

struct ustorm_nvmetcp_task_st_ctx {
	__le32 rem_rcv_len;
	__le32 exp_data_transfer_len;
	__le32 exp_data_sn;
	struct regpair reserved0;
	__le32 reg1_map;
#define REG1_NUM_SGES_MASK 0xF
#define REG1_NUM_SGES_SHIFT 0
#define REG1_RESERVED1_MASK 0xFFFFFFF
#define REG1_RESERVED1_SHIFT 4
	u8 flags2;
#define USTORM_NVMETCP_TASK_ST_CTX_AHS_EXIST_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_AHS_EXIST_SHIFT 0
#define USTORM_NVMETCP_TASK_ST_CTX_RESERVED1_MASK 0x7F
#define USTORM_NVMETCP_TASK_ST_CTX_RESERVED1_SHIFT 1
	struct nvmetcp_dif_flags dif_flags;
	__le16 reserved3;
	__le16 tqe_opaque[2];
	__le32 reserved5;
	__le32 nvme_tcp_opaque_lo;
	__le32 nvme_tcp_opaque_hi;
	u8 task_type;
	u8 error_flags;
#define USTORM_NVMETCP_TASK_ST_CTX_DATA_DIGEST_ERROR_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_DATA_DIGEST_ERROR_SHIFT 0
#define USTORM_NVMETCP_TASK_ST_CTX_DATA_TRUNCATED_ERROR_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_DATA_TRUNCATED_ERROR_SHIFT 1
#define USTORM_NVMETCP_TASK_ST_CTX_UNDER_RUN_ERROR_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_UNDER_RUN_ERROR_SHIFT 2
#define USTORM_NVMETCP_TASK_ST_CTX_NVME_TCP_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_NVME_TCP_SHIFT 3
	u8 flags;
#define USTORM_NVMETCP_TASK_ST_CTX_CQE_WRITE_MASK 0x3
#define USTORM_NVMETCP_TASK_ST_CTX_CQE_WRITE_SHIFT 0
#define USTORM_NVMETCP_TASK_ST_CTX_LOCAL_COMP_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_LOCAL_COMP_SHIFT 2
#define USTORM_NVMETCP_TASK_ST_CTX_Q0_R2TQE_WRITE_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_Q0_R2TQE_WRITE_SHIFT 3
#define USTORM_NVMETCP_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_SHIFT 4
#define USTORM_NVMETCP_TASK_ST_CTX_HQ_SCANNED_DONE_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_HQ_SCANNED_DONE_SHIFT 5
#define USTORM_NVMETCP_TASK_ST_CTX_R2T2RECV_DONE_MASK 0x1
#define USTORM_NVMETCP_TASK_ST_CTX_R2T2RECV_DONE_SHIFT 6
	u8 cq_rss_number;
};

struct e5_ystorm_nvmetcp_task_ag_ctx {
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 word0 /* icid */;
	u8 flags0;
	u8 flags1;
	u8 flags2;
	u8 flags3;
	__le32 TTT;
	u8 byte2;
	u8 byte3;
	u8 byte4;
	u8 e4_reserved7;
};

struct e5_mstorm_nvmetcp_task_ag_ctx {
	u8 cdu_validation;
	u8 byte1;
	__le16 task_cid;
	u8 flags0;
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CONNECTION_TYPE_MASK 0xF
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_EXIST_IN_QM0_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_EXIST_IN_QM0_SHIFT 4
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CONN_CLEAR_SQ_FLAG_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CONN_CLEAR_SQ_FLAG_SHIFT 5
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_VALID_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_VALID_SHIFT 6
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_FLAG_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_FLAG_SHIFT 7
	u8 flags1;
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_CF_MASK 0x3
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_CF_SHIFT 0
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF1_MASK 0x3
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF1_SHIFT 2
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF2_MASK 0x3
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF2_SHIFT 4
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_CF_EN_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_TASK_CLEANUP_CF_EN_SHIFT 6
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF1EN_MASK 0x1
#define E5_MSTORM_NVMETCP_TASK_AG_CTX_CF1EN_SHIFT 7
	u8 flags2;
	u8 flags3;
	__le32 reg0;
	u8 byte2;
	u8 byte3;
	u8 byte4;
	u8 e4_reserved7;
};

struct e5_ustorm_nvmetcp_task_ag_ctx {
	u8 reserved;
	u8 state_and_core_id;
	__le16 icid;
	u8 flags0;
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CONNECTION_TYPE_MASK 0xF
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E5_USTORM_NVMETCP_TASK_AG_CTX_EXIST_IN_QM0_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_EXIST_IN_QM0_SHIFT 4
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CONN_CLEAR_SQ_FLAG_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CONN_CLEAR_SQ_FLAG_SHIFT 5
#define E5_USTORM_NVMETCP_TASK_AG_CTX_HQ_SCANNED_CF_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_HQ_SCANNED_CF_SHIFT 6
	u8 flags1;
#define E5_USTORM_NVMETCP_TASK_AG_CTX_RESERVED1_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_RESERVED1_SHIFT 0
#define E5_USTORM_NVMETCP_TASK_AG_CTX_R2T2RECV_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_R2T2RECV_SHIFT 2
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CF3_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CF3_SHIFT 4
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_CF_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_CF_SHIFT 6
	u8 flags2;
#define E5_USTORM_NVMETCP_TASK_AG_CTX_HQ_SCANNED_CF_EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_HQ_SCANNED_CF_EN_SHIFT 0
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DISABLE_DATA_ACKED_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DISABLE_DATA_ACKED_SHIFT 1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_R2T2RECV_EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_R2T2RECV_EN_SHIFT 2
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CF3EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CF3EN_SHIFT 3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT 4
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_SHIFT 5
#define E5_USTORM_NVMETCP_TASK_AG_CTX_RULE1EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_RULE1EN_SHIFT 6
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_SHIFT 7
	u8 flags3;
	u8 flags4;
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED5_MASK 0x3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED5_SHIFT 0
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED6_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED6_SHIFT 2
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED7_MASK 0x1
#define E5_USTORM_NVMETCP_TASK_AG_CTX_E4_RESERVED7_SHIFT 3
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_TYPE_MASK 0xF
#define E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT 4
	u8 byte2;
	u8 byte3;
	u8 e4_reserved8;
	__le32 dif_err_intervals;
	__le32 dif_error_1st_interval;
	__le32 rcv_cont_len;
	__le32 exp_cont_len;
	__le32 total_data_acked;
	__le32 exp_data_acked;
	__le16 word1;
	__le16 next_tid;
	__le32 hdr_residual_count;
	__le32 exp_r2t_sn;
};

struct e5_nvmetcp_task_context {
	struct ystorm_nvmetcp_task_st_ctx ystorm_st_context;
	struct e5_ystorm_nvmetcp_task_ag_ctx ystorm_ag_context;
	struct regpair ystorm_ag_padding[2];
	struct e5_tdif_task_context tdif_context;
	struct e5_mstorm_nvmetcp_task_ag_ctx mstorm_ag_context;
	struct regpair mstorm_ag_padding[2];
	struct e5_ustorm_nvmetcp_task_ag_ctx ustorm_ag_context;
	struct regpair ustorm_ag_padding[2];
	struct mstorm_nvmetcp_task_st_ctx mstorm_st_context;
	struct regpair mstorm_st_padding[2];
	struct ustorm_nvmetcp_task_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
	struct e5_rdif_task_context rdif_context;
};

#endif /* __NVMETCP_COMMON__*/

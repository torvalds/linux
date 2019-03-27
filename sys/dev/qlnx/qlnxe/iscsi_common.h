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


#ifndef __ISCSI_COMMON__
#define __ISCSI_COMMON__ 
/**********************/
/* ISCSI FW CONSTANTS */
/**********************/

/* iSCSI HSI constants */
#define ISCSI_DEFAULT_MTU	(1500)

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
#define ISCSI_MAX_VAL_MAX_OUTSTANDING_R2T   (0xff) // 0x10000 according to RFC

#define ISCSI_AHS_CNTL_SIZE 4 

#define ISCSI_WQE_NUM_SGES_SLOWIO           (0xf)

/* iSCSI reserved params */
#define ISCSI_ITT_ALL_ONES					(0xffffffff)
#define ISCSI_TTT_ALL_ONES					(0xffffffff)

#define ISCSI_OPTION_1_OFF_CHIP_TCP 1
#define ISCSI_OPTION_2_ON_CHIP_TCP 2

#define ISCSI_INITIATOR_MODE 0
#define ISCSI_TARGET_MODE 1


/* iSCSI request op codes */
#define ISCSI_OPCODE_NOP_OUT				(0)
#define ISCSI_OPCODE_SCSI_CMD       		(1)
#define ISCSI_OPCODE_TMF_REQUEST			(2)
#define ISCSI_OPCODE_LOGIN_REQUEST			(3)
#define ISCSI_OPCODE_TEXT_REQUEST       	(4)
#define ISCSI_OPCODE_DATA_OUT				(5)
#define ISCSI_OPCODE_LOGOUT_REQUEST     	(6)

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


/*
 * Union of data bd_opaque/ tq_tid
 */
union bd_opaque_tq_union
{
	__le16 bd_opaque /* BDs opaque data */;
	__le16 tq_tid /* Immediate Data with DIF TQe TID */;
};


/*
 * ISCSI SGL entry
 */
struct cqe_error_bitmap
{
	u8 cqe_error_status_bits;
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_MASK         0x7 /* Mark task with DIF error (3 bit): [0]-CRC/checksum, [1]-app tag, [2]-reference tag */
#define CQE_ERROR_BITMAP_DIF_ERR_BITS_SHIFT        0
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_MASK      0x1 /* Mark task with data digest error (1 bit) */
#define CQE_ERROR_BITMAP_DATA_DIGEST_ERR_SHIFT     3
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_MASK  0x1 /* Mark receive on invalid connection */
#define CQE_ERROR_BITMAP_RCV_ON_INVALID_CONN_SHIFT 4
#define CQE_ERROR_BITMAP_DATA_TRUNCATED_ERR_MASK   0x1 /* Target Mode - Mark middle task error, data truncated */
#define CQE_ERROR_BITMAP_DATA_TRUNCATED_ERR_SHIFT  5
#define CQE_ERROR_BITMAP_UNDER_RUN_ERR_MASK        0x1
#define CQE_ERROR_BITMAP_UNDER_RUN_ERR_SHIFT       6
#define CQE_ERROR_BITMAP_RESERVED2_MASK            0x1
#define CQE_ERROR_BITMAP_RESERVED2_SHIFT           7
};


union cqe_error_status
{
	u8 error_status /* all error bits as uint8 */;
	struct cqe_error_bitmap error_bits /* cqe errors bitmap */;
};


/*
 * iSCSI Login Response PDU header
 */
struct data_hdr
{
	__le32 data[12] /* iscsi header data */;
};


struct lun_mapper_addr_reserved
{
	struct regpair lun_mapper_addr /* Lun mapper address */;
	u8 reserved0[8];
};

/*
 * rdif conetxt for dif on immediate
 */
struct dif_on_immediate_params
{
	__le32 initial_ref_tag;
	__le16 application_tag;
	__le16 application_tag_mask;
	__le16 flags1;
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_GUARD_MASK             0x1
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_GUARD_SHIFT            0
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_APP_TAG_MASK           0x1
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_APP_TAG_SHIFT          1
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_REF_TAG_MASK           0x1
#define DIF_ON_IMMEDIATE_PARAMS_VALIDATE_REF_TAG_SHIFT          2
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_GUARD_MASK              0x1
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_GUARD_SHIFT             3
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_APP_TAG_MASK            0x1
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_APP_TAG_SHIFT           4
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_REF_TAG_MASK            0x1
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_REF_TAG_SHIFT           5
#define DIF_ON_IMMEDIATE_PARAMS_INTERVAL_SIZE_MASK              0x1 /* 0=512B, 1=4KB */
#define DIF_ON_IMMEDIATE_PARAMS_INTERVAL_SIZE_SHIFT             6
#define DIF_ON_IMMEDIATE_PARAMS_NETWORK_INTERFACE_MASK          0x1 /* 0=None, 1=DIF */
#define DIF_ON_IMMEDIATE_PARAMS_NETWORK_INTERFACE_SHIFT         7
#define DIF_ON_IMMEDIATE_PARAMS_HOST_INTERFACE_MASK             0x3 /* 0=None, 1=DIF, 2=DIX */
#define DIF_ON_IMMEDIATE_PARAMS_HOST_INTERFACE_SHIFT            8
#define DIF_ON_IMMEDIATE_PARAMS_REF_TAG_MASK_MASK               0xF /* mask for refernce tag handling */
#define DIF_ON_IMMEDIATE_PARAMS_REF_TAG_MASK_SHIFT              10
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_APP_TAG_WITH_MASK_MASK  0x1 /* Forward application tag with mask */
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_APP_TAG_WITH_MASK_SHIFT 14
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_REF_TAG_WITH_MASK_MASK  0x1 /* Forward reference tag with mask */
#define DIF_ON_IMMEDIATE_PARAMS_FORWARD_REF_TAG_WITH_MASK_SHIFT 15
	u8 flags0;
#define DIF_ON_IMMEDIATE_PARAMS_RESERVED_MASK                   0x1
#define DIF_ON_IMMEDIATE_PARAMS_RESERVED_SHIFT                  0
#define DIF_ON_IMMEDIATE_PARAMS_IGNORE_APP_TAG_MASK             0x1
#define DIF_ON_IMMEDIATE_PARAMS_IGNORE_APP_TAG_SHIFT            1
#define DIF_ON_IMMEDIATE_PARAMS_INITIAL_REF_TAG_IS_VALID_MASK   0x1
#define DIF_ON_IMMEDIATE_PARAMS_INITIAL_REF_TAG_IS_VALID_SHIFT  2
#define DIF_ON_IMMEDIATE_PARAMS_HOST_GUARD_TYPE_MASK            0x1 /* 0 = IP checksum, 1 = CRC */
#define DIF_ON_IMMEDIATE_PARAMS_HOST_GUARD_TYPE_SHIFT           3
#define DIF_ON_IMMEDIATE_PARAMS_PROTECTION_TYPE_MASK            0x3 /* 1/2/3 - Protection Type */
#define DIF_ON_IMMEDIATE_PARAMS_PROTECTION_TYPE_SHIFT           4
#define DIF_ON_IMMEDIATE_PARAMS_CRC_SEED_MASK                   0x1 /* 0=0x0000, 1=0xffff */
#define DIF_ON_IMMEDIATE_PARAMS_CRC_SEED_SHIFT                  6
#define DIF_ON_IMMEDIATE_PARAMS_KEEP_REF_TAG_CONST_MASK         0x1 /* Keep reference tag constant */
#define DIF_ON_IMMEDIATE_PARAMS_KEEP_REF_TAG_CONST_SHIFT        7
	u8 reserved_zero[5];
};

/*
 * iSCSI dif on immediate mode attributes union
 */
union dif_configuration_params
{
	struct lun_mapper_addr_reserved lun_mapper_address /* lun mapper address */;
	struct dif_on_immediate_params def_dif_conf /* default dif on immediate rdif configuration */;
};



/*
 * Union of data/r2t sequence number
 */
union iscsi_seq_num
{
	__le16 data_sn /* data-in sequence number */;
	__le16 r2t_sn /* r2t pdu sequence number */;
};

/*
 * iSCSI DIF flags 
 */
struct iscsi_dif_flags
{
	u8 flags;
#define ISCSI_DIF_FLAGS_PROT_INTERVAL_SIZE_LOG_MASK  0xF /* Protection log interval (9=512 10=1024  11=2048 12=4096 13=8192) */
#define ISCSI_DIF_FLAGS_PROT_INTERVAL_SIZE_LOG_SHIFT 0
#define ISCSI_DIF_FLAGS_DIF_TO_PEER_MASK             0x1 /* If DIF protection is configured against target (0=no, 1=yes) */
#define ISCSI_DIF_FLAGS_DIF_TO_PEER_SHIFT            4
#define ISCSI_DIF_FLAGS_HOST_INTERFACE_MASK          0x7 /* If DIF/DIX protection is configured against the host (0=none, 1=DIF, 2=DIX 2 bytes, 3=DIX 4 bytes, 4=DIX 8 bytes) */
#define ISCSI_DIF_FLAGS_HOST_INTERFACE_SHIFT         5
};

/*
 * The iscsi storm task context of Ystorm
 */
struct ystorm_iscsi_task_state
{
	struct scsi_cached_sges data_desc;
	struct scsi_sgl_params sgl_params;
	__le32 exp_r2t_sn /* Initiator mode - Expected R2T PDU index in sequence. [variable, initialized 0] */;
	__le32 buffer_offset /* Payload data offset */;
	union iscsi_seq_num seq_num /* PDU index in sequence */;
	struct iscsi_dif_flags dif_flags /* Dif flags */;
	u8 flags;
#define YSTORM_ISCSI_TASK_STATE_LOCAL_COMP_MASK      0x1 /* local_completion  */
#define YSTORM_ISCSI_TASK_STATE_LOCAL_COMP_SHIFT     0
#define YSTORM_ISCSI_TASK_STATE_SLOW_IO_MASK         0x1 /* Equals 1 if SGL is predicted and 0 otherwise. */
#define YSTORM_ISCSI_TASK_STATE_SLOW_IO_SHIFT        1
#define YSTORM_ISCSI_TASK_STATE_SET_DIF_OFFSET_MASK  0x1 /* Indication for Ystorm that TDIFs offsetInIo is not synced with buffer_offset */
#define YSTORM_ISCSI_TASK_STATE_SET_DIF_OFFSET_SHIFT 2
#define YSTORM_ISCSI_TASK_STATE_RESERVED0_MASK       0x1F
#define YSTORM_ISCSI_TASK_STATE_RESERVED0_SHIFT      3
};

/*
 * The iscsi storm task context of Ystorm
 */
struct ystorm_iscsi_task_rxmit_opt
{
	__le32 fast_rxmit_sge_offset /* SGE offset from which to continue dummy-read or start fast retransmit */;
	__le32 scan_start_buffer_offset /* Starting buffer offset of next retransmit SGL scan */;
	__le32 fast_rxmit_buffer_offset /* Buffer offset from which to continue dummy-read or start fast retransmit */;
	u8 scan_start_sgl_index /* Starting SGL index of next retransmit SGL scan */;
	u8 fast_rxmit_sgl_index /* SGL index from which to continue dummy-read or start fast retransmit */;
	__le16 reserved /* reserved */;
};

/*
 * iSCSI Common PDU header
 */
struct iscsi_common_hdr
{
	u8 hdr_status /* Status field of ISCSI header */;
	u8 hdr_response /* Response field of ISCSI header for Responses / Reserved for Data-In */;
	u8 hdr_flags /* Flags field of ISCSI header */;
	u8 hdr_first_byte;
#define ISCSI_COMMON_HDR_OPCODE_MASK         0x3F /* Opcode */
#define ISCSI_COMMON_HDR_OPCODE_SHIFT        0
#define ISCSI_COMMON_HDR_IMM_MASK            0x1 /* Immediate */
#define ISCSI_COMMON_HDR_IMM_SHIFT           6
#define ISCSI_COMMON_HDR_RSRV_MASK           0x1 /* first bit of iSCSI PDU header */
#define ISCSI_COMMON_HDR_RSRV_SHIFT          7
	__le32 hdr_second_dword;
#define ISCSI_COMMON_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_COMMON_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_COMMON_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_COMMON_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun_reserved /* Bytes 8..15 : LUN (if PDU contains a LUN field) or reserved */;
	__le32 itt /* ITT - common to all headers */;
	__le32 ttt /* bytes 20 to 23 - common ttt to various PDU headers */;
	__le32 cmdstat_sn /* bytes 24 to 27 - common cmd_sn (initiator) or stat_sn (target) to various PDU headers */;
	__le32 exp_statcmd_sn /* bytes 28 to 31 - common expected stat_sn (initiator) or cmd_sn (target) to various PDU headers */;
	__le32 max_cmd_sn /* bytes 32 to 35 - common max cmd_sn to various PDU headers */;
	__le32 data[3] /* bytes 36 to 47 */;
};

/*
 * iSCSI Command PDU header
 */
struct iscsi_cmd_hdr
{
	__le16 reserved1 /* reserved */;
	u8 flags_attr;
#define ISCSI_CMD_HDR_ATTR_MASK           0x7 /* attributes */
#define ISCSI_CMD_HDR_ATTR_SHIFT          0
#define ISCSI_CMD_HDR_RSRV_MASK           0x3 /* reserved */
#define ISCSI_CMD_HDR_RSRV_SHIFT          3
#define ISCSI_CMD_HDR_WRITE_MASK          0x1 /* write */
#define ISCSI_CMD_HDR_WRITE_SHIFT         5
#define ISCSI_CMD_HDR_READ_MASK           0x1 /* read */
#define ISCSI_CMD_HDR_READ_SHIFT          6
#define ISCSI_CMD_HDR_FINAL_MASK          0x1 /* final */
#define ISCSI_CMD_HDR_FINAL_SHIFT         7
	u8 hdr_first_byte;
#define ISCSI_CMD_HDR_OPCODE_MASK         0x3F /* Opcode */
#define ISCSI_CMD_HDR_OPCODE_SHIFT        0
#define ISCSI_CMD_HDR_IMM_MASK            0x1 /* Immediate delivery */
#define ISCSI_CMD_HDR_IMM_SHIFT           6
#define ISCSI_CMD_HDR_RSRV1_MASK          0x1 /* first bit of iSCSI PDU header */
#define ISCSI_CMD_HDR_RSRV1_SHIFT         7
	__le32 hdr_second_dword;
#define ISCSI_CMD_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_CMD_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_CMD_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_CMD_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number. [constant, initialized] */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 expected_transfer_length /* Expected Data Transfer Length (only 3 bytes are significant) */;
	__le32 cmd_sn /* CmdSn. [constant, initialized] */;
	__le32 exp_stat_sn /* various fields for middle-path PDU. [constant, initialized] */;
	__le32 cdb[4] /* CDB. [constant, initialized] */;
};

/*
 * iSCSI Command PDU header with Extended CDB (Initiator Mode)
 */
struct iscsi_ext_cdb_cmd_hdr
{
	__le16 reserved1 /* reserved */;
	u8 flags_attr;
#define ISCSI_EXT_CDB_CMD_HDR_ATTR_MASK          0x7 /* attributes */
#define ISCSI_EXT_CDB_CMD_HDR_ATTR_SHIFT         0
#define ISCSI_EXT_CDB_CMD_HDR_RSRV_MASK          0x3 /* reserved */
#define ISCSI_EXT_CDB_CMD_HDR_RSRV_SHIFT         3
#define ISCSI_EXT_CDB_CMD_HDR_WRITE_MASK         0x1 /* write */
#define ISCSI_EXT_CDB_CMD_HDR_WRITE_SHIFT        5
#define ISCSI_EXT_CDB_CMD_HDR_READ_MASK          0x1 /* read */
#define ISCSI_EXT_CDB_CMD_HDR_READ_SHIFT         6
#define ISCSI_EXT_CDB_CMD_HDR_FINAL_MASK         0x1 /* final */
#define ISCSI_EXT_CDB_CMD_HDR_FINAL_SHIFT        7
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_EXT_CDB_CMD_HDR_DATA_SEG_LEN_MASK  0xFFFFFF /* DataSegmentLength */
#define ISCSI_EXT_CDB_CMD_HDR_DATA_SEG_LEN_SHIFT 0
#define ISCSI_EXT_CDB_CMD_HDR_CDB_SIZE_MASK      0xFF /* The Extended CDB size in bytes. Maximum Extended CDB size supported is CDB 64B. */
#define ISCSI_EXT_CDB_CMD_HDR_CDB_SIZE_SHIFT     24
	struct regpair lun /* Logical Unit Number. [constant, initialized] */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 expected_transfer_length /* Expected Data Transfer Length (only 3 bytes are significant) */;
	__le32 cmd_sn /* CmdSn. [constant, initialized] */;
	__le32 exp_stat_sn /* various fields for middle-path PDU. [constant, initialized] */;
	struct scsi_sge cdb_sge /* Extended CDBs dedicated SGE */;
};

/*
 * iSCSI login request PDU header
 */
struct iscsi_login_req_hdr
{
	u8 version_min /* Version-min */;
	u8 version_max /* Version-max */;
	u8 flags_attr;
#define ISCSI_LOGIN_REQ_HDR_NSG_MASK            0x3 /* Next Stage (NSG) */
#define ISCSI_LOGIN_REQ_HDR_NSG_SHIFT           0
#define ISCSI_LOGIN_REQ_HDR_CSG_MASK            0x3 /* Current stage (CSG) */
#define ISCSI_LOGIN_REQ_HDR_CSG_SHIFT           2
#define ISCSI_LOGIN_REQ_HDR_RSRV_MASK           0x3 /* reserved */
#define ISCSI_LOGIN_REQ_HDR_RSRV_SHIFT          4
#define ISCSI_LOGIN_REQ_HDR_C_MASK              0x1 /* C (Continue) bit */
#define ISCSI_LOGIN_REQ_HDR_C_SHIFT             6
#define ISCSI_LOGIN_REQ_HDR_T_MASK              0x1 /* T (Transit) bit */
#define ISCSI_LOGIN_REQ_HDR_T_SHIFT             7
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_LOGIN_REQ_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_LOGIN_REQ_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGIN_REQ_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_LOGIN_REQ_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 isid_tabc /* Session identifier high double word [constant, initialized] */;
	__le16 tsih /* TSIH */;
	__le16 isid_d /* Session identifier low word [constant, initialized] */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le16 reserved1;
	__le16 cid /* Unique Connection ID within the session [constant, initialized] */;
	__le32 cmd_sn /* CmdSn. [constant, initialized] */;
	__le32 exp_stat_sn /* various fields for middle-path PDU. [constant, initialized] */;
	__le32 reserved2[4];
};

/*
 * iSCSI logout request PDU header
 */
struct iscsi_logout_req_hdr
{
	__le16 reserved0 /* reserved */;
	u8 reason_code /* Reason Code */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 reserved1;
	__le32 reserved2[2] /* Reserved */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le16 reserved3 /* Reserved */;
	__le16 cid /* Unique Connection ID within the session [constant, initialized] */;
	__le32 cmd_sn /* CmdSn. [constant, initialized] */;
	__le32 exp_stat_sn /* various fields for middle-path PDU. [constant, initialized] */;
	__le32 reserved4[4] /* Reserved */;
};

/*
 * iSCSI Data-out PDU header
 */
struct iscsi_data_out_hdr
{
	__le16 reserved1 /* reserved */;
	u8 flags_attr;
#define ISCSI_DATA_OUT_HDR_RSRV_MASK   0x7F /* reserved */
#define ISCSI_DATA_OUT_HDR_RSRV_SHIFT  0
#define ISCSI_DATA_OUT_HDR_FINAL_MASK  0x1 /* final */
#define ISCSI_DATA_OUT_HDR_FINAL_SHIFT 7
	u8 opcode /* opcode */;
	__le32 reserved2 /* reserved */;
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant) */;
	__le32 ttt /* Target Transfer Tag (from R2T) */;
	__le32 reserved3 /* resrved */;
	__le32 exp_stat_sn /* Expected StatSn */;
	__le32 reserved4 /* resrved */;
	__le32 data_sn /* DataSN - PDU index in sequnece */;
	__le32 buffer_offset /* Buffer Offset - offset in task */;
	__le32 reserved5 /* resrved */;
};

/*
 * iSCSI Data-in PDU header
 */
struct iscsi_data_in_hdr
{
	u8 status_rsvd /* Status or reserved */;
	u8 reserved1 /* reserved */;
	u8 flags;
#define ISCSI_DATA_IN_HDR_STATUS_MASK     0x1 /* Status */
#define ISCSI_DATA_IN_HDR_STATUS_SHIFT    0
#define ISCSI_DATA_IN_HDR_UNDERFLOW_MASK  0x1 /* Residual Underflow */
#define ISCSI_DATA_IN_HDR_UNDERFLOW_SHIFT 1
#define ISCSI_DATA_IN_HDR_OVERFLOW_MASK   0x1 /* Residual Overflow */
#define ISCSI_DATA_IN_HDR_OVERFLOW_SHIFT  2
#define ISCSI_DATA_IN_HDR_RSRV_MASK       0x7 /* reserved - 0 */
#define ISCSI_DATA_IN_HDR_RSRV_SHIFT      3
#define ISCSI_DATA_IN_HDR_ACK_MASK        0x1 /* Acknowledge */
#define ISCSI_DATA_IN_HDR_ACK_SHIFT       6
#define ISCSI_DATA_IN_HDR_FINAL_MASK      0x1 /* final */
#define ISCSI_DATA_IN_HDR_FINAL_SHIFT     7
	u8 opcode /* opcode */;
	__le32 reserved2 /* reserved */;
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant) */;
	__le32 ttt /* Target Transfer Tag (from R2T) */;
	__le32 stat_sn /* StatSN or reserved */;
	__le32 exp_cmd_sn /* Expected CmdSn */;
	__le32 max_cmd_sn /* MaxCmdSn */;
	__le32 data_sn /* DataSN - PDU index in sequnece */;
	__le32 buffer_offset /* Buffer Offset - offset in task */;
	__le32 residual_count /* Residual Count */;
};

/*
 * iSCSI R2T PDU header
 */
struct iscsi_r2t_hdr
{
	u8 reserved0[3] /* reserved */;
	u8 opcode /* opcode */;
	__le32 reserved2 /* reserved */;
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag */;
	__le32 ttt /* Target Transfer Tag */;
	__le32 stat_sn /* stat sn */;
	__le32 exp_cmd_sn /* Expected CmdSn */;
	__le32 max_cmd_sn /* Max CmdSn */;
	__le32 r2t_sn /* DataSN - PDU index in sequnece */;
	__le32 buffer_offset /* Buffer Offset - offset in task */;
	__le32 desired_data_trns_len /* Desired data trnsfer len */;
};

/*
 * iSCSI NOP-out PDU header
 */
struct iscsi_nop_out_hdr
{
	__le16 reserved1 /* reserved */;
	u8 flags_attr;
#define ISCSI_NOP_OUT_HDR_RSRV_MASK    0x7F /* reserved */
#define ISCSI_NOP_OUT_HDR_RSRV_SHIFT   0
#define ISCSI_NOP_OUT_HDR_CONST1_MASK  0x1 /* const1 */
#define ISCSI_NOP_OUT_HDR_CONST1_SHIFT 7
	u8 opcode /* opcode */;
	__le32 reserved2 /* reserved */;
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant) */;
	__le32 ttt /* Target Transfer Tag (from R2T) */;
	__le32 cmd_sn /* CmdSN */;
	__le32 exp_stat_sn /* Expected StatSn */;
	__le32 reserved3 /* reserved */;
	__le32 reserved4 /* reserved */;
	__le32 reserved5 /* reserved */;
	__le32 reserved6 /* reserved */;
};

/*
 * iSCSI NOP-in PDU header
 */
struct iscsi_nop_in_hdr
{
	__le16 reserved0 /* reserved */;
	u8 flags_attr;
#define ISCSI_NOP_IN_HDR_RSRV_MASK           0x7F /* reserved */
#define ISCSI_NOP_IN_HDR_RSRV_SHIFT          0
#define ISCSI_NOP_IN_HDR_CONST1_MASK         0x1 /* const1 */
#define ISCSI_NOP_IN_HDR_CONST1_SHIFT        7
	u8 opcode /* opcode */;
	__le32 hdr_second_dword;
#define ISCSI_NOP_IN_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_NOP_IN_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_NOP_IN_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_NOP_IN_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant) */;
	__le32 ttt /* Target Transfer Tag */;
	__le32 stat_sn /* stat_sn */;
	__le32 exp_cmd_sn /* exp_cmd_sn */;
	__le32 max_cmd_sn /* max_cmd_sn */;
	__le32 reserved5 /* reserved */;
	__le32 reserved6 /* reserved */;
	__le32 reserved7 /* reserved */;
};

/*
 * iSCSI Login Response PDU header
 */
struct iscsi_login_response_hdr
{
	u8 version_active /* Version-active */;
	u8 version_max /* Version-max */;
	u8 flags_attr;
#define ISCSI_LOGIN_RESPONSE_HDR_NSG_MASK            0x3 /* Next Stage (NSG) */
#define ISCSI_LOGIN_RESPONSE_HDR_NSG_SHIFT           0
#define ISCSI_LOGIN_RESPONSE_HDR_CSG_MASK            0x3 /* Current stage (CSG) */
#define ISCSI_LOGIN_RESPONSE_HDR_CSG_SHIFT           2
#define ISCSI_LOGIN_RESPONSE_HDR_RSRV_MASK           0x3 /* reserved */
#define ISCSI_LOGIN_RESPONSE_HDR_RSRV_SHIFT          4
#define ISCSI_LOGIN_RESPONSE_HDR_C_MASK              0x1 /* C (Continue) bit */
#define ISCSI_LOGIN_RESPONSE_HDR_C_SHIFT             6
#define ISCSI_LOGIN_RESPONSE_HDR_T_MASK              0x1 /* T (Transit) bit */
#define ISCSI_LOGIN_RESPONSE_HDR_T_SHIFT             7
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGIN_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_LOGIN_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 isid_tabc /* Session identifier high double word [constant, initialized] */;
	__le16 tsih /* TSIH */;
	__le16 isid_d /* Session identifier low word [constant, initialized] */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 reserved1;
	__le32 stat_sn /* CmdSn. [constant, initialized] */;
	__le32 exp_cmd_sn /* various fields for middle-path PDU. [constant, initialized] */;
	__le32 max_cmd_sn /* max_cmd_sn */;
	__le16 reserved2;
	u8 status_detail /* status_detail */;
	u8 status_class /* status_class */;
	__le32 reserved4[2];
};

/*
 * iSCSI Logout Response PDU header
 */
struct iscsi_logout_response_hdr
{
	u8 reserved1 /* reserved */;
	u8 response /* response */;
	u8 flags /* flags and attributes */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_LOGOUT_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_LOGOUT_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_LOGOUT_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_LOGOUT_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	__le32 reserved2[2] /* Reserved */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 reserved3 /* Reserved */;
	__le32 stat_sn /* CmdSN */;
	__le32 exp_cmd_sn /* Expected StatSn */;
	__le32 max_cmd_sn /* CmdSN */;
	__le32 reserved4 /* Reserved */;
	__le16 time_2_retain /* Time to Retain  */;
	__le16 time_2_wait /* Time to wait */;
	__le32 reserved5[1] /* Reserved */;
};

/*
 * iSCSI Text Request PDU header
 */
struct iscsi_text_request_hdr
{
	__le16 reserved0 /* reserved */;
	u8 flags_attr;
#define ISCSI_TEXT_REQUEST_HDR_RSRV_MASK           0x3F /* reserved */
#define ISCSI_TEXT_REQUEST_HDR_RSRV_SHIFT          0
#define ISCSI_TEXT_REQUEST_HDR_C_MASK              0x1 /* C (Continue) bit */
#define ISCSI_TEXT_REQUEST_HDR_C_SHIFT             6
#define ISCSI_TEXT_REQUEST_HDR_F_MASK              0x1 /* F (Final) bit */
#define ISCSI_TEXT_REQUEST_HDR_F_SHIFT             7
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_TEXT_REQUEST_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_TEXT_REQUEST_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TEXT_REQUEST_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_TEXT_REQUEST_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 ttt /* Referenced Task Tag or 0xffffffff */;
	__le32 cmd_sn /* cmd_sn */;
	__le32 exp_stat_sn /* exp_stat_sn */;
	__le32 reserved4[4] /* Reserved */;
};

/*
 * iSCSI Text Response PDU header
 */
struct iscsi_text_response_hdr
{
	__le16 reserved1 /* reserved */;
	u8 flags;
#define ISCSI_TEXT_RESPONSE_HDR_RSRV_MASK           0x3F /* reserved */
#define ISCSI_TEXT_RESPONSE_HDR_RSRV_SHIFT          0
#define ISCSI_TEXT_RESPONSE_HDR_C_MASK              0x1 /* C (Continue) bit */
#define ISCSI_TEXT_RESPONSE_HDR_C_SHIFT             6
#define ISCSI_TEXT_RESPONSE_HDR_F_MASK              0x1 /* F (Final) bit */
#define ISCSI_TEXT_RESPONSE_HDR_F_SHIFT             7
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TEXT_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_TEXT_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 ttt /* Target Task Tag */;
	__le32 stat_sn /* CmdSN */;
	__le32 exp_cmd_sn /* Expected StatSn */;
	__le32 max_cmd_sn /* CmdSN */;
	__le32 reserved4[3] /* Reserved */;
};

/*
 * iSCSI TMF Request PDU header
 */
struct iscsi_tmf_request_hdr
{
	__le16 reserved0 /* reserved */;
	u8 function /* function */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_TMF_REQUEST_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_TMF_REQUEST_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TMF_REQUEST_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_TMF_REQUEST_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 rtt /* Referenced Task Tag or 0xffffffff */;
	__le32 cmd_sn /* cmd_sn */;
	__le32 exp_stat_sn /* exp_stat_sn */;
	__le32 ref_cmd_sn /* ref_cmd_sn */;
	__le32 exp_data_sn /* exp_data_sn */;
	__le32 reserved4[2] /* Reserved */;
};

struct iscsi_tmf_response_hdr
{
	u8 reserved2 /* reserved2 */;
	u8 hdr_response /* Response field of ISCSI header for Responses / Reserved for Data-In */;
	u8 hdr_flags /* Flags field of ISCSI header */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_TMF_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_TMF_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_TMF_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_TMF_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair reserved0;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 reserved1 /* Reserved */;
	__le32 stat_sn /* stat_sn */;
	__le32 exp_cmd_sn /* exp_cmd_sn */;
	__le32 max_cmd_sn /* max_cmd_sn */;
	__le32 reserved4[3] /* Reserved */;
};

/*
 * iSCSI Response PDU header
 */
struct iscsi_response_hdr
{
	u8 hdr_status /* Status field of ISCSI header */;
	u8 hdr_response /* Response field of ISCSI header for Responses / Reserved for Data-In */;
	u8 hdr_flags /* Flags field of ISCSI header */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_RESPONSE_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_RESPONSE_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_RESPONSE_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_RESPONSE_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 itt /* Initiator Task Tag (only 2 bytes are significant). [constant, initialized] */;
	__le32 snack_tag /* Currently ERL>0 is not supported */;
	__le32 stat_sn /* CmdSN */;
	__le32 exp_cmd_sn /* Expected StatSn */;
	__le32 max_cmd_sn /* CmdSN */;
	__le32 exp_data_sn /* exp_data_sn */;
	__le32 bi_residual_count /* bi residual count */;
	__le32 residual_count /* residual count */;
};

/*
 * iSCSI Reject PDU header
 */
struct iscsi_reject_hdr
{
	u8 reserved4 /* Reserved */;
	u8 hdr_reason /* The reject reason */;
	u8 hdr_flags /* Flags field of ISCSI header */;
	u8 opcode /* opcode. [constant, initialized] */;
	__le32 hdr_second_dword;
#define ISCSI_REJECT_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_REJECT_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_REJECT_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_REJECT_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair reserved0;
	__le32 all_ones;
	__le32 reserved2;
	__le32 stat_sn /* stat_sn */;
	__le32 exp_cmd_sn /* exp_cmd_sn */;
	__le32 max_cmd_sn /* max_cmd_sn */;
	__le32 data_sn /* data_sn */;
	__le32 reserved3[2] /* reserved3 */;
};

/*
 * iSCSI Asynchronous Message PDU header
 */
struct iscsi_async_msg_hdr
{
	__le16 reserved0 /* reserved */;
	u8 flags_attr;
#define ISCSI_ASYNC_MSG_HDR_RSRV_MASK           0x7F /* reserved */
#define ISCSI_ASYNC_MSG_HDR_RSRV_SHIFT          0
#define ISCSI_ASYNC_MSG_HDR_CONST1_MASK         0x1 /* const1 */
#define ISCSI_ASYNC_MSG_HDR_CONST1_SHIFT        7
	u8 opcode /* opcode */;
	__le32 hdr_second_dword;
#define ISCSI_ASYNC_MSG_HDR_DATA_SEG_LEN_MASK   0xFFFFFF /* DataSegmentLength */
#define ISCSI_ASYNC_MSG_HDR_DATA_SEG_LEN_SHIFT  0
#define ISCSI_ASYNC_MSG_HDR_TOTAL_AHS_LEN_MASK  0xFF /* TotalAHSLength */
#define ISCSI_ASYNC_MSG_HDR_TOTAL_AHS_LEN_SHIFT 24
	struct regpair lun /* Logical Unit Number */;
	__le32 all_ones /* should be 0xffffffff */;
	__le32 reserved1 /* reserved */;
	__le32 stat_sn /* stat_sn */;
	__le32 exp_cmd_sn /* exp_cmd_sn */;
	__le32 max_cmd_sn /* max_cmd_sn */;
	__le16 param1_rsrv /* Parameter1 or Reserved */;
	u8 async_vcode /* AsuncVCode */;
	u8 async_event /* AsyncEvent */;
	__le16 param3_rsrv /* Parameter3 or Reserved */;
	__le16 param2_rsrv /* Parameter2 or Reserved */;
	__le32 reserved7 /* reserved */;
};

/*
 * PDU header part of Ystorm task context
 */
union iscsi_task_hdr
{
	struct iscsi_common_hdr common /* Command PDU header */;
	struct data_hdr data /* Command PDU header */;
	struct iscsi_cmd_hdr cmd /* Command PDU header */;
	struct iscsi_ext_cdb_cmd_hdr ext_cdb_cmd /* Command PDU header with extended CDB - Initiator Mode */;
	struct iscsi_login_req_hdr login_req /* Login request PDU header */;
	struct iscsi_logout_req_hdr logout_req /* Logout request PDU header */;
	struct iscsi_data_out_hdr data_out /* Data-out PDU header */;
	struct iscsi_data_in_hdr data_in /* Data-in PDU header */;
	struct iscsi_r2t_hdr r2t /* R2T PDU header */;
	struct iscsi_nop_out_hdr nop_out /* NOP-out PDU header */;
	struct iscsi_nop_in_hdr nop_in /* NOP-in PDU header */;
	struct iscsi_login_response_hdr login_response /* Login response PDU header */;
	struct iscsi_logout_response_hdr logout_response /* Logout response PDU header */;
	struct iscsi_text_request_hdr text_request /* Text request PDU header */;
	struct iscsi_text_response_hdr text_response /* Text response PDU header */;
	struct iscsi_tmf_request_hdr tmf_request /* TMF request PDU header */;
	struct iscsi_tmf_response_hdr tmf_response /* TMF response PDU header */;
	struct iscsi_response_hdr response /* Text response PDU header */;
	struct iscsi_reject_hdr reject /* Reject PDU header */;
	struct iscsi_async_msg_hdr async_msg /* Asynchronous Message PDU header */;
};

/*
 * The iscsi storm task context of Ystorm
 */
struct ystorm_iscsi_task_st_ctx
{
	struct ystorm_iscsi_task_state state /* iSCSI task parameters and state */;
	struct ystorm_iscsi_task_rxmit_opt rxmit_opt /* iSCSI retransmit optimizations parameters */;
	union iscsi_task_hdr pdu_hdr /* PDU header - [constant initialized] */;
};

struct e4_ystorm_iscsi_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E4_YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK     0xF /* connection_type */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT    0
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK        0x1 /* exist_in_qm0 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT       4
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK        0x1 /* exist_in_qm1 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT       5
#define E4_YSTORM_ISCSI_TASK_AG_CTX_VALID_MASK       0x1 /* bit2 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT      6
#define E4_YSTORM_ISCSI_TASK_AG_CTX_TTT_VALID_MASK   0x1 /* bit3 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_TTT_VALID_SHIFT  7
	u8 flags1;
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF0_MASK         0x3 /* cf0 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT        0
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF1_MASK         0x3 /* cf1 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT        2
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_MASK  0x3 /* cf2special */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_SHIFT 4
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK       0x1 /* cf0en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT      6
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK       0x1 /* cf1en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT      7
	u8 flags2;
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK        0x1 /* bit4 */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT       0
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK     0x1 /* rule0en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT    1
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK     0x1 /* rule1en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT    2
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK     0x1 /* rule2en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT    3
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK     0x1 /* rule3en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT    4
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK     0x1 /* rule4en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT    5
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK     0x1 /* rule5en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT    6
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK     0x1 /* rule6en */
#define E4_YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT    7
	u8 byte2 /* byte2 */;
	__le32 TTT /* reg0 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	__le16 word1 /* word1 */;
};

struct e4_mstorm_iscsi_task_ag_ctx
{
	u8 cdu_validation /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 task_cid /* icid */;
	u8 flags0;
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK     0xF /* connection_type */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT    0
#define E4_MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK        0x1 /* exist_in_qm0 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT       4
#define E4_MSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                0x1 /* exist_in_qm1 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT               5
#define E4_MSTORM_ISCSI_TASK_AG_CTX_VALID_MASK               0x1 /* bit2 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT              6
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_MASK   0x1 /* bit3 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_SHIFT  7
	u8 flags1;
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_MASK     0x3 /* cf0 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_SHIFT    0
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF1_MASK                 0x3 /* cf1 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT                2
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF2_MASK                 0x3 /* cf2 */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT                4
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_MASK  0x1 /* cf0en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_SHIFT 6
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK               0x1 /* cf1en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT              7
	u8 flags2;
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK               0x1 /* cf2en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT              0
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK             0x1 /* rule0en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT            1
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK             0x1 /* rule1en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT            2
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK             0x1 /* rule2en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT            3
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK             0x1 /* rule3en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT            4
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK             0x1 /* rule4en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT            5
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK             0x1 /* rule5en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT            6
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK             0x1 /* rule6en */
#define E4_MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT            7
	u8 byte2 /* byte2 */;
	__le32 reg0 /* reg0 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	__le16 word1 /* word1 */;
};

struct e4_ustorm_iscsi_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state /* state */;
	__le16 icid /* icid */;
	u8 flags0;
#define E4_USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK        0xF /* connection_type */
#define E4_USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT       0
#define E4_USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK           0x1 /* exist_in_qm0 */
#define E4_USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT          4
#define E4_USTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                   0x1 /* exist_in_qm1 */
#define E4_USTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT                  5
#define E4_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_MASK          0x3 /* timer0cf */
#define E4_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_SHIFT         6
	u8 flags1;
#define E4_USTORM_ISCSI_TASK_AG_CTX_RESERVED1_MASK              0x3 /* timer1cf */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RESERVED1_SHIFT             0
#define E4_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_MASK               0x3 /* timer2cf */
#define E4_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_SHIFT              2
#define E4_USTORM_ISCSI_TASK_AG_CTX_CF3_MASK                    0x3 /* timer_stop_all */
#define E4_USTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT                   4
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_MASK           0x3 /* cf4 */
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_SHIFT          6
	u8 flags2;
#define E4_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_MASK       0x1 /* cf0en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_SHIFT      0
#define E4_USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_MASK     0x1 /* cf1en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_SHIFT    1
#define E4_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_MASK            0x1 /* cf2en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_SHIFT           2
#define E4_USTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK                  0x1 /* cf3en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT                 3
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK        0x1 /* cf4en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT       4
#define E4_USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_MASK  0x1 /* rule0en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_SHIFT 5
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK                0x1 /* rule1en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT               6
#define E4_USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_MASK    0x1 /* rule2en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_SHIFT   7
	u8 flags3;
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK                0x1 /* rule3en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT               0
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK                0x1 /* rule4en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT               1
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK                0x1 /* rule5en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT               2
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK                0x1 /* rule6en */
#define E4_USTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT               3
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_MASK         0xF /* nibble1 */
#define E4_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT        4
	__le32 dif_err_intervals /* reg0 */;
	__le32 dif_error_1st_interval /* reg1 */;
	__le32 rcv_cont_len /* reg2 */;
	__le32 exp_cont_len /* reg3 */;
	__le32 total_data_acked /* reg4 */;
	__le32 exp_data_acked /* reg5 */;
	u8 next_tid_valid /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word1 /* word1 */;
	__le16 next_tid /* word2 */;
	__le16 word3 /* word3 */;
	__le32 hdr_residual_count /* reg6 */;
	__le32 exp_r2t_sn /* reg7 */;
};

/*
 * The iscsi storm task context of Mstorm
 */
struct mstorm_iscsi_task_st_ctx
{
	struct scsi_cached_sges data_desc /* Union of Data SGL / cached sge */;
	struct scsi_sgl_params sgl_params;
	__le32 rem_task_size /* Remaining task size, used for placement verification */;
	__le32 data_buffer_offset /* Buffer offset */;
	u8 task_type /* Task type, (use: iscsi_task_type enum) */;
	struct iscsi_dif_flags dif_flags /* sizes of host/peer protection intervals + protection log interval */;
	__le16 dif_task_icid /* save tasks CID for validation - dif on immediate flow */;
	struct regpair sense_db /* Pointer to sense data buffer */;
	__le32 expected_itt /* ITT - for target mode validations */;
	__le32 reserved1 /* reserved1 */;
};

struct iscsi_reg1
{
	__le32 reg1_map;
#define ISCSI_REG1_NUM_SGES_MASK   0xF /* Written to R2tQE */
#define ISCSI_REG1_NUM_SGES_SHIFT  0
#define ISCSI_REG1_RESERVED1_MASK  0xFFFFFFF /* reserved */
#define ISCSI_REG1_RESERVED1_SHIFT 4
};

struct tqe_opaque
{
	__le16 opaque[2] /* TQe opaque */;
};

/*
 * The iscsi storm task context of Ustorm
 */
struct ustorm_iscsi_task_st_ctx
{
	__le32 rem_rcv_len /* Remaining data to be received in bytes. Used in validations */;
	__le32 exp_data_transfer_len /* iSCSI Initiator - The size of the transmitted task, iSCSI Target - the size of the Rx continuation */;
	__le32 exp_data_sn /* Expected data SN */;
	struct regpair lun /* LUN */;
	struct iscsi_reg1 reg1;
	u8 flags2;
#define USTORM_ISCSI_TASK_ST_CTX_AHS_EXIST_MASK              0x1 /* Initiator Mode - Mark AHS exist */
#define USTORM_ISCSI_TASK_ST_CTX_AHS_EXIST_SHIFT             0
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED1_MASK              0x7F
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED1_SHIFT             1
	struct iscsi_dif_flags dif_flags /* Dif flags (written to R2T WQE) */;
	__le16 reserved3;
	struct tqe_opaque tqe_opaque_list;
	__le32 reserved5;
	__le32 reserved6;
	__le32 reserved7;
	u8 task_type /* Task Type */;
	u8 error_flags;
#define USTORM_ISCSI_TASK_ST_CTX_DATA_DIGEST_ERROR_MASK      0x1 /* Mark task with data digest error (1 bit) */
#define USTORM_ISCSI_TASK_ST_CTX_DATA_DIGEST_ERROR_SHIFT     0
#define USTORM_ISCSI_TASK_ST_CTX_DATA_TRUNCATED_ERROR_MASK   0x1 /* Target Mode - Mark middle task error, data truncated */
#define USTORM_ISCSI_TASK_ST_CTX_DATA_TRUNCATED_ERROR_SHIFT  1
#define USTORM_ISCSI_TASK_ST_CTX_UNDER_RUN_ERROR_MASK        0x1
#define USTORM_ISCSI_TASK_ST_CTX_UNDER_RUN_ERROR_SHIFT       2
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED8_MASK              0x1F
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED8_SHIFT             3
	u8 flags;
#define USTORM_ISCSI_TASK_ST_CTX_CQE_WRITE_MASK              0x3 /* mark task cqe write (for cleanup flow) */
#define USTORM_ISCSI_TASK_ST_CTX_CQE_WRITE_SHIFT             0
#define USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP_MASK             0x1 /* local completion bit */
#define USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP_SHIFT            2
#define USTORM_ISCSI_TASK_ST_CTX_Q0_R2TQE_WRITE_MASK         0x1 /* write R2TQE from Q0 flow */
#define USTORM_ISCSI_TASK_ST_CTX_Q0_R2TQE_WRITE_SHIFT        3
#define USTORM_ISCSI_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_MASK  0x1 /* Mark total data acked or disabled */
#define USTORM_ISCSI_TASK_ST_CTX_TOTAL_DATA_ACKED_DONE_SHIFT 4
#define USTORM_ISCSI_TASK_ST_CTX_HQ_SCANNED_DONE_MASK        0x1 /* Mark HQ scanned or disabled */
#define USTORM_ISCSI_TASK_ST_CTX_HQ_SCANNED_DONE_SHIFT       5
#define USTORM_ISCSI_TASK_ST_CTX_R2T2RECV_DONE_MASK          0x1 /* Mark HQ scanned or disabled */
#define USTORM_ISCSI_TASK_ST_CTX_R2T2RECV_DONE_SHIFT         6
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED0_MASK              0x1
#define USTORM_ISCSI_TASK_ST_CTX_RESERVED0_SHIFT             7
	u8 cq_rss_number /* Task CQ_RSS number 0.63 */;
};

/*
 * iscsi task context
 */
struct e4_iscsi_task_context
{
	struct ystorm_iscsi_task_st_ctx ystorm_st_context /* ystorm storm context */;
	struct e4_ystorm_iscsi_task_ag_ctx ystorm_ag_context /* ystorm aggregative context */;
	struct regpair ystorm_ag_padding[2] /* padding */;
	struct tdif_task_context tdif_context /* tdif context */;
	struct e4_mstorm_iscsi_task_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct regpair mstorm_ag_padding[2] /* padding */;
	struct e4_ustorm_iscsi_task_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct mstorm_iscsi_task_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iscsi_task_st_ctx ustorm_st_context /* ustorm storm context */;
	struct rdif_task_context rdif_context /* rdif context */;
};


struct e5_ystorm_iscsi_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E5_YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK       0xF /* connection_type */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT      0
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK          0x1 /* exist_in_qm0 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT         4
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK          0x1 /* exist_in_qm1 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT         5
#define E5_YSTORM_ISCSI_TASK_AG_CTX_VALID_MASK         0x1 /* bit2 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT        6
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT3_MASK          0x1 /* bit3 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT3_SHIFT         7
	u8 flags1;
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF0_MASK           0x3 /* cf0 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT          0
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF1_MASK           0x3 /* cf1 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT          2
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_MASK    0x3 /* cf2special */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF2SPECIAL_SHIFT   4
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK         0x1 /* cf0en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT        6
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK         0x1 /* cf1en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT        7
	u8 flags2;
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK          0x1 /* bit4 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT         0
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK       0x1 /* rule0en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT      1
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK       0x1 /* rule1en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT      2
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK       0x1 /* rule2en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT      3
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK       0x1 /* rule3en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT      4
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK       0x1 /* rule4en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT      5
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK       0x1 /* rule5en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT      6
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK       0x1 /* rule6en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT      7
	u8 flags3;
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_MASK  0x1 /* bit5 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_SHIFT 0
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_MASK  0x3 /* cf3 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_SHIFT 1
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_MASK  0x3 /* cf4 */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_SHIFT 3
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_MASK  0x1 /* cf3en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_SHIFT 5
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_MASK  0x1 /* cf4en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_SHIFT 6
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_MASK  0x1 /* rule7en */
#define E5_YSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_SHIFT 7
	__le32 TTT /* reg0 */;
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 e4_reserved7 /* byte5 */;
};

struct e5_mstorm_iscsi_task_ag_ctx
{
	u8 cdu_validation /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 task_cid /* icid */;
	u8 flags0;
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK     0xF /* connection_type */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT    0
#define E5_MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK        0x1 /* exist_in_qm0 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT       4
#define E5_MSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                0x1 /* exist_in_qm1 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT               5
#define E5_MSTORM_ISCSI_TASK_AG_CTX_VALID_MASK               0x1 /* bit2 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_VALID_SHIFT              6
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_MASK   0x1 /* bit3 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_FLAG_SHIFT  7
	u8 flags1;
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_MASK     0x3 /* cf0 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_SHIFT    0
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF1_MASK                 0x3 /* cf1 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT                2
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF2_MASK                 0x3 /* cf2 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT                4
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_MASK  0x1 /* cf0en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_TASK_CLEANUP_CF_EN_SHIFT 6
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK               0x1 /* cf1en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT              7
	u8 flags2;
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK               0x1 /* cf2en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT              0
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK             0x1 /* rule0en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT            1
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK             0x1 /* rule1en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT            2
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK             0x1 /* rule2en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT            3
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK             0x1 /* rule3en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT            4
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK             0x1 /* rule4en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT            5
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK             0x1 /* rule5en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT            6
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK             0x1 /* rule6en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT            7
	u8 flags3;
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_MASK        0x1 /* bit4 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_SHIFT       0
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_MASK        0x3 /* cf3 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_SHIFT       1
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_MASK        0x3 /* cf4 */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_SHIFT       3
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_MASK        0x1 /* cf3en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_SHIFT       5
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_MASK        0x1 /* cf4en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_SHIFT       6
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_MASK        0x1 /* rule7en */
#define E5_MSTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_SHIFT       7
	__le32 reg0 /* reg0 */;
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 e4_reserved7 /* byte5 */;
};

struct e5_ustorm_iscsi_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	__le16 icid /* icid */;
	u8 flags0;
#define E5_USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_MASK        0xF /* connection_type */
#define E5_USTORM_ISCSI_TASK_AG_CTX_CONNECTION_TYPE_SHIFT       0
#define E5_USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_MASK           0x1 /* exist_in_qm0 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_EXIST_IN_QM0_SHIFT          4
#define E5_USTORM_ISCSI_TASK_AG_CTX_BIT1_MASK                   0x1 /* exist_in_qm1 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT                  5
#define E5_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_MASK          0x3 /* timer0cf */
#define E5_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_SHIFT         6
	u8 flags1;
#define E5_USTORM_ISCSI_TASK_AG_CTX_RESERVED1_MASK              0x3 /* timer1cf */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RESERVED1_SHIFT             0
#define E5_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_MASK               0x3 /* timer2cf */
#define E5_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_SHIFT              2
#define E5_USTORM_ISCSI_TASK_AG_CTX_CF3_MASK                    0x3 /* timer_stop_all */
#define E5_USTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT                   4
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_MASK           0x3 /* dif_error_cf */
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_SHIFT          6
	u8 flags2;
#define E5_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_MASK       0x1 /* cf0en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_HQ_SCANNED_CF_EN_SHIFT      0
#define E5_USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_MASK     0x1 /* cf1en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_DISABLE_DATA_ACKED_SHIFT    1
#define E5_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_MASK            0x1 /* cf2en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_R2T2RECV_EN_SHIFT           2
#define E5_USTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK                  0x1 /* cf3en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT                 3
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK        0x1 /* cf4en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT       4
#define E5_USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_MASK  0x1 /* rule0en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_CMP_DATA_TOTAL_EXP_EN_SHIFT 5
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK                0x1 /* rule1en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT               6
#define E5_USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_MASK    0x1 /* rule2en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_CMP_CONT_RCV_EXP_EN_SHIFT   7
	u8 flags3;
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK                0x1 /* rule3en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT               0
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK                0x1 /* rule4en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT               1
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK                0x1 /* rule5en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT               2
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE6EN_MASK                0x1 /* rule6en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_RULE6EN_SHIFT               3
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_MASK           0x1 /* bit2 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED1_SHIFT          4
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_MASK           0x1 /* bit3 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED2_SHIFT          5
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_MASK           0x1 /* bit4 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED3_SHIFT          6
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_MASK           0x1 /* rule7en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED4_SHIFT          7
	u8 flags4;
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_MASK           0x3 /* cf5 */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED5_SHIFT          0
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_MASK           0x1 /* cf5en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED6_SHIFT          2
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED7_MASK           0x1 /* rule8en */
#define E5_USTORM_ISCSI_TASK_AG_CTX_E4_RESERVED7_SHIFT          3
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_MASK         0xF /* dif_error_type */
#define E5_USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT        4
	u8 next_tid_valid /* byte2 */;
	u8 byte3 /* byte3 */;
	u8 e4_reserved8 /* byte4 */;
	__le32 dif_err_intervals /* dif_err_intervals */;
	__le32 dif_error_1st_interval /* dif_error_1st_interval */;
	__le32 rcv_cont_len /* reg2 */;
	__le32 exp_cont_len /* reg3 */;
	__le32 total_data_acked /* reg4 */;
	__le32 exp_data_acked /* reg5 */;
	__le16 word1 /* word1 */;
	__le16 next_tid /* word2 */;
	__le32 hdr_residual_count /* reg6 */;
	__le32 exp_r2t_sn /* reg7 */;
};

/*
 * iscsi task context
 */
struct e5_iscsi_task_context
{
	struct ystorm_iscsi_task_st_ctx ystorm_st_context /* ystorm storm context */;
	struct e5_ystorm_iscsi_task_ag_ctx ystorm_ag_context /* ystorm aggregative context */;
	struct regpair ystorm_ag_padding[2] /* padding */;
	struct tdif_task_context tdif_context /* tdif context */;
	struct e5_mstorm_iscsi_task_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct regpair mstorm_ag_padding[2] /* padding */;
	struct e5_ustorm_iscsi_task_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct mstorm_iscsi_task_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iscsi_task_st_ctx ustorm_st_context /* ustorm storm context */;
	struct rdif_task_context rdif_context /* rdif context */;
};





/*
 * ISCSI connection offload params passed by driver to FW in ISCSI offload ramrod 
 */
struct iscsi_conn_offload_params
{
	struct regpair sq_pbl_addr /* PBL SQ pointer */;
	struct regpair r2tq_pbl_addr /* PBL R2TQ pointer */;
	struct regpair xhq_pbl_addr /* PBL XHQ pointer */;
	struct regpair uhq_pbl_addr /* PBL UHQ pointer */;
	__le32 initial_ack /* Initial ack, received from TCP */;
	__le16 physical_q0 /* Physical QM queue to be tied to logical Q0 */;
	__le16 physical_q1 /* Physical QM queue to be tied to logical Q1 */;
	u8 flags;
#define ISCSI_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_MASK   0x1 /* TCP connect/terminate option. 0 - TCP on host (option-1); 1 - TCP on chip (option-2). */
#define ISCSI_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B_SHIFT  0
#define ISCSI_CONN_OFFLOAD_PARAMS_TARGET_MODE_MASK      0x1 /* iSCSI connect mode: 0-iSCSI Initiator, 1-iSCSI Target */
#define ISCSI_CONN_OFFLOAD_PARAMS_TARGET_MODE_SHIFT     1
#define ISCSI_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_MASK  0x1 /* Restricted mode: 0 - un-restricted (deviating from the RFC), 1 - restricted (according to the RFC) */
#define ISCSI_CONN_OFFLOAD_PARAMS_RESTRICTED_MODE_SHIFT 2
#define ISCSI_CONN_OFFLOAD_PARAMS_RESERVED1_MASK        0x1F /* reserved */
#define ISCSI_CONN_OFFLOAD_PARAMS_RESERVED1_SHIFT       3
	u8 pbl_page_size_log /* Page size with PBEs log; Page Size = 2^(page_size_log+12) */;
	u8 pbe_page_size_log /* PBE page size with log; Page Size = 2^(page_size_log+12) */;
	u8 default_cq /* Default CQ used to write unsolicited data */;
	__le32 stat_sn /* StatSn for Target Mode only: the first Login Response StatSn value for Target mode */;
};


/*
 * iSCSI connection statistics
 */
struct iscsi_conn_stats_params
{
	struct regpair iscsi_tcp_tx_packets_cnt /* Counts number of transmitted TCP packets for this iSCSI connection */;
	struct regpair iscsi_tcp_tx_bytes_cnt /* Counts number of transmitted TCP bytes for this iSCSI connection */;
	struct regpair iscsi_tcp_tx_rxmit_cnt /* Counts number of TCP retransmission events for this iSCSI connection */;
	struct regpair iscsi_tcp_rx_packets_cnt /* Counts number of received TCP packets for this iSCSI connection */;
	struct regpair iscsi_tcp_rx_bytes_cnt /* Counts number of received TCP bytes for this iSCSI connection */;
	struct regpair iscsi_tcp_rx_dup_ack_cnt /* Counts number of received TCP duplicate acks for this iSCSI connection */;
	__le32 iscsi_tcp_rx_chksum_err_cnt /* Counts number of received TCP packets with checksum err for this iSCSI connection */;
	__le32 reserved;
};


/*
 * spe message header 
 */
struct iscsi_slow_path_hdr
{
	u8 op_code /* iscsi bus-drv message opcode */;
	u8 flags;
#define ISCSI_SLOW_PATH_HDR_RESERVED0_MASK   0xF
#define ISCSI_SLOW_PATH_HDR_RESERVED0_SHIFT  0
#define ISCSI_SLOW_PATH_HDR_LAYER_CODE_MASK  0x7 /* protocol layer (L2,L3,L4,L5) */
#define ISCSI_SLOW_PATH_HDR_LAYER_CODE_SHIFT 4
#define ISCSI_SLOW_PATH_HDR_RESERVED1_MASK   0x1
#define ISCSI_SLOW_PATH_HDR_RESERVED1_SHIFT  7
};

/*
 * ISCSI connection update params passed by driver to FW in ISCSI update ramrod 
 */
struct iscsi_conn_update_ramrod_params
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. (MOTI_COHEN : draft for DrvSim sake) */;
	__le32 fw_cid /* Context ID (cid) of the connection. (MOTI_COHEN : draft for DrvSim sake) */;
	u8 flags;
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_HD_EN_MASK           0x1 /* Is header digest enabled */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_HD_EN_SHIFT          0
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DD_EN_MASK           0x1 /* Is data digest enabled */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DD_EN_SHIFT          1
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_INITIAL_R2T_MASK     0x1 /* Initial R2T */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_INITIAL_R2T_SHIFT    2
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_IMMEDIATE_DATA_MASK  0x1 /* Immediate data */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_IMMEDIATE_DATA_SHIFT 3
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_BLOCK_SIZE_MASK  0x1 /* 0 - 512B, 1 - 4K */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_BLOCK_SIZE_SHIFT 4
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_HOST_EN_MASK  0x1 /* 0 - no DIF, 1 - could be enabled per task */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_HOST_EN_SHIFT 5
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_IMM_EN_MASK   0x1 /* Support DIF on immediate, 1-Yes, 0-No */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_DIF_ON_IMM_EN_SHIFT  6
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_LUN_MAPPER_EN_MASK   0x1 /* valid only if dif_on_imm_en=1 Does this connection has dif configuration per Lun or Default dif configuration */
#define ISCSI_CONN_UPDATE_RAMROD_PARAMS_LUN_MAPPER_EN_SHIFT  7
	u8 reserved0[3];
	__le32 max_seq_size /* Maximum sequence size. Valid for TX and RX */;
	__le32 max_send_pdu_length /* Maximum PDU size. Valid for the TX */;
	__le32 max_recv_pdu_length /* Maximum PDU size. Valid for the RX */;
	__le32 first_seq_length /* Initial sequence length */;
	__le32 exp_stat_sn /* ExpStatSn - Option1 Only */;
	union dif_configuration_params dif_on_imme_params /* dif on immmediate params - Target mode Only */;
};


/*
 * iSCSI CQ element
 */
struct iscsi_cqe_common
{
	__le16 conn_id /* Drivers connection Id */;
	u8 cqe_type /* Indicates CQE type (use enum iscsi_cqes_type) */;
	union cqe_error_status error_bitmap /* CQE error status */;
	__le32 reserved[3];
	union iscsi_task_hdr iscsi_hdr /* iscsi header union */;
};

/*
 * iSCSI CQ element
 */
struct iscsi_cqe_solicited
{
	__le16 conn_id /* Drivers connection Id */;
	u8 cqe_type /* Indicates CQE type (use enum iscsi_cqes_type) */;
	union cqe_error_status error_bitmap /* CQE error status */;
	__le16 itid /* initiator itt (Initiator mode) or target ttt (Target mode) */;
	u8 task_type /* Task type */;
	u8 fw_dbg_field /* FW debug params */;
	u8 caused_conn_err /* Equals 1 if this TID caused the connection error, otherwise equals 0. */;
	u8 reserved0[3];
	__le32 data_truncated_bytes /* Target Mode only: Valid only if data_truncated_err equals 1: The remaining bytes till the end of the IO. */;
	union iscsi_task_hdr iscsi_hdr /* iscsi header union */;
};

/*
 * iSCSI CQ element
 */
struct iscsi_cqe_unsolicited
{
	__le16 conn_id /* Drivers connection Id */;
	u8 cqe_type /* Indicates CQE type (use enum iscsi_cqes_type) */;
	union cqe_error_status error_bitmap /* CQE error status */;
	__le16 reserved0 /* Reserved */;
	u8 reserved1 /* Reserved */;
	u8 unsol_cqe_type /* Represent this unsolicited CQE position in a sequence of packets belonging to the same unsolicited PDU (use enum iscsi_cqe_unsolicited_type) */;
	__le16 rqe_opaque /* Relevant for Unsolicited CQE only: The opaque data of RQ BDQ */;
	__le16 reserved2[3] /* Reserved */;
	union iscsi_task_hdr iscsi_hdr /* iscsi header union */;
};

/*
 * iSCSI CQ element
 */
union iscsi_cqe
{
	struct iscsi_cqe_common cqe_common /* Common CQE */;
	struct iscsi_cqe_solicited cqe_solicited /* Solicited CQE */;
	struct iscsi_cqe_unsolicited cqe_unsolicited /* Unsolicited CQE. relevant only when cqe_opcode == ISCSI_CQE_TYPE_UNSOLICITED */;
};


/*
 * iSCSI CQE type 
 */
enum iscsi_cqes_type
{
	ISCSI_CQE_TYPE_SOLICITED=1 /* iSCSI CQE with solicited data */,
	ISCSI_CQE_TYPE_UNSOLICITED /* iSCSI CQE with unsolicited data */,
	ISCSI_CQE_TYPE_SOLICITED_WITH_SENSE /* iSCSI CQE with solicited with sense data */,
	ISCSI_CQE_TYPE_TASK_CLEANUP /* iSCSI CQE task cleanup */,
	ISCSI_CQE_TYPE_DUMMY /* iSCSI Dummy CQE */,
	MAX_ISCSI_CQES_TYPE
};





/*
 * iSCSI CQE type 
 */
enum iscsi_cqe_unsolicited_type
{
	ISCSI_CQE_UNSOLICITED_NONE /* iSCSI CQE with unsolicited data */,
	ISCSI_CQE_UNSOLICITED_SINGLE /* iSCSI CQE with unsolicited data */,
	ISCSI_CQE_UNSOLICITED_FIRST /* iSCSI CQE with unsolicited data */,
	ISCSI_CQE_UNSOLICITED_MIDDLE /* iSCSI CQE with unsolicited data */,
	ISCSI_CQE_UNSOLICITED_LAST /* iSCSI CQE with unsolicited data */,
	MAX_ISCSI_CQE_UNSOLICITED_TYPE
};




/*
 * iscsi debug modes
 */
struct iscsi_debug_modes
{
	u8 flags;
#define ISCSI_DEBUG_MODES_ASSERT_IF_RX_CONN_ERROR_MASK             0x1 /* Assert on Rx connection error */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RX_CONN_ERROR_SHIFT            0
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_RESET_MASK                0x1 /* Assert if TCP RESET arrived */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_RESET_SHIFT               1
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_FIN_MASK                  0x1 /* Assert if TCP FIN arrived */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_FIN_SHIFT                 2
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_CLEANUP_MASK              0x1 /* Assert if cleanup flow */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_CLEANUP_SHIFT             3
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_REJECT_OR_ASYNC_MASK      0x1 /* Assert if REJECT PDU or ASYNC PDU arrived */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_REJECT_OR_ASYNC_SHIFT     4
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_NOP_MASK                  0x1 /* Assert if NOP IN PDU or NOP OUT PDU arrived */
#define ISCSI_DEBUG_MODES_ASSERT_IF_RECV_NOP_SHIFT                 5
#define ISCSI_DEBUG_MODES_ASSERT_IF_DIF_OR_DATA_DIGEST_ERROR_MASK  0x1 /* Assert if DIF or data digest error */
#define ISCSI_DEBUG_MODES_ASSERT_IF_DIF_OR_DATA_DIGEST_ERROR_SHIFT 6
#define ISCSI_DEBUG_MODES_ASSERT_IF_HQ_CORRUPT_MASK                0x1 /* Assert if HQ corruption detected */
#define ISCSI_DEBUG_MODES_ASSERT_IF_HQ_CORRUPT_SHIFT               7
};



/*
 * iSCSI kernel completion queue IDs 
 */
enum iscsi_eqe_opcode
{
	ISCSI_EVENT_TYPE_INIT_FUNC=0 /* iSCSI response after init Ramrod */,
	ISCSI_EVENT_TYPE_DESTROY_FUNC /* iSCSI response after destroy Ramrod */,
	ISCSI_EVENT_TYPE_OFFLOAD_CONN /* iSCSI response after option 2 offload Ramrod */,
	ISCSI_EVENT_TYPE_UPDATE_CONN /* iSCSI response after update Ramrod */,
	ISCSI_EVENT_TYPE_CLEAR_SQ /* iSCSI response after clear sq Ramrod */,
	ISCSI_EVENT_TYPE_TERMINATE_CONN /* iSCSI response after termination Ramrod */,
	ISCSI_EVENT_TYPE_MAC_UPDATE_CONN /* iSCSI response after MAC address update Ramrod */,
	ISCSI_EVENT_TYPE_COLLECT_STATS_CONN /* iSCSI response after collecting connection statistics Ramrod */,
	ISCSI_EVENT_TYPE_ASYN_CONNECT_COMPLETE /* iSCSI response after option 2 connect completed (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_TERMINATE_DONE /* iSCSI response after option 2 termination completed (A-syn EQE) */,
	ISCSI_EVENT_TYPE_START_OF_ERROR_TYPES=10 /* Never returned in EQE, used to separate Regular event types from Error event types */,
	ISCSI_EVENT_TYPE_ASYN_ABORT_RCVD /* iSCSI abort response after TCP RST packet recieve (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_CLOSE_RCVD /* iSCSI response after close receive (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_SYN_RCVD /* iSCSI response after TCP SYN+ACK packet receive (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_MAX_RT_TIME /* iSCSI error - tcp max retransmit time (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_MAX_RT_CNT /* iSCSI error - tcp max retransmit count (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_MAX_KA_PROBES_CNT /* iSCSI error - tcp ka probes count (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ASYN_FIN_WAIT2 /* iSCSI error - tcp fin wait 2 (A-syn EQE) */,
	ISCSI_EVENT_TYPE_ISCSI_CONN_ERROR /* iSCSI error response (A-syn EQE) */,
	ISCSI_EVENT_TYPE_TCP_CONN_ERROR /* iSCSI error - tcp error (A-syn EQE) */,
	MAX_ISCSI_EQE_OPCODE
};


/*
 * iSCSI EQE and CQE completion status 
 */
enum iscsi_error_types
{
	ISCSI_STATUS_NONE=0,
	ISCSI_CQE_ERROR_UNSOLICITED_RCV_ON_INVALID_CONN=1,
	ISCSI_CONN_ERROR_TASK_CID_MISMATCH /* iSCSI connection error - Corrupted Task context  */,
	ISCSI_CONN_ERROR_TASK_NOT_VALID /* iSCSI connection error - The task is not valid  */,
	ISCSI_CONN_ERROR_RQ_RING_IS_FULL /* iSCSI connection error - RQ full  */,
	ISCSI_CONN_ERROR_CMDQ_RING_IS_FULL /* iSCSI connection error - CMDQ full (Target only)  */,
	ISCSI_CONN_ERROR_HQE_CACHING_FAILED /* iSCSI connection error - HQ error  */,
	ISCSI_CONN_ERROR_HEADER_DIGEST_ERROR /* iSCSI connection error - Header digest error */,
	ISCSI_CONN_ERROR_LOCAL_COMPLETION_ERROR /* iSCSI connection error - Local completion bit is not correct   (A-syn EQE) */,
	ISCSI_CONN_ERROR_DATA_OVERRUN /* iSCSI connection error - data overrun */,
	ISCSI_CONN_ERROR_OUT_OF_SGES_ERROR /* iSCSI connection error - out of sges in task context */,
	ISCSI_CONN_ERROR_IP_OPTIONS_ERROR /* TCP connection error - IP option error  */,
	ISCSI_CONN_ERROR_PRS_ERRORS /* TCP connection error - error indication form parser */,
	ISCSI_CONN_ERROR_CONNECT_INVALID_TCP_OPTION /* TCP connection error - tcp options error(option 2 only)  */,
	ISCSI_CONN_ERROR_TCP_IP_FRAGMENT_ERROR /* TCP connection error - IP fragmentation error  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_AHS_LEN /* iSCSI connection error - invalid AHS length (Target only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_AHS_TYPE /* iSCSI connection error - invalid AHS type (Target only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_ITT_OUT_OF_RANGE /* iSCSI connection error - invalid ITT  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_TTT_OUT_OF_RANGE /* iSCSI connection error - invalid TTT (Target only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SEG_LEN_EXCEEDS_PDU_SIZE /* iSCSI connection error - PDU data_seg_len > max receive pdu size */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_OPCODE /* iSCSI connection error - invalid PDU opcode */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_OPCODE_BEFORE_UPDATE /* iSCSI connection error - invalid PDU opcode before update ramrod (Option 2 only)  */,
	ISCSI_CONN_ERROR_UNVALID_NOPIN_DSL /* iSCSI connection error - NOPIN dsl > 0 and ITT = 0xffffffff (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_CARRIES_NO_DATA /* iSCSI connection error - R2T dsl > 0 (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SN /* iSCSI connection error - DATA-SN error  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_IN_TTT /* iSCSI connection error - DATA-IN TTT error (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_OUT_ITT /* iSCSI connection error - DATA-OUT ITT error (Target only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_TTT /* iSCSI connection error - R2T TTT error (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_BUFFER_OFFSET /* iSCSI connection error - R2T buffer offset error (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_BUFFER_OFFSET_OOO /* iSCSI connection error - DATA PDU buffer offset error  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_R2T_SN /* iSCSI connection error - R2T SN error (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_0 /* iSCSI connection error - R2T desired data transfer length = 0 (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_1 /* iSCSI connection error - R2T desired data transfer length less then max burst size (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_2 /* iSCSI connection error - R2T desired data transfer length + buffer offset > task size (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_LUN /* iSCSI connection error - R2T unvalid LUN (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_F_BIT_ZERO /* iSCSI connection error - All data has been already received, however it is not the end of sequence (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_F_BIT_ZERO_S_BIT_ONE /* iSCSI connection error - S-bit and final bit = 1 (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_EXP_STAT_SN /* iSCSI connection error - STAT SN error (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DSL_NOT_ZERO /* iSCSI connection error - TMF or LOGOUT PDUs dsl > 0 (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_INVALID_DSL /* iSCSI connection error - CMD PDU dsl>0 while immediate data is disabled (Target only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DATA_SEG_LEN_TOO_BIG /* iSCSI connection error - Data In overrun (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_OUTSTANDING_R2T_COUNT /* iSCSI connection error - >1 outstanding R2T (Initiator only)  */,
	ISCSI_CONN_ERROR_PROTOCOL_ERR_DIF_TX /* iSCSI connection error - DIF Tx error + DIF error drop is enabled (Target only)  */,
	ISCSI_CONN_ERROR_SENSE_DATA_LENGTH /* iSCSI connection error - Sense data length > 256 (Initiator only)  */,
	ISCSI_CONN_ERROR_DATA_PLACEMENT_ERROR /* iSCSI connection error - Data placement error  */,
	ISCSI_CONN_ERROR_INVALID_ITT /* iSCSI connection error - Invalid ITT (Target Only) */,
	ISCSI_ERROR_UNKNOWN /* iSCSI connection error  */,
	MAX_ISCSI_ERROR_TYPES
};










/*
 * iSCSI Ramrod Command IDs 
 */
enum iscsi_ramrod_cmd_id
{
	ISCSI_RAMROD_CMD_ID_UNUSED=0,
	ISCSI_RAMROD_CMD_ID_INIT_FUNC=1 /* iSCSI function init Ramrod */,
	ISCSI_RAMROD_CMD_ID_DESTROY_FUNC=2 /* iSCSI function destroy Ramrod */,
	ISCSI_RAMROD_CMD_ID_OFFLOAD_CONN=3 /* iSCSI connection offload Ramrod */,
	ISCSI_RAMROD_CMD_ID_UPDATE_CONN=4 /* iSCSI connection update Ramrod  */,
	ISCSI_RAMROD_CMD_ID_TERMINATION_CONN=5 /* iSCSI connection offload Ramrod. Command ID known only to FW and VBD */,
	ISCSI_RAMROD_CMD_ID_CLEAR_SQ=6 /* iSCSI connection clear-sq ramrod.  */,
	ISCSI_RAMROD_CMD_ID_MAC_UPDATE=7 /* iSCSI connection update MAC address ramrod.  */,
	ISCSI_RAMROD_CMD_ID_CONN_STATS=8 /* iSCSI collect connection statistics ramrod.  */,
	MAX_ISCSI_RAMROD_CMD_ID
};







/*
 * ISCSI connection termination request
 */
struct iscsi_spe_conn_mac_update
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. */;
	__le32 fw_cid /* Context ID (cid) of the connection. */;
	__le16 remote_mac_addr_lo /* new remote mac address lo */;
	__le16 remote_mac_addr_mid /* new remote mac address mid */;
	__le16 remote_mac_addr_hi /* new remote mac address hi */;
	u8 reserved0[2];
};


/*
 * ISCSI and TCP connection(Option 1) offload params passed by driver to FW in ISCSI offload ramrod 
 */
struct iscsi_spe_conn_offload
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. */;
	__le32 fw_cid;
	struct iscsi_conn_offload_params iscsi /* iSCSI session offload params */;
	struct tcp_offload_params tcp /* iSCSI session offload params */;
};


/*
 * ISCSI and TCP connection(Option 2) offload params passed by driver to FW in ISCSI offload ramrod 
 */
struct iscsi_spe_conn_offload_option2
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. */;
	__le32 fw_cid;
	struct iscsi_conn_offload_params iscsi /* iSCSI session offload params */;
	struct tcp_offload_params_opt2 tcp /* iSCSI session offload params */;
};


/*
 * ISCSI collect connection statistics request
 */
struct iscsi_spe_conn_statistics
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. */;
	__le32 fw_cid /* Context ID (cid) of the connection. */;
	u8 reset_stats /* Indicates whether to reset the connection statistics. */;
	u8 reserved0[7];
	struct regpair stats_cnts_addr /* cmdq and unsolicited counters termination params */;
};


/*
 * ISCSI connection termination request
 */
struct iscsi_spe_conn_termination
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 conn_id /* ISCSI Connection ID. */;
	__le32 fw_cid /* Context ID (cid) of the connection. */;
	u8 abortive /* Mark termination as abort(reset) flow */;
	u8 reserved0[7];
	struct regpair queue_cnts_addr /* cmdq and unsolicited counters termination params */;
	struct regpair query_params_addr /* query_params_ptr */;
};


/*
 * iSCSI firmware function destroy parameters 
 */
struct iscsi_spe_func_dstry
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 reserved0;
	__le32 reserved1;
};


/*
 * iSCSI firmware function init parameters 
 */
struct iscsi_spe_func_init
{
	struct iscsi_slow_path_hdr hdr /* spe message header. */;
	__le16 half_way_close_timeout /* Half Way Close Timeout in Option 2 Close */;
	u8 num_sq_pages_in_ring /* Number of entries in the SQ PBL. Provided by driver at function init spe */;
	u8 num_r2tq_pages_in_ring /* Number of entries in the R2TQ PBL. Provided by driver at function init spe */;
	u8 num_uhq_pages_in_ring /* Number of entries in the uHQ PBL (xHQ entries is X2). Provided by driver at function init spe */;
	u8 ll2_rx_queue_id /* Queue ID of the Light-L2 Rx Queue */;
	u8 flags;
#define ISCSI_SPE_FUNC_INIT_COUNTERS_EN_MASK  0x1 /* Enable counters - function and connection counters */
#define ISCSI_SPE_FUNC_INIT_COUNTERS_EN_SHIFT 0
#define ISCSI_SPE_FUNC_INIT_RESERVED0_MASK    0x7F /* reserved */
#define ISCSI_SPE_FUNC_INIT_RESERVED0_SHIFT   1
	struct iscsi_debug_modes debug_mode /* Use iscsi_debug_mode flags */;
	__le16 reserved1;
	__le32 reserved2;
	struct scsi_init_func_params func_params /* Common SCSI init params passed by driver to FW in function init ramrod */;
	struct scsi_init_func_queues q_params /* SCSI RQ/CQ firmware function init parameters */;
};



/*
 * iSCSI task type
 */
enum iscsi_task_type
{
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
	ISCSI_TASK_TYPE_TARGET_IMM_W_DIF,
	MAX_ISCSI_TASK_TYPE
};






/*
 * iSCSI DesiredDataTransferLength/ttt union
 */
union iscsi_ttt_txlen_union
{
	__le32 desired_tx_len /* desired data transfer length */;
	__le32 ttt /* target transfer tag */;
};


/*
 * iSCSI uHQ element
 */
struct iscsi_uhqe
{
	__le32 reg1;
#define ISCSI_UHQE_PDU_PAYLOAD_LEN_MASK     0xFFFFF /* iSCSI payload (doesnt include padding or digest) or AHS length */
#define ISCSI_UHQE_PDU_PAYLOAD_LEN_SHIFT    0
#define ISCSI_UHQE_LOCAL_COMP_MASK          0x1 /* local compleiton flag */
#define ISCSI_UHQE_LOCAL_COMP_SHIFT         20
#define ISCSI_UHQE_TOGGLE_BIT_MASK          0x1 /* toggle bit to protect from uHQ full */
#define ISCSI_UHQE_TOGGLE_BIT_SHIFT         21
#define ISCSI_UHQE_PURE_PAYLOAD_MASK        0x1 /* indicates whether pdu_payload_len contains pure payload length. if not, pdu_payload_len is AHS length */
#define ISCSI_UHQE_PURE_PAYLOAD_SHIFT       22
#define ISCSI_UHQE_LOGIN_RESPONSE_PDU_MASK  0x1 /* indicates login pdu */
#define ISCSI_UHQE_LOGIN_RESPONSE_PDU_SHIFT 23
#define ISCSI_UHQE_TASK_ID_HI_MASK          0xFF /* most significant byte of task_id */
#define ISCSI_UHQE_TASK_ID_HI_SHIFT         24
	__le32 reg2;
#define ISCSI_UHQE_BUFFER_OFFSET_MASK       0xFFFFFF /* absolute offset in task */
#define ISCSI_UHQE_BUFFER_OFFSET_SHIFT      0
#define ISCSI_UHQE_TASK_ID_LO_MASK          0xFF /* least significant byte of task_id */
#define ISCSI_UHQE_TASK_ID_LO_SHIFT         24
};


/*
 * iSCSI WQ element 
 */
struct iscsi_wqe
{
	__le16 task_id /* The task identifier (itt) includes all the relevant information required for the task processing */;
	u8 flags;
#define ISCSI_WQE_WQE_TYPE_MASK  0x7 /* Wqe type [use iscsi_wqe_type] */
#define ISCSI_WQE_WQE_TYPE_SHIFT 0
#define ISCSI_WQE_NUM_SGES_MASK  0xF /* The driver will give a hint about sizes of SGEs for better credits evaluation at Xstorm */
#define ISCSI_WQE_NUM_SGES_SHIFT 3
#define ISCSI_WQE_RESPONSE_MASK  0x1 /* 1 if this Wqe triggers a response and advances stat_sn, 0 otherwise */
#define ISCSI_WQE_RESPONSE_SHIFT 7
	struct iscsi_dif_flags prot_flags /* Task data-integrity flags (protection) */;
	__le32 contlen_cdbsize;
#define ISCSI_WQE_CONT_LEN_MASK  0xFFFFFF /* expected/desired data transfer length */
#define ISCSI_WQE_CONT_LEN_SHIFT 0
#define ISCSI_WQE_CDB_SIZE_MASK  0xFF /* Initiator mode only: equals SCSI command CDB size if extended CDB is used, otherwise equals zero.  */
#define ISCSI_WQE_CDB_SIZE_SHIFT 24
};


/*
 * iSCSI wqe type 
 */
enum iscsi_wqe_type
{
	ISCSI_WQE_TYPE_NORMAL /* iSCSI WQE type normal. excluding status bit in target mode. */,
	ISCSI_WQE_TYPE_TASK_CLEANUP /* iSCSI WQE type task cleanup */,
	ISCSI_WQE_TYPE_MIDDLE_PATH /* iSCSI WQE type middle path */,
	ISCSI_WQE_TYPE_LOGIN /* iSCSI WQE type login */,
	ISCSI_WQE_TYPE_FIRST_R2T_CONT /* iSCSI WQE type First Write Continuation (Target) */,
	ISCSI_WQE_TYPE_NONFIRST_R2T_CONT /* iSCSI WQE type Non-First Write Continuation (Target) */,
	ISCSI_WQE_TYPE_RESPONSE /* iSCSI WQE type SCSI response */,
	MAX_ISCSI_WQE_TYPE
};


/*
 * iSCSI xHQ element
 */
struct iscsi_xhqe
{
	union iscsi_ttt_txlen_union ttt_or_txlen /* iSCSI DesiredDataTransferLength/ttt union */;
	__le32 exp_stat_sn /* expected StatSn */;
	struct iscsi_dif_flags prot_flags /* Task data-integrity flags (protection) */;
	u8 total_ahs_length /* Initiator mode only: Total AHS Length. greater than zero if and only if PDU is SCSI command and CDB > 16 */;
	u8 opcode /* Type opcode for command PDU */;
	u8 flags;
#define ISCSI_XHQE_FINAL_MASK       0x1 /* The Final(F) for this PDU */
#define ISCSI_XHQE_FINAL_SHIFT      0
#define ISCSI_XHQE_STATUS_BIT_MASK  0x1 /* Whether this PDU is Data-In PDU with status_bit = 1 */
#define ISCSI_XHQE_STATUS_BIT_SHIFT 1
#define ISCSI_XHQE_NUM_SGES_MASK    0xF /* If Predicted IO equals Min(8, number of SGEs in SGL), otherwise equals 0 */
#define ISCSI_XHQE_NUM_SGES_SHIFT   2
#define ISCSI_XHQE_RESERVED0_MASK   0x3 /* reserved */
#define ISCSI_XHQE_RESERVED0_SHIFT  6
	union iscsi_seq_num seq_num /* R2T/DataSN sequence number */;
	__le16 reserved1;
};



/*
 * Per PF iSCSI receive path statistics - mStorm RAM structure
 */
struct mstorm_iscsi_stats_drv
{
	struct regpair iscsi_rx_dropped_PDUs_task_not_valid /* Number of Rx silently dropped PDUs due to task not valid */;
	struct regpair iscsi_rx_dup_ack_cnt /* Received Dup-ACKs - after 3 dup ack, the counter doesnt count the same dup ack */;
};



/*
 * Per PF iSCSI transmit path statistics - pStorm RAM structure
 */
struct pstorm_iscsi_stats_drv
{
	struct regpair iscsi_tx_bytes_cnt /* Counts the number of tx bytes that were transmitted */;
	struct regpair iscsi_tx_packet_cnt /* Counts the number of tx packets that were transmitted */;
};



/*
 * Per PF iSCSI receive path statistics - tStorm RAM structure
 */
struct tstorm_iscsi_stats_drv
{
	struct regpair iscsi_rx_bytes_cnt /* Counts the number of rx bytes that were received */;
	struct regpair iscsi_rx_packet_cnt /* Counts the number of rx packets that were received */;
	struct regpair iscsi_rx_new_ooo_isle_events_cnt /* Counts the number of new out-of-order isle event */;
	struct regpair iscsi_rx_tcp_payload_bytes_cnt /* Received In-Order TCP Payload Bytes */;
	struct regpair iscsi_rx_tcp_pkt_cnt /* Received In-Order TCP Packets */;
	struct regpair iscsi_rx_pure_ack_cnt /* Received Pure-ACKs */;
	__le32 iscsi_cmdq_threshold_cnt /* Counts the number of times elements in cmdQ reached threshold */;
	__le32 iscsi_rq_threshold_cnt /* Counts the number of times elements in RQQ reached threshold */;
	__le32 iscsi_immq_threshold_cnt /* Counts the number of times elements in immQ reached threshold */;
};


/*
 * Per PF iSCSI receive path statistics - uStorm RAM structure
 */
struct ustorm_iscsi_stats_drv
{
	struct regpair iscsi_rx_data_pdu_cnt /* Number of data PDUs that were received */;
	struct regpair iscsi_rx_r2t_pdu_cnt /* Number of R2T PDUs that were received */;
	struct regpair iscsi_rx_total_pdu_cnt /* Number of total PDUs that were received */;
};



/*
 * Per PF iSCSI transmit path statistics - xStorm RAM structure
 */
struct xstorm_iscsi_stats_drv
{
	struct regpair iscsi_tx_go_to_slow_start_event_cnt /* Number of times slow start event occurred */;
	struct regpair iscsi_tx_fast_retransmit_event_cnt /* Number of times fast retransmit event occurred */;
	struct regpair iscsi_tx_pure_ack_cnt /* Transmitted Pure-ACKs */;
	struct regpair iscsi_tx_delayed_ack_cnt /* Transmitted Delayed ACKs */;
};


/*
 * Per PF iSCSI transmit path statistics - yStorm RAM structure
 */
struct ystorm_iscsi_stats_drv
{
	struct regpair iscsi_tx_data_pdu_cnt /* Number of data PDUs that were transmitted */;
	struct regpair iscsi_tx_r2t_pdu_cnt /* Number of R2T PDUs that were transmitted */;
	struct regpair iscsi_tx_total_pdu_cnt /* Number of total PDUs that were transmitted */;
	struct regpair iscsi_tx_tcp_payload_bytes_cnt /* Transmitted In-Order TCP Payload Bytes */;
	struct regpair iscsi_tx_tcp_pkt_cnt /* Transmitted In-Order TCP Packets */;
};






struct e4_tstorm_iscsi_task_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E4_TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK  0xF /* connection_type */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT 0
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT    4
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT    5
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT2_MASK     0x1 /* bit2 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT2_SHIFT    6
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT3_MASK     0x1 /* bit3 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT3_SHIFT    7
	u8 flags1;
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK     0x1 /* bit4 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT    0
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT5_MASK     0x1 /* bit5 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_BIT5_SHIFT    1
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT     2
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT     4
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT     6
	u8 flags2;
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT     0
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF4_SHIFT     2
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF5_SHIFT     4
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF6_SHIFT     6
	u8 flags3;
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF7_MASK      0x3 /* cf7 */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF7_SHIFT     0
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT   2
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT   3
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT   4
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT   5
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF4EN_SHIFT   6
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF5EN_SHIFT   7
	u8 flags4;
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF6EN_SHIFT   0
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF7EN_MASK    0x1 /* cf7en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_CF7EN_SHIFT   1
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT 2
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT 3
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT 4
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT 5
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT 6
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
};





struct e5_tstorm_iscsi_task_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E5_TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_MASK  0xF /* connection_type */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_NIBBLE0_SHIFT 0
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT0_SHIFT    4
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT1_SHIFT    5
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT2_MASK     0x1 /* bit2 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT2_SHIFT    6
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT3_MASK     0x1 /* bit3 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT3_SHIFT    7
	u8 flags1;
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT4_MASK     0x1 /* bit4 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT4_SHIFT    0
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT5_MASK     0x1 /* bit5 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_BIT5_SHIFT    1
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF0_SHIFT     2
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF1_SHIFT     4
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF2_SHIFT     6
	u8 flags2;
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF3_SHIFT     0
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF4_SHIFT     2
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF5_SHIFT     4
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF6_SHIFT     6
	u8 flags3;
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF7_MASK      0x3 /* cf7 */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF7_SHIFT     0
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF0EN_SHIFT   2
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF1EN_SHIFT   3
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF2EN_SHIFT   4
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF3EN_SHIFT   5
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF4EN_SHIFT   6
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF5EN_SHIFT   7
	u8 flags4;
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF6EN_SHIFT   0
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF7EN_MASK    0x1 /* cf7en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_CF7EN_SHIFT   1
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE0EN_SHIFT 2
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE1EN_SHIFT 3
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE2EN_SHIFT 4
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE3EN_SHIFT 5
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE4EN_SHIFT 6
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E5_TSTORM_ISCSI_TASK_AG_CTX_RULE5EN_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	u8 byte3 /* regpair0 */;
	u8 byte4 /* byte4 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg1 /* regpair1 */;
	__le32 reg2 /* reg2 */;
};




/*
 * iSCSI doorbell data
 */
struct iscsi_db_data
{
	u8 params;
#define ISCSI_DB_DATA_DEST_MASK         0x3 /* destination of doorbell (use enum db_dest) */
#define ISCSI_DB_DATA_DEST_SHIFT        0
#define ISCSI_DB_DATA_AGG_CMD_MASK      0x3 /* aggregative command to CM (use enum db_agg_cmd_sel) */
#define ISCSI_DB_DATA_AGG_CMD_SHIFT     2
#define ISCSI_DB_DATA_BYPASS_EN_MASK    0x1 /* enable QM bypass */
#define ISCSI_DB_DATA_BYPASS_EN_SHIFT   4
#define ISCSI_DB_DATA_RESERVED_MASK     0x1
#define ISCSI_DB_DATA_RESERVED_SHIFT    5
#define ISCSI_DB_DATA_AGG_VAL_SEL_MASK  0x3 /* aggregative value selection */
#define ISCSI_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8 agg_flags /* bit for every DQ counter flags in CM context that DQ can increment */;
	__le16 sq_prod;
};

#endif /* __ISCSI_COMMON__ */

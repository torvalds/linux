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

#ifndef __STORAGE_COMMON__
#define __STORAGE_COMMON__ 
/*********************/
/* SCSI CONSTANTS */
/*********************/


#define SCSI_MAX_NUM_OF_CMDQS (NUM_OF_GLOBAL_QUEUES / 2)
// Each Resource ID is one-one-valued mapped by the driver to a BDQ Resource ID (for instance per port)
#define BDQ_NUM_RESOURCES (4)

// ID 0 : RQ, ID 1 : IMMEDIATE_DATA, ID 2 : TQ
#define BDQ_ID_RQ			 (0)
#define BDQ_ID_IMM_DATA  	 (1)
#define BDQ_ID_TQ            (2)
#define BDQ_NUM_IDS          (3) 

#define SCSI_NUM_SGES_SLOW_SGL_THR	8

#define BDQ_MAX_EXTERNAL_RING_SIZE (1<<15)

/* SCSI op codes */
#define SCSI_OPCODE_COMPARE_AND_WRITE    (0x89)
#define SCSI_OPCODE_READ_10              (0x28)
#define SCSI_OPCODE_WRITE_6              (0x0A)
#define SCSI_OPCODE_WRITE_10             (0x2A)
#define SCSI_OPCODE_WRITE_12             (0xAA)
#define SCSI_OPCODE_WRITE_16             (0x8A)
#define SCSI_OPCODE_WRITE_AND_VERIFY_10  (0x2E)
#define SCSI_OPCODE_WRITE_AND_VERIFY_12  (0xAE)
#define SCSI_OPCODE_WRITE_AND_VERIFY_16  (0x8E)

/*
 * iSCSI Drv opaque
 */
struct iscsi_drv_opaque
{
	__le16 reserved_zero[3];
	__le16 opaque;
};


/*
 * Scsi 2B/8B opaque union
 */
union scsi_opaque
{
	struct regpair fcoe_opaque /* 8 Bytes opaque */;
	struct iscsi_drv_opaque iscsi_opaque /* 2 Bytes opaque */;
};

/*
 * SCSI buffer descriptor
 */
struct scsi_bd
{
	struct regpair address /* Physical Address of buffer */;
	union scsi_opaque opaque /* Driver Metadata (preferably Virtual Address of buffer) */;
};


/*
 * Scsi Drv BDQ struct
 */
struct scsi_bdq_ram_drv_data
{
	__le16 external_producer /* BDQ External Producer; updated by driver when it loads BDs to External Ring */;
	__le16 reserved0[3];
};


/*
 * SCSI SGE entry
 */
struct scsi_sge
{
	struct regpair sge_addr /* SGE address */;
	__le32 sge_len /* SGE length */;
	__le32 reserved;
};

/*
 * Cached SGEs section
 */
struct scsi_cached_sges
{
	struct scsi_sge sge[4] /* Cached SGEs section */;
};


/*
 * Scsi Drv CMDQ struct
 */
struct scsi_drv_cmdq
{
	__le16 cmdq_cons /* CMDQ consumer - updated by driver when CMDQ is consumed */;
	__le16 reserved0;
	__le32 reserved1;
};


/*
 * Common SCSI init params passed by driver to FW in function init ramrod 
 */
struct scsi_init_func_params
{
	__le16 num_tasks /* Number of tasks in global task list */;
	u8 log_page_size /* log of page size value */;
	u8 debug_mode /* Use iscsi_debug_mode enum */;
	u8 reserved2[12];
};


/*
 * SCSI RQ/CQ/CMDQ firmware function init parameters
 */
struct scsi_init_func_queues
{
	struct regpair glbl_q_params_addr /* Global Qs (CQ/RQ/CMDQ) params host address */;
	__le16 rq_buffer_size /* The buffer size of RQ BDQ */;
	__le16 cq_num_entries /* CQ num entries */;
	__le16 cmdq_num_entries /* CMDQ num entries */;
	u8 bdq_resource_id /* Each function-init Ramrod maps its funciton ID to a BDQ function ID, each BDQ function ID contains per-BDQ-ID BDQs */;
	u8 q_validity;
#define SCSI_INIT_FUNC_QUEUES_RQ_VALID_MASK               0x1
#define SCSI_INIT_FUNC_QUEUES_RQ_VALID_SHIFT              0
#define SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID_MASK         0x1
#define SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID_SHIFT        1
#define SCSI_INIT_FUNC_QUEUES_CMD_VALID_MASK              0x1
#define SCSI_INIT_FUNC_QUEUES_CMD_VALID_SHIFT             2
#define SCSI_INIT_FUNC_QUEUES_TQ_VALID_MASK               0x1
#define SCSI_INIT_FUNC_QUEUES_TQ_VALID_SHIFT              3
#define SCSI_INIT_FUNC_QUEUES_SOC_EN_MASK                 0x1 /* This bit is valid if TQ is enabled for this function, SOC option enabled/disabled */
#define SCSI_INIT_FUNC_QUEUES_SOC_EN_SHIFT                4
#define SCSI_INIT_FUNC_QUEUES_SOC_NUM_OF_BLOCKS_LOG_MASK  0x7 /* Relevant for TQe SOC option - num of blocks in SGE - log */
#define SCSI_INIT_FUNC_QUEUES_SOC_NUM_OF_BLOCKS_LOG_SHIFT 5
	__le16 cq_cmdq_sb_num_arr[SCSI_MAX_NUM_OF_CMDQS] /* CQ/CMDQ status block number array */;
	u8 num_queues /* Number of continuous global queues used */;
	u8 queue_relative_offset /* offset of continuous global queues used */;
	u8 cq_sb_pi /* Protocol Index of CQ in status block (CQ consumer) */;
	u8 cmdq_sb_pi /* Protocol Index of CMDQ in status block (CMDQ consumer) */;
	u8 bdq_pbl_num_entries[BDQ_NUM_IDS] /* Per BDQ ID, the PBL page size (number of entries in PBL) */;
	u8 reserved1 /* reserved */;
	struct regpair bdq_pbl_base_address[BDQ_NUM_IDS] /* Per BDQ ID, the PBL page Base Address */;
	__le16 bdq_xoff_threshold[BDQ_NUM_IDS] /* BDQ XOFF threshold - when number of entries will be below that TH, it will send XOFF */;
	__le16 cmdq_xoff_threshold /* CMDQ XOFF threshold - when number of entries will be below that TH, it will send XOFF */;
	__le16 bdq_xon_threshold[BDQ_NUM_IDS] /* BDQ XON threshold - when number of entries will be above that TH, it will send XON */;
	__le16 cmdq_xon_threshold /* CMDQ XON threshold - when number of entries will be above that TH, it will send XON */;
};



/*
 * Scsi Drv BDQ Data struct (2 BDQ IDs: 0 - RQ, 1 - Immediate Data)
 */
struct scsi_ram_per_bdq_resource_drv_data
{
	struct scsi_bdq_ram_drv_data drv_data_per_bdq_id[BDQ_NUM_IDS] /* External ring data */;
};



/*
 * SCSI SGL types
 */
enum scsi_sgl_mode
{
	SCSI_TX_SLOW_SGL /* Slow-SGL: More than SCSI_NUM_SGES_SLOW_SGL_THR SGEs and there is at least 1 middle SGE than is smaller than a page size. May be only at TX  */,
	SCSI_FAST_SGL /* Fast SGL: Less than SCSI_NUM_SGES_SLOW_SGL_THR SGEs or all middle SGEs are at least a page size */,
	MAX_SCSI_SGL_MODE
};


/*
 * SCSI SGL parameters
 */
struct scsi_sgl_params
{
	struct regpair sgl_addr /* SGL base address */;
	__le32 sgl_total_length /* SGL total legnth (bytes)  */;
	__le32 sge_offset /* Offset in SGE (bytes) */;
	__le16 sgl_num_sges /* Number of SGLs sges */;
	u8 sgl_index /* SGL index */;
	u8 reserved;
};


/*
 * SCSI terminate connection params
 */
struct scsi_terminate_extra_params
{
	__le16 unsolicited_cq_count /* Counts number of CQ placements done due to arrival of unsolicited packets on this connection */;
	__le16 cmdq_count /* Counts number of CMDQ placements on this connection */;
	u8 reserved[4];
};


/*
 * SCSI Task Queue Element
 */
struct scsi_tqe
{
	__le16 itid /* Physical Address of buffer */;
};

#endif /* __STORAGE_COMMON__ */

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

#ifndef __STORAGE_COMMON__
#define __STORAGE_COMMON__

/*********************/
/* SCSI CONSTANTS */
/*********************/

#define SCSI_MAX_NUM_OF_CMDQS		(NUM_OF_GLOBAL_QUEUES / 2)
#define BDQ_NUM_RESOURCES		(4)

#define BDQ_ID_RQ			(0)
#define BDQ_ID_IMM_DATA			(1)
#define BDQ_ID_TQ			(2)
#define BDQ_NUM_IDS			(3)

#define SCSI_NUM_SGES_SLOW_SGL_THR	8

#define BDQ_MAX_EXTERNAL_RING_SIZE	BIT(15)

/* SCSI op codes */
#define SCSI_OPCODE_COMPARE_AND_WRITE	(0x89)
#define SCSI_OPCODE_READ_10		(0x28)
#define SCSI_OPCODE_WRITE_6		(0x0A)
#define SCSI_OPCODE_WRITE_10		(0x2A)
#define SCSI_OPCODE_WRITE_12		(0xAA)
#define SCSI_OPCODE_WRITE_16		(0x8A)
#define SCSI_OPCODE_WRITE_AND_VERIFY_10	(0x2E)
#define SCSI_OPCODE_WRITE_AND_VERIFY_12	(0xAE)
#define SCSI_OPCODE_WRITE_AND_VERIFY_16	(0x8E)

/* iSCSI Drv opaque */
struct iscsi_drv_opaque {
	__le16 reserved_zero[3];
	__le16 opaque;
};

/* Scsi 2B/8B opaque union */
union scsi_opaque {
	struct regpair fcoe_opaque;
	struct iscsi_drv_opaque iscsi_opaque;
};

/* SCSI buffer descriptor */
struct scsi_bd {
	struct regpair address;
	union scsi_opaque opaque;
};

/* Scsi Drv BDQ struct */
struct scsi_bdq_ram_drv_data {
	__le16 external_producer;
	__le16 reserved0[3];
};

/* SCSI SGE entry */
struct scsi_sge {
	struct regpair sge_addr;
	__le32 sge_len;
	__le32 reserved;
};

/* Cached SGEs section */
struct scsi_cached_sges {
	struct scsi_sge sge[4];
};

/* Scsi Drv CMDQ struct */
struct scsi_drv_cmdq {
	__le16 cmdq_cons;
	__le16 reserved0;
	__le32 reserved1;
};

/* Common SCSI init params passed by driver to FW in function init ramrod */
struct scsi_init_func_params {
	__le16 num_tasks;
	u8 log_page_size;
	u8 debug_mode;
	u8 reserved2[12];
};

/* SCSI RQ/CQ/CMDQ firmware function init parameters */
struct scsi_init_func_queues {
	struct regpair glbl_q_params_addr;
	__le16 rq_buffer_size;
	__le16 cq_num_entries;
	__le16 cmdq_num_entries;
	u8 bdq_resource_id;
	u8 q_validity;
#define SCSI_INIT_FUNC_QUEUES_RQ_VALID_MASK			0x1
#define SCSI_INIT_FUNC_QUEUES_RQ_VALID_SHIFT			0
#define SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID_MASK		0x1
#define SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID_SHIFT		1
#define SCSI_INIT_FUNC_QUEUES_CMD_VALID_MASK			0x1
#define SCSI_INIT_FUNC_QUEUES_CMD_VALID_SHIFT			2
#define SCSI_INIT_FUNC_QUEUES_TQ_VALID_MASK			0x1
#define SCSI_INIT_FUNC_QUEUES_TQ_VALID_SHIFT			3
#define SCSI_INIT_FUNC_QUEUES_SOC_EN_MASK			0x1
#define SCSI_INIT_FUNC_QUEUES_SOC_EN_SHIFT			4
#define SCSI_INIT_FUNC_QUEUES_SOC_NUM_OF_BLOCKS_LOG_MASK	0x7
#define SCSI_INIT_FUNC_QUEUES_SOC_NUM_OF_BLOCKS_LOG_SHIFT	5
	__le16 cq_cmdq_sb_num_arr[SCSI_MAX_NUM_OF_CMDQS];
	u8 num_queues;
	u8 queue_relative_offset;
	u8 cq_sb_pi;
	u8 cmdq_sb_pi;
	u8 bdq_pbl_num_entries[BDQ_NUM_IDS];
	u8 reserved1;
	struct regpair bdq_pbl_base_address[BDQ_NUM_IDS];
	__le16 bdq_xoff_threshold[BDQ_NUM_IDS];
	__le16 cmdq_xoff_threshold;
	__le16 bdq_xon_threshold[BDQ_NUM_IDS];
	__le16 cmdq_xon_threshold;
};

/* Scsi Drv BDQ Data struct (2 BDQ IDs: 0 - RQ, 1 - Immediate Data) */
struct scsi_ram_per_bdq_resource_drv_data {
	struct scsi_bdq_ram_drv_data drv_data_per_bdq_id[BDQ_NUM_IDS];
};

/* SCSI SGL types */
enum scsi_sgl_mode {
	SCSI_TX_SLOW_SGL,
	SCSI_FAST_SGL,
	MAX_SCSI_SGL_MODE
};

/* SCSI SGL parameters */
struct scsi_sgl_params {
	struct regpair sgl_addr;
	__le32 sgl_total_length;
	__le32 sge_offset;
	__le16 sgl_num_sges;
	u8 sgl_index;
	u8 reserved;
};

/* SCSI terminate connection params */
struct scsi_terminate_extra_params {
	__le16 unsolicited_cq_count;
	__le16 cmdq_count;
	u8 reserved[4];
};

/* SCSI Task Queue Element */
struct scsi_tqe {
	__le16 itid;
};

#endif /* __STORAGE_COMMON__ */

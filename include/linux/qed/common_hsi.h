/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef __COMMON_HSI__
#define __COMMON_HSI__

#define CORE_SPQE_PAGE_SIZE_BYTES                       4096

#define X_FINAL_CLEANUP_AGG_INT 1

/* Queue Zone sizes in bytes */
#define TSTORM_QZONE_SIZE 8
#define MSTORM_QZONE_SIZE 0
#define USTORM_QZONE_SIZE 8
#define XSTORM_QZONE_SIZE 8
#define YSTORM_QZONE_SIZE 0
#define PSTORM_QZONE_SIZE 0

#define ETH_MAX_NUM_RX_QUEUES_PER_VF 16

#define FW_MAJOR_VERSION	8
#define FW_MINOR_VERSION	10
#define FW_REVISION_VERSION	5
#define FW_ENGINEERING_VERSION	0

/***********************/
/* COMMON HW CONSTANTS */
/***********************/

/* PCI functions */
#define MAX_NUM_PORTS_K2	(4)
#define MAX_NUM_PORTS_BB	(2)
#define MAX_NUM_PORTS		(MAX_NUM_PORTS_K2)

#define MAX_NUM_PFS_K2	(16)
#define MAX_NUM_PFS_BB	(8)
#define MAX_NUM_PFS	(MAX_NUM_PFS_K2)
#define MAX_NUM_OF_PFS_IN_CHIP (16) /* On both engines */

#define MAX_NUM_VFS_K2	(192)
#define MAX_NUM_VFS_BB	(120)
#define MAX_NUM_VFS	(MAX_NUM_VFS_K2)

#define MAX_NUM_FUNCTIONS_BB	(MAX_NUM_PFS_BB + MAX_NUM_VFS_BB)
#define MAX_NUM_FUNCTIONS	(MAX_NUM_PFS + MAX_NUM_VFS)

#define MAX_FUNCTION_NUMBER_BB	(MAX_NUM_PFS + MAX_NUM_VFS_BB)
#define MAX_FUNCTION_NUMBER	(MAX_NUM_PFS + MAX_NUM_VFS)

#define MAX_NUM_VPORTS_K2	(208)
#define MAX_NUM_VPORTS_BB	(160)
#define MAX_NUM_VPORTS		(MAX_NUM_VPORTS_K2)

#define MAX_NUM_L2_QUEUES_K2	(320)
#define MAX_NUM_L2_QUEUES_BB	(256)
#define MAX_NUM_L2_QUEUES	(MAX_NUM_L2_QUEUES_K2)

/* Traffic classes in network-facing blocks (PBF, BTB, NIG, BRB, PRS and QM) */
#define NUM_PHYS_TCS_4PORT_K2	(4)
#define NUM_OF_PHYS_TCS		(8)

#define NUM_TCS_4PORT_K2	(NUM_PHYS_TCS_4PORT_K2 + 1)
#define NUM_OF_TCS		(NUM_OF_PHYS_TCS + 1)

#define LB_TC			(NUM_OF_PHYS_TCS)

/* Num of possible traffic priority values */
#define NUM_OF_PRIO		(8)

#define MAX_NUM_VOQS_K2		(NUM_TCS_4PORT_K2 * MAX_NUM_PORTS_K2)
#define MAX_NUM_VOQS_BB		(NUM_OF_TCS * MAX_NUM_PORTS_BB)
#define MAX_NUM_VOQS		(MAX_NUM_VOQS_K2)
#define MAX_PHYS_VOQS		(NUM_OF_PHYS_TCS * MAX_NUM_PORTS_BB)

/* CIDs */
#define NUM_OF_CONNECTION_TYPES	(8)
#define NUM_OF_LCIDS		(320)
#define NUM_OF_LTIDS		(320)

/*****************/
/* CDU CONSTANTS */
/*****************/

#define CDU_SEG_TYPE_OFFSET_REG_TYPE_SHIFT              (17)
#define CDU_SEG_TYPE_OFFSET_REG_OFFSET_MASK             (0x1ffff)

/*****************/
/* DQ CONSTANTS  */
/*****************/

/* DEMS */
#define DQ_DEMS_LEGACY			0

/* XCM agg val selection */
#define DQ_XCM_AGG_VAL_SEL_WORD2  0
#define DQ_XCM_AGG_VAL_SEL_WORD3  1
#define DQ_XCM_AGG_VAL_SEL_WORD4  2
#define DQ_XCM_AGG_VAL_SEL_WORD5  3
#define DQ_XCM_AGG_VAL_SEL_REG3   4
#define DQ_XCM_AGG_VAL_SEL_REG4   5
#define DQ_XCM_AGG_VAL_SEL_REG5   6
#define DQ_XCM_AGG_VAL_SEL_REG6   7

/* XCM agg val selection */
#define	DQ_XCM_CORE_TX_BD_CONS_CMD	DQ_XCM_AGG_VAL_SEL_WORD3
#define	DQ_XCM_CORE_TX_BD_PROD_CMD	DQ_XCM_AGG_VAL_SEL_WORD4
#define	DQ_XCM_CORE_SPQ_PROD_CMD	DQ_XCM_AGG_VAL_SEL_WORD4
#define	DQ_XCM_ETH_EDPM_NUM_BDS_CMD	DQ_XCM_AGG_VAL_SEL_WORD2
#define	DQ_XCM_ETH_TX_BD_CONS_CMD	DQ_XCM_AGG_VAL_SEL_WORD3
#define	DQ_XCM_ETH_TX_BD_PROD_CMD	DQ_XCM_AGG_VAL_SEL_WORD4
#define	DQ_XCM_ETH_GO_TO_BD_CONS_CMD	DQ_XCM_AGG_VAL_SEL_WORD5

/* UCM agg val selection (HW) */
#define	DQ_UCM_AGG_VAL_SEL_WORD0	0
#define	DQ_UCM_AGG_VAL_SEL_WORD1	1
#define	DQ_UCM_AGG_VAL_SEL_WORD2	2
#define	DQ_UCM_AGG_VAL_SEL_WORD3	3
#define	DQ_UCM_AGG_VAL_SEL_REG0	4
#define	DQ_UCM_AGG_VAL_SEL_REG1	5
#define	DQ_UCM_AGG_VAL_SEL_REG2	6
#define	DQ_UCM_AGG_VAL_SEL_REG3	7

/* UCM agg val selection (FW) */
#define DQ_UCM_ETH_PMD_TX_CONS_CMD	DQ_UCM_AGG_VAL_SEL_WORD2
#define DQ_UCM_ETH_PMD_RX_CONS_CMD	DQ_UCM_AGG_VAL_SEL_WORD3
#define DQ_UCM_ROCE_CQ_CONS_CMD		DQ_UCM_AGG_VAL_SEL_REG0
#define DQ_UCM_ROCE_CQ_PROD_CMD		DQ_UCM_AGG_VAL_SEL_REG2

/* TCM agg val selection (HW) */
#define	DQ_TCM_AGG_VAL_SEL_WORD0	0
#define	DQ_TCM_AGG_VAL_SEL_WORD1	1
#define	DQ_TCM_AGG_VAL_SEL_WORD2	2
#define	DQ_TCM_AGG_VAL_SEL_WORD3	3
#define	DQ_TCM_AGG_VAL_SEL_REG1		4
#define	DQ_TCM_AGG_VAL_SEL_REG2		5
#define	DQ_TCM_AGG_VAL_SEL_REG6		6
#define	DQ_TCM_AGG_VAL_SEL_REG9		7

/* TCM agg val selection (FW) */
#define DQ_TCM_L2B_BD_PROD_CMD \
	DQ_TCM_AGG_VAL_SEL_WORD1
#define DQ_TCM_ROCE_RQ_PROD_CMD	\
	DQ_TCM_AGG_VAL_SEL_WORD0

/* XCM agg counter flag selection */
#define	DQ_XCM_AGG_FLG_SHIFT_BIT14	0
#define	DQ_XCM_AGG_FLG_SHIFT_BIT15	1
#define	DQ_XCM_AGG_FLG_SHIFT_CF12	2
#define	DQ_XCM_AGG_FLG_SHIFT_CF13	3
#define	DQ_XCM_AGG_FLG_SHIFT_CF18	4
#define	DQ_XCM_AGG_FLG_SHIFT_CF19	5
#define	DQ_XCM_AGG_FLG_SHIFT_CF22	6
#define	DQ_XCM_AGG_FLG_SHIFT_CF23	7

/* XCM agg counter flag selection */
#define DQ_XCM_CORE_DQ_CF_CMD		(1 << DQ_XCM_AGG_FLG_SHIFT_CF18)
#define DQ_XCM_CORE_TERMINATE_CMD	(1 << DQ_XCM_AGG_FLG_SHIFT_CF19)
#define DQ_XCM_CORE_SLOW_PATH_CMD	(1 << DQ_XCM_AGG_FLG_SHIFT_CF22)
#define DQ_XCM_ETH_DQ_CF_CMD		(1 << DQ_XCM_AGG_FLG_SHIFT_CF18)
#define DQ_XCM_ETH_TERMINATE_CMD	(1 << DQ_XCM_AGG_FLG_SHIFT_CF19)
#define DQ_XCM_ETH_SLOW_PATH_CMD	(1 << DQ_XCM_AGG_FLG_SHIFT_CF22)
#define DQ_XCM_ETH_TPH_EN_CMD		(1 << DQ_XCM_AGG_FLG_SHIFT_CF23)

/* UCM agg counter flag selection (HW) */
#define	DQ_UCM_AGG_FLG_SHIFT_CF0	0
#define	DQ_UCM_AGG_FLG_SHIFT_CF1	1
#define	DQ_UCM_AGG_FLG_SHIFT_CF3	2
#define	DQ_UCM_AGG_FLG_SHIFT_CF4	3
#define	DQ_UCM_AGG_FLG_SHIFT_CF5	4
#define	DQ_UCM_AGG_FLG_SHIFT_CF6	5
#define	DQ_UCM_AGG_FLG_SHIFT_RULE0EN	6
#define	DQ_UCM_AGG_FLG_SHIFT_RULE1EN	7

/* UCM agg counter flag selection (FW) */
#define DQ_UCM_ETH_PMD_TX_ARM_CMD	(1 << DQ_UCM_AGG_FLG_SHIFT_CF4)
#define DQ_UCM_ETH_PMD_RX_ARM_CMD	(1 << DQ_UCM_AGG_FLG_SHIFT_CF5)

#define	DQ_REGION_SHIFT	(12)

/* DPM */
#define	DQ_DPM_WQE_BUFF_SIZE	(320)

/* Conn type ranges */
#define	DQ_CONN_TYPE_RANGE_SHIFT	(4)

/*****************/
/* QM CONSTANTS  */
/*****************/

/* number of TX queues in the QM */
#define MAX_QM_TX_QUEUES_K2	512
#define MAX_QM_TX_QUEUES_BB	448
#define MAX_QM_TX_QUEUES	MAX_QM_TX_QUEUES_K2

/* number of Other queues in the QM */
#define MAX_QM_OTHER_QUEUES_BB	64
#define MAX_QM_OTHER_QUEUES_K2	128
#define MAX_QM_OTHER_QUEUES	MAX_QM_OTHER_QUEUES_K2

/* number of queues in a PF queue group */
#define QM_PF_QUEUE_GROUP_SIZE	8

/* the size of a single queue element in bytes */
#define QM_PQ_ELEMENT_SIZE                      4

/* base number of Tx PQs in the CM PQ representation.
 * should be used when storing PQ IDs in CM PQ registers and context
 */
#define CM_TX_PQ_BASE	0x200

/* QM registers data */
#define QM_LINE_CRD_REG_WIDTH		16
#define QM_LINE_CRD_REG_SIGN_BIT	(1 << (QM_LINE_CRD_REG_WIDTH - 1))
#define QM_BYTE_CRD_REG_WIDTH		24
#define QM_BYTE_CRD_REG_SIGN_BIT	(1 << (QM_BYTE_CRD_REG_WIDTH - 1))
#define QM_WFQ_CRD_REG_WIDTH		32
#define QM_WFQ_CRD_REG_SIGN_BIT		(1 << (QM_WFQ_CRD_REG_WIDTH - 1))
#define QM_RL_CRD_REG_WIDTH		32
#define QM_RL_CRD_REG_SIGN_BIT		(1 << (QM_RL_CRD_REG_WIDTH - 1))

/*****************/
/* CAU CONSTANTS */
/*****************/

#define CAU_FSM_ETH_RX  0
#define CAU_FSM_ETH_TX  1

/* Number of Protocol Indices per Status Block */
#define PIS_PER_SB    12

#define CAU_HC_STOPPED_STATE	3
#define CAU_HC_DISABLE_STATE	4
#define CAU_HC_ENABLE_STATE	0

/*****************/
/* IGU CONSTANTS */
/*****************/

#define MAX_SB_PER_PATH_K2	(368)
#define MAX_SB_PER_PATH_BB	(288)
#define MAX_TOT_SB_PER_PATH \
	MAX_SB_PER_PATH_K2

#define MAX_SB_PER_PF_MIMD	129
#define MAX_SB_PER_PF_SIMD	64
#define MAX_SB_PER_VF		64

/* Memory addresses on the BAR for the IGU Sub Block */
#define IGU_MEM_BASE			0x0000

#define IGU_MEM_MSIX_BASE		0x0000
#define IGU_MEM_MSIX_UPPER		0x0101
#define IGU_MEM_MSIX_RESERVED_UPPER	0x01ff

#define IGU_MEM_PBA_MSIX_BASE		0x0200
#define IGU_MEM_PBA_MSIX_UPPER		0x0202
#define IGU_MEM_PBA_MSIX_RESERVED_UPPER	0x03ff

#define IGU_CMD_INT_ACK_BASE		0x0400
#define IGU_CMD_INT_ACK_UPPER		(IGU_CMD_INT_ACK_BASE +	\
					 MAX_TOT_SB_PER_PATH -	\
					 1)
#define IGU_CMD_INT_ACK_RESERVED_UPPER	0x05ff

#define IGU_CMD_ATTN_BIT_UPD_UPPER	0x05f0
#define IGU_CMD_ATTN_BIT_SET_UPPER	0x05f1
#define IGU_CMD_ATTN_BIT_CLR_UPPER	0x05f2

#define IGU_REG_SISR_MDPC_WMASK_UPPER		0x05f3
#define IGU_REG_SISR_MDPC_WMASK_LSB_UPPER	0x05f4
#define IGU_REG_SISR_MDPC_WMASK_MSB_UPPER	0x05f5
#define IGU_REG_SISR_MDPC_WOMASK_UPPER		0x05f6

#define IGU_CMD_PROD_UPD_BASE			0x0600
#define IGU_CMD_PROD_UPD_UPPER			(IGU_CMD_PROD_UPD_BASE +\
						 MAX_TOT_SB_PER_PATH - \
						 1)
#define IGU_CMD_PROD_UPD_RESERVED_UPPER		0x07ff

/*****************/
/* PXP CONSTANTS */
/*****************/

/* PTT and GTT */
#define PXP_NUM_PF_WINDOWS		12
#define PXP_PER_PF_ENTRY_SIZE		8
#define PXP_NUM_GLOBAL_WINDOWS		243
#define PXP_GLOBAL_ENTRY_SIZE		4
#define PXP_ADMIN_WINDOW_ALLOWED_LENGTH	4
#define PXP_PF_WINDOW_ADMIN_START	0
#define PXP_PF_WINDOW_ADMIN_LENGTH	0x1000
#define PXP_PF_WINDOW_ADMIN_END		(PXP_PF_WINDOW_ADMIN_START + \
					 PXP_PF_WINDOW_ADMIN_LENGTH - 1)
#define PXP_PF_WINDOW_ADMIN_PER_PF_START	0
#define PXP_PF_WINDOW_ADMIN_PER_PF_LENGTH	(PXP_NUM_PF_WINDOWS * \
						 PXP_PER_PF_ENTRY_SIZE)
#define PXP_PF_WINDOW_ADMIN_PER_PF_END	(PXP_PF_WINDOW_ADMIN_PER_PF_START + \
					 PXP_PF_WINDOW_ADMIN_PER_PF_LENGTH - 1)
#define PXP_PF_WINDOW_ADMIN_GLOBAL_START	0x200
#define PXP_PF_WINDOW_ADMIN_GLOBAL_LENGTH	(PXP_NUM_GLOBAL_WINDOWS * \
						 PXP_GLOBAL_ENTRY_SIZE)
#define PXP_PF_WINDOW_ADMIN_GLOBAL_END \
		(PXP_PF_WINDOW_ADMIN_GLOBAL_START + \
		 PXP_PF_WINDOW_ADMIN_GLOBAL_LENGTH - 1)
#define PXP_PF_GLOBAL_PRETEND_ADDR	0x1f0
#define PXP_PF_ME_OPAQUE_MASK_ADDR	0xf4
#define PXP_PF_ME_OPAQUE_ADDR		0x1f8
#define PXP_PF_ME_CONCRETE_ADDR		0x1fc

#define PXP_EXTERNAL_BAR_PF_WINDOW_START	0x1000
#define PXP_EXTERNAL_BAR_PF_WINDOW_NUM		PXP_NUM_PF_WINDOWS
#define PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE	0x1000
#define PXP_EXTERNAL_BAR_PF_WINDOW_LENGTH \
	(PXP_EXTERNAL_BAR_PF_WINDOW_NUM * \
	 PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE)
#define PXP_EXTERNAL_BAR_PF_WINDOW_END \
	(PXP_EXTERNAL_BAR_PF_WINDOW_START + \
	 PXP_EXTERNAL_BAR_PF_WINDOW_LENGTH - 1)

#define PXP_EXTERNAL_BAR_GLOBAL_WINDOW_START \
	(PXP_EXTERNAL_BAR_PF_WINDOW_END + 1)
#define PXP_EXTERNAL_BAR_GLOBAL_WINDOW_NUM		PXP_NUM_GLOBAL_WINDOWS
#define PXP_EXTERNAL_BAR_GLOBAL_WINDOW_SINGLE_SIZE	0x1000
#define PXP_EXTERNAL_BAR_GLOBAL_WINDOW_LENGTH \
	(PXP_EXTERNAL_BAR_GLOBAL_WINDOW_NUM * \
	 PXP_EXTERNAL_BAR_GLOBAL_WINDOW_SINGLE_SIZE)
#define PXP_EXTERNAL_BAR_GLOBAL_WINDOW_END \
	(PXP_EXTERNAL_BAR_GLOBAL_WINDOW_START + \
	 PXP_EXTERNAL_BAR_GLOBAL_WINDOW_LENGTH - 1)


#define PXP_VF_BAR0_START_IGU                   0
#define PXP_VF_BAR0_IGU_LENGTH                  0x3000
#define PXP_VF_BAR0_END_IGU                     (PXP_VF_BAR0_START_IGU + \
						 PXP_VF_BAR0_IGU_LENGTH - 1)

#define PXP_VF_BAR0_START_DQ                    0x3000
#define PXP_VF_BAR0_DQ_LENGTH                   0x200
#define PXP_VF_BAR0_DQ_OPAQUE_OFFSET            0
#define PXP_VF_BAR0_ME_OPAQUE_ADDRESS           (PXP_VF_BAR0_START_DQ +	\
						 PXP_VF_BAR0_DQ_OPAQUE_OFFSET)
#define PXP_VF_BAR0_ME_CONCRETE_ADDRESS         (PXP_VF_BAR0_ME_OPAQUE_ADDRESS \
						 + 4)
#define PXP_VF_BAR0_END_DQ                      (PXP_VF_BAR0_START_DQ +	\
						 PXP_VF_BAR0_DQ_LENGTH - 1)

#define PXP_VF_BAR0_START_TSDM_ZONE_B           0x3200
#define PXP_VF_BAR0_SDM_LENGTH_ZONE_B           0x200
#define PXP_VF_BAR0_END_TSDM_ZONE_B             (PXP_VF_BAR0_START_TSDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_MSDM_ZONE_B           0x3400
#define PXP_VF_BAR0_END_MSDM_ZONE_B             (PXP_VF_BAR0_START_MSDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_USDM_ZONE_B           0x3600
#define PXP_VF_BAR0_END_USDM_ZONE_B             (PXP_VF_BAR0_START_USDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_XSDM_ZONE_B           0x3800
#define PXP_VF_BAR0_END_XSDM_ZONE_B             (PXP_VF_BAR0_START_XSDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_YSDM_ZONE_B           0x3a00
#define PXP_VF_BAR0_END_YSDM_ZONE_B             (PXP_VF_BAR0_START_YSDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_PSDM_ZONE_B           0x3c00
#define PXP_VF_BAR0_END_PSDM_ZONE_B             (PXP_VF_BAR0_START_PSDM_ZONE_B \
						 +			       \
						 PXP_VF_BAR0_SDM_LENGTH_ZONE_B \
						 - 1)

#define PXP_VF_BAR0_START_SDM_ZONE_A            0x4000
#define PXP_VF_BAR0_END_SDM_ZONE_A              0x10000

#define PXP_VF_BAR0_GRC_WINDOW_LENGTH           32

#define PXP_ILT_PAGE_SIZE_NUM_BITS_MIN		12
#define PXP_ILT_BLOCK_FACTOR_MULTIPLIER		1024

/* ILT Records */
#define PXP_NUM_ILT_RECORDS_BB 7600
#define PXP_NUM_ILT_RECORDS_K2 11000
#define MAX_NUM_ILT_RECORDS MAX(PXP_NUM_ILT_RECORDS_BB, PXP_NUM_ILT_RECORDS_K2)

#define SDM_COMP_TYPE_NONE              0
#define SDM_COMP_TYPE_WAKE_THREAD       1
#define SDM_COMP_TYPE_AGG_INT           2
#define SDM_COMP_TYPE_CM                3
#define SDM_COMP_TYPE_LOADER            4
#define SDM_COMP_TYPE_PXP               5
#define SDM_COMP_TYPE_INDICATE_ERROR    6
#define SDM_COMP_TYPE_RELEASE_THREAD    7
#define SDM_COMP_TYPE_RAM               8

/******************/
/* PBF CONSTANTS  */
/******************/

/* Number of PBF command queue lines. Each line is 32B. */
#define PBF_MAX_CMD_LINES 3328

/* Number of BTB blocks. Each block is 256B. */
#define BTB_MAX_BLOCKS 1440

/*****************/
/* PRS CONSTANTS */
/*****************/

/* Async data KCQ CQE */
struct async_data {
	__le32	cid;
	__le16	itid;
	u8	error_code;
	u8	fw_debug_param;
};

struct coalescing_timeset {
	u8 value;
#define	COALESCING_TIMESET_TIMESET_MASK		0x7F
#define	COALESCING_TIMESET_TIMESET_SHIFT	0
#define	COALESCING_TIMESET_VALID_MASK		0x1
#define	COALESCING_TIMESET_VALID_SHIFT		7
};

struct common_prs_pf_msg_info {
	__le32 value;
#define	COMMON_PRS_PF_MSG_INFO_NPAR_DEFAULT_PF_MASK	0x1
#define	COMMON_PRS_PF_MSG_INFO_NPAR_DEFAULT_PF_SHIFT	0
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_1_MASK		0x1
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_1_SHIFT		1
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_2_MASK		0x1
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_2_SHIFT		2
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_3_MASK		0x1
#define	COMMON_PRS_PF_MSG_INFO_FW_DEBUG_3_SHIFT		3
#define	COMMON_PRS_PF_MSG_INFO_RESERVED_MASK		0xFFFFFFF
#define	COMMON_PRS_PF_MSG_INFO_RESERVED_SHIFT		4
};

struct common_queue_zone {
	__le16 ring_drv_data_consumer;
	__le16 reserved;
};

struct eth_rx_prod_data {
	__le16 bd_prod;
	__le16 cqe_prod;
};

struct regpair {
	__le32	lo;
	__le32	hi;
};

struct vf_pf_channel_eqe_data {
	struct regpair msg_addr;
};

struct malicious_vf_eqe_data {
	u8 vf_id;
	u8 err_id;
	__le16 reserved[3];
};

struct initial_cleanup_eqe_data {
	u8 vf_id;
	u8 reserved[7];
};

/* Event Data Union */
union event_ring_data {
	u8 bytes[8];
	struct vf_pf_channel_eqe_data vf_pf_channel;
	struct malicious_vf_eqe_data malicious_vf;
	struct initial_cleanup_eqe_data vf_init_cleanup;
};

/* Event Ring Entry */
struct event_ring_entry {
	u8			protocol_id;
	u8			opcode;
	__le16			reserved0;
	__le16			echo;
	u8			fw_return_code;
	u8			flags;
#define EVENT_RING_ENTRY_ASYNC_MASK      0x1
#define EVENT_RING_ENTRY_ASYNC_SHIFT     0
#define EVENT_RING_ENTRY_RESERVED1_MASK  0x7F
#define EVENT_RING_ENTRY_RESERVED1_SHIFT 1
	union event_ring_data	data;
};

/* Multi function mode */
enum mf_mode {
	ERROR_MODE /* Unsupported mode */,
	MF_OVLAN,
	MF_NPAR,
	MAX_MF_MODE
};

/* Per-protocol connection types */
enum protocol_type {
	PROTOCOLID_RESERVED1,
	PROTOCOLID_RESERVED2,
	PROTOCOLID_RESERVED3,
	PROTOCOLID_CORE,
	PROTOCOLID_ETH,
	PROTOCOLID_RESERVED4,
	PROTOCOLID_RESERVED5,
	PROTOCOLID_PREROCE,
	PROTOCOLID_COMMON,
	PROTOCOLID_RESERVED6,
	MAX_PROTOCOL_TYPE
};

struct ustorm_eth_queue_zone {
	struct coalescing_timeset int_coalescing_timeset;
	u8 reserved[3];
};

struct ustorm_queue_zone {
	struct ustorm_eth_queue_zone eth;
	struct common_queue_zone common;
};

/* status block structure */
struct cau_pi_entry {
	u32 prod;
#define CAU_PI_ENTRY_PROD_VAL_MASK    0xFFFF
#define CAU_PI_ENTRY_PROD_VAL_SHIFT   0
#define CAU_PI_ENTRY_PI_TIMESET_MASK  0x7F
#define CAU_PI_ENTRY_PI_TIMESET_SHIFT 16
#define CAU_PI_ENTRY_FSM_SEL_MASK     0x1
#define CAU_PI_ENTRY_FSM_SEL_SHIFT    23
#define CAU_PI_ENTRY_RESERVED_MASK    0xFF
#define CAU_PI_ENTRY_RESERVED_SHIFT   24
};

/* status block structure */
struct cau_sb_entry {
	u32 data;
#define CAU_SB_ENTRY_SB_PROD_MASK      0xFFFFFF
#define CAU_SB_ENTRY_SB_PROD_SHIFT     0
#define CAU_SB_ENTRY_STATE0_MASK       0xF
#define CAU_SB_ENTRY_STATE0_SHIFT      24
#define CAU_SB_ENTRY_STATE1_MASK       0xF
#define CAU_SB_ENTRY_STATE1_SHIFT      28
	u32 params;
#define CAU_SB_ENTRY_SB_TIMESET0_MASK  0x7F
#define CAU_SB_ENTRY_SB_TIMESET0_SHIFT 0
#define CAU_SB_ENTRY_SB_TIMESET1_MASK  0x7F
#define CAU_SB_ENTRY_SB_TIMESET1_SHIFT 7
#define CAU_SB_ENTRY_TIMER_RES0_MASK   0x3
#define CAU_SB_ENTRY_TIMER_RES0_SHIFT  14
#define CAU_SB_ENTRY_TIMER_RES1_MASK   0x3
#define CAU_SB_ENTRY_TIMER_RES1_SHIFT  16
#define CAU_SB_ENTRY_VF_NUMBER_MASK    0xFF
#define CAU_SB_ENTRY_VF_NUMBER_SHIFT   18
#define CAU_SB_ENTRY_VF_VALID_MASK     0x1
#define CAU_SB_ENTRY_VF_VALID_SHIFT    26
#define CAU_SB_ENTRY_PF_NUMBER_MASK    0xF
#define CAU_SB_ENTRY_PF_NUMBER_SHIFT   27
#define CAU_SB_ENTRY_TPH_MASK          0x1
#define CAU_SB_ENTRY_TPH_SHIFT         31
};

/* core doorbell data */
struct core_db_data {
	u8 params;
#define CORE_DB_DATA_DEST_MASK         0x3
#define CORE_DB_DATA_DEST_SHIFT        0
#define CORE_DB_DATA_AGG_CMD_MASK      0x3
#define CORE_DB_DATA_AGG_CMD_SHIFT     2
#define CORE_DB_DATA_BYPASS_EN_MASK    0x1
#define CORE_DB_DATA_BYPASS_EN_SHIFT   4
#define CORE_DB_DATA_RESERVED_MASK     0x1
#define CORE_DB_DATA_RESERVED_SHIFT    5
#define CORE_DB_DATA_AGG_VAL_SEL_MASK  0x3
#define CORE_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8	agg_flags;
	__le16	spq_prod;
};

/* Enum of doorbell aggregative command selection */
enum db_agg_cmd_sel {
	DB_AGG_CMD_NOP,
	DB_AGG_CMD_SET,
	DB_AGG_CMD_ADD,
	DB_AGG_CMD_MAX,
	MAX_DB_AGG_CMD_SEL
};

/* Enum of doorbell destination */
enum db_dest {
	DB_DEST_XCM,
	DB_DEST_UCM,
	DB_DEST_TCM,
	DB_NUM_DESTINATIONS,
	MAX_DB_DEST
};

/* Structure for doorbell address, in legacy mode */
struct db_legacy_addr {
	__le32 addr;
#define DB_LEGACY_ADDR_RESERVED0_MASK  0x3
#define DB_LEGACY_ADDR_RESERVED0_SHIFT 0
#define DB_LEGACY_ADDR_DEMS_MASK       0x7
#define DB_LEGACY_ADDR_DEMS_SHIFT      2
#define DB_LEGACY_ADDR_ICID_MASK       0x7FFFFFF
#define DB_LEGACY_ADDR_ICID_SHIFT      5
};

/* Igu interrupt command */
enum igu_int_cmd {
	IGU_INT_ENABLE	= 0,
	IGU_INT_DISABLE = 1,
	IGU_INT_NOP	= 2,
	IGU_INT_NOP2	= 3,
	MAX_IGU_INT_CMD
};

/* IGU producer or consumer update command */
struct igu_prod_cons_update {
	u32 sb_id_and_flags;
#define IGU_PROD_CONS_UPDATE_SB_INDEX_MASK        0xFFFFFF
#define IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT       0
#define IGU_PROD_CONS_UPDATE_UPDATE_FLAG_MASK     0x1
#define IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT    24
#define IGU_PROD_CONS_UPDATE_ENABLE_INT_MASK      0x3
#define IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT     25
#define IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_MASK  0x1
#define IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT 27
#define IGU_PROD_CONS_UPDATE_TIMER_MASK_MASK      0x1
#define IGU_PROD_CONS_UPDATE_TIMER_MASK_SHIFT     28
#define IGU_PROD_CONS_UPDATE_RESERVED0_MASK       0x3
#define IGU_PROD_CONS_UPDATE_RESERVED0_SHIFT      29
#define IGU_PROD_CONS_UPDATE_COMMAND_TYPE_MASK    0x1
#define IGU_PROD_CONS_UPDATE_COMMAND_TYPE_SHIFT   31
	u32 reserved1;
};

/* Igu segments access for default status block only */
enum igu_seg_access {
	IGU_SEG_ACCESS_REG	= 0,
	IGU_SEG_ACCESS_ATTN	= 1,
	MAX_IGU_SEG_ACCESS
};

struct parsing_and_err_flags {
	__le16 flags;
#define PARSING_AND_ERR_FLAGS_L3TYPE_MASK                      0x3
#define PARSING_AND_ERR_FLAGS_L3TYPE_SHIFT                     0
#define PARSING_AND_ERR_FLAGS_L4PROTOCOL_MASK                  0x3
#define PARSING_AND_ERR_FLAGS_L4PROTOCOL_SHIFT                 2
#define PARSING_AND_ERR_FLAGS_IPV4FRAG_MASK                    0x1
#define PARSING_AND_ERR_FLAGS_IPV4FRAG_SHIFT                   4
#define PARSING_AND_ERR_FLAGS_TAG8021QEXIST_MASK               0x1
#define PARSING_AND_ERR_FLAGS_TAG8021QEXIST_SHIFT              5
#define PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK        0x1
#define PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT       6
#define PARSING_AND_ERR_FLAGS_TIMESYNCPKT_MASK                 0x1
#define PARSING_AND_ERR_FLAGS_TIMESYNCPKT_SHIFT                7
#define PARSING_AND_ERR_FLAGS_TIMESTAMPRECORDED_MASK           0x1
#define PARSING_AND_ERR_FLAGS_TIMESTAMPRECORDED_SHIFT          8
#define PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK                  0x1
#define PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT                 9
#define PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK                0x1
#define PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT               10
#define PARSING_AND_ERR_FLAGS_TUNNELEXIST_MASK                 0x1
#define PARSING_AND_ERR_FLAGS_TUNNELEXIST_SHIFT                11
#define PARSING_AND_ERR_FLAGS_TUNNEL8021QTAGEXIST_MASK         0x1
#define PARSING_AND_ERR_FLAGS_TUNNEL8021QTAGEXIST_SHIFT        12
#define PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_MASK            0x1
#define PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_SHIFT           13
#define PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_MASK  0x1
#define PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_SHIFT 14
#define PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_MASK          0x1
#define PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_SHIFT         15
};

/* Concrete Function ID. */
struct pxp_concrete_fid {
	__le16 fid;
#define PXP_CONCRETE_FID_PFID_MASK     0xF
#define PXP_CONCRETE_FID_PFID_SHIFT    0
#define PXP_CONCRETE_FID_PORT_MASK     0x3
#define PXP_CONCRETE_FID_PORT_SHIFT    4
#define PXP_CONCRETE_FID_PATH_MASK     0x1
#define PXP_CONCRETE_FID_PATH_SHIFT    6
#define PXP_CONCRETE_FID_VFVALID_MASK  0x1
#define PXP_CONCRETE_FID_VFVALID_SHIFT 7
#define PXP_CONCRETE_FID_VFID_MASK     0xFF
#define PXP_CONCRETE_FID_VFID_SHIFT    8
};

struct pxp_pretend_concrete_fid {
	__le16 fid;
#define PXP_PRETEND_CONCRETE_FID_PFID_MASK      0xF
#define PXP_PRETEND_CONCRETE_FID_PFID_SHIFT     0
#define PXP_PRETEND_CONCRETE_FID_RESERVED_MASK  0x7
#define PXP_PRETEND_CONCRETE_FID_RESERVED_SHIFT 4
#define PXP_PRETEND_CONCRETE_FID_VFVALID_MASK   0x1
#define PXP_PRETEND_CONCRETE_FID_VFVALID_SHIFT  7
#define PXP_PRETEND_CONCRETE_FID_VFID_MASK      0xFF
#define PXP_PRETEND_CONCRETE_FID_VFID_SHIFT     8
};

union pxp_pretend_fid {
	struct pxp_pretend_concrete_fid concrete_fid;
	__le16				opaque_fid;
};

/* Pxp Pretend Command Register. */
struct pxp_pretend_cmd {
	union pxp_pretend_fid	fid;
	__le16			control;
#define PXP_PRETEND_CMD_PATH_MASK              0x1
#define PXP_PRETEND_CMD_PATH_SHIFT             0
#define PXP_PRETEND_CMD_USE_PORT_MASK          0x1
#define PXP_PRETEND_CMD_USE_PORT_SHIFT         1
#define PXP_PRETEND_CMD_PORT_MASK              0x3
#define PXP_PRETEND_CMD_PORT_SHIFT             2
#define PXP_PRETEND_CMD_RESERVED0_MASK         0xF
#define PXP_PRETEND_CMD_RESERVED0_SHIFT        4
#define PXP_PRETEND_CMD_RESERVED1_MASK         0xF
#define PXP_PRETEND_CMD_RESERVED1_SHIFT        8
#define PXP_PRETEND_CMD_PRETEND_PATH_MASK      0x1
#define PXP_PRETEND_CMD_PRETEND_PATH_SHIFT     12
#define PXP_PRETEND_CMD_PRETEND_PORT_MASK      0x1
#define PXP_PRETEND_CMD_PRETEND_PORT_SHIFT     13
#define PXP_PRETEND_CMD_PRETEND_FUNCTION_MASK  0x1
#define PXP_PRETEND_CMD_PRETEND_FUNCTION_SHIFT 14
#define PXP_PRETEND_CMD_IS_CONCRETE_MASK       0x1
#define PXP_PRETEND_CMD_IS_CONCRETE_SHIFT      15
};

/* PTT Record in PXP Admin Window. */
struct pxp_ptt_entry {
	__le32			offset;
#define PXP_PTT_ENTRY_OFFSET_MASK     0x7FFFFF
#define PXP_PTT_ENTRY_OFFSET_SHIFT    0
#define PXP_PTT_ENTRY_RESERVED0_MASK  0x1FF
#define PXP_PTT_ENTRY_RESERVED0_SHIFT 23
	struct pxp_pretend_cmd	pretend;
};

/* RSS hash type */
enum rss_hash_type {
	RSS_HASH_TYPE_DEFAULT	= 0,
	RSS_HASH_TYPE_IPV4	= 1,
	RSS_HASH_TYPE_TCP_IPV4	= 2,
	RSS_HASH_TYPE_IPV6	= 3,
	RSS_HASH_TYPE_TCP_IPV6	= 4,
	RSS_HASH_TYPE_UDP_IPV4	= 5,
	RSS_HASH_TYPE_UDP_IPV6	= 6,
	MAX_RSS_HASH_TYPE
};

/* status block structure */
struct status_block {
	__le16	pi_array[PIS_PER_SB];
	__le32	sb_num;
#define STATUS_BLOCK_SB_NUM_MASK      0x1FF
#define STATUS_BLOCK_SB_NUM_SHIFT     0
#define STATUS_BLOCK_ZERO_PAD_MASK    0x7F
#define STATUS_BLOCK_ZERO_PAD_SHIFT   9
#define STATUS_BLOCK_ZERO_PAD2_MASK   0xFFFF
#define STATUS_BLOCK_ZERO_PAD2_SHIFT  16
	__le32 prod_index;
#define STATUS_BLOCK_PROD_INDEX_MASK  0xFFFFFF
#define STATUS_BLOCK_PROD_INDEX_SHIFT 0
#define STATUS_BLOCK_ZERO_PAD3_MASK   0xFF
#define STATUS_BLOCK_ZERO_PAD3_SHIFT  24
};

#endif /* __COMMON_HSI__ */

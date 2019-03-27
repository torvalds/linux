/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_FMAN_PORT_H
#define __FSL_FMAN_PORT_H

#include "fsl_fman_sp.h"

/** @Collection  Registers bit fields */

/** @Description  BMI defines */
#define BMI_EBD_EN                              0x80000000

#define BMI_PORT_CFG_EN				0x80000000
#define BMI_PORT_CFG_FDOVR			0x02000000
#define BMI_PORT_CFG_IM				0x01000000

#define BMI_PORT_STATUS_BSY			0x80000000

#define BMI_DMA_ATTR_SWP_SHIFT			FMAN_SP_DMA_ATTR_SWP_SHIFT
#define BMI_DMA_ATTR_IC_STASH_ON		0x10000000
#define BMI_DMA_ATTR_HDR_STASH_ON		0x04000000
#define BMI_DMA_ATTR_SG_STASH_ON		0x01000000
#define BMI_DMA_ATTR_WRITE_OPTIMIZE		FMAN_SP_DMA_ATTR_WRITE_OPTIMIZE

#define BMI_RX_FIFO_PRI_ELEVATION_SHIFT		16
#define BMI_RX_FIFO_THRESHOLD_ETHE		0x80000000

#define BMI_TX_FRAME_END_CS_IGNORE_SHIFT	24
#define BMI_RX_FRAME_END_CS_IGNORE_SHIFT	24
#define BMI_RX_FRAME_END_CUT_SHIFT		16

#define BMI_IC_TO_EXT_SHIFT			FMAN_SP_IC_TO_EXT_SHIFT
#define BMI_IC_FROM_INT_SHIFT			FMAN_SP_IC_FROM_INT_SHIFT

#define BMI_INT_BUF_MARG_SHIFT			28
#define BMI_EXT_BUF_MARG_START_SHIFT		FMAN_SP_EXT_BUF_MARG_START_SHIFT

#define BMI_CMD_MR_LEAC				0x00200000
#define BMI_CMD_MR_SLEAC			0x00100000
#define BMI_CMD_MR_MA				0x00080000
#define BMI_CMD_MR_DEAS				0x00040000
#define BMI_CMD_RX_MR_DEF			(BMI_CMD_MR_LEAC | \
						BMI_CMD_MR_SLEAC | \
						BMI_CMD_MR_MA | \
						BMI_CMD_MR_DEAS)
#define BMI_CMD_TX_MR_DEF			0
#define BMI_CMD_OP_MR_DEF			(BMI_CMD_MR_DEAS | \
						BMI_CMD_MR_MA)

#define BMI_CMD_ATTR_ORDER			0x80000000
#define BMI_CMD_ATTR_SYNC			0x02000000
#define BMI_CMD_ATTR_COLOR_SHIFT		26

#define BMI_FIFO_PIPELINE_DEPTH_SHIFT           12
#define BMI_NEXT_ENG_FD_BITS_SHIFT		24
#define BMI_FRAME_END_CS_IGNORE_SHIFT           24

#define BMI_COUNTERS_EN				0x80000000

#define BMI_EXT_BUF_POOL_VALID			FMAN_SP_EXT_BUF_POOL_VALID
#define BMI_EXT_BUF_POOL_EN_COUNTER		FMAN_SP_EXT_BUF_POOL_EN_COUNTER
#define BMI_EXT_BUF_POOL_BACKUP			FMAN_SP_EXT_BUF_POOL_BACKUP
#define BMI_EXT_BUF_POOL_ID_SHIFT		16
#define BMI_EXT_BUF_POOL_ID_MASK		0x003F0000
#define BMI_POOL_DEP_NUM_OF_POOLS_SHIFT		16

#define BMI_TX_FIFO_MIN_FILL_SHIFT		16
#define BMI_TX_FIFO_PIPELINE_DEPTH_SHIFT	12

#define MAX_PERFORMANCE_TASK_COMP		64
#define MAX_PERFORMANCE_RX_QUEUE_COMP		64
#define MAX_PERFORMANCE_TX_QUEUE_COMP		8
#define MAX_PERFORMANCE_DMA_COMP		16
#define MAX_PERFORMANCE_FIFO_COMP		1024

#define BMI_PERFORMANCE_TASK_COMP_SHIFT		24
#define BMI_PERFORMANCE_QUEUE_COMP_SHIFT	16
#define BMI_PERFORMANCE_DMA_COMP_SHIFT		12

#define BMI_RATE_LIMIT_GRAN_TX			16000 /* In Kbps */
#define BMI_RATE_LIMIT_GRAN_OP			10000 /* In frames */
#define BMI_RATE_LIMIT_MAX_RATE_IN_GRAN_UNITS	1024
#define BMI_RATE_LIMIT_MAX_BURST_SIZE		1024 /* In KBytes */
#define BMI_RATE_LIMIT_MAX_BURST_SHIFT		16
#define BMI_RATE_LIMIT_HIGH_BURST_SIZE_GRAN	0x80000000
#define BMI_RATE_LIMIT_SCALE_TSBS_SHIFT		16
#define BMI_RATE_LIMIT_SCALE_EN			0x80000000
#define BMI_SG_DISABLE                          FMAN_SP_SG_DISABLE

/** @Description  QMI defines */
#define QMI_PORT_CFG_EN				0x80000000
#define QMI_PORT_CFG_EN_COUNTERS		0x10000000

#define QMI_PORT_STATUS_DEQ_TNUM_BSY		0x80000000
#define QMI_PORT_STATUS_DEQ_FD_BSY		0x20000000

#define QMI_DEQ_CFG_PRI				0x80000000
#define QMI_DEQ_CFG_TYPE1			0x10000000
#define QMI_DEQ_CFG_TYPE2			0x20000000
#define QMI_DEQ_CFG_TYPE3			0x30000000
#define QMI_DEQ_CFG_PREFETCH_PARTIAL		0x01000000
#define QMI_DEQ_CFG_PREFETCH_FULL		0x03000000
#define QMI_DEQ_CFG_SP_MASK			0xf
#define QMI_DEQ_CFG_SP_SHIFT			20


/** @Description  General port defines */
#define FMAN_PORT_EXT_POOLS_NUM(fm_rev_maj) \
		(((fm_rev_maj) == 4) ? 4 : 8)
#define FMAN_PORT_MAX_EXT_POOLS_NUM	8
#define FMAN_PORT_OBS_EXT_POOLS_NUM	2
#define FMAN_PORT_CG_MAP_NUM		8
#define FMAN_PORT_PRS_RESULT_WORDS_NUM	8
#define FMAN_PORT_BMI_FIFO_UNITS	0x100
#define FMAN_PORT_IC_OFFSET_UNITS	0x10


/** @Collection    FM Port Register Map */

/** @Description   BMI Rx port register map */
struct fman_port_rx_bmi_regs {
	uint32_t fmbm_rcfg;		/**< Rx Configuration */
	uint32_t fmbm_rst;		/**< Rx Status */
	uint32_t fmbm_rda;		/**< Rx DMA attributes*/
	uint32_t fmbm_rfp;		/**< Rx FIFO Parameters*/
	uint32_t fmbm_rfed;		/**< Rx Frame End Data*/
	uint32_t fmbm_ricp;		/**< Rx Internal Context Parameters*/
	uint32_t fmbm_rim;		/**< Rx Internal Buffer Margins*/
	uint32_t fmbm_rebm;		/**< Rx External Buffer Margins*/
	uint32_t fmbm_rfne;		/**< Rx Frame Next Engine*/
	uint32_t fmbm_rfca;		/**< Rx Frame Command Attributes.*/
	uint32_t fmbm_rfpne;		/**< Rx Frame Parser Next Engine*/
	uint32_t fmbm_rpso;		/**< Rx Parse Start Offset*/
	uint32_t fmbm_rpp;		/**< Rx Policer Profile  */
	uint32_t fmbm_rccb;		/**< Rx Coarse Classification Base */
	uint32_t fmbm_reth;		/**< Rx Excessive Threshold */
	uint32_t reserved003c[1];	/**< (0x03C 0x03F) */
	uint32_t fmbm_rprai[FMAN_PORT_PRS_RESULT_WORDS_NUM];
					/**< Rx Parse Results Array Init*/
	uint32_t fmbm_rfqid;		/**< Rx Frame Queue ID*/
	uint32_t fmbm_refqid;		/**< Rx Error Frame Queue ID*/
	uint32_t fmbm_rfsdm;		/**< Rx Frame Status Discard Mask*/
	uint32_t fmbm_rfsem;		/**< Rx Frame Status Error Mask*/
	uint32_t fmbm_rfene;		/**< Rx Frame Enqueue Next Engine */
	uint32_t reserved0074[0x2];	/**< (0x074-0x07C)  */
	uint32_t fmbm_rcmne;		/**< Rx Frame Continuous Mode Next Engine */
	uint32_t reserved0080[0x20];/**< (0x080 0x0FF)  */
	uint32_t fmbm_ebmpi[FMAN_PORT_MAX_EXT_POOLS_NUM];
					/**< Buffer Manager pool Information-*/
	uint32_t fmbm_acnt[FMAN_PORT_MAX_EXT_POOLS_NUM];
					/**< Allocate Counter-*/
	uint32_t reserved0130[8];
					/**< 0x130/0x140 - 0x15F reserved -*/
	uint32_t fmbm_rcgm[FMAN_PORT_CG_MAP_NUM];
					/**< Congestion Group Map*/
	uint32_t fmbm_mpd;		/**< BM Pool Depletion  */
	uint32_t reserved0184[0x1F];	/**< (0x184 0x1FF) */
	uint32_t fmbm_rstc;		/**< Rx Statistics Counters*/
	uint32_t fmbm_rfrc;		/**< Rx Frame Counter*/
	uint32_t fmbm_rfbc;		/**< Rx Bad Frames Counter*/
	uint32_t fmbm_rlfc;		/**< Rx Large Frames Counter*/
	uint32_t fmbm_rffc;		/**< Rx Filter Frames Counter*/
	uint32_t fmbm_rfdc;		/**< Rx Frame Discard Counter*/
	uint32_t fmbm_rfldec;		/**< Rx Frames List DMA Error Counter*/
	uint32_t fmbm_rodc;		/**< Rx Out of Buffers Discard nntr*/
	uint32_t fmbm_rbdc;		/**< Rx Buffers Deallocate Counter*/
	uint32_t reserved0224[0x17];	/**< (0x224 0x27F) */
	uint32_t fmbm_rpc;		/**< Rx Performance Counters*/
	uint32_t fmbm_rpcp;		/**< Rx Performance Count Parameters*/
	uint32_t fmbm_rccn;		/**< Rx Cycle Counter*/
	uint32_t fmbm_rtuc;		/**< Rx Tasks Utilization Counter*/
	uint32_t fmbm_rrquc;		/**< Rx Receive Queue Utilization cntr*/
	uint32_t fmbm_rduc;		/**< Rx DMA Utilization Counter*/
	uint32_t fmbm_rfuc;		/**< Rx FIFO Utilization Counter*/
	uint32_t fmbm_rpac;		/**< Rx Pause Activation Counter*/
	uint32_t reserved02a0[0x18];	/**< (0x2A0 0x2FF) */
	uint32_t fmbm_rdbg;		/**< Rx Debug-*/
};

/** @Description   BMI Tx port register map */
struct fman_port_tx_bmi_regs {
	uint32_t fmbm_tcfg;		/**< Tx Configuration */
	uint32_t fmbm_tst;		/**< Tx Status */
	uint32_t fmbm_tda;		/**< Tx DMA attributes */
	uint32_t fmbm_tfp;		/**< Tx FIFO Parameters */
	uint32_t fmbm_tfed;		/**< Tx Frame End Data */
	uint32_t fmbm_ticp;		/**< Tx Internal Context Parameters */
	uint32_t fmbm_tfdne;		/**< Tx Frame Dequeue Next Engine. */
	uint32_t fmbm_tfca;		/**< Tx Frame Command attribute. */
	uint32_t fmbm_tcfqid;		/**< Tx Confirmation Frame Queue ID. */
	uint32_t fmbm_tefqid;		/**< Tx Frame Error Queue ID */
	uint32_t fmbm_tfene;		/**< Tx Frame Enqueue Next Engine */
	uint32_t fmbm_trlmts;		/**< Tx Rate Limiter Scale */
	uint32_t fmbm_trlmt;		/**< Tx Rate Limiter */
	uint32_t reserved0034[0x0e];	/**< (0x034-0x6c) */
	uint32_t fmbm_tccb;		/**< Tx Coarse Classification base */
	uint32_t fmbm_tfne;		/**< Tx Frame Next Engine */
	uint32_t fmbm_tpfcm[0x02];	/**< Tx Priority based Flow Control (PFC) Mapping */
	uint32_t fmbm_tcmne;		/**< Tx Frame Continuous Mode Next Engine */
	uint32_t reserved0080[0x60];	/**< (0x080-0x200) */
	uint32_t fmbm_tstc;		/**< Tx Statistics Counters */
	uint32_t fmbm_tfrc;		/**< Tx Frame Counter */
	uint32_t fmbm_tfdc;		/**< Tx Frames Discard Counter */
	uint32_t fmbm_tfledc;		/**< Tx Frame len error discard cntr */
	uint32_t fmbm_tfufdc;		/**< Tx Frame unsprt frmt discard cntr*/
	uint32_t fmbm_tbdc;		/**< Tx Buffers Deallocate Counter */
	uint32_t reserved0218[0x1A];	/**< (0x218-0x280) */
	uint32_t fmbm_tpc;		/**< Tx Performance Counters*/
	uint32_t fmbm_tpcp;		/**< Tx Performance Count Parameters*/
	uint32_t fmbm_tccn;		/**< Tx Cycle Counter*/
	uint32_t fmbm_ttuc;		/**< Tx Tasks Utilization Counter*/
	uint32_t fmbm_ttcquc;		/**< Tx Transmit conf Q util Counter*/
	uint32_t fmbm_tduc;		/**< Tx DMA Utilization Counter*/
	uint32_t fmbm_tfuc;		/**< Tx FIFO Utilization Counter*/
};

/** @Description   BMI O/H port register map */
struct fman_port_oh_bmi_regs {
	uint32_t fmbm_ocfg;		/**< O/H Configuration  */
	uint32_t fmbm_ost;		/**< O/H Status */
	uint32_t fmbm_oda;		/**< O/H DMA attributes  */
	uint32_t fmbm_oicp;		/**< O/H Internal Context Parameters */
	uint32_t fmbm_ofdne;		/**< O/H Frame Dequeue Next Engine  */
	uint32_t fmbm_ofne;		/**< O/H Frame Next Engine  */
	uint32_t fmbm_ofca;		/**< O/H Frame Command Attributes.  */
	uint32_t fmbm_ofpne;		/**< O/H Frame Parser Next Engine  */
	uint32_t fmbm_opso;		/**< O/H Parse Start Offset  */
	uint32_t fmbm_opp;		/**< O/H Policer Profile */
	uint32_t fmbm_occb;		/**< O/H Coarse Classification base */
	uint32_t fmbm_oim;		/**< O/H Internal margins*/
	uint32_t fmbm_ofp;		/**< O/H Fifo Parameters*/
	uint32_t fmbm_ofed;		/**< O/H Frame End Data*/
	uint32_t reserved0030[2];	/**< (0x038 - 0x03F) */
	uint32_t fmbm_oprai[FMAN_PORT_PRS_RESULT_WORDS_NUM];
				/**< O/H Parse Results Array Initialization  */
	uint32_t fmbm_ofqid;		/**< O/H Frame Queue ID  */
	uint32_t fmbm_oefqid;		/**< O/H Error Frame Queue ID  */
	uint32_t fmbm_ofsdm;		/**< O/H Frame Status Discard Mask  */
	uint32_t fmbm_ofsem;		/**< O/H Frame Status Error Mask  */
	uint32_t fmbm_ofene;		/**< O/H Frame Enqueue Next Engine  */
	uint32_t fmbm_orlmts;		/**< O/H Rate Limiter Scale  */
	uint32_t fmbm_orlmt;		/**< O/H Rate Limiter  */
	uint32_t fmbm_ocmne;		/**< O/H Continuous Mode Next Engine  */
	uint32_t reserved0080[0x20];	/**< 0x080 - 0x0FF Reserved */
	uint32_t fmbm_oebmpi[2];	/**< Buf Mngr Observed Pool Info */
	uint32_t reserved0108[0x16];	/**< 0x108 - 0x15F Reserved */
	uint32_t fmbm_ocgm[FMAN_PORT_CG_MAP_NUM]; /**< Observed Congestion Group Map */
	uint32_t fmbm_ompd;		/**< Observed BMan Pool Depletion */
	uint32_t reserved0184[0x1F];	/**< 0x184 - 0x1FF Reserved */
	uint32_t fmbm_ostc;		/**< O/H Statistics Counters  */
	uint32_t fmbm_ofrc;		/**< O/H Frame Counter  */
	uint32_t fmbm_ofdc;		/**< O/H Frames Discard Counter  */
	uint32_t fmbm_ofledc;		/**< O/H Frames Len Err Discard Cntr */
	uint32_t fmbm_ofufdc;		/**< O/H Frames Unsprtd Discard Cutr  */
	uint32_t fmbm_offc;		/**< O/H Filter Frames Counter  */
	uint32_t fmbm_ofwdc;		/**< Rx Frames WRED Discard Counter  */
	uint32_t fmbm_ofldec;		/**< O/H Frames List DMA Error Cntr */
	uint32_t fmbm_obdc;		/**< O/H Buffers Deallocate Counter */
	uint32_t reserved0218[0x17];	/**< (0x218 - 0x27F) */
	uint32_t fmbm_opc;		/**< O/H Performance Counters  */
	uint32_t fmbm_opcp;		/**< O/H Performance Count Parameters */
	uint32_t fmbm_occn;		/**< O/H Cycle Counter  */
	uint32_t fmbm_otuc;		/**< O/H Tasks Utilization Counter  */
	uint32_t fmbm_oduc;		/**< O/H DMA Utilization Counter */
	uint32_t fmbm_ofuc;		/**< O/H FIFO Utilization Counter */
};

/** @Description   BMI port register map */
union fman_port_bmi_regs {
	struct fman_port_rx_bmi_regs rx;
	struct fman_port_tx_bmi_regs tx;
	struct fman_port_oh_bmi_regs oh;
};

/** @Description   QMI port register map */
struct fman_port_qmi_regs {
	uint32_t fmqm_pnc;		/**< PortID n Configuration Register */
	uint32_t fmqm_pns;		/**< PortID n Status Register */
	uint32_t fmqm_pnts;		/**< PortID n Task Status Register */
	uint32_t reserved00c[4];	/**< 0xn00C - 0xn01B */
	uint32_t fmqm_pnen;		/**< PortID n Enqueue NIA Register */
	uint32_t fmqm_pnetfc;		/**< PortID n Enq Total Frame Counter */
	uint32_t reserved024[2];	/**< 0xn024 - 0x02B */
	uint32_t fmqm_pndn;		/**< PortID n Dequeue NIA Register */
	uint32_t fmqm_pndc;		/**< PortID n Dequeue Config Register */
	uint32_t fmqm_pndtfc;		/**< PortID n Dequeue tot Frame cntr */
	uint32_t fmqm_pndfdc;		/**< PortID n Dequeue FQID Dflt Cntr */
	uint32_t fmqm_pndcc;		/**< PortID n Dequeue Confirm Counter */
};


enum fman_port_dma_swap {
	E_FMAN_PORT_DMA_NO_SWAP,	/**< No swap, transfer data as is */
	E_FMAN_PORT_DMA_SWAP_LE,
	/**< The transferred data should be swapped in PPC Little Endian mode */
	E_FMAN_PORT_DMA_SWAP_BE
	/**< The transferred data should be swapped in Big Endian mode */
};

/* Default port color */
enum fman_port_color {
	E_FMAN_PORT_COLOR_GREEN,	/**< Default port color is green */
	E_FMAN_PORT_COLOR_YELLOW,	/**< Default port color is yellow */
	E_FMAN_PORT_COLOR_RED,		/**< Default port color is red */
	E_FMAN_PORT_COLOR_OVERRIDE	/**< Ignore color */
};

/* QMI dequeue from the SP channel - types */
enum fman_port_deq_type {
	E_FMAN_PORT_DEQ_BY_PRI,
	/**< Priority precedence and Intra-Class scheduling */
	E_FMAN_PORT_DEQ_ACTIVE_FQ,
	/**< Active FQ precedence and Intra-Class scheduling */
	E_FMAN_PORT_DEQ_ACTIVE_FQ_NO_ICS
	/**< Active FQ precedence and override Intra-Class scheduling */
};

/* QMI dequeue prefetch modes */
enum fman_port_deq_prefetch {
	E_FMAN_PORT_DEQ_NO_PREFETCH, /**< No prefetch mode */
	E_FMAN_PORT_DEQ_PART_PREFETCH, /**< Partial prefetch mode */
	E_FMAN_PORT_DEQ_FULL_PREFETCH /**< Full prefetch mode */
};

/* Parameters for defining performance counters behavior */
struct fman_port_perf_cnt_params {
	uint8_t task_val;	/**< Task compare value */
	uint8_t queue_val;
	/**< Rx or Tx conf queue compare value (unused for O/H ports) */
	uint8_t dma_val;	/**< Dma compare value */
	uint32_t fifo_val;	/**< Fifo compare value (in bytes) */
};

/** @Description   FM Port configuration structure, used at init */
struct fman_port_cfg {
	struct fman_port_perf_cnt_params perf_cnt_params;
	/* BMI parameters */
	enum fman_port_dma_swap		dma_swap_data;
	bool				dma_ic_stash_on;
	bool				dma_header_stash_on;
	bool				dma_sg_stash_on;
	bool				dma_write_optimize;
	uint16_t			ic_ext_offset;
	uint8_t				ic_int_offset;
	uint16_t			ic_size;
	enum fman_port_color		color;
	bool				sync_req;
	bool				discard_override;
	uint8_t				checksum_bytes_ignore;
	uint8_t				rx_cut_end_bytes;
	uint32_t			rx_pri_elevation;
	uint32_t			rx_fifo_thr;
	uint8_t				rx_fd_bits;
	uint8_t				int_buf_start_margin;
	uint16_t			ext_buf_start_margin;
	uint16_t			ext_buf_end_margin;
	uint32_t			tx_fifo_min_level;
	uint32_t			tx_fifo_low_comf_level;
	uint8_t				tx_fifo_deq_pipeline_depth;
	bool				stats_counters_enable;
	bool				perf_counters_enable;
	/* QMI parameters */
	bool				deq_high_pri;
	enum fman_port_deq_type		deq_type;
	enum fman_port_deq_prefetch	deq_prefetch_opt;
	uint16_t			deq_byte_cnt;
	bool				queue_counters_enable;
	bool				no_scatter_gather;
	int				errata_A006675;
	int				errata_A006320;
	int				excessive_threshold_register;
	int				fmbm_rebm_has_sgd;
	int				fmbm_tfne_has_features;
	int				qmi_deq_options_support;
};

enum fman_port_type {
	E_FMAN_PORT_TYPE_OP = 0,
	/**< Offline parsing port, shares id-s with
	 * host command, so must have exclusive id-s */
	E_FMAN_PORT_TYPE_RX,        /**< 1G Rx port */
	E_FMAN_PORT_TYPE_RX_10G,    /**< 10G Rx port */
	E_FMAN_PORT_TYPE_TX,        /**< 1G Tx port */
	E_FMAN_PORT_TYPE_TX_10G,     /**< 10G Tx port */
	E_FMAN_PORT_TYPE_DUMMY,
	E_FMAN_PORT_TYPE_HC = E_FMAN_PORT_TYPE_DUMMY
	/**< Host command port, shares id-s with
	 * offline parsing ports, so must have exclusive id-s */
};

struct fman_port_params {
	uint32_t discard_mask;
	uint32_t err_mask;
	uint32_t dflt_fqid;
	uint32_t err_fqid;
	uint8_t deq_sp;
	bool dont_release_buf;
};

/* Port context - used by most API functions */
struct fman_port {
	enum fman_port_type type;
	uint8_t fm_rev_maj;
	uint8_t fm_rev_min;
	union fman_port_bmi_regs *bmi_regs;
	struct fman_port_qmi_regs *qmi_regs;
	bool im_en;
	uint8_t ext_pools_num;
};

/** @Description   External buffer pools configuration */
struct fman_port_bpools {
	uint8_t	count;			/**< Num of pools to set up */
	bool	counters_enable;	/**< Enable allocate counters */
	uint8_t grp_bp_depleted_num;
	/**< Number of depleted pools - if reached the BMI indicates
	 * the MAC to send a pause frame */
	struct {
		uint8_t		bpid;	/**< BM pool ID */
		uint16_t	size;
		/**< Pool's size - must be in ascending order */
		bool		is_backup;
		/**< If this is a backup pool */
		bool		grp_bp_depleted;
		/**< Consider this buffer in multiple pools depletion criteria*/
		bool		single_bp_depleted;
		/**< Consider this buffer in single pool depletion criteria */
		bool		pfc_priorities_en;
	} bpool[FMAN_PORT_MAX_EXT_POOLS_NUM];
};

enum fman_port_rate_limiter_scale_down {
	E_FMAN_PORT_RATE_DOWN_NONE,
	E_FMAN_PORT_RATE_DOWN_BY_2,
	E_FMAN_PORT_RATE_DOWN_BY_4,
	E_FMAN_PORT_RATE_DOWN_BY_8
};

/* Rate limiter configuration */
struct fman_port_rate_limiter {
	uint8_t		count_1micro_bit;
	bool		high_burst_size_gran;
	/**< Defines burst_size granularity for OP ports; when TRUE,
	 * burst_size below counts in frames, otherwise in 10^3 frames */
	uint16_t	burst_size;
	/**< Max burst size, in KBytes for Tx port, according to
	 * high_burst_size_gran definition for OP port */
	uint32_t	rate;
	/**< In Kbps for Tx port, in frames/sec for OP port */
	enum fman_port_rate_limiter_scale_down rate_factor;
};

/* BMI statistics counters */
enum fman_port_stats_counters {
	E_FMAN_PORT_STATS_CNT_FRAME,
	/**< Number of processed frames; valid for all ports */
	E_FMAN_PORT_STATS_CNT_DISCARD,
	/**< For Rx ports - frames discarded by QMAN, for Tx or O/H ports -
	 * frames discarded due to DMA error; valid for all ports */
	E_FMAN_PORT_STATS_CNT_DEALLOC_BUF,
	/**< Number of buffer deallocate operations; valid for all ports */
	E_FMAN_PORT_STATS_CNT_RX_BAD_FRAME,
	/**< Number of bad Rx frames, like CRC error, Rx FIFO overflow etc;
	 * valid for Rx ports only */
	E_FMAN_PORT_STATS_CNT_RX_LARGE_FRAME,
	/**< Number of Rx oversized frames, that is frames exceeding max frame
	 * size configured for the corresponding ETH controller;
	 * valid for Rx ports only */
	E_FMAN_PORT_STATS_CNT_RX_OUT_OF_BUF,
	/**< Frames discarded due to lack of external buffers; valid for
	 * Rx ports only */
	E_FMAN_PORT_STATS_CNT_LEN_ERR,
	/**< Frames discarded due to frame length error; valid for Tx and
	 * O/H ports only */
	E_FMAN_PORT_STATS_CNT_UNSUPPORTED_FORMAT,
	/**< Frames discarded due to unsupported FD format; valid for Tx
	 * and O/H ports only */
	E_FMAN_PORT_STATS_CNT_FILTERED_FRAME,
	/**< Number of frames filtered out by PCD module; valid for
	 * Rx and OP ports only */
	E_FMAN_PORT_STATS_CNT_DMA_ERR,
	/**< Frames rejected by QMAN that were not able to release their
	 * buffers due to DMA error; valid for Rx and O/H ports only */
	E_FMAN_PORT_STATS_CNT_WRED_DISCARD
	/**< Frames going through O/H port that were not able to to enter the
	 * return queue due to WRED algorithm; valid for O/H ports only */
};

/* BMI performance counters */
enum fman_port_perf_counters {
	E_FMAN_PORT_PERF_CNT_CYCLE,	/**< Cycle counter */
	E_FMAN_PORT_PERF_CNT_TASK_UTIL,	/**< Tasks utilization counter */
	E_FMAN_PORT_PERF_CNT_QUEUE_UTIL,
	/**< For Rx ports - Rx queue utilization, for Tx ports - Tx conf queue
	 * utilization; not valid for O/H ports */
	E_FMAN_PORT_PERF_CNT_DMA_UTIL,	/**< DMA utilization counter */
	E_FMAN_PORT_PERF_CNT_FIFO_UTIL,	/**< FIFO utilization counter */
	E_FMAN_PORT_PERF_CNT_RX_PAUSE
	/**< Number of cycles in which Rx pause activation control is on;
	 * valid for Rx ports only */
};

/* QMI counters */
enum fman_port_qmi_counters {
	E_FMAN_PORT_ENQ_TOTAL,	/**< EnQ tot frame cntr */
	E_FMAN_PORT_DEQ_TOTAL,	/**< DeQ tot frame cntr; invalid for Rx ports */
	E_FMAN_PORT_DEQ_FROM_DFLT,
	/**< Dequeue from default FQID counter not valid for Rx ports */
	E_FMAN_PORT_DEQ_CONFIRM	/**< DeQ confirm cntr invalid for Rx ports */
};


/** @Collection    FM Port API */
void fman_port_defconfig(struct fman_port_cfg *cfg, enum fman_port_type type);
int fman_port_init(struct fman_port *port,
		struct fman_port_cfg *cfg,
		struct fman_port_params *params);
int fman_port_enable(struct fman_port *port);
int fman_port_disable(const struct fman_port *port);
int fman_port_set_bpools(const struct fman_port *port,
		const struct fman_port_bpools *bp);
int fman_port_set_rate_limiter(struct fman_port *port,
		struct fman_port_rate_limiter *rate_limiter);
int fman_port_delete_rate_limiter(struct fman_port *port);
int fman_port_set_err_mask(struct fman_port *port, uint32_t err_mask);
int fman_port_set_discard_mask(struct fman_port *port, uint32_t discard_mask);
int fman_port_modify_rx_fd_bits(struct fman_port *port,
		uint8_t rx_fd_bits,
		bool add);
int fman_port_set_perf_cnt_params(struct fman_port *port,
		struct fman_port_perf_cnt_params *params);
int fman_port_set_stats_cnt_mode(struct fman_port *port, bool enable);
int fman_port_set_perf_cnt_mode(struct fman_port *port, bool enable);
int fman_port_set_queue_cnt_mode(struct fman_port *port, bool enable);
int fman_port_set_bpool_cnt_mode(struct fman_port *port,
		uint8_t bpid,
		bool enable);
uint32_t fman_port_get_stats_counter(struct fman_port *port,
		enum fman_port_stats_counters counter);
void fman_port_set_stats_counter(struct fman_port *port,
		enum fman_port_stats_counters counter,
		uint32_t value);
uint32_t fman_port_get_perf_counter(struct fman_port *port,
		enum fman_port_perf_counters counter);
void fman_port_set_perf_counter(struct fman_port *port,
		enum fman_port_perf_counters counter,
		uint32_t value);
uint32_t fman_port_get_qmi_counter(struct fman_port *port,
		enum fman_port_qmi_counters counter);
void fman_port_set_qmi_counter(struct fman_port *port,
		enum fman_port_qmi_counters counter,
		uint32_t value);
uint32_t fman_port_get_bpool_counter(struct fman_port *port, uint8_t bpid);
void fman_port_set_bpool_counter(struct fman_port *port,
		uint8_t bpid,
		uint32_t value);
int fman_port_add_congestion_grps(struct fman_port *port,
		uint32_t grps_map[FMAN_PORT_CG_MAP_NUM]);
int fman_port_remove_congestion_grps(struct fman_port  *port,
		uint32_t grps_map[FMAN_PORT_CG_MAP_NUM]);


#endif /* __FSL_FMAN_PORT_H */

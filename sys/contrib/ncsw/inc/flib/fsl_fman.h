/*
 * Copyright 2013 Freescale Semiconductor Inc.
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

#ifndef __FSL_FMAN_H
#define __FSL_FMAN_H

#include "common/general.h"

struct fman_ext_pool_params {
	uint8_t                 id;    /**< External buffer pool id */
	uint16_t                size;  /**< External buffer pool buffer size */
};

struct fman_ext_pools {
	uint8_t num_pools_used;        /**< Number of pools use by this port */
	struct fman_ext_pool_params *ext_buf_pool;
					/**< Parameters for each port */
};

struct fman_backup_bm_pools {
	uint8_t		 num_backup_pools; /**< Number of BM backup pools -
					must be smaller than the total number
					of pools defined for the specified
					port.*/
	uint8_t		*pool_ids;      /**< numOfBackupPools pool id's,
					specifying which pools should be used
					only as backup. Pool id's specified
					here must be a subset of the pools
					used by the specified port.*/
};

/**************************************************************************//**
 @Description   A structure for defining BM pool depletion criteria
*//***************************************************************************/
struct fman_buf_pool_depletion {
	bool buf_pool_depletion_enabled;
	bool pools_grp_mode_enable;    /**< select mode in which pause frames
					will be sent after a number of pools
					(all together!) are depleted */
	uint8_t num_pools;             /**< the number of depleted pools that
					will invoke pause frames transmission.
					*/
	bool *pools_to_consider;       /**< For each pool, TRUE if it should be
					considered for depletion (Note - this
					pool must be used by this port!). */
	bool single_pool_mode_enable;  /**< select mode in which pause frames
					will be sent after a single-pool
					is depleted; */
	bool *pools_to_consider_for_single_mode;
				       /**< For each pool, TRUE if it should be
					considered for depletion (Note - this
					pool must be used by this port!) */
	bool has_pfc_priorities;
	bool *pfc_priorities_en;       /**< This field is used by the MAC as
					the Priority Enable Vector in the PFC
					frame which is transmitted */
};

/**************************************************************************//**
 @Description   Enum for defining port DMA swap mode
*//***************************************************************************/
enum fman_dma_swap_option {
	FMAN_DMA_NO_SWP,           /**< No swap, transfer data as is.*/
	FMAN_DMA_SWP_PPC_LE,       /**< The transferred data should be swapped
					in PowerPc Little Endian mode. */
	FMAN_DMA_SWP_BE            /**< The transferred data should be swapped
					in Big Endian mode */
};

/**************************************************************************//**
 @Description   Enum for defining port DMA cache attributes
*//***************************************************************************/
enum fman_dma_cache_option {
	FMAN_DMA_NO_STASH = 0,     /**< Cacheable, no Allocate (No Stashing) */
	FMAN_DMA_STASH = 1         /**< Cacheable and Allocate (Stashing on) */
};

typedef struct t_FmPrsResult fm_prs_result_t;
typedef enum e_EnetMode enet_mode_t;
typedef t_Handle handle_t;

struct fman_revision_info {
	uint8_t         majorRev;               /**< Major revision */
	uint8_t         minorRev;               /**< Minor revision */
};

/* sizes */
#define CAPWAP_FRAG_EXTRA_SPACE                 32
#define OFFSET_UNITS                            16
#define MAX_INT_OFFSET                          240
#define MAX_IC_SIZE                             256
#define MAX_EXT_OFFSET                          496
#define MAX_EXT_BUFFER_OFFSET                   511

/**************************************************************************
 @Description       Memory Mapped Registers
***************************************************************************/
#define FMAN_LIODN_TBL	64	/* size of LIODN table */

struct fman_fpm_regs {
	uint32_t fmfp_tnc;	/**< FPM TNUM Control 0x00 */
	uint32_t fmfp_prc;	/**< FPM Port_ID FmCtl Association 0x04 */
	uint32_t fmfp_brkc;		/**< FPM Breakpoint Control 0x08 */
	uint32_t fmfp_mxd;	/**< FPM Flush Control 0x0c */
	uint32_t fmfp_dist1;	/**< FPM Dispatch Thresholds1 0x10 */
	uint32_t fmfp_dist2;	/**< FPM Dispatch Thresholds2 0x14 */
	uint32_t fm_epi;	/**< FM Error Pending Interrupts 0x18 */
	uint32_t fm_rie;	/**< FM Error Interrupt Enable 0x1c */
	uint32_t fmfp_fcev[4];	/**< FPM FMan-Controller Event 1-4 0x20-0x2f */
	uint32_t res0030[4];	/**< res 0x30 - 0x3f */
	uint32_t fmfp_cee[4];	/**< PM FMan-Controller Event 1-4 0x40-0x4f */
	uint32_t res0050[4];	/**< res 0x50-0x5f */
	uint32_t fmfp_tsc1;	/**< FPM TimeStamp Control1 0x60 */
	uint32_t fmfp_tsc2;	/**< FPM TimeStamp Control2 0x64 */
	uint32_t fmfp_tsp;	/**< FPM Time Stamp 0x68 */
	uint32_t fmfp_tsf;	/**< FPM Time Stamp Fraction 0x6c */
	uint32_t fm_rcr;	/**< FM Rams Control 0x70 */
	uint32_t fmfp_extc;	/**< FPM External Requests Control 0x74 */
	uint32_t fmfp_ext1;	/**< FPM External Requests Config1 0x78 */
	uint32_t fmfp_ext2;	/**< FPM External Requests Config2 0x7c */
	uint32_t fmfp_drd[16];	/**< FPM Data_Ram Data 0-15 0x80 - 0xbf */
	uint32_t fmfp_dra;	/**< FPM Data Ram Access 0xc0 */
	uint32_t fm_ip_rev_1;	/**< FM IP Block Revision 1 0xc4 */
	uint32_t fm_ip_rev_2;	/**< FM IP Block Revision 2 0xc8 */
	uint32_t fm_rstc;	/**< FM Reset Command 0xcc */
	uint32_t fm_cld;	/**< FM Classifier Debug 0xd0 */
	uint32_t fm_npi;	/**< FM Normal Pending Interrupts 0xd4 */
	uint32_t fmfp_exte;	/**< FPM External Requests Enable 0xd8 */
	uint32_t fmfp_ee;	/**< FPM Event & Mask 0xdc */
	uint32_t fmfp_cev[4];	/**< FPM CPU Event 1-4 0xe0-0xef */
	uint32_t res00f0[4];	/**< res 0xf0-0xff */
	uint32_t fmfp_ps[64];	/**< FPM Port Status 0x100-0x1ff */
	uint32_t fmfp_clfabc;	/**< FPM CLFABC 0x200 */
	uint32_t fmfp_clfcc;	/**< FPM CLFCC 0x204 */
	uint32_t fmfp_clfaval;	/**< FPM CLFAVAL 0x208 */
	uint32_t fmfp_clfbval;	/**< FPM CLFBVAL 0x20c */
	uint32_t fmfp_clfcval;	/**< FPM CLFCVAL 0x210 */
	uint32_t fmfp_clfamsk;	/**< FPM CLFAMSK 0x214 */
	uint32_t fmfp_clfbmsk;	/**< FPM CLFBMSK 0x218 */
	uint32_t fmfp_clfcmsk;	/**< FPM CLFCMSK 0x21c */
	uint32_t fmfp_clfamc;	/**< FPM CLFAMC 0x220 */
	uint32_t fmfp_clfbmc;	/**< FPM CLFBMC 0x224 */
	uint32_t fmfp_clfcmc;	/**< FPM CLFCMC 0x228 */
	uint32_t fmfp_decceh;	/**< FPM DECCEH 0x22c */
	uint32_t res0230[116];	/**< res 0x230 - 0x3ff */
	uint32_t fmfp_ts[128];	/**< 0x400: FPM Task Status 0x400 - 0x5ff */
	uint32_t res0600[0x400 - 384];
};

struct fman_bmi_regs {
	uint32_t fmbm_init; /**< BMI Initialization 0x00 */
	uint32_t fmbm_cfg1; /**< BMI Configuration 1 0x04 */
	uint32_t fmbm_cfg2; /**< BMI Configuration 2 0x08 */
	uint32_t res000c[5]; /**< 0x0c - 0x1f */
	uint32_t fmbm_ievr; /**< Interrupt Event Register 0x20 */
	uint32_t fmbm_ier; /**< Interrupt Enable Register 0x24 */
	uint32_t fmbm_ifr; /**< Interrupt Force Register 0x28 */
	uint32_t res002c[5]; /**< 0x2c - 0x3f */
	uint32_t fmbm_arb[8]; /**< BMI Arbitration 0x40 - 0x5f */
	uint32_t res0060[12]; /**<0x60 - 0x8f */
	uint32_t fmbm_dtc[3]; /**< Debug Trap Counter 0x90 - 0x9b */
	uint32_t res009c; /**< 0x9c */
	uint32_t fmbm_dcv[3][4]; /**< Debug Compare val 0xa0-0xcf */
	uint32_t fmbm_dcm[3][4]; /**< Debug Compare Mask 0xd0-0xff */
	uint32_t fmbm_gde; /**< BMI Global Debug Enable 0x100 */
	uint32_t fmbm_pp[63]; /**< BMI Port Parameters 0x104 - 0x1ff */
	uint32_t res0200; /**< 0x200 */
	uint32_t fmbm_pfs[63]; /**< BMI Port FIFO Size 0x204 - 0x2ff */
	uint32_t res0300; /**< 0x300 */
	uint32_t fmbm_spliodn[63]; /**< Port Partition ID 0x304 - 0x3ff */
};

struct fman_qmi_regs {
	uint32_t fmqm_gc; /**< General Configuration Register 0x00 */
	uint32_t res0004; /**< 0x04 */
	uint32_t fmqm_eie; /**< Error Interrupt Event Register 0x08 */
	uint32_t fmqm_eien; /**< Error Interrupt Enable Register 0x0c */
	uint32_t fmqm_eif; /**< Error Interrupt Force Register 0x10 */
	uint32_t fmqm_ie; /**< Interrupt Event Register 0x14 */
	uint32_t fmqm_ien; /**< Interrupt Enable Register 0x18 */
	uint32_t fmqm_if; /**< Interrupt Force Register 0x1c */
	uint32_t fmqm_gs; /**< Global Status Register 0x20 */
	uint32_t fmqm_ts; /**< Task Status Register 0x24 */
	uint32_t fmqm_etfc; /**< Enqueue Total Frame Counter 0x28 */
	uint32_t fmqm_dtfc; /**< Dequeue Total Frame Counter 0x2c */
	uint32_t fmqm_dc0; /**< Dequeue Counter 0 0x30 */
	uint32_t fmqm_dc1; /**< Dequeue Counter 1 0x34 */
	uint32_t fmqm_dc2; /**< Dequeue Counter 2 0x38 */
	uint32_t fmqm_dc3; /**< Dequeue Counter 3 0x3c */
	uint32_t fmqm_dfdc; /**< Dequeue FQID from Default Counter 0x40 */
	uint32_t fmqm_dfcc; /**< Dequeue FQID from Context Counter 0x44 */
	uint32_t fmqm_dffc; /**< Dequeue FQID from FD Counter 0x48 */
	uint32_t fmqm_dcc; /**< Dequeue Confirm Counter 0x4c */
	uint32_t res0050[7]; /**< 0x50 - 0x6b */
	uint32_t fmqm_tapc; /**< Tnum Aging Period Control 0x6c */
	uint32_t fmqm_dmcvc; /**< Dequeue MAC Command Valid Counter 0x70 */
	uint32_t fmqm_difdcc; /**< Dequeue Invalid FD Command Counter 0x74 */
	uint32_t fmqm_da1v; /**< Dequeue A1 Valid Counter 0x78 */
	uint32_t res007c; /**< 0x7c */
	uint32_t fmqm_dtc; /**< 0x80 Debug Trap Counter 0x80 */
	uint32_t fmqm_efddd; /**< 0x84 Enqueue Frame desc Dynamic dbg 0x84 */
	uint32_t res0088[2]; /**< 0x88 - 0x8f */
	struct {
		uint32_t fmqm_dtcfg1; /**< 0x90 dbg trap cfg 1 Register 0x00 */
		uint32_t fmqm_dtval1; /**< Debug Trap Value 1 Register 0x04 */
		uint32_t fmqm_dtm1; /**< Debug Trap Mask 1 Register 0x08 */
		uint32_t fmqm_dtc1; /**< Debug Trap Counter 1 Register 0x0c */
		uint32_t fmqm_dtcfg2; /**< dbg Trap cfg 2 Register 0x10 */
		uint32_t fmqm_dtval2; /**< Debug Trap Value 2 Register 0x14 */
		uint32_t fmqm_dtm2; /**< Debug Trap Mask 2 Register 0x18 */
		uint32_t res001c; /**< 0x1c */
	} dbg_traps[3]; /**< 0x90 - 0xef */
	uint8_t res00f0[0x400 - 0xf0]; /**< 0xf0 - 0x3ff */
};

struct fman_dma_regs {
	uint32_t fmdmsr; /**< FM DMA status register 0x00 */
	uint32_t fmdmmr; /**< FM DMA mode register 0x04 */
	uint32_t fmdmtr; /**< FM DMA bus threshold register 0x08 */
	uint32_t fmdmhy; /**< FM DMA bus hysteresis register 0x0c */
	uint32_t fmdmsetr; /**< FM DMA SOS emergency Threshold Register 0x10 */
	uint32_t fmdmtah; /**< FM DMA transfer bus address high reg 0x14 */
	uint32_t fmdmtal; /**< FM DMA transfer bus address low reg 0x18 */
	uint32_t fmdmtcid; /**< FM DMA transfer bus communication ID reg 0x1c */
	uint32_t fmdmra; /**< FM DMA bus internal ram address register 0x20 */
	uint32_t fmdmrd; /**< FM DMA bus internal ram data register 0x24 */
	uint32_t fmdmwcr; /**< FM DMA CAM watchdog counter value 0x28 */
	uint32_t fmdmebcr; /**< FM DMA CAM base in MURAM register 0x2c */
	uint32_t fmdmccqdr; /**< FM DMA CAM and CMD Queue Debug reg 0x30 */
	uint32_t fmdmccqvr1; /**< FM DMA CAM and CMD Queue Value reg #1 0x34 */
	uint32_t fmdmccqvr2; /**< FM DMA CAM and CMD Queue Value reg #2 0x38 */
	uint32_t fmdmcqvr3; /**< FM DMA CMD Queue Value register #3 0x3c */
	uint32_t fmdmcqvr4; /**< FM DMA CMD Queue Value register #4 0x40 */
	uint32_t fmdmcqvr5; /**< FM DMA CMD Queue Value register #5 0x44 */
	uint32_t fmdmsefrc; /**< FM DMA Semaphore Entry Full Reject Cntr 0x48 */
	uint32_t fmdmsqfrc; /**< FM DMA Semaphore Queue Full Reject Cntr 0x4c */
	uint32_t fmdmssrc; /**< FM DMA Semaphore SYNC Reject Counter 0x50 */
	uint32_t fmdmdcr;  /**< FM DMA Debug Counter 0x54 */
	uint32_t fmdmemsr; /**< FM DMA Emergency Smoother Register 0x58 */
	uint32_t res005c; /**< 0x5c */
	uint32_t fmdmplr[FMAN_LIODN_TBL / 2]; /**< DMA LIODN regs 0x60-0xdf */
	uint32_t res00e0[0x400 - 56];
};

struct fman_rg {
	struct fman_fpm_regs *fpm_rg;
	struct fman_dma_regs *dma_rg;
	struct fman_bmi_regs *bmi_rg;
	struct fman_qmi_regs *qmi_rg;
};

enum fman_dma_cache_override {
	E_FMAN_DMA_NO_CACHE_OR = 0, /**< No override of the Cache field */
	E_FMAN_DMA_NO_STASH_DATA, /**< No data stashing in system level cache */
	E_FMAN_DMA_MAY_STASH_DATA, /**< Stashing allowed in sys level cache */
	E_FMAN_DMA_STASH_DATA /**< Stashing performed in system level cache */
};

enum fman_dma_aid_mode {
	E_FMAN_DMA_AID_OUT_PORT_ID = 0,           /**< 4 LSB of PORT_ID */
	E_FMAN_DMA_AID_OUT_TNUM                   /**< 4 LSB of TNUM */
};

enum fman_dma_dbg_cnt_mode {
	E_FMAN_DMA_DBG_NO_CNT = 0, /**< No counting */
	E_FMAN_DMA_DBG_CNT_DONE, /**< Count DONE commands */
	E_FMAN_DMA_DBG_CNT_COMM_Q_EM, /**< command Q emergency signal */
	E_FMAN_DMA_DBG_CNT_INT_READ_EM, /**< Read buf emergency signal */
	E_FMAN_DMA_DBG_CNT_INT_WRITE_EM, /**< Write buf emergency signal */
	E_FMAN_DMA_DBG_CNT_FPM_WAIT, /**< FPM WAIT signal */
	E_FMAN_DMA_DBG_CNT_SIGLE_BIT_ECC, /**< Single bit ECC errors */
	E_FMAN_DMA_DBG_CNT_RAW_WAR_PROT /**< RAW & WAR protection counter */
};

enum fman_dma_emergency_level {
	E_FMAN_DMA_EM_EBS = 0, /**< EBS emergency */
	E_FMAN_DMA_EM_SOS /**< SOS emergency */
};

enum fman_catastrophic_err {
	E_FMAN_CATAST_ERR_STALL_PORT = 0, /**< Port_ID stalled reset required */
	E_FMAN_CATAST_ERR_STALL_TASK /**< Only erroneous task is stalled */
};

enum fman_dma_err {
	E_FMAN_DMA_ERR_CATASTROPHIC = 0, /**< Catastrophic DMA error */
	E_FMAN_DMA_ERR_REPORT /**< Reported DMA error */
};

struct fman_cfg {
	uint16_t	liodn_bs_pr_port[FMAN_LIODN_TBL];/* base per port */
	bool		en_counters;
	uint8_t		disp_limit_tsh;
	uint8_t		prs_disp_tsh;
	uint8_t		plcr_disp_tsh;
	uint8_t		kg_disp_tsh;
	uint8_t		bmi_disp_tsh;
	uint8_t		qmi_enq_disp_tsh;
	uint8_t		qmi_deq_disp_tsh;
	uint8_t		fm_ctl1_disp_tsh;
	uint8_t		fm_ctl2_disp_tsh;
	enum fman_dma_cache_override	dma_cache_override;
	enum fman_dma_aid_mode		dma_aid_mode;
	bool		dma_aid_override;
	uint8_t		dma_axi_dbg_num_of_beats;
	uint8_t		dma_cam_num_of_entries;
	uint32_t	dma_watchdog;
	uint8_t		dma_comm_qtsh_asrt_emer;
	uint8_t		dma_write_buf_tsh_asrt_emer;
	uint8_t		dma_read_buf_tsh_asrt_emer;
	uint8_t		dma_comm_qtsh_clr_emer;
	uint8_t		dma_write_buf_tsh_clr_emer;
	uint8_t		dma_read_buf_tsh_clr_emer;
	uint32_t	dma_sos_emergency;
	enum fman_dma_dbg_cnt_mode	dma_dbg_cnt_mode;
	bool		dma_stop_on_bus_error;
	bool		dma_en_emergency;
	uint32_t	dma_emergency_bus_select;
	enum fman_dma_emergency_level	dma_emergency_level;
	bool		dma_en_emergency_smoother;
	uint32_t	dma_emergency_switch_counter;
	bool		halt_on_external_activ;
	bool		halt_on_unrecov_ecc_err;
	enum fman_catastrophic_err	catastrophic_err;
	enum fman_dma_err		dma_err;
	bool		en_muram_test_mode;
	bool		en_iram_test_mode;
	bool		external_ecc_rams_enable;
	uint16_t	tnum_aging_period;
	uint32_t	exceptions;
	uint16_t	clk_freq;
	bool		pedantic_dma;
	uint32_t	cam_base_addr;
	uint32_t	fifo_base_addr;
	uint32_t	total_fifo_size;
	uint8_t		total_num_of_tasks;
	bool		qmi_deq_option_support;
	uint32_t	qmi_def_tnums_thresh;
	bool		fman_partition_array;
	uint8_t		num_of_fman_ctrl_evnt_regs;
};

/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/
#define FMAN_EX_DMA_BUS_ERROR			0x80000000
#define FMAN_EX_DMA_READ_ECC			0x40000000
#define FMAN_EX_DMA_SYSTEM_WRITE_ECC		0x20000000
#define FMAN_EX_DMA_FM_WRITE_ECC		0x10000000
#define FMAN_EX_FPM_STALL_ON_TASKS		0x08000000
#define FMAN_EX_FPM_SINGLE_ECC			0x04000000
#define FMAN_EX_FPM_DOUBLE_ECC			0x02000000
#define FMAN_EX_QMI_SINGLE_ECC			0x01000000
#define FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID	0x00800000
#define FMAN_EX_QMI_DOUBLE_ECC			0x00400000
#define FMAN_EX_BMI_LIST_RAM_ECC		0x00200000
#define FMAN_EX_BMI_PIPELINE_ECC		0x00100000
#define FMAN_EX_BMI_STATISTICS_RAM_ECC		0x00080000
#define FMAN_EX_IRAM_ECC			0x00040000
#define FMAN_EX_NURAM_ECC			0x00020000
#define FMAN_EX_BMI_DISPATCH_RAM_ECC		0x00010000

enum fman_exceptions {
	E_FMAN_EX_DMA_BUS_ERROR = 0, /**< DMA bus error. */
	E_FMAN_EX_DMA_READ_ECC, /**< Read Buffer ECC error */
	E_FMAN_EX_DMA_SYSTEM_WRITE_ECC, /**< Write Buffer ECC err on sys side */
	E_FMAN_EX_DMA_FM_WRITE_ECC, /**< Write Buffer ECC error on FM side */
	E_FMAN_EX_FPM_STALL_ON_TASKS, /**< Stall of tasks on FPM */
	E_FMAN_EX_FPM_SINGLE_ECC, /**< Single ECC on FPM. */
	E_FMAN_EX_FPM_DOUBLE_ECC, /**< Double ECC error on FPM ram access */
	E_FMAN_EX_QMI_SINGLE_ECC, /**< Single ECC on QMI. */
	E_FMAN_EX_QMI_DOUBLE_ECC, /**< Double bit ECC occurred on QMI */
	E_FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID,/**< DeQ from unknown port id */
	E_FMAN_EX_BMI_LIST_RAM_ECC, /**< Linked List RAM ECC error */
	E_FMAN_EX_BMI_STORAGE_PROFILE_ECC, /**< storage profile */
	E_FMAN_EX_BMI_STATISTICS_RAM_ECC, /**< Statistics RAM ECC Err Enable */
	E_FMAN_EX_BMI_DISPATCH_RAM_ECC, /**< Dispatch RAM ECC Error Enable */
	E_FMAN_EX_IRAM_ECC, /**< Double bit ECC occurred on IRAM*/
	E_FMAN_EX_MURAM_ECC /**< Double bit ECC occurred on MURAM*/
};

enum fman_counters {
	E_FMAN_COUNTERS_ENQ_TOTAL_FRAME = 0, /**< QMI tot enQ frames counter */
	E_FMAN_COUNTERS_DEQ_TOTAL_FRAME, /**< QMI tot deQ frames counter */
	E_FMAN_COUNTERS_DEQ_0, /**< QMI 0 frames from QMan counter */
	E_FMAN_COUNTERS_DEQ_1, /**< QMI 1 frames from QMan counter */
	E_FMAN_COUNTERS_DEQ_2, /**< QMI 2 frames from QMan counter */
	E_FMAN_COUNTERS_DEQ_3, /**< QMI 3 frames from QMan counter */
	E_FMAN_COUNTERS_DEQ_FROM_DEFAULT, /**< QMI deQ from dflt queue cntr */
	E_FMAN_COUNTERS_DEQ_FROM_CONTEXT, /**< QMI deQ from FQ context cntr */
	E_FMAN_COUNTERS_DEQ_FROM_FD, /**< QMI deQ from FD command field cntr */
	E_FMAN_COUNTERS_DEQ_CONFIRM, /**< QMI dequeue confirm counter */
	E_FMAN_COUNTERS_SEMAPHOR_ENTRY_FULL_REJECT, /**< DMA full entry cntr */
	E_FMAN_COUNTERS_SEMAPHOR_QUEUE_FULL_REJECT, /**< DMA full CAM Q cntr */
	E_FMAN_COUNTERS_SEMAPHOR_SYNC_REJECT /**< DMA sync counter */
};

#define FPM_PRT_FM_CTL1	0x00000001
#define FPM_PRT_FM_CTL2	0x00000002

/**************************************************************************//**
 @Description       DMA definitions
*//***************************************************************************/

/* masks */
#define DMA_MODE_AID_OR			0x20000000
#define DMA_MODE_SBER			0x10000000
#define DMA_MODE_BER			0x00200000
#define DMA_MODE_EB             0x00100000
#define DMA_MODE_ECC			0x00000020
#define DMA_MODE_PRIVILEGE_PROT	0x00001000
#define DMA_MODE_SECURE_PROT	0x00000800
#define DMA_MODE_EMER_READ		0x00080000
#define DMA_MODE_EMER_WRITE		0x00040000
#define DMA_MODE_CACHE_OR_MASK  0xC0000000
#define DMA_MODE_CEN_MASK       0x0000E000
#define DMA_MODE_DBG_MASK       0x00000380
#define DMA_MODE_AXI_DBG_MASK   0x0F000000

#define DMA_EMSR_EMSTR_MASK         0x0000FFFF

#define DMA_TRANSFER_PORTID_MASK	0xFF000000
#define DMA_TRANSFER_TNUM_MASK		0x00FF0000
#define DMA_TRANSFER_LIODN_MASK		0x00000FFF

#define DMA_HIGH_LIODN_MASK		0x0FFF0000
#define DMA_LOW_LIODN_MASK		0x00000FFF

#define DMA_STATUS_CMD_QUEUE_NOT_EMPTY	0x10000000
#define DMA_STATUS_BUS_ERR		0x08000000
#define DMA_STATUS_READ_ECC		0x04000000
#define DMA_STATUS_SYSTEM_WRITE_ECC	0x02000000
#define DMA_STATUS_FM_WRITE_ECC		0x01000000
#define DMA_STATUS_SYSTEM_DPEXT_ECC	0x00800000
#define DMA_STATUS_FM_DPEXT_ECC		0x00400000
#define DMA_STATUS_SYSTEM_DPDAT_ECC	0x00200000
#define DMA_STATUS_FM_DPDAT_ECC		0x00100000
#define DMA_STATUS_FM_SPDAT_ECC		0x00080000

#define FM_LIODN_BASE_MASK		0x00000FFF

/* shifts */
#define DMA_MODE_CACHE_OR_SHIFT			30
#define DMA_MODE_BUS_PRI_SHIFT			16
#define DMA_MODE_AXI_DBG_SHIFT			24
#define DMA_MODE_CEN_SHIFT			13
#define DMA_MODE_BUS_PROT_SHIFT			10
#define DMA_MODE_DBG_SHIFT			7
#define DMA_MODE_EMER_LVL_SHIFT			6
#define DMA_MODE_AID_MODE_SHIFT			4
#define DMA_MODE_MAX_AXI_DBG_NUM_OF_BEATS	16
#define DMA_MODE_MAX_CAM_NUM_OF_ENTRIES		32

#define DMA_THRESH_COMMQ_SHIFT			24
#define DMA_THRESH_READ_INT_BUF_SHIFT		16

#define DMA_LIODN_SHIFT				16

#define DMA_TRANSFER_PORTID_SHIFT		24
#define DMA_TRANSFER_TNUM_SHIFT			16

/* sizes */
#define DMA_MAX_WATCHDOG			0xffffffff

/* others */
#define DMA_CAM_SIZEOF_ENTRY			0x40
#define DMA_CAM_ALIGN				0x1000
#define DMA_CAM_UNITS				8

/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/

#define FM_DEBUG_STATUS_REGISTER_OFFSET	0x000d1084UL
#define FM_UCODE_DEBUG_INSTRUCTION	0x6ffff805UL

/**************************************************************************//**
 @Description       FPM defines
*//***************************************************************************/

/* masks */
#define FPM_EV_MASK_DOUBLE_ECC		0x80000000
#define FPM_EV_MASK_STALL		0x40000000
#define FPM_EV_MASK_SINGLE_ECC		0x20000000
#define FPM_EV_MASK_RELEASE_FM		0x00010000
#define FPM_EV_MASK_DOUBLE_ECC_EN	0x00008000
#define FPM_EV_MASK_STALL_EN		0x00004000
#define FPM_EV_MASK_SINGLE_ECC_EN	0x00002000
#define FPM_EV_MASK_EXTERNAL_HALT	0x00000008
#define FPM_EV_MASK_ECC_ERR_HALT	0x00000004

#define FPM_RAM_RAMS_ECC_EN		0x80000000
#define FPM_RAM_IRAM_ECC_EN		0x40000000
#define FPM_RAM_MURAM_ECC		0x00008000
#define FPM_RAM_IRAM_ECC		0x00004000
#define FPM_RAM_MURAM_TEST_ECC		0x20000000
#define FPM_RAM_IRAM_TEST_ECC		0x10000000
#define FPM_RAM_RAMS_ECC_EN_SRC_SEL	0x08000000

#define FPM_IRAM_ECC_ERR_EX_EN		0x00020000
#define FPM_MURAM_ECC_ERR_EX_EN		0x00040000

#define FPM_REV1_MAJOR_MASK		0x0000FF00
#define FPM_REV1_MINOR_MASK		0x000000FF

#define FPM_REV2_INTEG_MASK		0x00FF0000
#define FPM_REV2_ERR_MASK		0x0000FF00
#define FPM_REV2_CFG_MASK		0x000000FF

#define FPM_TS_FRACTION_MASK		0x0000FFFF
#define FPM_TS_CTL_EN			0x80000000

#define FPM_PRC_REALSE_STALLED		0x00800000

#define FPM_PS_STALLED			0x00800000
#define FPM_PS_FM_CTL1_SEL		0x80000000
#define FPM_PS_FM_CTL2_SEL		0x40000000
#define FPM_PS_FM_CTL_SEL_MASK	(FPM_PS_FM_CTL1_SEL | FPM_PS_FM_CTL2_SEL)

#define FPM_RSTC_FM_RESET		0x80000000
#define FPM_RSTC_10G0_RESET		0x04000000
#define FPM_RSTC_1G0_RESET		0x40000000
#define FPM_RSTC_1G1_RESET		0x20000000
#define FPM_RSTC_1G2_RESET		0x10000000
#define FPM_RSTC_1G3_RESET		0x08000000
#define FPM_RSTC_1G4_RESET		0x02000000


#define FPM_DISP_LIMIT_MASK             0x1F000000
#define FPM_THR1_PRS_MASK               0xFF000000
#define FPM_THR1_KG_MASK                0x00FF0000
#define FPM_THR1_PLCR_MASK              0x0000FF00
#define FPM_THR1_BMI_MASK               0x000000FF

#define FPM_THR2_QMI_ENQ_MASK           0xFF000000
#define FPM_THR2_QMI_DEQ_MASK           0x000000FF
#define FPM_THR2_FM_CTL1_MASK           0x00FF0000
#define FPM_THR2_FM_CTL2_MASK           0x0000FF00

/* shifts */
#define FPM_DISP_LIMIT_SHIFT		24

#define FPM_THR1_PRS_SHIFT		24
#define FPM_THR1_KG_SHIFT		16
#define FPM_THR1_PLCR_SHIFT		8
#define FPM_THR1_BMI_SHIFT		0

#define FPM_THR2_QMI_ENQ_SHIFT		24
#define FPM_THR2_QMI_DEQ_SHIFT		0
#define FPM_THR2_FM_CTL1_SHIFT		16
#define FPM_THR2_FM_CTL2_SHIFT		8

#define FPM_EV_MASK_CAT_ERR_SHIFT	1
#define FPM_EV_MASK_DMA_ERR_SHIFT	0

#define FPM_REV1_MAJOR_SHIFT		8
#define FPM_REV1_MINOR_SHIFT		0

#define FPM_REV2_INTEG_SHIFT		16
#define FPM_REV2_ERR_SHIFT		8
#define FPM_REV2_CFG_SHIFT		0

#define FPM_TS_INT_SHIFT		16

#define FPM_PORT_FM_CTL_PORTID_SHIFT	24

#define FPM_PS_FM_CTL_SEL_SHIFT		30
#define FPM_PRC_ORA_FM_CTL_SEL_SHIFT	16

#define FPM_DISP_LIMIT_SHIFT            24

/* Interrupts defines */
#define FPM_EVENT_FM_CTL_0		0x00008000
#define FPM_EVENT_FM_CTL		0x0000FF00
#define FPM_EVENT_FM_CTL_BRK		0x00000080

/* others */
#define FPM_MAX_DISP_LIMIT		31
#define FPM_RSTC_FM_RESET               0x80000000
#define FPM_RSTC_1G0_RESET              0x40000000
#define FPM_RSTC_1G1_RESET              0x20000000
#define FPM_RSTC_1G2_RESET              0x10000000
#define FPM_RSTC_1G3_RESET              0x08000000
#define FPM_RSTC_10G0_RESET             0x04000000
#define FPM_RSTC_1G4_RESET              0x02000000
#define FPM_RSTC_1G5_RESET              0x01000000
#define FPM_RSTC_1G6_RESET              0x00800000
#define FPM_RSTC_1G7_RESET              0x00400000
#define FPM_RSTC_10G1_RESET             0x00200000
/**************************************************************************//**
 @Description       BMI defines
*//***************************************************************************/
/* masks */
#define BMI_INIT_START				0x80000000
#define BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC	0x80000000
#define BMI_ERR_INTR_EN_LIST_RAM_ECC		0x40000000
#define BMI_ERR_INTR_EN_STATISTICS_RAM_ECC	0x20000000
#define BMI_ERR_INTR_EN_DISPATCH_RAM_ECC	0x10000000
#define BMI_NUM_OF_TASKS_MASK			0x3F000000
#define BMI_NUM_OF_EXTRA_TASKS_MASK		0x000F0000
#define BMI_NUM_OF_DMAS_MASK			0x00000F00
#define BMI_NUM_OF_EXTRA_DMAS_MASK		0x0000000F
#define BMI_FIFO_SIZE_MASK			0x000003FF
#define BMI_EXTRA_FIFO_SIZE_MASK		0x03FF0000
#define BMI_CFG2_DMAS_MASK			0x0000003F
#define BMI_TOTAL_FIFO_SIZE_MASK           0x07FF0000
#define BMI_TOTAL_NUM_OF_TASKS_MASK        0x007F0000

/* shifts */
#define BMI_CFG2_TASKS_SHIFT		16
#define BMI_CFG2_DMAS_SHIFT		0
#define BMI_CFG1_FIFO_SIZE_SHIFT	16
#define BMI_FIFO_SIZE_SHIFT		0
#define BMI_EXTRA_FIFO_SIZE_SHIFT	16
#define BMI_NUM_OF_TASKS_SHIFT		24
#define BMI_EXTRA_NUM_OF_TASKS_SHIFT	16
#define BMI_NUM_OF_DMAS_SHIFT		8
#define BMI_EXTRA_NUM_OF_DMAS_SHIFT	0

/* others */
#define BMI_FIFO_ALIGN			0x100
#define FMAN_BMI_FIFO_UNITS		0x100


/**************************************************************************//**
 @Description       QMI defines
*//***************************************************************************/
/* masks */
#define QMI_CFG_ENQ_EN			0x80000000
#define QMI_CFG_DEQ_EN			0x40000000
#define QMI_CFG_EN_COUNTERS		0x10000000
#define QMI_CFG_SOFT_RESET		0x01000000
#define QMI_CFG_DEQ_MASK		0x0000003F
#define QMI_CFG_ENQ_MASK		0x00003F00

#define QMI_ERR_INTR_EN_DOUBLE_ECC	0x80000000
#define QMI_ERR_INTR_EN_DEQ_FROM_DEF	0x40000000
#define QMI_INTR_EN_SINGLE_ECC		0x80000000

/* shifts */
#define QMI_CFG_ENQ_SHIFT		8
#define QMI_TAPC_TAP			22

#define QMI_GS_HALT_NOT_BUSY            0x00000002

/**************************************************************************//**
 @Description       IRAM defines
*//***************************************************************************/
/* masks */
#define IRAM_IADD_AIE			0x80000000
#define IRAM_READY			0x80000000

uint32_t fman_get_bmi_err_event(struct fman_bmi_regs *bmi_rg);
uint32_t fman_get_qmi_err_event(struct fman_qmi_regs *qmi_rg);
uint32_t fman_get_dma_com_id(struct fman_dma_regs *dma_rg);
uint64_t fman_get_dma_addr(struct fman_dma_regs *dma_rg);
uint32_t fman_get_dma_err_event(struct fman_dma_regs *dma_rg);
uint32_t fman_get_fpm_err_event(struct fman_fpm_regs *fpm_rg);
uint32_t fman_get_muram_err_event(struct fman_fpm_regs *fpm_rg);
uint32_t fman_get_iram_err_event(struct fman_fpm_regs *fpm_rg);
uint32_t fman_get_qmi_event(struct fman_qmi_regs *qmi_rg);
uint32_t fman_get_fpm_error_interrupts(struct fman_fpm_regs *fpm_rg);
uint32_t fman_get_ctrl_intr(struct fman_fpm_regs *fpm_rg,
				uint8_t event_reg_id);
uint8_t fman_get_qmi_deq_th(struct fman_qmi_regs *qmi_rg);
uint8_t fman_get_qmi_enq_th(struct fman_qmi_regs *qmi_rg);
uint16_t fman_get_size_of_fifo(struct fman_bmi_regs *bmi_rg, uint8_t port_id);
uint32_t fman_get_total_fifo_size(struct fman_bmi_regs *bmi_rg);
uint16_t fman_get_size_of_extra_fifo(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id);
uint8_t fman_get_num_of_tasks(struct fman_bmi_regs *bmi_rg, uint8_t port_id);
uint8_t fman_get_num_extra_tasks(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id);
uint8_t fman_get_num_of_dmas(struct fman_bmi_regs *bmi_rg, uint8_t port_id);
uint8_t fman_get_num_extra_dmas(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id);
uint32_t fman_get_normal_pending(struct fman_fpm_regs *fpm_rg);
uint32_t fman_get_controller_event(struct fman_fpm_regs *fpm_rg,
					uint8_t reg_id);
uint32_t fman_get_error_pending(struct fman_fpm_regs *fpm_rg);
void fman_get_revision(struct fman_fpm_regs *fpm_rg, uint8_t *major,
				uint8_t *minor);
uint32_t fman_get_counter(struct fman_rg *fman_rg,
				enum fman_counters reg_name);
uint32_t fman_get_dma_status(struct fman_dma_regs *dma_rg);


int fman_set_erratum_10gmac_a004_wa(struct fman_fpm_regs *fpm_rg);
void fman_set_ctrl_intr(struct fman_fpm_regs *fpm_rg, uint8_t event_reg_id,
				uint32_t enable_events);
void fman_set_num_of_riscs_per_port(struct fman_fpm_regs *fpm_rg,
				uint8_t port_id,
				uint8_t num_fman_ctrls,
				uint32_t or_fman_ctrl);
void fman_set_order_restoration_per_port(struct fman_fpm_regs *fpm_rg,
				uint8_t port_id,
				bool independent_mode,
				bool is_rx_port);
void fman_set_qmi_enq_th(struct fman_qmi_regs *qmi_rg, uint8_t val);
void fman_set_qmi_deq_th(struct fman_qmi_regs *qmi_rg, uint8_t val);
void fman_set_liodn_per_port(struct fman_rg *fman_rg,
				uint8_t port_id,
				uint16_t liodn_base,
				uint16_t liodn_offset);
void fman_set_size_of_fifo(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint32_t size_of_fifo,
				uint32_t extra_size_of_fifo);
void fman_set_num_of_tasks(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint8_t num_of_tasks,
				uint8_t num_of_extra_tasks);
void fman_set_num_of_open_dmas(struct fman_bmi_regs *bmi_rg,
				uint8_t port_id,
				uint8_t num_of_open_dmas,
				uint8_t num_of_extra_open_dmas,
				uint8_t total_num_of_dmas);
void fman_set_ports_bandwidth(struct fman_bmi_regs *bmi_rg, uint8_t *weights);
int fman_set_exception(struct fman_rg *fman_rg,
				enum fman_exceptions exception,
				bool enable);
void fman_set_dma_emergency(struct fman_dma_regs *dma_rg, bool is_write,
				bool enable);
void fman_set_dma_ext_bus_pri(struct fman_dma_regs *dma_rg, uint32_t pri);
void fman_set_congestion_group_pfc_priority(uint32_t *cpg_rg,
                                            uint32_t congestion_group_id,
                                            uint8_t piority_bit_map,
                                            uint32_t reg_num);


void fman_defconfig(struct fman_cfg *cfg, bool is_master);
void fman_regconfig(struct fman_rg *fman_rg, struct fman_cfg *cfg);
int fman_fpm_init(struct fman_fpm_regs *fpm_rg, struct fman_cfg *cfg);
int fman_bmi_init(struct fman_bmi_regs *bmi_rg, struct fman_cfg *cfg);
int fman_qmi_init(struct fman_qmi_regs *qmi_rg, struct fman_cfg *cfg);
int fman_dma_init(struct fman_dma_regs *dma_rg, struct fman_cfg *cfg);
void fman_free_resources(struct fman_rg *fman_rg);
int fman_enable(struct fman_rg *fman_rg, struct fman_cfg *cfg);
void fman_reset(struct fman_fpm_regs *fpm_rg);
void fman_resume(struct fman_fpm_regs *fpm_rg);


void fman_enable_time_stamp(struct fman_fpm_regs *fpm_rg,
				uint8_t count1ubit,
				uint16_t fm_clk_freq);
void fman_enable_rams_ecc(struct fman_fpm_regs *fpm_rg);
void fman_qmi_disable_dispatch_limit(struct fman_fpm_regs *fpm_rg);
void fman_disable_rams_ecc(struct fman_fpm_regs *fpm_rg);
void fman_resume_stalled_port(struct fman_fpm_regs *fpm_rg, uint8_t port_id);
int fman_reset_mac(struct fman_fpm_regs *fpm_rg, uint8_t macId, bool is_10g);
bool fman_is_port_stalled(struct fman_fpm_regs *fpm_rg, uint8_t port_id);
bool fman_rams_ecc_is_external_ctl(struct fman_fpm_regs *fpm_rg);
bool fman_is_qmi_halt_not_busy_state(struct fman_qmi_regs *qmi_rg);
int fman_modify_counter(struct fman_rg *fman_rg,
				enum fman_counters reg_name,
				uint32_t val);
void fman_force_intr(struct fman_rg *fman_rg,
				enum fman_exceptions exception);
void fman_set_vsp_window(struct fman_bmi_regs *bmi_rg,
			    	     uint8_t port_id,
				         uint8_t base_storage_profile,
				         uint8_t log2_num_of_profiles);

/**************************************************************************//**
 @Description       default values
*//***************************************************************************/
#define DEFAULT_CATASTROPHIC_ERR                E_FMAN_CATAST_ERR_STALL_PORT
#define DEFAULT_DMA_ERR                         E_FMAN_DMA_ERR_CATASTROPHIC
#define DEFAULT_HALT_ON_EXTERNAL_ACTIVATION     FALSE   /* do not change! if changed, must be disabled for rev1 ! */
#define DEFAULT_HALT_ON_UNRECOVERABLE_ECC_ERROR FALSE   /* do not change! if changed, must be disabled for rev1 ! */
#define DEFAULT_EXTERNAL_ECC_RAMS_ENABLE        FALSE
#define DEFAULT_AID_OVERRIDE                    FALSE
#define DEFAULT_AID_MODE                        E_FMAN_DMA_AID_OUT_TNUM
#define DEFAULT_DMA_COMM_Q_LOW                  0x2A
#define DEFAULT_DMA_COMM_Q_HIGH                 0x3F
#define DEFAULT_CACHE_OVERRIDE                  E_FMAN_DMA_NO_CACHE_OR
#define DEFAULT_DMA_CAM_NUM_OF_ENTRIES          64
#define DEFAULT_DMA_DBG_CNT_MODE                E_FMAN_DMA_DBG_NO_CNT
#define DEFAULT_DMA_EN_EMERGENCY                FALSE
#define DEFAULT_DMA_SOS_EMERGENCY               0
#define DEFAULT_DMA_WATCHDOG                    0 /* disabled */
#define DEFAULT_DMA_EN_EMERGENCY_SMOOTHER       FALSE
#define DEFAULT_DMA_EMERGENCY_SWITCH_COUNTER    0
#define DEFAULT_DISP_LIMIT                      0
#define DEFAULT_PRS_DISP_TH                     16
#define DEFAULT_PLCR_DISP_TH                    16
#define DEFAULT_KG_DISP_TH                      16
#define DEFAULT_BMI_DISP_TH                     16
#define DEFAULT_QMI_ENQ_DISP_TH                 16
#define DEFAULT_QMI_DEQ_DISP_TH                 16
#define DEFAULT_FM_CTL1_DISP_TH                 16
#define DEFAULT_FM_CTL2_DISP_TH                 16
#define DEFAULT_TNUM_AGING_PERIOD               4


#endif /* __FSL_FMAN_H */

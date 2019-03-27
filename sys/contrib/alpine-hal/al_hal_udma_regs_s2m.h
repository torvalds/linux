/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @file   al_hal_udma_regs_s2m.h
 *
 * @brief C Header file for the UDMA S2M registers
 *
 */

#ifndef __AL_HAL_UDMA_S2M_REG_H
#define __AL_HAL_UDMA_S2M_REG_H

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct udma_axi_s2m {
	/* [0x0] Data write master configuration */
	uint32_t data_wr_cfg_1;
	/* [0x4] Data write master configuration */
	uint32_t data_wr_cfg_2;
	/* [0x8] Descriptor read master configuration */
	uint32_t desc_rd_cfg_4;
	/* [0xc] Descriptor read master configuration */
	uint32_t desc_rd_cfg_5;
	/* [0x10] Completion  write master configuration */
	uint32_t comp_wr_cfg_1;
	/* [0x14] Completion  write master configuration */
	uint32_t comp_wr_cfg_2;
	/* [0x18] Data write master configuration */
	uint32_t data_wr_cfg;
	/* [0x1c] Descriptors read master configuration */
	uint32_t desc_rd_cfg_3;
	/* [0x20] Completion descriptors write master configuration */
	uint32_t desc_wr_cfg_1;
	/* [0x24] AXI outstanding read configuration */
	uint32_t ostand_cfg_rd;
	/* [0x28] AXI outstanding write configuration */
	uint32_t ostand_cfg_wr;
	uint32_t rsrvd[53];
};
struct udma_s2m {
	/*
	 * [0x0] DMA state
	 * 00  - No pending tasks
	 * 01 – Normal (active)
	 * 10 – Abort (error condition)
	 * 11 – Reserved
	 */
	uint32_t state;
	/* [0x4] CPU request to change DMA state */
	uint32_t change_state;
	uint32_t rsrvd_0;
	/*
	 * [0xc] S2M DMA error log mask.
	 * Each error has an interrupt controller cause bit.
	 * This register determines if these errors cause the S2M DMA to log the
	 * error condition.
	 * 0 - Log is enable
	 * 1 - Log is masked.
	 */
	uint32_t err_log_mask;
	uint32_t rsrvd_1;
	/*
	 * [0x14] DMA header log
	 * Sample the packet header that caused the error
	 */
	uint32_t log_0;
	/*
	 * [0x18] DMA header log
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_1;
	/*
	 * [0x1c] DMA header log
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_2;
	/*
	 * [0x20] DMA header log
	 * Sample the packet header that caused the error
	 */
	uint32_t log_3;
	/* [0x24] DMA clear error log */
	uint32_t clear_err_log;
	/* [0x28] S2M stream data FIFO status */
	uint32_t s_data_fifo_status;
	/* [0x2c] S2M stream header FIFO status */
	uint32_t s_header_fifo_status;
	/* [0x30] S2M AXI data FIFO status */
	uint32_t axi_data_fifo_status;
	/* [0x34] S2M unack FIFO status */
	uint32_t unack_fifo_status;
	/* [0x38] Select queue for debug */
	uint32_t indirect_ctrl;
	/*
	 * [0x3c] S2M prefetch FIFO status.
	 * Status of the selected queue in S2M_indirect_ctrl
	 */
	uint32_t sel_pref_fifo_status;
	/*
	 * [0x40] S2M completion FIFO status.
	 * Status of the selected queue in S2M_indirect_ctrl
	 */
	uint32_t sel_comp_fifo_status;
	/* [0x44] S2M state machine and FIFO clear control */
	uint32_t clear_ctrl;
	/* [0x48] S2M Misc Check enable */
	uint32_t check_en;
	/* [0x4c] S2M FIFO enable control, internal */
	uint32_t fifo_en;
	/* [0x50] Stream interface configuration */
	uint32_t stream_cfg;
	uint32_t rsrvd[43];
};
struct udma_s2m_rd {
	/* [0x0] S2M descriptor prefetch configuration */
	uint32_t desc_pref_cfg_1;
	/* [0x4] S2M descriptor prefetch configuration */
	uint32_t desc_pref_cfg_2;
	/* [0x8] S2M descriptor prefetch configuration */
	uint32_t desc_pref_cfg_3;
	/* [0xc] S2M descriptor prefetch configuration */
	uint32_t desc_pref_cfg_4;
	uint32_t rsrvd[12];
};
struct udma_s2m_wr {
	/* [0x0] Stream data FIFO configuration */
	uint32_t data_cfg_1;
	/* [0x4] Data write configuration */
	uint32_t data_cfg_2;
	uint32_t rsrvd[14];
};
struct udma_s2m_comp {
	/* [0x0] Completion controller configuration */
	uint32_t cfg_1c;
	/* [0x4] Completion controller configuration */
	uint32_t cfg_2c;
	uint32_t rsrvd_0;
	/* [0xc] Completion controller application acknowledge configuration */
	uint32_t cfg_application_ack;
	uint32_t rsrvd[12];
};
struct udma_s2m_stat {
	uint32_t rsrvd_0;
	/* [0x4] Number of dropped packets */
	uint32_t drop_pkt;
	/*
	 * [0x8] Counting the net length of the data buffers [64-bit]
	 * Should be read before rx_bytes_high
	 */
	uint32_t rx_bytes_low;
	/*
	 * [0xc] Counting the net length of the data buffers [64-bit]
	 * Should be read after tx_bytes_low (value is sampled when reading
	 * Should be read before rx_bytes_low
	 */
	uint32_t rx_bytes_high;
	/* [0x10] Total number of descriptors read from the host memory */
	uint32_t prefed_desc;
	/* [0x14] Number of packets written into the completion ring */
	uint32_t comp_pkt;
	/* [0x18] Number of descriptors written into the completion ring */
	uint32_t comp_desc;
	/*
	 * [0x1c] Number of acknowledged packets.
	 * (acknowledge sent to the stream interface)
	 */
	uint32_t ack_pkts;
	uint32_t rsrvd[56];
};
struct udma_s2m_feature {
	/*
	 * [0x0] S2M Feature register
	 * S2M instantiation parameters
	 */
	uint32_t reg_1;
	/* [0x4] Reserved S2M feature register */
	uint32_t reg_2;
	/*
	 * [0x8] S2M Feature register
	 * S2M instantiation parameters
	 */
	uint32_t reg_3;
	/*
	 * [0xc] S2M Feature register.
	 * S2M instantiation parameters.
	 */
	uint32_t reg_4;
	/*
	 * [0x10] S2M Feature register.
	 * S2M instantiation parameters.
	 */
	uint32_t reg_5;
	/* [0x14] S2M Feature register. S2M instantiation parameters. */
	uint32_t reg_6;
	uint32_t rsrvd[58];
};
struct udma_s2m_q {
	uint32_t rsrvd_0[8];
	/* [0x20] S2M Descriptor ring configuration */
	uint32_t cfg;
	/* [0x24] S2M Descriptor ring status and information */
	uint32_t status;
	/* [0x28] Rx Descriptor Ring Base Pointer [31:4] */
	uint32_t rdrbp_low;
	/* [0x2c] Rx Descriptor Ring Base Pointer [63:32] */
	uint32_t rdrbp_high;
	/*
	 * [0x30] Rx Descriptor Ring Length[23:2]
	 */
	uint32_t rdrl;
	/* [0x34] RX Descriptor Ring Head Pointer */
	uint32_t rdrhp;
	/* [0x38] Rx Descriptor Tail Pointer increment */
	uint32_t rdrtp_inc;
	/* [0x3c] Rx Descriptor Tail Pointer */
	uint32_t rdrtp;
	/* [0x40] RX Descriptor Current Pointer */
	uint32_t rdcp;
	/* [0x44] Rx Completion Ring Base Pointer [31:4] */
	uint32_t rcrbp_low;
	/* [0x48] Rx Completion Ring Base Pointer [63:32] */
	uint32_t rcrbp_high;
	/* [0x4c] Rx Completion Ring Head Pointer */
	uint32_t rcrhp;
	/*
	 * [0x50] RX Completion Ring Head Pointer internal.
	 * (Before the coalescing FIFO)
	 */
	uint32_t rcrhp_internal;
	/* [0x54] Completion controller configuration for the queue */
	uint32_t comp_cfg;
	/* [0x58] Completion controller configuration for the queue */
	uint32_t comp_cfg_2;
	/* [0x5c] Packet handler configuration */
	uint32_t pkt_cfg;
	/* [0x60] Queue QoS configuration */
	uint32_t qos_cfg;
	/* [0x64] DMB software control */
	uint32_t q_sw_ctrl;
	/* [0x68] Number of S2M Rx packets after completion  */
	uint32_t q_rx_pkt;
	uint32_t rsrvd[997];
};

struct udma_s2m_regs {
	uint32_t rsrvd_0[64];
	struct udma_axi_s2m axi_s2m;                     /* [0x100] */
	struct udma_s2m s2m;                             /* [0x200] */
	struct udma_s2m_rd s2m_rd;                       /* [0x300] */
	struct udma_s2m_wr s2m_wr;                       /* [0x340] */
	struct udma_s2m_comp s2m_comp;                   /* [0x380] */
	uint32_t rsrvd_1[80];
	struct udma_s2m_stat s2m_stat;                   /* [0x500] */
	struct udma_s2m_feature s2m_feature;             /* [0x600] */
	uint32_t rsrvd_2[576];
	struct udma_s2m_q s2m_q[4];                      /* [0x1000] */
};


/*
* Registers Fields
*/


/**** data_wr_cfg_1 register ****/
/* AXI write  ID (AWID) */
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWID_MASK 0x000000FF
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWCACHE_MASK 0x000F0000
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWBURST_MASK 0x03000000
#define UDMA_AXI_S2M_DATA_WR_CFG_1_AWBURST_SHIFT 24

/**** data_wr_cfg_2 register ****/
/* User extension */
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWUSER_MASK 0x000FFFFF
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWSIZE_MASK 0x00700000
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWSIZE_SHIFT 20
/*
 * AXI Master QoS.
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWQOS_MASK 0x07000000
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWPROT_MASK 0x70000000
#define UDMA_AXI_S2M_DATA_WR_CFG_2_AWPROT_SHIFT 28

/**** desc_rd_cfg_4 register ****/
/* AXI read  ID (ARID) */
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARID_MASK 0x000000FF
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARCACHE_MASK 0x000F0000
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARBURST_MASK 0x03000000
#define UDMA_AXI_S2M_DESC_RD_CFG_4_ARBURST_SHIFT 24

/**** desc_rd_cfg_5 register ****/
/* User extension */
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARUSER_MASK 0x000FFFFF
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARSIZE_MASK 0x00700000
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARSIZE_SHIFT 20
/*
 * AXI Master QoS.
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARQOS_MASK 0x07000000
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARPROT_MASK 0x70000000
#define UDMA_AXI_S2M_DESC_RD_CFG_5_ARPROT_SHIFT 28

/**** comp_wr_cfg_1 register ****/
/* AXI write  ID (AWID) */
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWID_MASK 0x000000FF
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWCACHE_MASK 0x000F0000
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWBURST_MASK 0x03000000
#define UDMA_AXI_S2M_COMP_WR_CFG_1_AWBURST_SHIFT 24

/**** comp_wr_cfg_2 register ****/
/* User extension */
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWUSER_MASK 0x000FFFFF
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWSIZE_MASK 0x00700000
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWSIZE_SHIFT 20
/*
 * AXI Master QoS.
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWQOS_MASK 0x07000000
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWPROT_MASK 0x70000000
#define UDMA_AXI_S2M_COMP_WR_CFG_2_AWPROT_SHIFT 28

/**** data_wr_cfg register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst. This value is
 * used for the burst split decision.
 */
#define UDMA_AXI_S2M_DATA_WR_CFG_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_S2M_DATA_WR_CFG_MAX_AXI_BEATS_SHIFT 0

/**** desc_rd_cfg_3 register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst. This value is
 * used for the burst split decision.
 */
#define UDMA_AXI_S2M_DESC_RD_CFG_3_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_S2M_DESC_RD_CFG_3_MAX_AXI_BEATS_SHIFT 0
/*
 * Enables breaking descriptor read request.
 * Aligned to max_AXI_beats when the total read size is less than max_AXI_beats.
 */
#define UDMA_AXI_S2M_DESC_RD_CFG_3_ALWAYS_BREAK_ON_MAX_BOUDRY (1 << 16)

/**** desc_wr_cfg_1 register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst. This value is
 * used for the burst split decision.
 */
#define UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_SHIFT 0
/*
 * Minimum burst for writing completion descriptors.
 * (AXI beats).
 * Value must be aligned to cache lines (64 bytes).
 * Default value is 2 cache lines, 8 beats.
 */
#define UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK 0x00FF0000
#define UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_SHIFT 16

/**** ostand_cfg_rd register ****/
/*
 * Maximum number of outstanding descriptor reads to the AXI.
 * (AXI transactions).
 */
#define UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_MASK 0x0000003F
#define UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_SHIFT 0
/* Maximum number of outstanding stream acknowledges. */
#define UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_STREAM_ACK_MASK 0x001F0000
#define UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_STREAM_ACK_SHIFT 16

/**** ostand_cfg_wr register ****/
/*
 * Maximum number of outstanding data writes to the AXI.
 * (AXI transactions).
 */
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_MASK 0x0000003F
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_SHIFT 0
/*
 * Maximum number of outstanding data beats for data write to AXI.
 * (AXI beats).
 */
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_BEATS_WR_OSTAND_MASK 0x0000FF00
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_BEATS_WR_OSTAND_SHIFT 8
/*
 * Maximum number of outstanding descriptor writes to the AXI.
 * (AXI transactions).
 */
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_REQ_MASK 0x003F0000
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_REQ_SHIFT 16
/*
 * Maximum number of outstanding data beats for descriptor write to AXI.
 * (AXI beats).
 */
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_DATA_WR_OSTAND_MASK 0xFF000000
#define UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_DATA_WR_OSTAND_SHIFT 24

/**** state register ****/

#define UDMA_S2M_STATE_COMP_CTRL_MASK 0x00000003
#define UDMA_S2M_STATE_COMP_CTRL_SHIFT 0

#define UDMA_S2M_STATE_STREAM_IF_MASK 0x00000030
#define UDMA_S2M_STATE_STREAM_IF_SHIFT 4

#define UDMA_S2M_STATE_DATA_WR_CTRL_MASK 0x00000300
#define UDMA_S2M_STATE_DATA_WR_CTRL_SHIFT 8

#define UDMA_S2M_STATE_DESC_PREF_MASK 0x00003000
#define UDMA_S2M_STATE_DESC_PREF_SHIFT 12

#define UDMA_S2M_STATE_AXI_WR_DATA_MASK 0x00030000
#define UDMA_S2M_STATE_AXI_WR_DATA_SHIFT 16

/**** change_state register ****/
/* Start normal operation */
#define UDMA_S2M_CHANGE_STATE_NORMAL (1 << 0)
/* Stop normal operation */
#define UDMA_S2M_CHANGE_STATE_DIS    (1 << 1)
/*
 * Stop all machines.
 * (Prefetch, scheduling, completion and stream interface)
 */
#define UDMA_S2M_CHANGE_STATE_ABORT  (1 << 2)

/**** clear_err_log register ****/
/* Clear error log */
#define UDMA_S2M_CLEAR_ERR_LOG_CLEAR (1 << 0)

/**** s_data_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_S_DATA_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_S_DATA_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_S2M_S_DATA_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_S_DATA_FIFO_STATUS_FULL (1 << 28)

/**** s_header_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_S_HEADER_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_S_HEADER_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_S2M_S_HEADER_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_S_HEADER_FIFO_STATUS_FULL (1 << 28)

/**** axi_data_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_AXI_DATA_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_AXI_DATA_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_S2M_AXI_DATA_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_AXI_DATA_FIFO_STATUS_FULL (1 << 28)

/**** unack_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_UNACK_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_UNACK_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_S2M_UNACK_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_UNACK_FIFO_STATUS_FULL (1 << 28)

/**** indirect_ctrl register ****/
/* Selected queue for status read */
#define UDMA_S2M_INDIRECT_CTRL_Q_NUM_MASK 0x00000FFF
#define UDMA_S2M_INDIRECT_CTRL_Q_NUM_SHIFT 0

/**** sel_pref_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_SEL_PREF_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_SEL_PREF_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_S2M_SEL_PREF_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_SEL_PREF_FIFO_STATUS_FULL (1 << 28)

/**** sel_comp_fifo_status register ****/
/* FIFO used indication */
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_USED_SHIFT 0
/* Coalescing ACTIVE FSM state indication. */
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_COAL_ACTIVE_STATE_MASK 0x00300000
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_COAL_ACTIVE_STATE_SHIFT 20
/* FIFO empty indication */
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_S2M_SEL_COMP_FIFO_STATUS_FULL (1 << 28)

/**** stream_cfg register ****/
/*
 * Disables the stream interface operation.
 * Changing to 1 stops at the end of packet reception.
 */
#define UDMA_S2M_STREAM_CFG_DISABLE  (1 << 0)
/*
 * Flush the stream interface operation.
 * Changing to 1 stops at the end of packet reception and assert ready to the
 * stream I/F.
 */
#define UDMA_S2M_STREAM_CFG_FLUSH    (1 << 4)
/* Stop descriptor prefetch when the stream is disabled and the S2M is idle. */
#define UDMA_S2M_STREAM_CFG_STOP_PREFETCH (1 << 8)

/**** desc_pref_cfg_1 register ****/
/*
 * Size of the descriptor prefetch FIFO.
 * (descriptors)
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_1_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_S2M_RD_DESC_PREF_CFG_1_FIFO_DEPTH_SHIFT 0

/**** desc_pref_cfg_2 register ****/
/* Enable promotion of the current queue in progress */
#define UDMA_S2M_RD_DESC_PREF_CFG_2_Q_PROMOTION (1 << 0)
/* Force promotion of the current queue in progress */
#define UDMA_S2M_RD_DESC_PREF_CFG_2_FORCE_PROMOTION (1 << 1)
/* Enable prefetch prediction of next packet in line. */
#define UDMA_S2M_RD_DESC_PREF_CFG_2_EN_PREF_PREDICTION (1 << 2)
/*
 * Threshold for queue promotion.
 * Queue is promoted for prefetch if there are less descriptors in the prefetch
 * FIFO than the threshold
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_2_PROMOTION_TH_MASK 0x0000FF00
#define UDMA_S2M_RD_DESC_PREF_CFG_2_PROMOTION_TH_SHIFT 8
/*
 * Force RR arbitration in the prefetch arbiter.
 * 0 - Standard arbitration based on queue QoS
 * 1 - Force round robin arbitration
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_2_PREF_FORCE_RR (1 << 16)

/**** desc_pref_cfg_3 register ****/
/*
 * Minimum descriptor burst size when prefetch FIFO level is below the
 * descriptor prefetch threshold
 * (must be 1)
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK 0x0000000F
#define UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_SHIFT 0
/*
 * Minimum descriptor burst size when prefetch FIFO level is above the
 * descriptor prefetch threshold
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK 0x000000F0
#define UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT 4
/*
 * Descriptor fetch threshold.
 * Used as a threshold to determine the allowed minimum descriptor burst size.
 * (Must be at least "max_desc_per_pkt")
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_MASK 0x0000FF00
#define UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT 8

/**** desc_pref_cfg_4 register ****/
/*
 * Used as a threshold for generating almost FULL indication to the application
 */
#define UDMA_S2M_RD_DESC_PREF_CFG_4_A_FULL_THR_MASK 0x000000FF
#define UDMA_S2M_RD_DESC_PREF_CFG_4_A_FULL_THR_SHIFT 0

/**** data_cfg_1 register ****/
/*
 * Maximum number of data beats in the data write FIFO.
 * Defined based on data FIFO size
 * (default FIFO size 512B → 32 beats)
 */
#define UDMA_S2M_WR_DATA_CFG_1_DATA_FIFO_DEPTH_MASK 0x000003FF
#define UDMA_S2M_WR_DATA_CFG_1_DATA_FIFO_DEPTH_SHIFT 0
/*
 * Maximum number of packets in the data write FIFO.
 * Defined based on header FIFO size
 */
#define UDMA_S2M_WR_DATA_CFG_1_MAX_PKT_LIMIT_MASK 0x00FF0000
#define UDMA_S2M_WR_DATA_CFG_1_MAX_PKT_LIMIT_SHIFT 16
/*
 * Internal use
 * Data FIFO margin
 */
#define UDMA_S2M_WR_DATA_CFG_1_FIFO_MARGIN_MASK 0xFF000000
#define UDMA_S2M_WR_DATA_CFG_1_FIFO_MARGIN_SHIFT 24

/**** data_cfg_2 register ****/
/*
 * Drop timer.
 * Waiting time for the host to write new descriptor to the queue
 * (for the current packet in process)
 */
#define UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_MASK 0x00FFFFFF
#define UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_SHIFT 0
/*
 * Drop enable.
 * Enable packet drop if there are no available descriptors in the system for
 * this queue
 */
#define UDMA_S2M_WR_DATA_CFG_2_DROP_IF_NO_DESC (1 << 27)
/*
 * Lack of descriptors hint.
 * Generate interrupt when a packet is waiting but there are no available
 * descriptors in the queue
 */
#define UDMA_S2M_WR_DATA_CFG_2_HINT_IF_NO_DESC (1 << 28)
/*
 * Drop conditions
 * Wait until a descriptor is available in the prefetch FIFO or the host before
 * dropping packet.
 * 1 - Drop if a descriptor is not available in the prefetch.
 * 0 - Drop if a descriptor is not available in the system
 */
#define UDMA_S2M_WR_DATA_CFG_2_WAIT_FOR_PREF (1 << 29)
/*
 * DRAM write optimization
 * 0 - Data write with byte enable
 * 1 - Data write is always in Full AXI bus width (128 bit)
 */
#define UDMA_S2M_WR_DATA_CFG_2_FULL_LINE_MODE (1 << 30)
/*
 * Direct data write address
 * 1 - Use buffer 1 instead of buffer 2 when direct data placement is used with
 * header split.
 * 0 - Use buffer 2 for the header.
 */
#define UDMA_S2M_WR_DATA_CFG_2_DIRECT_HDR_USE_BUF1 (1 << 31)

/**** cfg_1c register ****/
/*
 * Completion descriptor size.
 * (words)
 */
#define UDMA_S2M_COMP_CFG_1C_DESC_SIZE_MASK 0x0000000F
#define UDMA_S2M_COMP_CFG_1C_DESC_SIZE_SHIFT 0
/*
 * Completion queue counter configuration.
 * Completion FIFO in use counter measured in words or descriptors
 * 1 - Words
 * 0 - Descriptors
 */
#define UDMA_S2M_COMP_CFG_1C_CNT_WORDS (1 << 8)
/*
 * Enable promotion of the current queue in progress in the completion write
 * scheduler.
 */
#define UDMA_S2M_COMP_CFG_1C_Q_PROMOTION (1 << 12)
/* Force RR arbitration in the completion arbiter */
#define UDMA_S2M_COMP_CFG_1C_FORCE_RR (1 << 16)
/* Minimum number of free completion entries to qualify for promotion */
#define UDMA_S2M_COMP_CFG_1C_Q_FREE_MIN_MASK 0xF0000000
#define UDMA_S2M_COMP_CFG_1C_Q_FREE_MIN_SHIFT 28

/**** cfg_2c register ****/
/*
 * Completion FIFO size.
 * (words per queue)
 */
#define UDMA_S2M_COMP_CFG_2C_COMP_FIFO_DEPTH_MASK 0x00000FFF
#define UDMA_S2M_COMP_CFG_2C_COMP_FIFO_DEPTH_SHIFT 0
/*
 * Unacknowledged FIFO size.
 * (descriptors)
 */
#define UDMA_S2M_COMP_CFG_2C_UNACK_FIFO_DEPTH_MASK 0x0FFF0000
#define UDMA_S2M_COMP_CFG_2C_UNACK_FIFO_DEPTH_SHIFT 16

/**** reg_1 register ****/
/*
 * Descriptor prefetch FIFO size
 * (descriptors)
 */
#define UDMA_S2M_FEATURE_REG_1_DESC_PREFERCH_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_S2M_FEATURE_REG_1_DESC_PREFERCH_FIFO_DEPTH_SHIFT 0

/**** reg_3 register ****/
/*
 * Maximum number of data beats in the data write FIFO.
 * Defined based on data FIFO size
 * (default FIFO size 512B →32 beats)
 */
#define UDMA_S2M_FEATURE_REG_3_DATA_FIFO_DEPTH_MASK 0x000003FF
#define UDMA_S2M_FEATURE_REG_3_DATA_FIFO_DEPTH_SHIFT 0
/*
 * Maximum number of packets in the data write FIFO.
 * Defined based on header FIFO size
 */
#define UDMA_S2M_FEATURE_REG_3_DATA_WR_MAX_PKT_LIMIT_MASK 0x00FF0000
#define UDMA_S2M_FEATURE_REG_3_DATA_WR_MAX_PKT_LIMIT_SHIFT 16

/**** reg_4 register ****/
/*
 * Completion FIFO size.
 * (words per queue)
 */
#define UDMA_S2M_FEATURE_REG_4_COMP_FIFO_DEPTH_MASK 0x00000FFF
#define UDMA_S2M_FEATURE_REG_4_COMP_FIFO_DEPTH_SHIFT 0
/*
 * Unacknowledged FIFO size.
 * (descriptors)
 */
#define UDMA_S2M_FEATURE_REG_4_COMP_UNACK_FIFO_DEPTH_MASK 0x0FFF0000
#define UDMA_S2M_FEATURE_REG_4_COMP_UNACK_FIFO_DEPTH_SHIFT 16

/**** reg_5 register ****/
/* Maximum number of outstanding data writes to the AXI */
#define UDMA_S2M_FEATURE_REG_5_MAX_DATA_WR_OSTAND_MASK 0x0000003F
#define UDMA_S2M_FEATURE_REG_5_MAX_DATA_WR_OSTAND_SHIFT 0
/*
 * Maximum number of outstanding data beats for data write to AXI.
 * (AXI beats)
 */
#define UDMA_S2M_FEATURE_REG_5_MAX_DATA_BEATS_WR_OSTAND_MASK 0x0000FF00
#define UDMA_S2M_FEATURE_REG_5_MAX_DATA_BEATS_WR_OSTAND_SHIFT 8
/*
 * Maximum number of outstanding descriptor reads to the AXI.
 * (AXI transactions)
 */
#define UDMA_S2M_FEATURE_REG_5_MAX_COMP_REQ_MASK 0x003F0000
#define UDMA_S2M_FEATURE_REG_5_MAX_COMP_REQ_SHIFT 16
/*
 * Maximum number of outstanding data beats for descriptor write to AXI.
 * (AXI beats)
 */
#define UDMA_S2M_FEATURE_REG_5_MAX_COMP_DATA_WR_OSTAND_MASK 0xFF000000
#define UDMA_S2M_FEATURE_REG_5_MAX_COMP_DATA_WR_OSTAND_SHIFT 24

/**** reg_6 register ****/
/* Maximum number of outstanding descriptor reads to the AXI */
#define UDMA_S2M_FEATURE_REG_6_MAX_DESC_RD_OSTAND_MASK 0x0000003F
#define UDMA_S2M_FEATURE_REG_6_MAX_DESC_RD_OSTAND_SHIFT 0
/* Maximum number of outstanding stream acknowledges */
#define UDMA_S2M_FEATURE_REG_6_MAX_STREAM_ACK_MASK 0x001F0000
#define UDMA_S2M_FEATURE_REG_6_MAX_STREAM_ACK_SHIFT 16

/**** cfg register ****/
/*
 * Configure the AXI AWCACHE
 * for header write.
 */
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_HDR_MASK 0x0000000F
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_HDR_SHIFT 0
/*
 * Configure the AXI AWCACHE
 * for data write.
 */
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_DATA_MASK 0x000000F0
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_DATA_SHIFT 4
/*
 * Enable operation of this queue.
 * Start prefetch.
 */
#define UDMA_S2M_Q_CFG_EN_PREF       (1 << 16)
/* Enables the reception of packets from the stream to this queue */
#define UDMA_S2M_Q_CFG_EN_STREAM     (1 << 17)
/* Allow prefetch of less than minimum prefetch burst size. */
#define UDMA_S2M_Q_CFG_ALLOW_LT_MIN_PREF (1 << 20)
/*
 * Configure the AXI AWCACHE
 * for completion descriptor write
 */
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_COMP_MASK 0x0F000000
#define UDMA_S2M_Q_CFG_AXI_AWCACHE_COMP_SHIFT 24
/*
 * AXI QoS
 * This value is used in AXI transactions associated with this queue and the
 * prefetch and completion arbiters.
 */
#define UDMA_S2M_Q_CFG_AXI_QOS_MASK  0x70000000
#define UDMA_S2M_Q_CFG_AXI_QOS_SHIFT 28

/**** status register ****/
/* Indicates how many entries are used in the Queue */
#define UDMA_S2M_Q_STATUS_Q_USED_MASK 0x01FFFFFF
#define UDMA_S2M_Q_STATUS_Q_USED_SHIFT 0
/*
 * prefetch status
 * 0 – prefetch operation is stopped
 * 1 – prefetch is operational
 */
#define UDMA_S2M_Q_STATUS_PREFETCH   (1 << 28)
/*
 * Queue receive status
 * 0 -queue RX operation is stopped
 * 1 – RX queue is active and processing packets
 */
#define UDMA_S2M_Q_STATUS_RX         (1 << 29)
/*
 * Indicates if the queue is full.
 * (Used by the host when head pointer equals tail pointer)
 */
#define UDMA_S2M_Q_STATUS_Q_FULL     (1 << 31)
/*
 * S2M Descriptor Ring Base address [31:4].
 * Value of the base address of the S2M descriptor ring
 * [3:0] - 0 - 16B alignment is enforced
 * ([11:4] should be 0 for 4KB alignment)
 */
#define UDMA_S2M_Q_RDRBP_LOW_ADDR_MASK 0xFFFFFFF0
#define UDMA_S2M_Q_RDRBP_LOW_ADDR_SHIFT 4

/**** RDRL register ****/
/*
 * Length of the descriptor ring.
 * (descriptors)
 * Associated with the ring base address ends at maximum burst size alignment
 */
#define UDMA_S2M_Q_RDRL_OFFSET_MASK  0x00FFFFFF
#define UDMA_S2M_Q_RDRL_OFFSET_SHIFT 0

/**** RDRHP register ****/
/*
 * Relative offset of the next descriptor that needs to be read into the
 * prefetch FIFO.
 * Incremented when the DMA reads valid descriptors from the host memory to the
 * prefetch FIFO.
 * Note that this is the offset in # of descriptors and not in byte address.
 */
#define UDMA_S2M_Q_RDRHP_OFFSET_MASK 0x00FFFFFF
#define UDMA_S2M_Q_RDRHP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_S2M_Q_RDRHP_RING_ID_MASK 0xC0000000
#define UDMA_S2M_Q_RDRHP_RING_ID_SHIFT 30

/**** RDRTP_inc register ****/
/*
 * Increments the value in Q_RDRTP with the value written to this field in
 * number of descriptors.
 */
#define UDMA_S2M_Q_RDRTP_INC_VAL_MASK 0x00FFFFFF
#define UDMA_S2M_Q_RDRTP_INC_VAL_SHIFT 0

/**** RDRTP register ****/
/*
 * Relative offset of the next free descriptor in the host memory.
 * Note that this is the offset in # of descriptors and not in byte address.
 */
#define UDMA_S2M_Q_RDRTP_OFFSET_MASK 0x00FFFFFF
#define UDMA_S2M_Q_RDRTP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_S2M_Q_RDRTP_RING_ID_MASK 0xC0000000
#define UDMA_S2M_Q_RDRTP_RING_ID_SHIFT 30

/**** RDCP register ****/
/* Relative offset of the first descriptor in the prefetch FIFO.  */
#define UDMA_S2M_Q_RDCP_OFFSET_MASK  0x00FFFFFF
#define UDMA_S2M_Q_RDCP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_S2M_Q_RDCP_RING_ID_MASK 0xC0000000
#define UDMA_S2M_Q_RDCP_RING_ID_SHIFT 30
/*
 * S2M Descriptor Ring Base address [31:4].
 * Value of the base address of the S2M descriptor ring
 * [3:0] - 0 - 16B alignment is enforced
 * ([11:4] Must be 0 for 4KB alignment)
 * NOTE:
 * Length of the descriptor ring (in descriptors) associated with the ring base
 * address ends at maximum burst size alignment
 */
#define UDMA_S2M_Q_RCRBP_LOW_ADDR_MASK 0xFFFFFFF0
#define UDMA_S2M_Q_RCRBP_LOW_ADDR_SHIFT 4

/**** RCRHP register ****/
/*
 * Relative offset of the next descriptor that needs to be updated by the
 * completion controller.
 * Note: This is in descriptors and not in byte address.
 */
#define UDMA_S2M_Q_RCRHP_OFFSET_MASK 0x00FFFFFF
#define UDMA_S2M_Q_RCRHP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_S2M_Q_RCRHP_RING_ID_MASK 0xC0000000
#define UDMA_S2M_Q_RCRHP_RING_ID_SHIFT 30

/**** RCRHP_internal register ****/
/*
 * Relative offset of the next descriptor that needs to be updated by the
 * completion controller.
 * Note: This is in descriptors and not in byte address.
 */
#define UDMA_S2M_Q_RCRHP_INTERNAL_OFFSET_MASK 0x00FFFFFF
#define UDMA_S2M_Q_RCRHP_INTERNAL_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_S2M_Q_RCRHP_INTERNAL_RING_ID_MASK 0xC0000000
#define UDMA_S2M_Q_RCRHP_INTERNAL_RING_ID_SHIFT 30

/**** comp_cfg register ****/
/* Enables writing to the completion ring. */
#define UDMA_S2M_Q_COMP_CFG_EN_COMP_RING_UPDATE (1 << 0)
/* Disables the completion coalescing function. */
#define UDMA_S2M_Q_COMP_CFG_DIS_COMP_COAL (1 << 1)
/* Reserved */
#define UDMA_S2M_Q_COMP_CFG_FIRST_PKT_PROMOTION (1 << 2)
/*
 * Buffer 2 location.
 * Determines the position of the buffer 2 length in the S2M completion
 * descriptor.
 * 0 - WORD 1 [31:16]
 * 1 - WORD 2 [31:16]
 */
#define UDMA_S2M_Q_COMP_CFG_BUF2_LEN_LOCATION (1 << 3)

/**** pkt_cfg register ****/
/* Header size. (bytes) */
#define UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_MASK 0x0000FFFF
#define UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_SHIFT 0
/* Force header split */
#define UDMA_S2M_Q_PKT_CFG_FORCE_HDR_SPLIT (1 << 16)
/* Enable header split. */
#define UDMA_S2M_Q_PKT_CFG_EN_HDR_SPLIT (1 << 17)

/**** qos_cfg register ****/
/* Queue QoS */
#define UDMA_S2M_QOS_CFG_Q_QOS_MASK 0x000000FF
#define UDMA_S2M_QOS_CFG_Q_QOS_SHIFT 0
/* Reset the tail pointer hardware. */
#define UDMA_S2M_Q_SW_CTRL_RST_TAIL_PTR (1 << 1)
/* Reset the head pointer hardware. */
#define UDMA_S2M_Q_SW_CTRL_RST_HEAD_PTR (1 << 2)
/* Reset the current pointer hardware. */
#define UDMA_S2M_Q_SW_CTRL_RST_CURRENT_PTR (1 << 3)
/* Reset the prefetch FIFO */
#define UDMA_S2M_Q_SW_CTRL_RST_PREFETCH (1 << 4)
/* Reset the queue */
#define UDMA_S2M_Q_SW_CTRL_RST_Q   (1 << 8)

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_UDMA_S2M_REG_H */

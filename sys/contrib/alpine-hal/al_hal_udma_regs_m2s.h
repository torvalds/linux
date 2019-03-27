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
 * @file   al_hal_udma_regs_m2s.h
 *
 * @brief C Header file for the UDMA M2S registers
 *
 */

#ifndef __AL_HAL_UDMA_M2S_REG_H
#define __AL_HAL_UDMA_M2S_REG_H

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct udma_axi_m2s {
	/* [0x0] Completion write master configuration */
	uint32_t comp_wr_cfg_1;
	/* [0x4] Completion write master configuration */
	uint32_t comp_wr_cfg_2;
	/* [0x8] Data read master configuration */
	uint32_t data_rd_cfg_1;
	/* [0xc] Data read master configuration */
	uint32_t data_rd_cfg_2;
	/* [0x10] Descriptor read master configuration */
	uint32_t desc_rd_cfg_1;
	/* [0x14] Descriptor read master configuration */
	uint32_t desc_rd_cfg_2;
	/* [0x18] Data read master configuration */
	uint32_t data_rd_cfg;
	/* [0x1c] Descriptors read master configuration */
	uint32_t desc_rd_cfg_3;
	/* [0x20] Descriptors write master configuration (completion) */
	uint32_t desc_wr_cfg_1;
	/* [0x24] AXI outstanding  configuration */
	uint32_t ostand_cfg;
	uint32_t rsrvd[54];
};
struct udma_m2s {
	/*
	 * [0x0] DMA state.
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
	 * [0xc] M2S DMA error log mask.
	 * Each error has an interrupt controller cause bit.
	 * This register determines if these errors cause the M2S DMA to log the
	 * error condition.
	 * 0 - Log is enabled.
	 * 1 - Log is masked.
	 */
	uint32_t err_log_mask;
	uint32_t rsrvd_1;
	/*
	 * [0x14] DMA header log.
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_0;
	/*
	 * [0x18] DMA header log.
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_1;
	/*
	 * [0x1c] DMA header log.
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_2;
	/*
	 * [0x20] DMA header log.
	 * Sample the packet header that caused the error.
	 */
	uint32_t log_3;
	/* [0x24] DMA clear error log */
	uint32_t clear_err_log;
	/* [0x28] M2S data FIFO status */
	uint32_t data_fifo_status;
	/* [0x2c] M2S header FIFO status */
	uint32_t header_fifo_status;
	/* [0x30] M2S unack FIFO status */
	uint32_t unack_fifo_status;
	/* [0x34] Select queue for debug */
	uint32_t indirect_ctrl;
	/*
	 * [0x38] M2S prefetch FIFO status.
	 * Status of the selected queue in M2S_indirect_ctrl
	 */
	uint32_t sel_pref_fifo_status;
	/*
	 * [0x3c] M2S completion FIFO status.
	 * Status of the selected queue in M2S_indirect_ctrl
	 */
	uint32_t sel_comp_fifo_status;
	/*
	 * [0x40] M2S rate limit status.
	 * Status of the selected queue in M2S_indirect_ctrl
	 */
	uint32_t sel_rate_limit_status;
	/*
	 * [0x44] M2S DWRR scheduler status.
	 * Status of the selected queue in M2S_indirect_ctrl
	 */
	uint32_t sel_dwrr_status;
	/* [0x48] M2S state machine and FIFO clear control */
	uint32_t clear_ctrl;
	/* [0x4c] Misc Check enable */
	uint32_t check_en;
	/* [0x50] M2S FIFO enable control, internal */
	uint32_t fifo_en;
	/* [0x54] M2S packet length configuration */
	uint32_t cfg_len;
	/* [0x58] Stream interface configuration */
	uint32_t stream_cfg;
	uint32_t rsrvd[41];
};
struct udma_m2s_rd {
	/* [0x0] M2S descriptor prefetch configuration */
	uint32_t desc_pref_cfg_1;
	/* [0x4] M2S descriptor prefetch configuration */
	uint32_t desc_pref_cfg_2;
	/* [0x8] M2S descriptor prefetch configuration */
	uint32_t desc_pref_cfg_3;
	uint32_t rsrvd_0;
	/* [0x10] Data burst read configuration */
	uint32_t data_cfg;
	uint32_t rsrvd[11];
};
struct udma_m2s_dwrr {
	/* [0x0] Tx DMA DWRR scheduler configuration */
	uint32_t cfg_sched;
	/* [0x4] Token bucket rate limit control */
	uint32_t ctrl_deficit_cnt;
	uint32_t rsrvd[14];
};
struct udma_m2s_rate_limiter {
	/* [0x0] Token bucket rate limit configuration */
	uint32_t gen_cfg;
	/*
	 * [0x4] Token bucket rate limit control.
	 * Controls the cycle counters.
	 */
	uint32_t ctrl_cycle_cnt;
	/*
	 * [0x8] Token bucket rate limit control.
	 * Controls the token bucket counter.
	 */
	uint32_t ctrl_token;
	uint32_t rsrvd[13];
};

struct udma_rlimit_common {
	/* [0x0] Token bucket configuration */
	uint32_t cfg_1s;
	/* [0x4] Token bucket rate limit configuration */
	uint32_t cfg_cycle;
	/* [0x8] Token bucket rate limit configuration */
	uint32_t cfg_token_size_1;
	/* [0xc] Token bucket rate limit configuration */
	uint32_t cfg_token_size_2;
	/* [0x10] Token bucket rate limit configuration */
	uint32_t sw_ctrl;
	/*
	 * [0x14] Mask the different types of rate limiter.
	 * 0 - Rate limit is active.
	 * 1 - Rate limit is masked.
	 */
	uint32_t mask;
};

struct udma_m2s_stream_rate_limiter {
	struct udma_rlimit_common rlimit;
	uint32_t rsrvd[10];
};
struct udma_m2s_comp {
	/* [0x0] Completion controller configuration */
	uint32_t cfg_1c;
	/* [0x4] Completion controller coalescing configuration */
	uint32_t cfg_coal;
	/* [0x8] Completion controller application acknowledge configuration */
	uint32_t cfg_application_ack;
	uint32_t rsrvd[61];
};
struct udma_m2s_stat {
	/* [0x0] Statistics counters configuration */
	uint32_t cfg_st;
	/* [0x4] Counting number of descriptors with First-bit set. */
	uint32_t tx_pkt;
	/*
	 * [0x8] Counting the net length of the data buffers [64-bit]
	 * Should be read before tx_bytes_high
	 */
	uint32_t tx_bytes_low;
	/*
	 * [0xc] Counting the net length of the data buffers [64-bit],
	 * Should be read after tx_bytes_low (value is sampled when reading
	 * Should be read before tx_bytes_low
	 */
	uint32_t tx_bytes_high;
	/* [0x10] Total number of descriptors read from the host memory */
	uint32_t prefed_desc;
	/* [0x14] Number of packets read from the unack FIFO */
	uint32_t comp_pkt;
	/* [0x18] Number of descriptors written into the completion ring */
	uint32_t comp_desc;
	/*
	 * [0x1c] Number of acknowledged packets.
	 * (acknowledge received from the stream interface)
	 */
	uint32_t ack_pkts;
	uint32_t rsrvd[56];
};
struct udma_m2s_feature {
	/*
	 * [0x0] M2S Feature register.
	 * M2S instantiation parameters
	 */
	uint32_t reg_1;
	/* [0x4] Reserved M2S feature register */
	uint32_t reg_2;
	/*
	 * [0x8] M2S Feature register.
	 * M2S instantiation parameters
	 */
	uint32_t reg_3;
	/*
	 * [0xc] M2S Feature register.
	 * M2S instantiation parameters
	 */
	uint32_t reg_4;
	/*
	 * [0x10] M2S Feature register.
	 * M2S instantiation parameters
	 */
	uint32_t reg_5;
	uint32_t rsrvd[59];
};
struct udma_m2s_q {
	uint32_t rsrvd_0[8];
	/* [0x20] M2S descriptor ring configuration */
	uint32_t cfg;
	/* [0x24] M2S descriptor ring status and information */
	uint32_t status;
	/* [0x28] TX Descriptor Ring Base Pointer [31:4] */
	uint32_t tdrbp_low;
	/* [0x2c] TX Descriptor Ring Base Pointer [63:32] */
	uint32_t tdrbp_high;
	/*
	 * [0x30] TX Descriptor Ring Length[23:2]
	 */
	uint32_t tdrl;
	/* [0x34] TX Descriptor Ring Head Pointer */
	uint32_t tdrhp;
	/* [0x38] Tx Descriptor Tail Pointer increment */
	uint32_t tdrtp_inc;
	/* [0x3c] Tx Descriptor Tail Pointer */
	uint32_t tdrtp;
	/* [0x40] TX Descriptor Current Pointer */
	uint32_t tdcp;
	/* [0x44] Tx Completion Ring Base Pointer [31:4] */
	uint32_t tcrbp_low;
	/* [0x48] TX Completion Ring Base Pointer [63:32] */
	uint32_t tcrbp_high;
	/* [0x4c] TX Completion Ring Head Pointer */
	uint32_t tcrhp;
	/*
	 * [0x50] Tx Completion Ring Head Pointer internal (Before the
	 * coalescing FIFO)
	 */
	uint32_t tcrhp_internal;
	uint32_t rsrvd_1[3];
	/* [0x60] Rate limit configuration */
	struct udma_rlimit_common rlimit;
	uint32_t rsrvd_2[2];
	/* [0x80] DWRR scheduler configuration */
	uint32_t dwrr_cfg_1;
	/* [0x84] DWRR scheduler configuration */
	uint32_t dwrr_cfg_2;
	/* [0x88] DWRR scheduler configuration */
	uint32_t dwrr_cfg_3;
	/* [0x8c] DWRR scheduler software control */
	uint32_t dwrr_sw_ctrl;
	uint32_t rsrvd_3[4];
	/* [0xa0] Completion controller configuration */
	uint32_t comp_cfg;
	uint32_t rsrvd_4[3];
	/* [0xb0] SW control  */
	uint32_t q_sw_ctrl;
	uint32_t rsrvd_5[3];
	/* [0xc0] Number of M2S Tx packets after the scheduler */
	uint32_t q_tx_pkt;
	uint32_t rsrvd[975];
};

struct udma_m2s_regs {
	uint32_t rsrvd_0[64];
	struct udma_axi_m2s axi_m2s;                     /* [0x100] */
	struct udma_m2s m2s;                             /* [0x200] */
	struct udma_m2s_rd m2s_rd;                       /* [0x300] */
	struct udma_m2s_dwrr m2s_dwrr;                   /* [0x340] */
	struct udma_m2s_rate_limiter m2s_rate_limiter;   /* [0x380] */
	struct udma_m2s_stream_rate_limiter m2s_stream_rate_limiter; /* [0x3c0] */
	struct udma_m2s_comp m2s_comp;                   /* [0x400] */
	struct udma_m2s_stat m2s_stat;                   /* [0x500] */
	struct udma_m2s_feature m2s_feature;             /* [0x600] */
	uint32_t rsrvd_1[576];
	struct udma_m2s_q m2s_q[4];                      /* [0x1000] */
};


/*
* Registers Fields
*/


/**** comp_wr_cfg_1 register ****/
/* AXI write  ID (AWID) */
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWID_MASK 0x000000FF
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWCACHE_MASK 0x000F0000
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWBURST_MASK 0x03000000
#define UDMA_AXI_M2S_COMP_WR_CFG_1_AWBURST_SHIFT 24

/**** comp_wr_cfg_2 register ****/
/* User extension */
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWUSER_MASK 0x000FFFFF
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWSIZE_MASK 0x00700000
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWSIZE_SHIFT 20
/*
 * AXI Master QoS.
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWQOS_MASK 0x07000000
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWPROT_MASK 0x70000000
#define UDMA_AXI_M2S_COMP_WR_CFG_2_AWPROT_SHIFT 28

/**** data_rd_cfg_1 register ****/
/* AXI read  ID (ARID) */
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARID_MASK 0x000000FF
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARCACHE_MASK 0x000F0000
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARBURST_MASK 0x03000000
#define UDMA_AXI_M2S_DATA_RD_CFG_1_ARBURST_SHIFT 24

/**** data_rd_cfg_2 register ****/
/* User extension */
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARUSER_MASK 0x000FFFFF
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARSIZE_MASK 0x00700000
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARSIZE_SHIFT 20
/*
 * AXI Master QoS.
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARQOS_MASK 0x07000000
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARPROT_MASK 0x70000000
#define UDMA_AXI_M2S_DATA_RD_CFG_2_ARPROT_SHIFT 28

/**** desc_rd_cfg_1 register ****/
/* AXI read  ID (ARID) */
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARID_MASK 0x000000FF
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARID_SHIFT 0
/* Cache Type */
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARCACHE_MASK 0x000F0000
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARCACHE_SHIFT 16
/* Burst type */
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARBURST_MASK 0x03000000
#define UDMA_AXI_M2S_DESC_RD_CFG_1_ARBURST_SHIFT 24

/**** desc_rd_cfg_2 register ****/
/* User extension */
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARUSER_MASK 0x000FFFFF
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARUSER_SHIFT 0
/* Bus size, 128-bit */
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARSIZE_MASK 0x00700000
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARSIZE_SHIFT 20
/*
 * AXI Master QoS
 * Used for arbitration between AXI masters
 */
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARQOS_MASK 0x07000000
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARQOS_SHIFT 24
/* Protection Type */
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARPROT_MASK 0x70000000
#define UDMA_AXI_M2S_DESC_RD_CFG_2_ARPROT_SHIFT 28

/**** data_rd_cfg register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst.
 * This value is used for a burst split decision.
 */
#define UDMA_AXI_M2S_DATA_RD_CFG_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_M2S_DATA_RD_CFG_MAX_AXI_BEATS_SHIFT 0
/*
 * Enable breaking data read request.
 * Aligned to max_AXI_beats when the total read size is less than max_AXI_beats
 */
#define UDMA_AXI_M2S_DATA_RD_CFG_ALWAYS_BREAK_ON_MAX_BOUDRY (1 << 16)

/**** desc_rd_cfg_3 register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst.
 * This value is used for a burst split decision.
 * Maximum burst size for reading data( in AXI beats, 128-bits)
 * (default – 16 beats, 256 bytes)
 */
#define UDMA_AXI_M2S_DESC_RD_CFG_3_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_M2S_DESC_RD_CFG_3_MAX_AXI_BEATS_SHIFT 0
/*
 * Enable breaking descriptor read request.
 * Aligned to max_AXI_beats when the total read size is less than max_AXI_beats.
 */
#define UDMA_AXI_M2S_DESC_RD_CFG_3_ALWAYS_BREAK_ON_MAX_BOUDRY (1 << 16)

/**** desc_wr_cfg_1 register ****/
/*
 * Defines the maximum number of AXI beats for a single AXI burst.
 * This value is used for a burst split decision.
 */
#define UDMA_AXI_M2S_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK 0x000000FF
#define UDMA_AXI_M2S_DESC_WR_CFG_1_MAX_AXI_BEATS_SHIFT 0
/*
 * Minimum burst for writing completion descriptors.
 * Defined in AXI beats
 * 4 Descriptors per beat.
 * Value must be aligned to cache lines (64 bytes).
 * Default value is 2 cache lines, 32 descriptors, 8 beats.
 */
#define UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK 0x00FF0000
#define UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS_SHIFT 16

/**** ostand_cfg register ****/
/* Maximum number of outstanding data reads to the AXI (AXI transactions) */
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_DATA_RD_MASK 0x0000003F
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_DATA_RD_SHIFT 0
/*
 * Maximum number of outstanding descriptor reads to the AXI (AXI transactions)
 */
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_DESC_RD_MASK 0x00003F00
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_DESC_RD_SHIFT 8
/*
 * Maximum number of outstanding descriptor writes to the AXI (AXI transactions)
 */
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_REQ_MASK 0x003F0000
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_REQ_SHIFT 16
/*
 * Maximum number of outstanding data beats for descriptor write to AXI (AXI
 * beats)
 */
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_DATA_WR_MASK 0xFF000000
#define UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_DATA_WR_SHIFT 24

/**** state register ****/
/* Completion control */
#define UDMA_M2S_STATE_COMP_CTRL_MASK 0x00000003
#define UDMA_M2S_STATE_COMP_CTRL_SHIFT 0
/* Stream interface */
#define UDMA_M2S_STATE_STREAM_IF_MASK 0x00000030
#define UDMA_M2S_STATE_STREAM_IF_SHIFT 4
/* Data read control */
#define UDMA_M2S_STATE_DATA_RD_CTRL_MASK 0x00000300
#define UDMA_M2S_STATE_DATA_RD_CTRL_SHIFT 8
/* Descriptor prefetch */
#define UDMA_M2S_STATE_DESC_PREF_MASK 0x00003000
#define UDMA_M2S_STATE_DESC_PREF_SHIFT 12

/**** change_state register ****/
/* Start normal operation */
#define UDMA_M2S_CHANGE_STATE_NORMAL (1 << 0)
/* Stop normal operation */
#define UDMA_M2S_CHANGE_STATE_DIS    (1 << 1)
/*
 * Stop all machines.
 * (Prefetch, scheduling, completion and stream interface)
 */
#define UDMA_M2S_CHANGE_STATE_ABORT  (1 << 2)

/**** err_log_mask register ****/
/*
 * Mismatch of packet serial number.
 * (between first packet in the unacknowledged FIFO and received ack from the
 * stream)
 */
#define UDMA_M2S_ERR_LOG_MASK_COMP_PKT_MISMATCH (1 << 0)
/* Parity error */
#define UDMA_M2S_ERR_LOG_MASK_STREAM_AXI_PARITY (1 << 1)
/* AXI response error */
#define UDMA_M2S_ERR_LOG_MASK_STREAM_AXI_RESPONSE (1 << 2)
/* AXI timeout (ack not received) */
#define UDMA_M2S_ERR_LOG_MASK_STREAM_AXI_TOUT (1 << 3)
/* Parity error */
#define UDMA_M2S_ERR_LOG_MASK_COMP_AXI_PARITY (1 << 4)
/* AXI response error */
#define UDMA_M2S_ERR_LOG_MASK_COMP_AXI_RESPONSE (1 << 5)
/* AXI timeout */
#define UDMA_M2S_ERR_LOG_MASK_COMP_AXI_TOUT (1 << 6)
/* Parity error */
#define UDMA_M2S_ERR_LOG_MASK_DATA_AXI_PARITY (1 << 7)
/* AXI response error */
#define UDMA_M2S_ERR_LOG_MASK_DATA_AXI_RESPONSE (1 << 8)
/* AXI timeout */
#define UDMA_M2S_ERR_LOG_MASK_DATA_AXI_TOUT (1 << 9)
/* Parity error */
#define UDMA_M2S_ERR_LOG_MASK_PREF_AXI_PARITY (1 << 10)
/* AXI response error */
#define UDMA_M2S_ERR_LOG_MASK_PREF_AXI_RESPONSE (1 << 11)
/* AXI timeout */
#define UDMA_M2S_ERR_LOG_MASK_PREF_AXI_TOUT (1 << 12)
/* Packet length error */
#define UDMA_M2S_ERR_LOG_MASK_PREF_PKT_LEN_OVERFLOW (1 << 13)
/* Maximum number of descriptors per packet error */
#define UDMA_M2S_ERR_LOG_MASK_PREF_MAX_DESC_CNT (1 << 14)
/* Error in first bit indication of the descriptor */
#define UDMA_M2S_ERR_LOG_MASK_PREF_FIRST (1 << 15)
/* Error in last bit indication of the descriptor */
#define UDMA_M2S_ERR_LOG_MASK_PREF_LAST (1 << 16)
/* Ring_ID error */
#define UDMA_M2S_ERR_LOG_MASK_PREF_RING_ID (1 << 17)
/* Data buffer parity error */
#define UDMA_M2S_ERR_LOG_MASK_DATA_BUFF_PARITY (1 << 18)
/* Internal error */
#define UDMA_M2S_ERR_LOG_MASK_INTERNAL_MASK 0xFFF80000
#define UDMA_M2S_ERR_LOG_MASK_INTERNAL_SHIFT 19

/**** clear_err_log register ****/
/* Clear error log */
#define UDMA_M2S_CLEAR_ERR_LOG_CLEAR (1 << 0)

/**** data_fifo_status register ****/
/* FIFO used indication */
#define UDMA_M2S_DATA_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_M2S_DATA_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_M2S_DATA_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_M2S_DATA_FIFO_STATUS_FULL (1 << 28)

/**** header_fifo_status register ****/
/* FIFO used indication */
#define UDMA_M2S_HEADER_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_M2S_HEADER_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_M2S_HEADER_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_M2S_HEADER_FIFO_STATUS_FULL (1 << 28)

/**** unack_fifo_status register ****/
/* FIFO used indication */
#define UDMA_M2S_UNACK_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_M2S_UNACK_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_M2S_UNACK_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_M2S_UNACK_FIFO_STATUS_FULL (1 << 28)

/**** indirect_ctrl register ****/
/* Selected queue for status read */
#define UDMA_M2S_INDIRECT_CTRL_Q_NUM_MASK 0x00000FFF
#define UDMA_M2S_INDIRECT_CTRL_Q_NUM_SHIFT 0

/**** sel_pref_fifo_status register ****/
/* FIFO used indication */
#define UDMA_M2S_SEL_PREF_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_M2S_SEL_PREF_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_M2S_SEL_PREF_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_M2S_SEL_PREF_FIFO_STATUS_FULL (1 << 28)

/**** sel_comp_fifo_status register ****/
/* FIFO used indication */
#define UDMA_M2S_SEL_COMP_FIFO_STATUS_USED_MASK 0x0000FFFF
#define UDMA_M2S_SEL_COMP_FIFO_STATUS_USED_SHIFT 0
/* FIFO empty indication */
#define UDMA_M2S_SEL_COMP_FIFO_STATUS_EMPTY (1 << 24)
/* FIFO full indication */
#define UDMA_M2S_SEL_COMP_FIFO_STATUS_FULL (1 << 28)

/**** sel_rate_limit_status register ****/
/* Token counter */
#define UDMA_M2S_SEL_RATE_LIMIT_STATUS_TOKEN_CNT_MASK 0x00FFFFFF
#define UDMA_M2S_SEL_RATE_LIMIT_STATUS_TOKEN_CNT_SHIFT 0

/**** sel_dwrr_status register ****/
/* Deficit counter */
#define UDMA_M2S_SEL_DWRR_STATUS_DEFICIT_CNT_MASK 0x00FFFFFF
#define UDMA_M2S_SEL_DWRR_STATUS_DEFICIT_CNT_SHIFT 0

/**** cfg_len register ****/
/* Maximum packet size for the M2S */
#define UDMA_M2S_CFG_LEN_MAX_PKT_SIZE_MASK 0x000FFFFF
#define UDMA_M2S_CFG_LEN_MAX_PKT_SIZE_SHIFT 0
/*
 * Length encoding for 64K.
 * 0 - length 0x0000 = 0
 * 1 - length 0x0000 = 64k
 */
#define UDMA_M2S_CFG_LEN_ENCODE_64K  (1 << 24)

/**** stream_cfg register ****/
/*
 * Disables the stream interface operation.
 * Changing to 1 stops at the end of packet transmission.
 */
#define UDMA_M2S_STREAM_CFG_DISABLE  (1 << 0)
/*
 * Configuration of the stream FIFO read control.
 * 0 - Cut through
 * 1 - Threshold based
 */
#define UDMA_M2S_STREAM_CFG_RD_MODE  (1 << 1)
/* Minimum number of beats to start packet transmission. */
#define UDMA_M2S_STREAM_CFG_RD_TH_MASK 0x0003FF00
#define UDMA_M2S_STREAM_CFG_RD_TH_SHIFT 8

/**** desc_pref_cfg_1 register ****/
/* Size of the descriptor prefetch FIFO (in descriptors) */
#define UDMA_M2S_RD_DESC_PREF_CFG_1_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_M2S_RD_DESC_PREF_CFG_1_FIFO_DEPTH_SHIFT 0

/**** desc_pref_cfg_2 register ****/
/* Maximum number of descriptors per packet */
#define UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK 0x0000001F
#define UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_SHIFT 0
/*
 * Force RR arbitration in the prefetch arbiter.
 * 0 -Standard arbitration based on queue QoS
 * 1 - Force Round Robin arbitration
 */
#define UDMA_M2S_RD_DESC_PREF_CFG_2_PREF_FORCE_RR (1 << 16)

/**** desc_pref_cfg_3 register ****/
/*
 * Minimum descriptor burst size when prefetch FIFO level is below the
 * descriptor prefetch threshold
 * (must be 1)
 */
#define UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK 0x0000000F
#define UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_SHIFT 0
/*
 * Minimum descriptor burst size when prefetch FIFO level is above the
 * descriptor prefetch threshold
 */
#define UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK 0x000000F0
#define UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT 4
/*
 * Descriptor fetch threshold.
 * Used as a threshold to determine the allowed minimum descriptor burst size.
 * (Must be at least max_desc_per_pkt)
 */
#define UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_MASK 0x0000FF00
#define UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT 8

/**** data_cfg register ****/
/*
 * Maximum number of data beats in the data read FIFO.
 * Defined based on data FIFO size
 * (default FIFO size 2KB → 128 beats)
 */
#define UDMA_M2S_RD_DATA_CFG_DATA_FIFO_DEPTH_MASK 0x000003FF
#define UDMA_M2S_RD_DATA_CFG_DATA_FIFO_DEPTH_SHIFT 0
/*
 * Maximum number of packets in the data read FIFO.
 * Defined based on header FIFO size
 */
#define UDMA_M2S_RD_DATA_CFG_MAX_PKT_LIMIT_MASK 0x00FF0000
#define UDMA_M2S_RD_DATA_CFG_MAX_PKT_LIMIT_SHIFT 16

/**** cfg_sched register ****/
/*
 * Enable the DWRR scheduler.
 * If this bit is 0, queues with same QoS will be served with RR scheduler.
 */
#define UDMA_M2S_DWRR_CFG_SCHED_EN_DWRR (1 << 0)
/*
 * Scheduler operation mode.
 * 0 - Byte mode
 * 1 - Packet mode
 */
#define UDMA_M2S_DWRR_CFG_SCHED_PKT_MODE_EN (1 << 4)
/*
 * Enable incrementing the weight factor between DWRR iterations.
 * 00 - Don't increase the increment factor.
 * 01 - Increment once
 * 10 - Increment exponential
 * 11 - Reserved
 */
#define UDMA_M2S_DWRR_CFG_SCHED_WEIGHT_INC_MASK 0x00000300
#define UDMA_M2S_DWRR_CFG_SCHED_WEIGHT_INC_SHIFT 8
/*
 * Increment factor power of 2.
 * 7 --> 128 bytes
 * This is the factor used to multiply the weight.
 */
#define UDMA_M2S_DWRR_CFG_SCHED_INC_FACTOR_MASK 0x000F0000
#define UDMA_M2S_DWRR_CFG_SCHED_INC_FACTOR_SHIFT 16

/**** ctrl_deficit_cnt register ****/
/*
 * Init value for the deficit counter.
 * Initializes the deficit counters of all queues to this value any time this
 * register is written.
 */
#define UDMA_M2S_DWRR_CTRL_DEFICIT_CNT_INIT_MASK 0x00FFFFFF
#define UDMA_M2S_DWRR_CTRL_DEFICIT_CNT_INIT_SHIFT 0

/**** gen_cfg register ****/
/* Size of the basic token fill cycle, system clock cycles */
#define UDMA_M2S_RATE_LIMITER_GEN_CFG_SHORT_CYCLE_SIZE_MASK 0x0000FFFF
#define UDMA_M2S_RATE_LIMITER_GEN_CFG_SHORT_CYCLE_SIZE_SHIFT 0
/*
 * Rate limiter operation mode.
 * 0 - Byte mode
 * 1 - Packet mode
 */
#define UDMA_M2S_RATE_LIMITER_GEN_CFG_PKT_MODE_EN (1 << 24)

/**** ctrl_cycle_cnt register ****/
/* Reset the short and long cycle counters. */
#define UDMA_M2S_RATE_LIMITER_CTRL_CYCLE_CNT_RST (1 << 0)

/**** ctrl_token register ****/
/*
 * Init value for the token counter.
 * Initializes the token counters of all queues to this value any time this
 * register is written.
 */
#define UDMA_M2S_RATE_LIMITER_CTRL_TOKEN_RST_MASK 0x00FFFFFF
#define UDMA_M2S_RATE_LIMITER_CTRL_TOKEN_RST_SHIFT 0

/**** cfg_1s register ****/
/* Maximum number of accumulated bytes in the token counter */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_MAX_BURST_SIZE_MASK 0x00FFFFFF
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_MAX_BURST_SIZE_SHIFT 0
/* Enable the rate limiter. */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_EN (1 << 24)
/* Stop token fill. */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_PAUSE (1 << 25)

/**** cfg_cycle register ****/
/* Number of short cycles between token fills */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_CYCLE_LONG_CYCLE_SIZE_MASK 0x0000FFFF
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_CYCLE_LONG_CYCLE_SIZE_SHIFT 0

/**** cfg_token_size_1 register ****/
/* Number of bits to add in each long cycle */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_1_LONG_CYCLE_MASK 0x0007FFFF
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_1_LONG_CYCLE_SHIFT 0

/**** cfg_token_size_2 register ****/
/* Number of bits to add in each short cycle */
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_2_SHORT_CYCLE_MASK 0x0007FFFF
#define UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_2_SHORT_CYCLE_SHIFT 0

/**** sw_ctrl register ****/
/* Reset the token bucket counter. */
#define UDMA_M2S_STREAM_RATE_LIMITER_SW_CTRL_RST_TOKEN_CNT (1 << 0)

/**** mask register ****/
/* Mask the external rate limiter. */
#define UDMA_M2S_STREAM_RATE_LIMITER_MASK_EXTERNAL_RATE_LIMITER (1 << 0)
/* Mask the internal rate limiter. */
#define UDMA_M2S_STREAM_RATE_LIMITER_MASK_INTERNAL_RATE_LIMITER (1 << 1)
/* Mask the external application pause interface. */
#define UDMA_M2S_STREAM_RATE_LIMITER_MASK_EXTERNAL_PAUSE (1 << 3)

/**** cfg_1c register ****/
/*
 * Completion FIFO size
 *  (descriptors per queue)
 */
#define UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_SHIFT 0
/*
 * Unacknowledged FIFO size.
 * (descriptors)
 */
#define UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_MASK 0x0001FF00
#define UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_SHIFT 8
/*
 * Enable promotion.
 * Enable the promotion of the current queue in progress for the completion
 * write scheduler.
 */
#define UDMA_M2S_COMP_CFG_1C_Q_PROMOTION (1 << 24)
/* Force RR arbitration in the completion arbiter */
#define UDMA_M2S_COMP_CFG_1C_FORCE_RR (1 << 25)
/* Minimum number of free completion entries to qualify for promotion */
#define UDMA_M2S_COMP_CFG_1C_Q_FREE_MIN_MASK 0xF0000000
#define UDMA_M2S_COMP_CFG_1C_Q_FREE_MIN_SHIFT 28

/**** cfg_application_ack register ****/
/*
 * Acknowledge timeout timer.
 * ACK from the application through the stream interface)
 */
#define UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_MASK 0x00FFFFFF
#define UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_SHIFT 0

/**** cfg_st register ****/
/* Use additional length value for all statistics counters. */
#define UDMA_M2S_STAT_CFG_ST_USE_EXTRA_LEN (1 << 0)

/**** reg_1 register ****/
/*
 * Read the size of the descriptor prefetch FIFO
 * (descriptors).
 */
#define UDMA_M2S_FEATURE_REG_1_DESC_PREFERCH_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_M2S_FEATURE_REG_1_DESC_PREFERCH_FIFO_DEPTH_SHIFT 0

/**** reg_3 register ****/
/*
 * Maximum number of data beats in the data read FIFO.
 * Defined based on data FIFO size
 * (default FIFO size 2KB → 128 beats)
 */
#define UDMA_M2S_FEATURE_REG_3_DATA_FIFO_DEPTH_MASK 0x000003FF
#define UDMA_M2S_FEATURE_REG_3_DATA_FIFO_DEPTH_SHIFT 0
/*
 * Maximum number of packets in the data read FIFO.
 * Defined based on header FIFO size
 */
#define UDMA_M2S_FEATURE_REG_3_DATA_RD_MAX_PKT_LIMIT_MASK 0x00FF0000
#define UDMA_M2S_FEATURE_REG_3_DATA_RD_MAX_PKT_LIMIT_SHIFT 16

/**** reg_4 register ****/
/*
 * Size of the completion FIFO of each queue
 * (words)
 */
#define UDMA_M2S_FEATURE_REG_4_COMP_FIFO_DEPTH_MASK 0x000000FF
#define UDMA_M2S_FEATURE_REG_4_COMP_FIFO_DEPTH_SHIFT 0
/* Size of the unacknowledged FIFO (descriptors) */
#define UDMA_M2S_FEATURE_REG_4_COMP_UNACK_FIFO_DEPTH_MASK 0x0001FF00
#define UDMA_M2S_FEATURE_REG_4_COMP_UNACK_FIFO_DEPTH_SHIFT 8

/**** reg_5 register ****/
/* Maximum number of outstanding data reads to AXI */
#define UDMA_M2S_FEATURE_REG_5_MAX_DATA_RD_OSTAND_MASK 0x0000003F
#define UDMA_M2S_FEATURE_REG_5_MAX_DATA_RD_OSTAND_SHIFT 0
/* Maximum number of outstanding descriptor reads to AXI */
#define UDMA_M2S_FEATURE_REG_5_MAX_DESC_RD_OSTAND_MASK 0x00003F00
#define UDMA_M2S_FEATURE_REG_5_MAX_DESC_RD_OSTAND_SHIFT 8
/*
 * Maximum number of outstanding descriptor writes to AXI.
 * (AXI transactions)
 */
#define UDMA_M2S_FEATURE_REG_5_MAX_COMP_REQ_MASK 0x003F0000
#define UDMA_M2S_FEATURE_REG_5_MAX_COMP_REQ_SHIFT 16
/*
 * Maximum number of outstanding data beats for descriptor write to AXI.
 * (AXI beats)
 */
#define UDMA_M2S_FEATURE_REG_5_MAX_COMP_DATA_WR_OSTAND_MASK 0xFF000000
#define UDMA_M2S_FEATURE_REG_5_MAX_COMP_DATA_WR_OSTAND_SHIFT 24

/**** cfg register ****/
/*
 * Length offset to be used for each packet from this queue.
 * (length offset is used for the scheduler and rate limiter).
 */
#define UDMA_M2S_Q_CFG_PKT_LEN_OFFSET_MASK 0x0000FFFF
#define UDMA_M2S_Q_CFG_PKT_LEN_OFFSET_SHIFT 0
/*
 * Enable operation of this queue.
 * Start prefetch.
 */
#define UDMA_M2S_Q_CFG_EN_PREF       (1 << 16)
/*
 * Enable operation of this queue.
 * Start scheduling.
 */
#define UDMA_M2S_Q_CFG_EN_SCHEDULING (1 << 17)
/* Allow prefetch of less than minimum prefetch burst size. */
#define UDMA_M2S_Q_CFG_ALLOW_LT_MIN_PREF (1 << 20)
/* Configure the AXI AWCACHE for completion write.  */
#define UDMA_M2S_Q_CFG_AXI_AWCACHE_COMP_MASK 0x0F000000
#define UDMA_M2S_Q_CFG_AXI_AWCACHE_COMP_SHIFT 24
/*
 * AXI QoS for the selected queue.
 * This value is used in AXI transactions associated with this queue and the
 * prefetch and completion arbiters.
 */
#define UDMA_M2S_Q_CFG_AXI_QOS_MASK  0x70000000
#define UDMA_M2S_Q_CFG_AXI_QOS_SHIFT 28

/**** status register ****/
/* Indicates how many entries are used in the queue */
#define UDMA_M2S_Q_STATUS_Q_USED_MASK 0x01FFFFFF
#define UDMA_M2S_Q_STATUS_Q_USED_SHIFT 0
/*
 * prefetch status
 * 0 – prefetch operation is stopped
 * 1 – prefetch is operational
 */
#define UDMA_M2S_Q_STATUS_PREFETCH   (1 << 28)
/*
 * Queue scheduler status
 * 0 – queue is not active and not participating in scheduling
 * 1 – queue is active and participating in the scheduling process
 */
#define UDMA_M2S_Q_STATUS_SCHEDULER  (1 << 29)
/* Queue is suspended due to DMB */
#define UDMA_M2S_Q_STATUS_Q_DMB      (1 << 30)
/*
 * Queue full indication.
 * (used by the host when head pointer equals tail pointer).
 */
#define UDMA_M2S_Q_STATUS_Q_FULL     (1 << 31)
/*
 * M2S Descriptor Ring Base address [31:4].
 * Value of the base address of the M2S descriptor ring
 * [3:0] - 0 - 16B alignment is enforced
 * ([11:4] should be 0 for 4KB alignment)
 */
#define UDMA_M2S_Q_TDRBP_LOW_ADDR_MASK 0xFFFFFFF0
#define UDMA_M2S_Q_TDRBP_LOW_ADDR_SHIFT 4

/**** TDRL register ****/
/*
 * Length of the descriptor ring.
 * (descriptors)
 * Associated with the ring base address, ends at maximum burst size alignment.
 */
#define UDMA_M2S_Q_TDRL_OFFSET_MASK  0x00FFFFFF
#define UDMA_M2S_Q_TDRL_OFFSET_SHIFT 0

/**** TDRHP register ****/
/*
 * Relative offset of the next descriptor that needs to be read into the
 * prefetch FIFO.
 * Incremented when the DMA reads valid descriptors from the host memory to the
 * prefetch FIFO.
 * Note that this is the offset in # of descriptors and not in byte address.
 */
#define UDMA_M2S_Q_TDRHP_OFFSET_MASK 0x00FFFFFF
#define UDMA_M2S_Q_TDRHP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_M2S_Q_TDRHP_RING_ID_MASK 0xC0000000
#define UDMA_M2S_Q_TDRHP_RING_ID_SHIFT 30

/**** TDRTP_inc register ****/
/* Increments the value in Q_TDRTP (descriptors) */
#define UDMA_M2S_Q_TDRTP_INC_VAL_MASK 0x00FFFFFF
#define UDMA_M2S_Q_TDRTP_INC_VAL_SHIFT 0

/**** TDRTP register ****/
/*
 * Relative offset of the next free descriptor in the host memory.
 * Note that this is the offset in # of descriptors and not in byte address.
 */
#define UDMA_M2S_Q_TDRTP_OFFSET_MASK 0x00FFFFFF
#define UDMA_M2S_Q_TDRTP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_M2S_Q_TDRTP_RING_ID_MASK 0xC0000000
#define UDMA_M2S_Q_TDRTP_RING_ID_SHIFT 30

/**** TDCP register ****/
/*
 * Relative offset of the first descriptor in the prefetch FIFO.
 * This is the next descriptor that will be read by the scheduler.
 */
#define UDMA_M2S_Q_TDCP_OFFSET_MASK  0x00FFFFFF
#define UDMA_M2S_Q_TDCP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_M2S_Q_TDCP_RING_ID_MASK 0xC0000000
#define UDMA_M2S_Q_TDCP_RING_ID_SHIFT 30
/*
 * M2S Descriptor Ring Base address [31:4].
 * Value of the base address of the M2S descriptor ring
 * [3:0] - 0 - 16B alignment is enforced
 * ([11:4] should be 0 for 4KB alignment)
 * NOTE:
 * Length of the descriptor ring (in descriptors) associated with the ring base
 * address. Ends at maximum burst size alignment.
 */
#define UDMA_M2S_Q_TCRBP_LOW_ADDR_MASK 0xFFFFFFF0
#define UDMA_M2S_Q_TCRBP_LOW_ADDR_SHIFT 4

/**** TCRHP register ****/
/*
 * Relative offset of the next descriptor that needs to be updated by the
 * completion controller.
 * Note: This is in descriptors and not in byte address.
 */
#define UDMA_M2S_Q_TCRHP_OFFSET_MASK 0x00FFFFFF
#define UDMA_M2S_Q_TCRHP_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_M2S_Q_TCRHP_RING_ID_MASK 0xC0000000
#define UDMA_M2S_Q_TCRHP_RING_ID_SHIFT 30

/**** TCRHP_internal register ****/
/*
 * Relative offset of the next descriptor that needs to be updated by the
 * completion controller.
 * Note: This is in descriptors and not in byte address.
 */
#define UDMA_M2S_Q_TCRHP_INTERNAL_OFFSET_MASK 0x00FFFFFF
#define UDMA_M2S_Q_TCRHP_INTERNAL_OFFSET_SHIFT 0
/* Ring ID */
#define UDMA_M2S_Q_TCRHP_INTERNAL_RING_ID_MASK 0xC0000000
#define UDMA_M2S_Q_TCRHP_INTERNAL_RING_ID_SHIFT 30

/**** rate_limit_cfg_1 register ****/
/* Maximum number of accumulated bytes in the token counter. */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_1_MAX_BURST_SIZE_MASK 0x00FFFFFF
#define UDMA_M2S_Q_RATE_LIMIT_CFG_1_MAX_BURST_SIZE_SHIFT 0
/* Enable the rate limiter. */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_1_EN (1 << 24)
/* Stop token fill. */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_1_PAUSE (1 << 25)

/**** rate_limit_cfg_cycle register ****/
/* Number of short cycles between token fills */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_CYCLE_LONG_CYCLE_SIZE_MASK 0x0000FFFF
#define UDMA_M2S_Q_RATE_LIMIT_CFG_CYCLE_LONG_CYCLE_SIZE_SHIFT 0

/**** rate_limit_cfg_token_size_1 register ****/
/* Number of bits to add in each long cycle */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_TOKEN_SIZE_1_LONG_CYCLE_MASK 0x0007FFFF
#define UDMA_M2S_Q_RATE_LIMIT_CFG_TOKEN_SIZE_1_LONG_CYCLE_SHIFT 0

/**** rate_limit_cfg_token_size_2 register ****/
/* Number of bits to add in each cycle */
#define UDMA_M2S_Q_RATE_LIMIT_CFG_TOKEN_SIZE_2_SHORT_CYCLE_MASK 0x0007FFFF
#define UDMA_M2S_Q_RATE_LIMIT_CFG_TOKEN_SIZE_2_SHORT_CYCLE_SHIFT 0

/**** rate_limit_sw_ctrl register ****/
/* Reset the token bucket counter. */
#define UDMA_M2S_Q_RATE_LIMIT_SW_CTRL_RST_TOKEN_CNT (1 << 0)

/**** rate_limit_mask register ****/
/* Mask the external rate limiter. */
#define UDMA_M2S_Q_RATE_LIMIT_MASK_EXTERNAL_RATE_LIMITER (1 << 0)
/* Mask the internal rate limiter. */
#define UDMA_M2S_Q_RATE_LIMIT_MASK_INTERNAL_RATE_LIMITER (1 << 1)
/*
 * Mask the internal pause mechanism for DMB.
 * (Data Memory Barrier).
 */
#define UDMA_M2S_Q_RATE_LIMIT_MASK_INTERNAL_PAUSE_DMB (1 << 2)
/* Mask the external application pause interface. */
#define UDMA_M2S_Q_RATE_LIMIT_MASK_EXTERNAL_PAUSE (1 << 3)

/**** dwrr_cfg_1 register ****/
/* Maximum number of accumulated bytes in the deficit counter */
#define UDMA_M2S_Q_DWRR_CFG_1_MAX_DEFICIT_CNT_SIZE_MASK 0x00FFFFFF
#define UDMA_M2S_Q_DWRR_CFG_1_MAX_DEFICIT_CNT_SIZE_SHIFT 0
/* Bypass the DWRR.  */
#define UDMA_M2S_Q_DWRR_CFG_1_STRICT (1 << 24)
/* Stop deficit counter increment. */
#define UDMA_M2S_Q_DWRR_CFG_1_PAUSE  (1 << 25)

/**** dwrr_cfg_2 register ****/
/*
 * Value for the queue QoS.
 * Queues with the same QoS value are scheduled with RR/DWRR.
 * Only LOG(number of queues) is used.
 */
#define UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_MASK 0x000000FF
#define UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_SHIFT 0

/**** dwrr_cfg_3 register ****/
/* Queue weight */
#define UDMA_M2S_Q_DWRR_CFG_3_WEIGHT_MASK 0x000000FF
#define UDMA_M2S_Q_DWRR_CFG_3_WEIGHT_SHIFT 0

/**** dwrr_sw_ctrl register ****/
/* Reset the DWRR deficit counter. */
#define UDMA_M2S_Q_DWRR_SW_CTRL_RST_CNT (1 << 0)

/**** comp_cfg register ****/
/* Enable writing to the completion ring */
#define UDMA_M2S_Q_COMP_CFG_EN_COMP_RING_UPDATE (1 << 0)
/* Disable the completion coalescing function. */
#define UDMA_M2S_Q_COMP_CFG_DIS_COMP_COAL (1 << 1)

/**** q_sw_ctrl register ****/
/*
 * Reset the DMB hardware barrier
 * (enable queue operation).
 */
#define UDMA_M2S_Q_SW_CTRL_RST_DMB (1 << 0)
/* Reset the tail pointer hardware. */
#define UDMA_M2S_Q_SW_CTRL_RST_TAIL_PTR (1 << 1)
/* Reset the head pointer hardware. */
#define UDMA_M2S_Q_SW_CTRL_RST_HEAD_PTR (1 << 2)
/* Reset the current pointer hardware. */
#define UDMA_M2S_Q_SW_CTRL_RST_CURRENT_PTR (1 << 3)
/* Reset the queue */
#define UDMA_M2S_Q_SW_CTRL_RST_Q   (1 << 8)

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_UDMA_M2S_REG_H */

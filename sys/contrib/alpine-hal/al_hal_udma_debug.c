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
 * @file   al_hal_udma_debug.c
 *
 * @brief  Universal DMA HAL driver for debug
 *
 */

#define DEBUG

#include <al_hal_common.h>
#include <al_hal_udma_regs.h>
#include <al_hal_udma_debug.h>

static void al_udma_regs_m2s_axi_print(struct al_udma *udma)
{
	al_dbg("M2S AXI regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, comp_wr_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, comp_wr_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, data_rd_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, data_rd_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, desc_rd_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, desc_rd_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, data_rd_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, desc_rd_cfg_3);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, desc_wr_cfg_1);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, axi_m2s,
			desc_wr_cfg_1,
			max_axi_beats,
			UDMA_AXI_M2S_DESC_WR_CFG_1_MAX_AXI_BEATS);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, axi_m2s,
			desc_wr_cfg_1,
			min_axi_beats,
			UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, axi_m2s, ostand_cfg);
}

static void al_udma_regs_m2s_general_print(struct al_udma *udma)
{
	al_dbg("M2S general regs:\n");

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, state);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s, state,
			comp_ctrl,
			UDMA_M2S_STATE_COMP_CTRL);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s, state,
			stream_if,
			UDMA_M2S_STATE_STREAM_IF);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s, state,
			rd_ctrl,
			UDMA_M2S_STATE_DATA_RD_CTRL);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s, state,
			desc_pref,
			UDMA_M2S_STATE_DESC_PREF);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, err_log_mask);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, log_0);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, log_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, log_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, log_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, data_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, header_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, unack_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, check_en);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, fifo_en);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, cfg_len);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, stream_cfg);
}

static void al_udma_regs_m2s_rd_print(struct al_udma *udma)
{
	al_dbg("M2S read regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_rd, desc_pref_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_rd, desc_pref_cfg_2);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_rd, desc_pref_cfg_3);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_rd,
			desc_pref_cfg_3,
			min_burst_below_thr,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_rd,
			desc_pref_cfg_3,
			min_burst_above_thr,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_rd,
			desc_pref_cfg_3,
			pref_thr,
			UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_rd, data_cfg);
}

static void al_udma_regs_m2s_dwrr_print(struct al_udma *udma)
{
	al_dbg("M2S DWRR regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_dwrr, cfg_sched);
}

static void al_udma_regs_m2s_rate_limiter_print(struct al_udma *udma)
{
	al_dbg("M2S rate limiter regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_rate_limiter, gen_cfg);
}

static void al_udma_regs_m2s_stream_rate_limiter_print(struct al_udma *udma)
{
	al_dbg("M2S stream rate limiter regs:\n");

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stream_rate_limiter,
			rlimit.cfg_1s);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stream_rate_limiter,
			rlimit.cfg_cycle);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stream_rate_limiter,
			rlimit.cfg_token_size_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stream_rate_limiter,
			rlimit.cfg_token_size_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stream_rate_limiter,
			rlimit.mask);
}

static void al_udma_regs_m2s_comp_print(struct al_udma *udma)
{
	al_dbg("M2S completion regs:\n");

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_comp, cfg_1c);

	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_comp, cfg_1c,
			comp_fifo_depth,
			UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_comp, cfg_1c,
			unack_fifo_depth,
			UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH);
	AL_UDMA_PRINT_REG_BIT(udma, "  ", "\n", m2s, m2s_comp, cfg_1c,
			q_promotion,
			UDMA_M2S_COMP_CFG_1C_Q_PROMOTION);
	AL_UDMA_PRINT_REG_BIT(udma, "  ", "\n", m2s, m2s_comp, cfg_1c,
			force_rr,
			UDMA_M2S_COMP_CFG_1C_FORCE_RR);
	AL_UDMA_PRINT_REG_FIELD(udma, "  ", "\n", "%d", m2s, m2s_comp, cfg_1c,
			q_free_min,
			UDMA_M2S_COMP_CFG_1C_Q_FREE_MIN);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_comp, cfg_coal);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_comp, cfg_application_ack);
}

static void al_udma_regs_m2s_stat_print(struct al_udma *udma)
{
	al_dbg("M2S statistics regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, cfg_st);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, tx_pkt);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, tx_bytes_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, tx_bytes_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, prefed_desc);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, comp_pkt);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, comp_desc);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_stat, ack_pkts);
}

static void al_udma_regs_m2s_feature_print(struct al_udma *udma)
{
	al_dbg("M2S feature regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_feature, reg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_feature, reg_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_feature, reg_4);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_feature, reg_5);
}

static void al_udma_regs_m2s_q_print(struct al_udma *udma, uint32_t qid)
{
	al_dbg("M2S Q[%d] status regs:\n", qid);
	al_reg_write32(&udma->udma_regs->m2s.m2s.indirect_ctrl, qid);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, sel_pref_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, sel_comp_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, sel_rate_limit_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s, sel_dwrr_status);

	al_dbg("M2S Q[%d] regs:\n", qid);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdrbp_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdrbp_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdrl);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdrhp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdrtp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tdcp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tcrbp_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tcrbp_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], tcrhp);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], rlimit.cfg_1s);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], rlimit.cfg_cycle);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid],
			rlimit.cfg_token_size_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid],
			rlimit.cfg_token_size_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], rlimit.mask);

	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], dwrr_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], dwrr_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], dwrr_cfg_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], comp_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", m2s, m2s_q[qid], q_tx_pkt);
}

static void al_udma_regs_s2m_axi_print(struct al_udma *udma)
{
	al_dbg("S2M AXI regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, data_wr_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, data_wr_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, desc_rd_cfg_4);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, desc_rd_cfg_5);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, comp_wr_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, comp_wr_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, data_wr_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, desc_rd_cfg_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, desc_wr_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, ostand_cfg_rd);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, axi_s2m, ostand_cfg_wr);
}

static void al_udma_regs_s2m_general_print(struct al_udma *udma)
{
	al_dbg("S2M general regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, state);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, err_log_mask);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, log_0);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, log_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, log_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, log_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, s_data_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, s_header_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, axi_data_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, unack_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, check_en);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, fifo_en);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, stream_cfg);
}

static void al_udma_regs_s2m_rd_print(struct al_udma *udma)
{
	al_dbg("S2M read regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_rd, desc_pref_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_rd, desc_pref_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_rd, desc_pref_cfg_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_rd, desc_pref_cfg_4);
}

static void al_udma_regs_s2m_wr_print(struct al_udma *udma)
{
	al_dbg("S2M write regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_wr, data_cfg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_wr, data_cfg_1);
}

static void al_udma_regs_s2m_comp_print(struct al_udma *udma)
{
	al_dbg("S2M completion regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_comp, cfg_1c);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_comp, cfg_2c);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_comp, cfg_application_ack);
}

static void al_udma_regs_s2m_stat_print(struct al_udma *udma)
{
	al_dbg("S2M statistics regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, drop_pkt);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, rx_bytes_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, rx_bytes_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, prefed_desc);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, comp_pkt);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, comp_desc);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_stat, ack_pkts);
}

static void al_udma_regs_s2m_feature_print(struct al_udma *udma)
{
	al_dbg("S2M feature regs:\n");
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_feature, reg_1);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_feature, reg_3);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_feature, reg_4);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_feature, reg_5);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_feature, reg_6);
}

static void al_udma_regs_s2m_q_print(struct al_udma *udma, uint32_t qid)
{
	al_dbg("S2M Q[%d] status regs:\n", qid);
	al_reg_write32(&udma->udma_regs->m2s.m2s.indirect_ctrl, qid);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, sel_pref_fifo_status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m, sel_comp_fifo_status);

	al_dbg("S2M Q[%d] regs:\n", qid);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], status);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdrbp_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdrbp_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdrl);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdrhp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdrtp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rdcp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rcrbp_low);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rcrbp_high);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rcrhp);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], rcrhp_internal);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], comp_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], comp_cfg_2);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], pkt_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], qos_cfg);
	AL_UDMA_PRINT_REG(udma, " ", "\n", s2m, s2m_q[qid], q_rx_pkt);
}

void al_udma_regs_print(struct al_udma *udma, unsigned int mask)
{
	uint32_t i;

	if (!udma)
		return;

	if (udma->type == UDMA_TX) {
		if (mask & AL_UDMA_DEBUG_AXI)
			al_udma_regs_m2s_axi_print(udma);
		if (mask & AL_UDMA_DEBUG_GENERAL)
			al_udma_regs_m2s_general_print(udma);
		if (mask & AL_UDMA_DEBUG_READ)
			al_udma_regs_m2s_rd_print(udma);
		if (mask & AL_UDMA_DEBUG_DWRR)
			al_udma_regs_m2s_dwrr_print(udma);
		if (mask & AL_UDMA_DEBUG_RATE_LIMITER)
			al_udma_regs_m2s_rate_limiter_print(udma);
		if (mask & AL_UDMA_DEBUG_STREAM_RATE_LIMITER)
			al_udma_regs_m2s_stream_rate_limiter_print(udma);
		if (mask & AL_UDMA_DEBUG_COMP)
			al_udma_regs_m2s_comp_print(udma);
		if (mask & AL_UDMA_DEBUG_STAT)
			al_udma_regs_m2s_stat_print(udma);
		if (mask & AL_UDMA_DEBUG_FEATURE)
			al_udma_regs_m2s_feature_print(udma);
		for (i = 0; i < DMA_MAX_Q; i++) {
			if (mask & AL_UDMA_DEBUG_QUEUE(i))
				al_udma_regs_m2s_q_print(udma, i);
		}
	} else {
		if (mask & AL_UDMA_DEBUG_AXI)
			al_udma_regs_s2m_axi_print(udma);
		if (mask & AL_UDMA_DEBUG_GENERAL)
			al_udma_regs_s2m_general_print(udma);
		if (mask & AL_UDMA_DEBUG_READ)
			al_udma_regs_s2m_rd_print(udma);
		if (mask & AL_UDMA_DEBUG_WRITE)
			al_udma_regs_s2m_wr_print(udma);
		if (mask & AL_UDMA_DEBUG_COMP)
			al_udma_regs_s2m_comp_print(udma);
		if (mask & AL_UDMA_DEBUG_STAT)
			al_udma_regs_s2m_stat_print(udma);
		if (mask & AL_UDMA_DEBUG_FEATURE)
			al_udma_regs_s2m_feature_print(udma);
		for (i = 0; i < DMA_MAX_Q; i++) {
			if (mask & AL_UDMA_DEBUG_QUEUE(i))
				al_udma_regs_s2m_q_print(udma, i);
		}
	}
}

void al_udma_q_struct_print(struct al_udma *udma, uint32_t qid)
{
	struct al_udma_q *queue;

	if (!udma)
		return;

	if (qid >= DMA_MAX_Q)
		return;

	queue = &udma->udma_q[qid];

	al_dbg("Q[%d] struct:\n", qid);
	al_dbg(" size_mask = 0x%08x\n", (uint32_t)queue->size_mask);
	al_dbg(" q_regs = %p\n", queue->q_regs);
	al_dbg(" desc_base_ptr = %p\n", queue->desc_base_ptr);
	al_dbg(" next_desc_idx = %d\n", (uint16_t)queue->next_desc_idx);
	al_dbg(" desc_ring_id = %d\n", (uint32_t)queue->desc_ring_id);
	al_dbg(" cdesc_base_ptr = %p\n", queue->cdesc_base_ptr);
	al_dbg(" cdesc_size = %d\n", (uint32_t)queue->cdesc_size);
	al_dbg(" next_cdesc_idx = %d\n", (uint16_t)queue->next_cdesc_idx);
	al_dbg(" end_cdesc_ptr = %p\n", queue->end_cdesc_ptr);
	al_dbg(" comp_head_idx = %d\n", (uint16_t)queue->comp_head_idx);
	al_dbg(" comp_head_ptr = %p\n", queue->comp_head_ptr);
	al_dbg(" pkt_crnt_descs = %d\n", (uint32_t)queue->pkt_crnt_descs);
	al_dbg(" comp_ring_id = %d\n", (uint32_t)queue->comp_ring_id);
	al_dbg(" desc_phy_base = 0x%016jx\n", (uintmax_t)queue->desc_phy_base);
	al_dbg(" cdesc_phy_base = 0x%016jx\n",
			(uintmax_t)queue->cdesc_phy_base);
	al_dbg(" flags = 0x%08x\n", (uint32_t)queue->flags);
	al_dbg(" size = %d\n", (uint32_t)queue->size);
	al_dbg(" status = %d\n", (uint32_t)queue->status);
	al_dbg(" udma = %p\n", queue->udma);
	al_dbg(" qid = %d\n", (uint32_t)queue->qid);
}

void al_udma_ring_print(struct al_udma *udma, uint32_t qid,
		enum al_udma_ring_type rtype)
{
	struct al_udma_q *queue;
	uint32_t desc_size;
	void *base_ptr;
	uint32_t i;

	if (!udma)
		return;

	if (qid >= DMA_MAX_Q)
		return;

	queue = &udma->udma_q[qid];
	if (rtype == AL_RING_SUBMISSION) {
		base_ptr = queue->desc_base_ptr;
		desc_size = sizeof(union al_udma_desc);
		if (base_ptr)
			al_dbg("Q[%d] submission ring pointers:\n", qid);
		else {
			al_dbg("Q[%d] submission ring is not allocated\n", qid);
			return;
		}
	} else {
		base_ptr = queue->cdesc_base_ptr;
		desc_size = queue->cdesc_size;
		if (base_ptr)
			al_dbg("Q[%d] completion ring pointers:\n", qid);
		else {
			al_dbg("Q[%d] completion ring is not allocated\n", qid);
			return;
		}
	}

	for (i = 0; i < queue->size; i++) {
		uint32_t *curr_addr = (void*)((uintptr_t)base_ptr + i * desc_size);
		if (desc_size == 16)
			al_dbg("[%04d](%p): %08x %08x %08x %08x\n",
					i,
					curr_addr,
					(uint32_t)*curr_addr,
					(uint32_t)*(curr_addr+1),
					(uint32_t)*(curr_addr+2),
					(uint32_t)*(curr_addr+3));
		else if (desc_size == 8)
			al_dbg("[%04d](%p): %08x %08x\n",
					i,
					curr_addr,
					(uint32_t)*curr_addr,
					(uint32_t)*(curr_addr+1));
		else if (desc_size == 4)
			al_dbg("[%04d](%p): %08x\n",
					i,
					curr_addr,
					(uint32_t)*curr_addr);
		else
			break;
	}
}

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
 * @file   al_hal_udma_config.c
 *
 * @brief  Universal DMA HAL driver for configurations
 *
 */

#include <al_hal_common.h>
#include <al_hal_udma_regs.h>
#include <al_hal_udma_config.h>

/**************** Misc configurations *********************/
/** Configure AXI generic configuration */
int al_udma_axi_set(struct udma_gen_axi *axi_regs,
					struct al_udma_axi_conf *axi)
{
        uint32_t reg;

        al_reg_write32(&axi_regs->cfg_1, axi->axi_timeout);

        reg = al_reg_read32(&axi_regs->cfg_2);
        reg &= ~UDMA_GEN_AXI_CFG_2_ARB_PROMOTION_MASK;
        reg |= axi->arb_promotion;
        al_reg_write32(&axi_regs->cfg_2, reg);

        reg = al_reg_read32(&axi_regs->endian_cfg);
        if (axi->swap_8_bytes == AL_TRUE)
                reg |= UDMA_GEN_AXI_ENDIAN_CFG_SWAP_64B_EN;
        else
                reg &= ~UDMA_GEN_AXI_ENDIAN_CFG_SWAP_64B_EN;

        if (axi->swap_s2m_data == AL_TRUE)
                reg |= UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DATA;
        else
                reg &= ~UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DATA;

        if (axi->swap_s2m_desc == AL_TRUE)
                reg |= UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DESC;
        else
                reg &= ~UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DESC;

        if (axi->swap_m2s_data == AL_TRUE)
                reg |= UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DATA;
        else
                reg &= ~UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DATA;

        if (axi->swap_m2s_desc == AL_TRUE)
                reg |= UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DESC;
        else
                reg &= ~UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DESC;

        al_reg_write32(&axi_regs->endian_cfg, reg);
        return 0;
}

/* Configure UDMA AXI M2S configuration */
/** Configure AXI M2S submaster */
static int al_udma_m2s_axi_sm_set(struct al_udma_axi_submaster *m2s_sm,
				      uint32_t *cfg_1, uint32_t *cfg_2,
				      uint32_t *cfg_max_beats)
{
	uint32_t reg;
	reg = al_reg_read32(cfg_1);
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_1_AWID_MASK;
	reg |= m2s_sm->id & UDMA_AXI_M2S_COMP_WR_CFG_1_AWID_MASK;
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_1_AWCACHE_MASK;
	reg |= (m2s_sm->cache_type <<
			UDMA_AXI_M2S_COMP_WR_CFG_1_AWCACHE_SHIFT) &
		UDMA_AXI_M2S_COMP_WR_CFG_1_AWCACHE_MASK;
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_1_AWBURST_MASK;
	reg |= (m2s_sm->burst << UDMA_AXI_M2S_COMP_WR_CFG_1_AWBURST_SHIFT) &
		UDMA_AXI_M2S_COMP_WR_CFG_1_AWBURST_MASK;
	al_reg_write32(cfg_1, reg);

	reg = al_reg_read32(cfg_2);
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_2_AWUSER_MASK;
	reg |= m2s_sm->used_ext & UDMA_AXI_M2S_COMP_WR_CFG_2_AWUSER_MASK;
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_2_AWSIZE_MASK;
	reg |= (m2s_sm->bus_size <<
			UDMA_AXI_M2S_COMP_WR_CFG_2_AWSIZE_SHIFT) &
		UDMA_AXI_M2S_COMP_WR_CFG_2_AWSIZE_MASK;
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_2_AWQOS_MASK;
	reg |= (m2s_sm->qos << UDMA_AXI_M2S_COMP_WR_CFG_2_AWQOS_SHIFT) &
		UDMA_AXI_M2S_COMP_WR_CFG_2_AWQOS_MASK;
	reg &= ~UDMA_AXI_M2S_COMP_WR_CFG_2_AWPROT_MASK;
	reg |= (m2s_sm->prot << UDMA_AXI_M2S_COMP_WR_CFG_2_AWPROT_SHIFT) &
		UDMA_AXI_M2S_COMP_WR_CFG_2_AWPROT_MASK;
	al_reg_write32(cfg_2, reg);

	reg = al_reg_read32(cfg_max_beats);
	reg &= ~UDMA_AXI_M2S_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK;
	reg |= m2s_sm->max_beats &
			UDMA_AXI_M2S_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK;
	al_reg_write32(cfg_max_beats, reg);

	return 0;
}

/** Configure UDMA AXI M2S configuration */
int al_udma_m2s_axi_set(struct al_udma *udma,
					struct al_udma_m2s_axi_conf *axi_m2s)
{
	uint32_t reg;

	al_udma_m2s_axi_sm_set(&axi_m2s->comp_write,
			       &udma->udma_regs->m2s.axi_m2s.comp_wr_cfg_1,
			       &udma->udma_regs->m2s.axi_m2s.comp_wr_cfg_2,
			       &udma->udma_regs->m2s.axi_m2s.desc_wr_cfg_1);

	al_udma_m2s_axi_sm_set(&axi_m2s->data_read,
			       &udma->udma_regs->m2s.axi_m2s.data_rd_cfg_1,
			       &udma->udma_regs->m2s.axi_m2s.data_rd_cfg_2,
			       &udma->udma_regs->m2s.axi_m2s.data_rd_cfg);

	al_udma_m2s_axi_sm_set(&axi_m2s->desc_read,
			       &udma->udma_regs->m2s.axi_m2s.desc_rd_cfg_1,
			       &udma->udma_regs->m2s.axi_m2s.desc_rd_cfg_2,
			       &udma->udma_regs->m2s.axi_m2s.desc_rd_cfg_3);

	reg = al_reg_read32(&udma->udma_regs->m2s.axi_m2s.data_rd_cfg);
	if (axi_m2s->break_on_max_boundary == AL_TRUE)
		reg |= UDMA_AXI_M2S_DATA_RD_CFG_ALWAYS_BREAK_ON_MAX_BOUDRY;
	else
		reg &= ~UDMA_AXI_M2S_DATA_RD_CFG_ALWAYS_BREAK_ON_MAX_BOUDRY;
	al_reg_write32(&udma->udma_regs->m2s.axi_m2s.data_rd_cfg, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.axi_m2s.desc_wr_cfg_1);
	reg &= ~UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK;
	reg |= (axi_m2s->min_axi_beats <<
			UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS_SHIFT) &
		UDMA_AXI_M2S_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK;
	al_reg_write32(&udma->udma_regs->m2s.axi_m2s.desc_wr_cfg_1, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.axi_m2s.ostand_cfg);
	reg &= ~UDMA_AXI_M2S_OSTAND_CFG_MAX_DATA_RD_MASK;
	reg |= axi_m2s->ostand_max_data_read &
			UDMA_AXI_M2S_OSTAND_CFG_MAX_DATA_RD_MASK;
	reg &= ~UDMA_AXI_M2S_OSTAND_CFG_MAX_DESC_RD_MASK;
	reg |= (axi_m2s->ostand_max_desc_read <<
			UDMA_AXI_M2S_OSTAND_CFG_MAX_DESC_RD_SHIFT) &
		UDMA_AXI_M2S_OSTAND_CFG_MAX_DESC_RD_MASK;
	reg &= ~UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_REQ_MASK;
	reg |= (axi_m2s->ostand_max_comp_req <<
			UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_REQ_SHIFT) &
		UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_REQ_MASK;
	reg &= ~UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_DATA_WR_MASK;
	reg |= (axi_m2s->ostand_max_comp_write <<
			UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_DATA_WR_SHIFT) &
		UDMA_AXI_M2S_OSTAND_CFG_MAX_COMP_DATA_WR_MASK;
	al_reg_write32(&udma->udma_regs->m2s.axi_m2s.ostand_cfg, reg);
	return 0;
}

/** Configure AXI S2M submaster */
static int al_udma_s2m_axi_sm_set(struct al_udma_axi_submaster *s2m_sm,
				      uint32_t *cfg_1, uint32_t *cfg_2,
				      uint32_t *cfg_max_beats)
{
	uint32_t reg;
	reg = al_reg_read32(cfg_1);
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_1_AWID_MASK;
	reg |= s2m_sm->id & UDMA_AXI_S2M_COMP_WR_CFG_1_AWID_MASK;
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_1_AWCACHE_MASK;
	reg |= (s2m_sm->cache_type <<
			UDMA_AXI_S2M_COMP_WR_CFG_1_AWCACHE_SHIFT) &
		UDMA_AXI_S2M_COMP_WR_CFG_1_AWCACHE_MASK;
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_1_AWBURST_MASK;
	reg |= (s2m_sm->burst << UDMA_AXI_S2M_COMP_WR_CFG_1_AWBURST_SHIFT) &
		UDMA_AXI_S2M_COMP_WR_CFG_1_AWBURST_MASK;
	al_reg_write32(cfg_1, reg);

	reg = al_reg_read32(cfg_2);
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_2_AWUSER_MASK;
	reg |= s2m_sm->used_ext & UDMA_AXI_S2M_COMP_WR_CFG_2_AWUSER_MASK;
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_2_AWSIZE_MASK;
	reg |= (s2m_sm->bus_size << UDMA_AXI_S2M_COMP_WR_CFG_2_AWSIZE_SHIFT) &
		UDMA_AXI_S2M_COMP_WR_CFG_2_AWSIZE_MASK;
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_2_AWQOS_MASK;
	reg |= (s2m_sm->qos << UDMA_AXI_S2M_COMP_WR_CFG_2_AWQOS_SHIFT) &
		UDMA_AXI_S2M_COMP_WR_CFG_2_AWQOS_MASK;
	reg &= ~UDMA_AXI_S2M_COMP_WR_CFG_2_AWPROT_MASK;
	reg |= (s2m_sm->prot << UDMA_AXI_S2M_COMP_WR_CFG_2_AWPROT_SHIFT) &
		UDMA_AXI_S2M_COMP_WR_CFG_2_AWPROT_MASK;
	al_reg_write32(cfg_2, reg);

	reg = al_reg_read32(cfg_max_beats);
	reg &= ~UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK;
	reg |= s2m_sm->max_beats &
			UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK;
	al_reg_write32(cfg_max_beats, reg);

	return 0;
}

/** Configure UDMA AXI S2M configuration */
int al_udma_s2m_axi_set(struct al_udma *udma,
				struct al_udma_s2m_axi_conf *axi_s2m)
{

	uint32_t reg;

	al_udma_s2m_axi_sm_set(&axi_s2m->data_write,
			       &udma->udma_regs->s2m.axi_s2m.data_wr_cfg_1,
			       &udma->udma_regs->s2m.axi_s2m.data_wr_cfg_2,
			       &udma->udma_regs->s2m.axi_s2m.data_wr_cfg);

	al_udma_s2m_axi_sm_set(&axi_s2m->desc_read,
			       &udma->udma_regs->s2m.axi_s2m.desc_rd_cfg_4,
			       &udma->udma_regs->s2m.axi_s2m.desc_rd_cfg_5,
			       &udma->udma_regs->s2m.axi_s2m.desc_rd_cfg_3);

	al_udma_s2m_axi_sm_set(&axi_s2m->comp_write,
			       &udma->udma_regs->s2m.axi_s2m.comp_wr_cfg_1,
			       &udma->udma_regs->s2m.axi_s2m.comp_wr_cfg_2,
			       &udma->udma_regs->s2m.axi_s2m.desc_wr_cfg_1);

	reg = al_reg_read32(&udma->udma_regs->s2m.axi_s2m.desc_rd_cfg_3);
	if (axi_s2m->break_on_max_boundary == AL_TRUE)
		reg |= UDMA_AXI_S2M_DESC_RD_CFG_3_ALWAYS_BREAK_ON_MAX_BOUDRY;
	else
		reg &= ~UDMA_AXI_S2M_DESC_RD_CFG_3_ALWAYS_BREAK_ON_MAX_BOUDRY;
	al_reg_write32(&udma->udma_regs->s2m.axi_s2m.desc_rd_cfg_3, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.axi_s2m.desc_wr_cfg_1);
	reg &= ~UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK;
	reg |= (axi_s2m->min_axi_beats <<
			UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_SHIFT) &
		UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK;
	al_reg_write32(&udma->udma_regs->s2m.axi_s2m.desc_wr_cfg_1, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.axi_s2m.ostand_cfg_rd);
	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_MASK;
	reg |= axi_s2m->ostand_max_desc_read &
			UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_MASK;

	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_STREAM_ACK_MASK;
	reg |= (axi_s2m->ack_fifo_depth <<
			UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_STREAM_ACK_SHIFT) &
		UDMA_AXI_S2M_OSTAND_CFG_RD_MAX_STREAM_ACK_MASK;

	al_reg_write32(&udma->udma_regs->s2m.axi_s2m.ostand_cfg_rd, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.axi_s2m.ostand_cfg_wr);
	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_MASK;
	reg |= axi_s2m->ostand_max_data_req &
			UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_MASK;
	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_BEATS_WR_OSTAND_MASK;
	reg |= (axi_s2m->ostand_max_data_write <<
		UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_BEATS_WR_OSTAND_SHIFT) &
		UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_DATA_BEATS_WR_OSTAND_MASK;
	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_REQ_MASK;
	reg |= (axi_s2m->ostand_max_comp_req <<
			UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_REQ_SHIFT) &
		UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_REQ_MASK;
	reg &= ~UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_DATA_WR_OSTAND_MASK;
	reg |= (axi_s2m->ostand_max_comp_write <<
		UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_DATA_WR_OSTAND_SHIFT) &
		UDMA_AXI_S2M_OSTAND_CFG_WR_MAX_COMP_DATA_WR_OSTAND_MASK;
	al_reg_write32(&udma->udma_regs->s2m.axi_s2m.ostand_cfg_wr, reg);
	return 0;
}

/** M2S packet len configuration */
int al_udma_m2s_packet_size_cfg_set(struct al_udma *udma,
				struct al_udma_m2s_pkt_len_conf *conf)
{
	uint32_t reg = al_reg_read32(&udma->udma_regs->m2s.m2s.cfg_len);
	uint32_t max_supported_size = UDMA_M2S_CFG_LEN_MAX_PKT_SIZE_MASK;

	al_assert(udma->type == UDMA_TX);

	if (conf->encode_64k_as_zero == AL_TRUE)
		max_supported_size += 1;	/* 64K */

	if (conf->max_pkt_size > max_supported_size) {
		al_err("udma [%s]: requested max_pkt_size (0x%x) exceeds the"
			"supported limit (0x%x)\n", udma->name,
			 conf->max_pkt_size, max_supported_size);
		return -EINVAL;
	}

	reg &= ~UDMA_M2S_CFG_LEN_ENCODE_64K;
	if (conf->encode_64k_as_zero == AL_TRUE)
		reg |= UDMA_M2S_CFG_LEN_ENCODE_64K;
	else
		reg &= ~UDMA_M2S_CFG_LEN_ENCODE_64K;

	reg &= ~UDMA_M2S_CFG_LEN_MAX_PKT_SIZE_MASK;
	reg |= conf->max_pkt_size;

	al_reg_write32(&udma->udma_regs->m2s.m2s.cfg_len, reg);
	return 0;
}

/** Report Error - to be used for abort */
void al_udma_err_report(struct al_udma *udma __attribute__((__unused__)))
{
	return;
}

/** Statistics - TBD */
void al_udma_stats_get(struct al_udma *udma __attribute__((__unused__)))
{
	return;
}

/** Configure UDMA M2S descriptor prefetch */
int al_udma_m2s_pref_set(struct al_udma *udma,
				struct al_udma_m2s_desc_pref_conf *conf)
{
	uint32_t reg;

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_1);
	reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_1_FIFO_DEPTH_MASK;
	reg |= conf->desc_fifo_depth;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_1, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_2);

	if (conf->sch_mode == SRR)
		reg |= UDMA_M2S_RD_DESC_PREF_CFG_2_PREF_FORCE_RR;
	else if (conf->sch_mode == STRICT)
		reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_2_PREF_FORCE_RR;
	else {
		al_err("udma [%s]: requested descriptor preferch arbiter "
			"mode (%d) is invalid\n", udma->name, conf->sch_mode);
		return -EINVAL;
	}
	reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK;
	reg |= conf->max_desc_per_packet &
		UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_2, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_3);
	reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK;
	reg |= conf->min_burst_below_thr &
		UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK;

	reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK;
	reg |=(conf->min_burst_above_thr <<
	       UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT) &
		UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK;

	reg &= ~UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_MASK;
	reg |= (conf->pref_thr <<
			UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) &
		UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_MASK;

	al_reg_write32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_3, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.data_cfg);
	reg &= ~UDMA_M2S_RD_DATA_CFG_DATA_FIFO_DEPTH_MASK;
	reg |= conf->data_fifo_depth &
			UDMA_M2S_RD_DATA_CFG_DATA_FIFO_DEPTH_MASK;

	reg &= ~UDMA_M2S_RD_DATA_CFG_MAX_PKT_LIMIT_MASK;
	reg |= (conf->max_pkt_limit
			<< UDMA_M2S_RD_DATA_CFG_MAX_PKT_LIMIT_SHIFT) &
		UDMA_M2S_RD_DATA_CFG_MAX_PKT_LIMIT_MASK;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rd.data_cfg, reg);

	return 0;
}

/** Ger the M2S UDMA descriptor prefetch */
int al_udma_m2s_pref_get(struct al_udma *udma,
				struct al_udma_m2s_desc_pref_conf *conf)
{
	uint32_t reg;

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_1);
	conf->desc_fifo_depth =
	    AL_REG_FIELD_GET(reg, UDMA_M2S_RD_DESC_PREF_CFG_1_FIFO_DEPTH_MASK,
			UDMA_M2S_RD_DESC_PREF_CFG_1_FIFO_DEPTH_SHIFT);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_2);
	if (reg & UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK)
		conf->sch_mode = SRR;
	else
		conf->sch_mode = STRICT;
	conf->max_desc_per_packet =
	    AL_REG_FIELD_GET(reg,
			UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK,
			UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_SHIFT);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_3);

	conf->min_burst_below_thr =
	    AL_REG_FIELD_GET(reg,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_SHIFT);

	conf->min_burst_above_thr =
	    AL_REG_FIELD_GET(reg,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK,
			UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT);

	conf->pref_thr = AL_REG_FIELD_GET(reg,
				UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_MASK,
				UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT);
	return 0;
}

/* set max descriptors */
int al_udma_m2s_max_descs_set(struct al_udma *udma, uint8_t max_descs)
{
	uint32_t pref_thr = max_descs;
	uint32_t min_burst_above_thr = 4;
	al_assert(max_descs <= AL_UDMA_M2S_MAX_ALLOWED_DESCS_PER_PACKET);
	al_assert(max_descs > 0);

	/* increase min_burst_above_thr so larger burst can be used to fetch
	 * descriptors */
	if (pref_thr >= 8)
		min_burst_above_thr = 8;
	else {
	/* don't set prefetch threshold too low so we can have the
	 * min_burst_above_thr >= 4 */
		pref_thr = 4;
	}

	al_reg_write32_masked(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_2,
			      UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK,
			      max_descs << UDMA_M2S_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_SHIFT);

	al_reg_write32_masked(&udma->udma_regs->m2s.m2s_rd.desc_pref_cfg_3,
			      UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_MASK |
			      UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK,
			      (pref_thr << UDMA_M2S_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) |
			      (min_burst_above_thr << UDMA_M2S_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT));

	return 0;
}

/* set s2m max descriptors */
int al_udma_s2m_max_descs_set(struct al_udma *udma, uint8_t max_descs)
{
	uint32_t pref_thr = max_descs;
	uint32_t min_burst_above_thr = 4;
	al_assert(max_descs <= AL_UDMA_S2M_MAX_ALLOWED_DESCS_PER_PACKET);
	al_assert(max_descs > 0);

	/* increase min_burst_above_thr so larger burst can be used to fetch
	 * descriptors */
	if (pref_thr >= 8)
		min_burst_above_thr = 8;
	else
	/* don't set prefetch threshold too low so we can have the
	 * min_burst_above_thr >= 4 */
		pref_thr = 4;

	al_reg_write32_masked(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_3,
			      UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_MASK |
			      UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK,
			      (pref_thr << UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) |
			      (min_burst_above_thr << UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT));

	return 0;
}

int al_udma_s2m_full_line_write_set(struct al_udma *udma, al_bool enable)
{
	uint32_t	val = 0;

	if (enable == AL_TRUE) {
		val = UDMA_S2M_WR_DATA_CFG_2_FULL_LINE_MODE;
		al_info("udma [%s]: full line write enabled\n", udma->name);
	}

	al_reg_write32_masked(&udma->udma_regs->s2m.s2m_wr.data_cfg_2,
			UDMA_S2M_WR_DATA_CFG_2_FULL_LINE_MODE,
			val);
	return 0;
}

/** Configure S2M UDMA descriptor prefetch */
int al_udma_s2m_pref_set(struct al_udma *udma,
				struct al_udma_s2m_desc_pref_conf *conf)
{
	uint32_t reg;

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_1);
	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_1_FIFO_DEPTH_MASK;
	reg |= conf->desc_fifo_depth;
	al_reg_write32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_1, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_2);

	if (conf->sch_mode == SRR)
		reg |= UDMA_S2M_RD_DESC_PREF_CFG_2_PREF_FORCE_RR;
	else if (conf->sch_mode == STRICT)
		reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_2_PREF_FORCE_RR;
	else {
		al_err("udma [%s]: requested descriptor preferch arbiter "
			"mode (%d) is invalid\n", udma->name, conf->sch_mode);
		return -EINVAL;
	}
	if (conf->q_promotion == AL_TRUE)
		reg |= UDMA_S2M_RD_DESC_PREF_CFG_2_Q_PROMOTION;
	else
		reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_2_Q_PROMOTION;

	if (conf->force_promotion == AL_TRUE)
		reg |= UDMA_S2M_RD_DESC_PREF_CFG_2_FORCE_PROMOTION;
	else
		reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_2_FORCE_PROMOTION;

	if (conf->en_pref_prediction == AL_TRUE)
		reg |= UDMA_S2M_RD_DESC_PREF_CFG_2_EN_PREF_PREDICTION;
	else
		reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_2_EN_PREF_PREDICTION;

	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_2_PROMOTION_TH_MASK;
	reg |= (conf->promotion_th
			<< UDMA_S2M_RD_DESC_PREF_CFG_2_PROMOTION_TH_SHIFT) &
		UDMA_S2M_RD_DESC_PREF_CFG_2_PROMOTION_TH_MASK;

	al_reg_write32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_2, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_3);
	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_MASK;
	reg |= (conf->pref_thr << UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) &
		UDMA_S2M_RD_DESC_PREF_CFG_3_PREF_THR_MASK;

	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK;
	reg |= conf->min_burst_below_thr &
		UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_BELOW_THR_MASK;

	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK;
	reg |=(conf->min_burst_above_thr <<
	       UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_SHIFT) &
		UDMA_S2M_RD_DESC_PREF_CFG_3_MIN_BURST_ABOVE_THR_MASK;

	al_reg_write32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_3, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_4);
	reg &= ~UDMA_S2M_RD_DESC_PREF_CFG_4_A_FULL_THR_MASK;
	reg |= conf->a_full_thr & UDMA_S2M_RD_DESC_PREF_CFG_4_A_FULL_THR_MASK;
	al_reg_write32(&udma->udma_regs->s2m.s2m_rd.desc_pref_cfg_4, reg);


	return 0;
}

/* Configure S2M UDMA data write */
int al_udma_s2m_data_write_set(struct al_udma *udma,
				struct al_udma_s2m_data_write_conf *conf)
{
	uint32_t reg;

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_wr.data_cfg_1);
	reg &= ~UDMA_S2M_WR_DATA_CFG_1_DATA_FIFO_DEPTH_MASK;
	reg |= conf->data_fifo_depth &
			UDMA_S2M_WR_DATA_CFG_1_DATA_FIFO_DEPTH_MASK;
	reg &= ~UDMA_S2M_WR_DATA_CFG_1_MAX_PKT_LIMIT_MASK;
	reg |= (conf->max_pkt_limit <<
				UDMA_S2M_WR_DATA_CFG_1_MAX_PKT_LIMIT_SHIFT) &
			UDMA_S2M_WR_DATA_CFG_1_MAX_PKT_LIMIT_MASK;
	reg &= ~UDMA_S2M_WR_DATA_CFG_1_FIFO_MARGIN_MASK;
	reg |= (conf->fifo_margin <<
				UDMA_S2M_WR_DATA_CFG_1_FIFO_MARGIN_SHIFT) &
			UDMA_S2M_WR_DATA_CFG_1_FIFO_MARGIN_MASK;
	al_reg_write32(&udma->udma_regs->s2m.s2m_wr.data_cfg_1, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_wr.data_cfg_2);
	reg &= ~UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_MASK;
	reg |= conf->desc_wait_timer &
			UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_MASK;
	reg &= ~(UDMA_S2M_WR_DATA_CFG_2_DROP_IF_NO_DESC |
		 UDMA_S2M_WR_DATA_CFG_2_HINT_IF_NO_DESC |
		 UDMA_S2M_WR_DATA_CFG_2_WAIT_FOR_PREF |
		 UDMA_S2M_WR_DATA_CFG_2_FULL_LINE_MODE |
		 UDMA_S2M_WR_DATA_CFG_2_DIRECT_HDR_USE_BUF1);
	reg |= conf->flags &
		(UDMA_S2M_WR_DATA_CFG_2_DROP_IF_NO_DESC |
		 UDMA_S2M_WR_DATA_CFG_2_HINT_IF_NO_DESC |
		 UDMA_S2M_WR_DATA_CFG_2_WAIT_FOR_PREF |
		 UDMA_S2M_WR_DATA_CFG_2_FULL_LINE_MODE |
		 UDMA_S2M_WR_DATA_CFG_2_DIRECT_HDR_USE_BUF1);
	al_reg_write32(&udma->udma_regs->s2m.s2m_wr.data_cfg_2, reg);

	return 0;
}

/* Configure S2M UDMA completion */
int al_udma_s2m_completion_set(struct al_udma *udma,
				struct al_udma_s2m_completion_conf *conf)
{
	uint32_t reg = al_reg_read32(&udma->udma_regs->s2m.s2m_comp.cfg_1c);
	reg &= ~UDMA_S2M_COMP_CFG_1C_DESC_SIZE_MASK;
	reg |= conf->desc_size & UDMA_S2M_COMP_CFG_1C_DESC_SIZE_MASK;
	if (conf->cnt_words == AL_TRUE)
		reg |= UDMA_S2M_COMP_CFG_1C_CNT_WORDS;
	else
		reg &= ~UDMA_S2M_COMP_CFG_1C_CNT_WORDS;
	if (conf->q_promotion == AL_TRUE)
		reg |= UDMA_S2M_COMP_CFG_1C_Q_PROMOTION;
	else
		reg &= ~UDMA_S2M_COMP_CFG_1C_Q_PROMOTION;
	if (conf->force_rr == AL_TRUE)
		reg |= UDMA_S2M_COMP_CFG_1C_FORCE_RR;
	else
		reg &= ~UDMA_S2M_COMP_CFG_1C_FORCE_RR;
	reg &= ~UDMA_S2M_COMP_CFG_1C_Q_FREE_MIN_MASK;
	reg |= (conf->q_free_min << UDMA_S2M_COMP_CFG_1C_Q_FREE_MIN_SHIFT) &
		UDMA_S2M_COMP_CFG_1C_Q_FREE_MIN_MASK;
	al_reg_write32(&udma->udma_regs->s2m.s2m_comp.cfg_1c, reg);

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_comp.cfg_2c);
	reg &= ~UDMA_S2M_COMP_CFG_2C_COMP_FIFO_DEPTH_MASK;
	reg |= conf->comp_fifo_depth
				& UDMA_S2M_COMP_CFG_2C_COMP_FIFO_DEPTH_MASK;
	reg &= ~UDMA_S2M_COMP_CFG_2C_UNACK_FIFO_DEPTH_MASK;
	reg |= (conf->unack_fifo_depth
			<< UDMA_S2M_COMP_CFG_2C_UNACK_FIFO_DEPTH_SHIFT) &
			UDMA_S2M_COMP_CFG_2C_UNACK_FIFO_DEPTH_MASK;
	al_reg_write32(&udma->udma_regs->s2m.s2m_comp.cfg_2c, reg);

	al_reg_write32(&udma->udma_regs->s2m.s2m_comp.cfg_application_ack,
		       conf->timeout);
	return 0;
}

/** Configure the M2S UDMA scheduling mode */
int al_udma_m2s_sc_set(struct al_udma *udma,
					struct al_udma_m2s_dwrr_conf *sched)
{
	uint32_t reg = al_reg_read32(&udma->udma_regs->m2s.m2s_dwrr.cfg_sched);

	if (sched->enable_dwrr == AL_TRUE)
		reg |= UDMA_M2S_DWRR_CFG_SCHED_EN_DWRR;
	else
		reg &= ~UDMA_M2S_DWRR_CFG_SCHED_EN_DWRR;

	if (sched->pkt_mode == AL_TRUE)
		reg |= UDMA_M2S_DWRR_CFG_SCHED_PKT_MODE_EN;
	else
		reg &= ~UDMA_M2S_DWRR_CFG_SCHED_PKT_MODE_EN;

	reg &= ~UDMA_M2S_DWRR_CFG_SCHED_WEIGHT_INC_MASK;
	reg |= sched->weight << UDMA_M2S_DWRR_CFG_SCHED_WEIGHT_INC_SHIFT;
	reg &= ~UDMA_M2S_DWRR_CFG_SCHED_INC_FACTOR_MASK;
	reg |= sched->inc_factor << UDMA_M2S_DWRR_CFG_SCHED_INC_FACTOR_SHIFT;
	al_reg_write32(&udma->udma_regs->m2s.m2s_dwrr.cfg_sched, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_dwrr.ctrl_deficit_cnt);
	reg &= ~UDMA_M2S_DWRR_CTRL_DEFICIT_CNT_INIT_MASK;
	reg |= sched->deficit_init_val;
	al_reg_write32(&udma->udma_regs->m2s.m2s_dwrr.ctrl_deficit_cnt, reg);

	return 0;
}

/** Configure the M2S UDMA rate limitation */
int al_udma_m2s_rlimit_set(struct al_udma *udma,
					struct al_udma_m2s_rlimit_mode *mode)
{
	uint32_t reg = al_reg_read32(
				&udma->udma_regs->m2s.m2s_rate_limiter.gen_cfg);

	if (mode->pkt_mode_en == AL_TRUE)
		reg |= UDMA_M2S_RATE_LIMITER_GEN_CFG_PKT_MODE_EN;
	else
		reg &= ~UDMA_M2S_RATE_LIMITER_GEN_CFG_PKT_MODE_EN;
	reg &= ~UDMA_M2S_RATE_LIMITER_GEN_CFG_SHORT_CYCLE_SIZE_MASK;
	reg |= mode->short_cycle_sz &
	    UDMA_M2S_RATE_LIMITER_GEN_CFG_SHORT_CYCLE_SIZE_MASK;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rate_limiter.gen_cfg, reg);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_rate_limiter.ctrl_token);
	reg &= ~UDMA_M2S_RATE_LIMITER_CTRL_TOKEN_RST_MASK;
	reg |= mode->token_init_val &
			UDMA_M2S_RATE_LIMITER_CTRL_TOKEN_RST_MASK;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rate_limiter.ctrl_token, reg);

	return 0;
}

int al_udma_m2s_rlimit_reset(struct al_udma *udma)
{
	uint32_t reg = al_reg_read32(
			&udma->udma_regs->m2s.m2s_rate_limiter.ctrl_cycle_cnt);
	reg |= UDMA_M2S_RATE_LIMITER_CTRL_CYCLE_CNT_RST;
	al_reg_write32(&udma->udma_regs->m2s.m2s_rate_limiter.ctrl_cycle_cnt,
						reg);
	return 0;
}

/** Configure the Stream/Q rate limitation */
static int al_udma_common_rlimit_set(struct udma_rlimit_common *regs,
					  struct al_udma_m2s_rlimit_cfg *conf)
{
	uint32_t reg = al_reg_read32(&regs->cfg_1s);
	/* mask max burst size, and enable/pause control bits */
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_MAX_BURST_SIZE_MASK;
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_EN;
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_PAUSE;
	reg |= conf->max_burst_sz &
		UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_MAX_BURST_SIZE_MASK;
	al_reg_write32(&regs->cfg_1s, reg);

	reg = al_reg_read32(&regs->cfg_cycle);
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_CYCLE_LONG_CYCLE_SIZE_MASK;
	reg |= conf->long_cycle_sz &
		UDMA_M2S_STREAM_RATE_LIMITER_CFG_CYCLE_LONG_CYCLE_SIZE_MASK;
	al_reg_write32(&regs->cfg_cycle, reg);

	reg = al_reg_read32(&regs->cfg_token_size_1);
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_1_LONG_CYCLE_MASK;
	reg |= conf->long_cycle &
		UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_1_LONG_CYCLE_MASK;
	al_reg_write32(&regs->cfg_token_size_1, reg);

	reg = al_reg_read32(&regs->cfg_token_size_2);
	reg &= ~UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_2_SHORT_CYCLE_MASK;
	reg |= conf->short_cycle &
		UDMA_M2S_STREAM_RATE_LIMITER_CFG_TOKEN_SIZE_2_SHORT_CYCLE_MASK;
	al_reg_write32(&regs->cfg_token_size_2, reg);

	reg = al_reg_read32(&regs->mask);
	reg &= ~0xf;		/* only bits 0-3 defined */
	reg |= conf->mask & 0xf;
	al_reg_write32(&regs->mask, reg);

	return 0;
}

static int al_udma_common_rlimit_act(struct udma_rlimit_common *regs,
					  enum al_udma_m2s_rlimit_action act)
{
	uint32_t reg;

	switch (act) {
	case AL_UDMA_STRM_RLIMIT_ENABLE:
		reg = al_reg_read32(&regs->cfg_1s);
		reg |= UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_EN;
		al_reg_write32(&regs->cfg_1s, reg);
		break;
	case AL_UDMA_STRM_RLIMIT_PAUSE:
		reg = al_reg_read32(&regs->cfg_1s);
		reg |= UDMA_M2S_STREAM_RATE_LIMITER_CFG_1S_PAUSE;
		al_reg_write32(&regs->cfg_1s, reg);
		break;
	case AL_UDMA_STRM_RLIMIT_RESET:
		reg = al_reg_read32(&regs->sw_ctrl);
		reg |= UDMA_M2S_STREAM_RATE_LIMITER_SW_CTRL_RST_TOKEN_CNT;
		al_reg_write32(&regs->sw_ctrl, reg);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/** Configure the M2S Stream rate limitation */
int al_udma_m2s_strm_rlimit_set(struct al_udma *udma,
				struct al_udma_m2s_rlimit_cfg *conf)
{
	struct udma_rlimit_common *rlimit_regs =
	    &udma->udma_regs->m2s.m2s_stream_rate_limiter.rlimit;

	return al_udma_common_rlimit_set(rlimit_regs, conf);
}

int al_udma_m2s_strm_rlimit_act(struct al_udma *udma,
				enum al_udma_m2s_rlimit_action act)
{
	struct udma_rlimit_common *rlimit_regs =
	    &udma->udma_regs->m2s.m2s_stream_rate_limiter.rlimit;

	if (al_udma_common_rlimit_act(rlimit_regs, act) == -EINVAL) {
		al_err("udma [%s]: udma stream rate limit invalid action "
			"(%d)\n", udma->name, act);
		return -EINVAL;
	}
	return 0;
}

/** Configure the M2S UDMA Q rate limitation */
int al_udma_m2s_q_rlimit_set(struct al_udma_q *udma_q,
				struct al_udma_m2s_rlimit_cfg *conf)
{
	struct udma_rlimit_common *rlimit_regs = &udma_q->q_regs->m2s_q.rlimit;

	return al_udma_common_rlimit_set(rlimit_regs, conf);
}

int al_udma_m2s_q_rlimit_act(struct al_udma_q *udma_q,
				enum al_udma_m2s_rlimit_action act)
{
	struct udma_rlimit_common *rlimit_regs = &udma_q->q_regs->m2s_q.rlimit;

	if (al_udma_common_rlimit_act(rlimit_regs, act) == -EINVAL) {
		al_err("udma [%s %d]: udma stream rate limit invalid action "
				"(%d)\n",
				udma_q->udma->name, udma_q->qid, act);
		return -EINVAL;
	}
	return 0;
}

/** Configure the M2S UDMA Q scheduling mode */
int al_udma_m2s_q_sc_set(struct al_udma_q *udma_q,
					struct al_udma_m2s_q_dwrr_conf *conf)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->m2s_q.dwrr_cfg_1);

	reg &= ~UDMA_M2S_Q_DWRR_CFG_1_MAX_DEFICIT_CNT_SIZE_MASK;
	reg |= conf->max_deficit_cnt_sz &
		UDMA_M2S_Q_DWRR_CFG_1_MAX_DEFICIT_CNT_SIZE_MASK;
	if (conf->strict == AL_TRUE)
		reg |= UDMA_M2S_Q_DWRR_CFG_1_STRICT;
	else
		reg &= ~UDMA_M2S_Q_DWRR_CFG_1_STRICT;
	al_reg_write32(&udma_q->q_regs->m2s_q.dwrr_cfg_1, reg);

	reg = al_reg_read32(&udma_q->q_regs->m2s_q.dwrr_cfg_2);
	reg &= ~UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_MASK;
	reg |= (conf->axi_qos << UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_SHIFT) &
	    UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_MASK;
	reg &= ~UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_MASK;
	reg |= conf->q_qos & UDMA_M2S_Q_DWRR_CFG_2_Q_QOS_MASK;
	al_reg_write32(&udma_q->q_regs->m2s_q.dwrr_cfg_2, reg);

	reg = al_reg_read32(&udma_q->q_regs->m2s_q.dwrr_cfg_3);
	reg &= ~UDMA_M2S_Q_DWRR_CFG_3_WEIGHT_MASK;
	reg |= conf->weight & UDMA_M2S_Q_DWRR_CFG_3_WEIGHT_MASK;
	al_reg_write32(&udma_q->q_regs->m2s_q.dwrr_cfg_3, reg);

	return 0;
}

int al_udma_m2s_q_sc_pause(struct al_udma_q *udma_q, al_bool set)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->m2s_q.dwrr_cfg_1);

	if (set == AL_TRUE)
		reg |= UDMA_M2S_Q_DWRR_CFG_1_PAUSE;
	else
		reg &= ~UDMA_M2S_Q_DWRR_CFG_1_PAUSE;
	al_reg_write32(&udma_q->q_regs->m2s_q.dwrr_cfg_1, reg);

	return 0;
}

int al_udma_m2s_q_sc_reset(struct al_udma_q *udma_q)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->m2s_q.dwrr_sw_ctrl);

	reg |= UDMA_M2S_Q_DWRR_SW_CTRL_RST_CNT;
	al_reg_write32(&udma_q->q_regs->m2s_q.dwrr_sw_ctrl, reg);

	return 0;
}

/** M2S UDMA completion and application timeouts */
int al_udma_m2s_comp_timeouts_set(struct al_udma *udma,
				struct al_udma_m2s_comp_timeouts *conf)
{
	uint32_t reg = al_reg_read32(&udma->udma_regs->m2s.m2s_comp.cfg_1c);

	if (conf->sch_mode == SRR)
		reg |= UDMA_M2S_COMP_CFG_1C_FORCE_RR;
	else if (conf->sch_mode == STRICT)
		reg &= ~UDMA_M2S_COMP_CFG_1C_FORCE_RR;
	else {
		al_err("udma [%s]: requested completion descriptor preferch "
				"arbiter mode (%d) is invalid\n",
				udma->name, conf->sch_mode);
		return -EINVAL;
	}
	if (conf->enable_q_promotion == AL_TRUE)
		reg |= UDMA_M2S_COMP_CFG_1C_Q_PROMOTION;
	else
		reg &= ~UDMA_M2S_COMP_CFG_1C_Q_PROMOTION;
	reg &= ~UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_MASK;
	reg |=
	    conf->comp_fifo_depth << UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_SHIFT;

	reg &= ~UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_MASK;
	reg |= conf->unack_fifo_depth
				<< UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_SHIFT;
	al_reg_write32(&udma->udma_regs->m2s.m2s_comp.cfg_1c, reg);

	al_reg_write32(&udma->udma_regs->m2s.m2s_comp.cfg_coal
							, conf->coal_timeout);

	reg = al_reg_read32(&udma->udma_regs->m2s.m2s_comp.cfg_application_ack);
	reg &= ~UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_MASK;
	reg |= conf->app_timeout << UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_SHIFT;
	al_reg_write32(&udma->udma_regs->m2s.m2s_comp.cfg_application_ack, reg);
	return 0;
}

int al_udma_m2s_comp_timeouts_get(struct al_udma *udma,
					struct al_udma_m2s_comp_timeouts *conf)
{
	uint32_t reg = al_reg_read32(&udma->udma_regs->m2s.m2s_comp.cfg_1c);

	if (reg & UDMA_M2S_COMP_CFG_1C_FORCE_RR)
		conf->sch_mode = SRR;
	else
		conf->sch_mode = STRICT;

	if (reg & UDMA_M2S_COMP_CFG_1C_Q_PROMOTION)
		conf->enable_q_promotion = AL_TRUE;
	else
		conf->enable_q_promotion = AL_FALSE;

	conf->comp_fifo_depth =
	    AL_REG_FIELD_GET(reg,
			     UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_MASK,
			     UDMA_M2S_COMP_CFG_1C_COMP_FIFO_DEPTH_SHIFT);
	conf->unack_fifo_depth =
	    AL_REG_FIELD_GET(reg,
			     UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_MASK,
			     UDMA_M2S_COMP_CFG_1C_UNACK_FIFO_DEPTH_SHIFT);

	conf->coal_timeout = al_reg_read32(
				&udma->udma_regs->m2s.m2s_comp.cfg_coal);

	reg = al_reg_read32(
			&udma->udma_regs->m2s.m2s_comp.cfg_application_ack);

	conf->app_timeout =
	    AL_REG_FIELD_GET(reg,
			     UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_MASK,
			     UDMA_M2S_COMP_CFG_APPLICATION_ACK_TOUT_SHIFT);

	return 0;
}

/**
 * S2M UDMA configure no descriptors behaviour
 */
int al_udma_s2m_no_desc_cfg_set(struct al_udma *udma, al_bool drop_packet, al_bool gen_interrupt, uint32_t wait_for_desc_timeout)
{
	uint32_t reg;

	reg = al_reg_read32(&udma->udma_regs->s2m.s2m_wr.data_cfg_2);

	if ((drop_packet == AL_TRUE) && (wait_for_desc_timeout == 0)) {
		al_err("udam [%s]: setting timeout to 0 will cause the udma to wait forever instead of dropping the packet", udma->name);
		return -EINVAL;
	}

	if (drop_packet == AL_TRUE)
		reg |= UDMA_S2M_WR_DATA_CFG_2_DROP_IF_NO_DESC;
	else
		reg &= ~UDMA_S2M_WR_DATA_CFG_2_DROP_IF_NO_DESC;

	if (gen_interrupt == AL_TRUE)
		reg |= UDMA_S2M_WR_DATA_CFG_2_HINT_IF_NO_DESC;
	else
		reg &= ~UDMA_S2M_WR_DATA_CFG_2_HINT_IF_NO_DESC;

	AL_REG_FIELD_SET(reg, UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_MASK, UDMA_S2M_WR_DATA_CFG_2_DESC_WAIT_TIMER_SHIFT, wait_for_desc_timeout);

	al_reg_write32(&udma->udma_regs->s2m.s2m_wr.data_cfg_2, reg);

	return 0;
}

/* S2M UDMA configure a queue's completion update */
int al_udma_s2m_q_compl_updade_config(struct al_udma_q *udma_q, al_bool enable)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->s2m_q.comp_cfg);

	if (enable == AL_TRUE)
		reg |= UDMA_S2M_Q_COMP_CFG_EN_COMP_RING_UPDATE;
	else
		reg &= ~UDMA_S2M_Q_COMP_CFG_EN_COMP_RING_UPDATE;

	al_reg_write32(&udma_q->q_regs->s2m_q.comp_cfg, reg);

	return 0;
}

/* S2M UDMA configure a queue's completion descriptors coalescing */
int al_udma_s2m_q_compl_coal_config(struct al_udma_q *udma_q, al_bool enable, uint32_t
		coal_timeout)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->s2m_q.comp_cfg);

	if (enable == AL_TRUE)
		reg &= ~UDMA_S2M_Q_COMP_CFG_DIS_COMP_COAL;
	else
		reg |= UDMA_S2M_Q_COMP_CFG_DIS_COMP_COAL;

	al_reg_write32(&udma_q->q_regs->s2m_q.comp_cfg, reg);

	al_reg_write32(&udma_q->q_regs->s2m_q.comp_cfg_2, coal_timeout);
	return 0;
}

/* S2M UDMA configure completion descriptors write burst parameters */
int al_udma_s2m_compl_desc_burst_config(struct al_udma *udma, uint16_t
		burst_size)
{
	if ((burst_size != 64) && (burst_size != 128) && (burst_size != 256)) {
		al_err("%s: invalid burst_size value (%d)\n", __func__,
				burst_size);
		return -EINVAL;
	}

	/* convert burst size from bytes to beats (16 byte) */
	burst_size = burst_size / 16;
	al_reg_write32_masked(&udma->udma_regs->s2m.axi_s2m.desc_wr_cfg_1,
			UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_MASK |
			UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_MASK,
			burst_size << UDMA_AXI_S2M_DESC_WR_CFG_1_MIN_AXI_BEATS_SHIFT |
			burst_size << UDMA_AXI_S2M_DESC_WR_CFG_1_MAX_AXI_BEATS_SHIFT);
	return 0;
}

/* S2M UDMA configure a queue's completion descriptors header split */
int al_udma_s2m_q_compl_hdr_split_config(struct al_udma_q *udma_q, al_bool enable,
					 al_bool force_hdr_split, uint32_t hdr_len)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->s2m_q.pkt_cfg);

	reg &= ~UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_MASK;
	reg &= ~UDMA_S2M_Q_PKT_CFG_EN_HDR_SPLIT;
	reg &= ~UDMA_S2M_Q_PKT_CFG_FORCE_HDR_SPLIT;

	if (enable == AL_TRUE) {
		reg |= hdr_len & UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_MASK;
		reg |= UDMA_S2M_Q_PKT_CFG_EN_HDR_SPLIT;

		if (force_hdr_split == AL_TRUE)
			reg |= UDMA_S2M_Q_PKT_CFG_FORCE_HDR_SPLIT;
	}

	al_reg_write32(&udma_q->q_regs->s2m_q.pkt_cfg, reg);

	return 0;
}

/* S2M UDMA per queue completion configuration */
int al_udma_s2m_q_comp_set(struct al_udma_q *udma_q,
					struct al_udma_s2m_q_comp_conf *conf)
{
	uint32_t reg = al_reg_read32(&udma_q->q_regs->s2m_q.comp_cfg);
	if (conf->en_comp_ring_update == AL_TRUE)
		reg |= UDMA_S2M_Q_COMP_CFG_EN_COMP_RING_UPDATE;
	else
		reg &= ~UDMA_S2M_Q_COMP_CFG_EN_COMP_RING_UPDATE;

	if (conf->dis_comp_coal == AL_TRUE)
		reg |= UDMA_S2M_Q_COMP_CFG_DIS_COMP_COAL;
	else
		reg &= ~UDMA_S2M_Q_COMP_CFG_DIS_COMP_COAL;

	al_reg_write32(&udma_q->q_regs->s2m_q.comp_cfg, reg);

	al_reg_write32(&udma_q->q_regs->s2m_q.comp_cfg_2, conf->comp_timer);

	reg = al_reg_read32(&udma_q->q_regs->s2m_q.pkt_cfg);

	reg &= ~UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_MASK;
	reg |= conf->hdr_split_size & UDMA_S2M_Q_PKT_CFG_HDR_SPLIT_SIZE_MASK;
	if (conf->force_hdr_split == AL_TRUE)
		reg |= UDMA_S2M_Q_PKT_CFG_FORCE_HDR_SPLIT;
	else
		reg &= ~UDMA_S2M_Q_PKT_CFG_FORCE_HDR_SPLIT;
	if (conf->en_hdr_split == AL_TRUE)
		reg |= UDMA_S2M_Q_PKT_CFG_EN_HDR_SPLIT;
	else
		reg &= ~UDMA_S2M_Q_PKT_CFG_EN_HDR_SPLIT;

	al_reg_write32(&udma_q->q_regs->s2m_q.pkt_cfg, reg);

	reg = al_reg_read32(&udma_q->q_regs->s2m_q.qos_cfg);
	reg &= ~UDMA_S2M_QOS_CFG_Q_QOS_MASK;
	reg |= conf->q_qos & UDMA_S2M_QOS_CFG_Q_QOS_MASK;
	al_reg_write32(&udma_q->q_regs->s2m_q.qos_cfg, reg);

	return 0;
}

/* UDMA Target-ID control configuration per queue */
void al_udma_gen_tgtid_conf_queue_set(
	struct unit_regs		*unit_regs,
	struct al_udma_gen_tgtid_conf	*conf,
	uint32_t qid)
{
	uint32_t *tx_tgtid_reg, *rx_tgtid_reg, *tx_tgtaddr_reg, *rx_tgtaddr_reg;
	unsigned int rev_id;

	al_assert(qid < DMA_MAX_Q);
	rev_id = al_udma_get_revision(unit_regs);

	/* Target-ID TX DESC EN */
	al_reg_write32_masked(&unit_regs->gen.tgtid.cfg_tgtid_0,
			(conf->tx_q_conf[qid].desc_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_DESC_EN_SHIFT,
			(conf->tx_q_conf[qid].desc_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_DESC_EN_SHIFT);

	/* Target-ID TX QUEUE EN */
	al_reg_write32_masked(&unit_regs->gen.tgtid.cfg_tgtid_0,
			(conf->tx_q_conf[qid].queue_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_QUEUE_EN_SHIFT,
			(conf->tx_q_conf[qid].queue_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_QUEUE_EN_SHIFT);

	/* Target-ID RX DESC EN */
	al_reg_write32_masked(&unit_regs->gen.tgtid.cfg_tgtid_0,
			(conf->rx_q_conf[qid].desc_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_DESC_EN_SHIFT,
			(conf->rx_q_conf[qid].desc_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_DESC_EN_SHIFT);

	/* Target-ID RX QUEUE EN */
	al_reg_write32_masked(&unit_regs->gen.tgtid.cfg_tgtid_0,
			(conf->rx_q_conf[qid].queue_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_QUEUE_EN_SHIFT,
			(conf->rx_q_conf[qid].queue_en << qid) <<
			UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_QUEUE_EN_SHIFT);

	switch (qid) {
	case 0:
	case 1:
		tx_tgtid_reg = &unit_regs->gen.tgtid.cfg_tgtid_1;
		rx_tgtid_reg = &unit_regs->gen.tgtid.cfg_tgtid_3;
		tx_tgtaddr_reg = &unit_regs->gen.tgtaddr.cfg_tgtaddr_0;
		rx_tgtaddr_reg = &unit_regs->gen.tgtaddr.cfg_tgtaddr_2;
		break;
	case 2:
	case 3:
		tx_tgtid_reg = &unit_regs->gen.tgtid.cfg_tgtid_2;
		rx_tgtid_reg = &unit_regs->gen.tgtid.cfg_tgtid_4;
		tx_tgtaddr_reg = &unit_regs->gen.tgtaddr.cfg_tgtaddr_1;
		rx_tgtaddr_reg = &unit_regs->gen.tgtaddr.cfg_tgtaddr_3;
		break;
	default:
		al_assert(AL_FALSE);
		return;
	}

	al_reg_write32_masked(tx_tgtid_reg,
		UDMA_GEN_TGTID_CFG_TGTID_MASK(qid),
		conf->tx_q_conf[qid].tgtid << UDMA_GEN_TGTID_CFG_TGTID_SHIFT(qid));

	al_reg_write32_masked(rx_tgtid_reg,
		UDMA_GEN_TGTID_CFG_TGTID_MASK(qid),
		conf->rx_q_conf[qid].tgtid << UDMA_GEN_TGTID_CFG_TGTID_SHIFT(qid));

	if (rev_id >= AL_UDMA_REV_ID_REV2) {
		al_reg_write32_masked(tx_tgtaddr_reg,
			UDMA_GEN_TGTADDR_CFG_MASK(qid),
			conf->tx_q_conf[qid].tgtaddr << UDMA_GEN_TGTADDR_CFG_SHIFT(qid));

		al_reg_write32_masked(rx_tgtaddr_reg,
			UDMA_GEN_TGTADDR_CFG_MASK(qid),
			conf->rx_q_conf[qid].tgtaddr << UDMA_GEN_TGTADDR_CFG_SHIFT(qid));
	}
}

/* UDMA Target-ID control configuration */
void al_udma_gen_tgtid_conf_set(
	struct unit_regs		*unit_regs,
	struct al_udma_gen_tgtid_conf	*conf)
{
	int i;

	for (i = 0; i < DMA_MAX_Q; i++)
		al_udma_gen_tgtid_conf_queue_set(unit_regs, conf, i);
}

/* UDMA Target-ID MSIX control configuration */
void al_udma_gen_tgtid_msix_conf_set(
	struct unit_regs			*unit_regs,
	struct al_udma_gen_tgtid_msix_conf	*conf)
{
	al_reg_write32_masked(
		&unit_regs->gen.tgtid.cfg_tgtid_0,
		UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_ACCESS_EN |
		UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_SEL,
		(conf->access_en ? UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_ACCESS_EN : 0) |
		(conf->sel ? UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_SEL : 0));
}

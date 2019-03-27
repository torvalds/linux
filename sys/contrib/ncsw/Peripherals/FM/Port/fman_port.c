/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
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


#include "common/general.h"

#include "fman_common.h"
#include "fsl_fman_port.h"


/* problem Eyal: the following should not be here*/
#define NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_ENQ_FRAME        0x00000028

static uint32_t get_no_pcd_nia_bmi_ac_enc_frame(struct fman_port_cfg *cfg)
{
    if (cfg->errata_A006675)
        return NIA_ENG_FM_CTL |
            NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_ENQ_FRAME;
    else
        return NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME;
}

static int init_bmi_rx(struct fman_port *port,
        struct fman_port_cfg *cfg,
        struct fman_port_params *params)
{
    struct fman_port_rx_bmi_regs *regs = &port->bmi_regs->rx;
    uint32_t tmp;

    /* Rx Configuration register */
    tmp = 0;
    if (port->im_en)
        tmp |= BMI_PORT_CFG_IM;
    else if (cfg->discard_override)
        tmp |= BMI_PORT_CFG_FDOVR;
    iowrite32be(tmp, &regs->fmbm_rcfg);

    /* DMA attributes */
    tmp = (uint32_t)cfg->dma_swap_data << BMI_DMA_ATTR_SWP_SHIFT;
    if (cfg->dma_ic_stash_on)
        tmp |= BMI_DMA_ATTR_IC_STASH_ON;
    if (cfg->dma_header_stash_on)
        tmp |= BMI_DMA_ATTR_HDR_STASH_ON;
    if (cfg->dma_sg_stash_on)
        tmp |= BMI_DMA_ATTR_SG_STASH_ON;
    if (cfg->dma_write_optimize)
        tmp |= BMI_DMA_ATTR_WRITE_OPTIMIZE;
    iowrite32be(tmp, &regs->fmbm_rda);

    /* Rx FIFO parameters */
    tmp = (cfg->rx_pri_elevation / FMAN_PORT_BMI_FIFO_UNITS - 1) <<
            BMI_RX_FIFO_PRI_ELEVATION_SHIFT;
    tmp |= cfg->rx_fifo_thr / FMAN_PORT_BMI_FIFO_UNITS - 1;
    iowrite32be(tmp, &regs->fmbm_rfp);

    if (cfg->excessive_threshold_register)
        /* always allow access to the extra resources */
        iowrite32be(BMI_RX_FIFO_THRESHOLD_ETHE, &regs->fmbm_reth);

    /* Frame end data */
    tmp = (uint32_t)cfg->checksum_bytes_ignore <<
            BMI_RX_FRAME_END_CS_IGNORE_SHIFT;
    tmp |= (uint32_t)cfg->rx_cut_end_bytes <<
            BMI_RX_FRAME_END_CUT_SHIFT;
    if (cfg->errata_A006320)
        tmp &= 0xffe0ffff;
    iowrite32be(tmp, &regs->fmbm_rfed);

    /* Internal context parameters */
    tmp = ((uint32_t)cfg->ic_ext_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
            BMI_IC_TO_EXT_SHIFT;
    tmp |= ((uint32_t)cfg->ic_int_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
            BMI_IC_FROM_INT_SHIFT;
    tmp |= cfg->ic_size / FMAN_PORT_IC_OFFSET_UNITS;
    iowrite32be(tmp, &regs->fmbm_ricp);

    /* Internal buffer offset */
    tmp = ((uint32_t)cfg->int_buf_start_margin / FMAN_PORT_IC_OFFSET_UNITS)
            << BMI_INT_BUF_MARG_SHIFT;
    iowrite32be(tmp, &regs->fmbm_rim);

    /* External buffer margins */
    if (!port->im_en)
    {
        tmp = (uint32_t)cfg->ext_buf_start_margin <<
                BMI_EXT_BUF_MARG_START_SHIFT;
        tmp |= (uint32_t)cfg->ext_buf_end_margin;
        if (cfg->fmbm_rebm_has_sgd && cfg->no_scatter_gather)
            tmp |= BMI_SG_DISABLE;
        iowrite32be(tmp, &regs->fmbm_rebm);
    }

    /* Frame attributes */
    tmp = BMI_CMD_RX_MR_DEF;
    if (!port->im_en)
    {
        tmp |= BMI_CMD_ATTR_ORDER;
        tmp |= (uint32_t)cfg->color << BMI_CMD_ATTR_COLOR_SHIFT;
        if (cfg->sync_req)
            tmp |= BMI_CMD_ATTR_SYNC;
    }
    iowrite32be(tmp, &regs->fmbm_rfca);

    /* NIA */
    if (port->im_en)
        tmp = NIA_ENG_FM_CTL | NIA_FM_CTL_AC_IND_MODE_RX;
    else
    {
        tmp = (uint32_t)cfg->rx_fd_bits << BMI_NEXT_ENG_FD_BITS_SHIFT;
        tmp |= get_no_pcd_nia_bmi_ac_enc_frame(cfg);
    }
    iowrite32be(tmp, &regs->fmbm_rfne);

    /* Enqueue NIA */
    iowrite32be(NIA_ENG_QMI_ENQ | NIA_ORDER_RESTOR, &regs->fmbm_rfene);

    /* Default/error queues */
    if (!port->im_en)
    {
        iowrite32be((params->dflt_fqid & 0x00FFFFFF), &regs->fmbm_rfqid);
        iowrite32be((params->err_fqid & 0x00FFFFFF), &regs->fmbm_refqid);
    }

    /* Discard/error masks */
    iowrite32be(params->discard_mask, &regs->fmbm_rfsdm);
    iowrite32be(params->err_mask, &regs->fmbm_rfsem);

    /* Statistics counters */
    tmp = 0;
    if (cfg->stats_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_rstc);

    /* Performance counters */
    fman_port_set_perf_cnt_params(port, &cfg->perf_cnt_params);
    tmp = 0;
    if (cfg->perf_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_rpc);

    return 0;
}

static int init_bmi_tx(struct fman_port *port,
        struct fman_port_cfg *cfg,
        struct fman_port_params *params)
{
    struct fman_port_tx_bmi_regs *regs = &port->bmi_regs->tx;
    uint32_t tmp;

    /* Tx Configuration register */
    tmp = 0;
    if (port->im_en)
        tmp |= BMI_PORT_CFG_IM;
    iowrite32be(tmp, &regs->fmbm_tcfg);

    /* DMA attributes */
    tmp = (uint32_t)cfg->dma_swap_data << BMI_DMA_ATTR_SWP_SHIFT;
    if (cfg->dma_ic_stash_on)
        tmp |= BMI_DMA_ATTR_IC_STASH_ON;
    if (cfg->dma_header_stash_on)
        tmp |= BMI_DMA_ATTR_HDR_STASH_ON;
    if (cfg->dma_sg_stash_on)
        tmp |= BMI_DMA_ATTR_SG_STASH_ON;
    iowrite32be(tmp, &regs->fmbm_tda);

    /* Tx FIFO parameters */
    tmp = (cfg->tx_fifo_min_level / FMAN_PORT_BMI_FIFO_UNITS) <<
            BMI_TX_FIFO_MIN_FILL_SHIFT;
    tmp |= ((uint32_t)cfg->tx_fifo_deq_pipeline_depth - 1) <<
            BMI_FIFO_PIPELINE_DEPTH_SHIFT;
    tmp |= (uint32_t)(cfg->tx_fifo_low_comf_level /
            FMAN_PORT_BMI_FIFO_UNITS - 1);
    iowrite32be(tmp, &regs->fmbm_tfp);

    /* Frame end data */
    tmp = (uint32_t)cfg->checksum_bytes_ignore <<
            BMI_FRAME_END_CS_IGNORE_SHIFT;
    iowrite32be(tmp, &regs->fmbm_tfed);

    /* Internal context parameters */
    if (!port->im_en)
    {
        tmp = ((uint32_t)cfg->ic_ext_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
                BMI_IC_TO_EXT_SHIFT;
        tmp |= ((uint32_t)cfg->ic_int_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
                BMI_IC_FROM_INT_SHIFT;
        tmp |= cfg->ic_size / FMAN_PORT_IC_OFFSET_UNITS;
        iowrite32be(tmp, &regs->fmbm_ticp);
    }
    /* Frame attributes */
    tmp = BMI_CMD_TX_MR_DEF;
    if (port->im_en)
        tmp |= BMI_CMD_MR_DEAS;
    else
    {
        tmp |= BMI_CMD_ATTR_ORDER;
        tmp |= (uint32_t)cfg->color << BMI_CMD_ATTR_COLOR_SHIFT;
    }
    iowrite32be(tmp, &regs->fmbm_tfca);

    /* Dequeue NIA + enqueue NIA */
    if (port->im_en)
    {
        iowrite32be(NIA_ENG_FM_CTL | NIA_FM_CTL_AC_IND_MODE_TX, &regs->fmbm_tfdne);
        iowrite32be(NIA_ENG_FM_CTL | NIA_FM_CTL_AC_IND_MODE_TX, &regs->fmbm_tfene);
    }
    else
    {
        iowrite32be(NIA_ENG_QMI_DEQ, &regs->fmbm_tfdne);
        iowrite32be(NIA_ENG_QMI_ENQ | NIA_ORDER_RESTOR, &regs->fmbm_tfene);
        if (cfg->fmbm_tfne_has_features)
            iowrite32be(!params->dflt_fqid ?
                BMI_EBD_EN | NIA_BMI_AC_FETCH_ALL_FRAME :
                NIA_BMI_AC_FETCH_ALL_FRAME, &regs->fmbm_tfne);
        if (!params->dflt_fqid && params->dont_release_buf)
        {
            iowrite32be(0x00FFFFFF, &regs->fmbm_tcfqid);
            iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_TX_RELEASE, &regs->fmbm_tfene);
            if (cfg->fmbm_tfne_has_features)
                iowrite32be(ioread32be(&regs->fmbm_tfne) & ~BMI_EBD_EN, &regs->fmbm_tfne);
        }
    }

    /* Confirmation/error queues */
    if (!port->im_en)
    {
        if (params->dflt_fqid || !params->dont_release_buf)
            iowrite32be(params->dflt_fqid & 0x00FFFFFF, &regs->fmbm_tcfqid);
        iowrite32be((params->err_fqid & 0x00FFFFFF), &regs->fmbm_tefqid);
    }
    /* Statistics counters */
    tmp = 0;
    if (cfg->stats_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_tstc);

    /* Performance counters */
    fman_port_set_perf_cnt_params(port, &cfg->perf_cnt_params);
    tmp = 0;
    if (cfg->perf_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_tpc);

    return 0;
}

static int init_bmi_oh(struct fman_port *port,
        struct fman_port_cfg *cfg,
        struct fman_port_params *params)
{
    struct fman_port_oh_bmi_regs *regs = &port->bmi_regs->oh;
    uint32_t tmp;

    /* OP Configuration register */
    tmp = 0;
    if (cfg->discard_override)
        tmp |= BMI_PORT_CFG_FDOVR;
    iowrite32be(tmp, &regs->fmbm_ocfg);

    /* DMA attributes */
    tmp = (uint32_t)cfg->dma_swap_data << BMI_DMA_ATTR_SWP_SHIFT;
    if (cfg->dma_ic_stash_on)
        tmp |= BMI_DMA_ATTR_IC_STASH_ON;
    if (cfg->dma_header_stash_on)
        tmp |= BMI_DMA_ATTR_HDR_STASH_ON;
    if (cfg->dma_sg_stash_on)
        tmp |= BMI_DMA_ATTR_SG_STASH_ON;
    if (cfg->dma_write_optimize)
        tmp |= BMI_DMA_ATTR_WRITE_OPTIMIZE;
    iowrite32be(tmp, &regs->fmbm_oda);

    /* Tx FIFO parameters */
    tmp = ((uint32_t)cfg->tx_fifo_deq_pipeline_depth - 1) <<
            BMI_FIFO_PIPELINE_DEPTH_SHIFT;
    iowrite32be(tmp, &regs->fmbm_ofp);

    /* Internal context parameters */
    tmp = ((uint32_t)cfg->ic_ext_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
            BMI_IC_TO_EXT_SHIFT;
    tmp |= ((uint32_t)cfg->ic_int_offset / FMAN_PORT_IC_OFFSET_UNITS) <<
            BMI_IC_FROM_INT_SHIFT;
    tmp |= cfg->ic_size / FMAN_PORT_IC_OFFSET_UNITS;
    iowrite32be(tmp, &regs->fmbm_oicp);

    /* Frame attributes */
    tmp = BMI_CMD_OP_MR_DEF;
    tmp |= (uint32_t)cfg->color << BMI_CMD_ATTR_COLOR_SHIFT;
    if (cfg->sync_req)
        tmp |= BMI_CMD_ATTR_SYNC;
    if (port->type == E_FMAN_PORT_TYPE_OP)
        tmp |= BMI_CMD_ATTR_ORDER;
    iowrite32be(tmp, &regs->fmbm_ofca);

    /* Internal buffer offset */
    tmp = ((uint32_t)cfg->int_buf_start_margin / FMAN_PORT_IC_OFFSET_UNITS)
            << BMI_INT_BUF_MARG_SHIFT;
    iowrite32be(tmp, &regs->fmbm_oim);

    /* Dequeue NIA */
    iowrite32be(NIA_ENG_QMI_DEQ, &regs->fmbm_ofdne);

    /* NIA and Enqueue NIA */
    if (port->type == E_FMAN_PORT_TYPE_HC) {
        iowrite32be(NIA_ENG_FM_CTL | NIA_FM_CTL_AC_HC,
                &regs->fmbm_ofne);
        iowrite32be(NIA_ENG_QMI_ENQ, &regs->fmbm_ofene);
    } else {
        iowrite32be(get_no_pcd_nia_bmi_ac_enc_frame(cfg),
                &regs->fmbm_ofne);
        iowrite32be(NIA_ENG_QMI_ENQ | NIA_ORDER_RESTOR,
                &regs->fmbm_ofene);
    }

    /* Default/error queues */
    iowrite32be((params->dflt_fqid & 0x00FFFFFF), &regs->fmbm_ofqid);
    iowrite32be((params->err_fqid & 0x00FFFFFF), &regs->fmbm_oefqid);

    /* Discard/error masks */
    if (port->type == E_FMAN_PORT_TYPE_OP) {
        iowrite32be(params->discard_mask, &regs->fmbm_ofsdm);
        iowrite32be(params->err_mask, &regs->fmbm_ofsem);
    }

    /* Statistics counters */
    tmp = 0;
    if (cfg->stats_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_ostc);

    /* Performance counters */
    fman_port_set_perf_cnt_params(port, &cfg->perf_cnt_params);
    tmp = 0;
    if (cfg->perf_counters_enable)
        tmp = BMI_COUNTERS_EN;
    iowrite32be(tmp, &regs->fmbm_opc);

    return 0;
}

static int init_qmi(struct fman_port *port,
        struct fman_port_cfg *cfg,
        struct fman_port_params *params)
{
    struct fman_port_qmi_regs *regs = port->qmi_regs;
    uint32_t tmp;

    tmp = 0;
    if (cfg->queue_counters_enable)
        tmp |= QMI_PORT_CFG_EN_COUNTERS;
    iowrite32be(tmp, &regs->fmqm_pnc);

    /* Rx port configuration */
    if ((port->type == E_FMAN_PORT_TYPE_RX) ||
            (port->type == E_FMAN_PORT_TYPE_RX_10G)) {
        /* Enqueue NIA */
        iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_RELEASE, &regs->fmqm_pnen);
        return 0;
    }

    /* Continue with Tx and O/H port configuration */
    if ((port->type == E_FMAN_PORT_TYPE_TX) ||
            (port->type == E_FMAN_PORT_TYPE_TX_10G)) {
        /* Enqueue NIA */
        iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_TX_RELEASE,
                &regs->fmqm_pnen);
        /* Dequeue NIA */
        iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_TX, &regs->fmqm_pndn);
    } else {
        /* Enqueue NIA */
        iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_RELEASE, &regs->fmqm_pnen);
        /* Dequeue NIA */
        iowrite32be(NIA_ENG_BMI | NIA_BMI_AC_FETCH, &regs->fmqm_pndn);
    }

    /* Dequeue Configuration register */
    tmp = 0;
    if (cfg->deq_high_pri)
        tmp |= QMI_DEQ_CFG_PRI;

    switch (cfg->deq_type) {
    case E_FMAN_PORT_DEQ_BY_PRI:
        tmp |= QMI_DEQ_CFG_TYPE1;
        break;
    case E_FMAN_PORT_DEQ_ACTIVE_FQ:
        tmp |= QMI_DEQ_CFG_TYPE2;
        break;
    case E_FMAN_PORT_DEQ_ACTIVE_FQ_NO_ICS:
        tmp |= QMI_DEQ_CFG_TYPE3;
        break;
    default:
        return -EINVAL;
    }

    if (cfg->qmi_deq_options_support) {
        if ((port->type == E_FMAN_PORT_TYPE_HC) &&
            (cfg->deq_prefetch_opt != E_FMAN_PORT_DEQ_NO_PREFETCH))
            return -EINVAL;

        switch (cfg->deq_prefetch_opt) {
        case E_FMAN_PORT_DEQ_NO_PREFETCH:
            break;
        case E_FMAN_PORT_DEQ_PART_PREFETCH:
            tmp |= QMI_DEQ_CFG_PREFETCH_PARTIAL;
            break;
        case E_FMAN_PORT_DEQ_FULL_PREFETCH:
            tmp |= QMI_DEQ_CFG_PREFETCH_FULL;
            break;
        default:
            return -EINVAL;
        }
    }
    tmp |= (uint32_t)(params->deq_sp & QMI_DEQ_CFG_SP_MASK) <<
            QMI_DEQ_CFG_SP_SHIFT;
    tmp |= cfg->deq_byte_cnt;
    iowrite32be(tmp, &regs->fmqm_pndc);

    return 0;
}

static void get_rx_stats_reg(struct fman_port *port,
        enum fman_port_stats_counters counter,
        uint32_t **stats_reg)
{
    struct fman_port_rx_bmi_regs *regs = &port->bmi_regs->rx;

    switch (counter) {
    case E_FMAN_PORT_STATS_CNT_FRAME:
        *stats_reg = &regs->fmbm_rfrc;
        break;
    case E_FMAN_PORT_STATS_CNT_DISCARD:
        *stats_reg = &regs->fmbm_rfdc;
        break;
    case E_FMAN_PORT_STATS_CNT_DEALLOC_BUF:
        *stats_reg = &regs->fmbm_rbdc;
        break;
    case E_FMAN_PORT_STATS_CNT_RX_BAD_FRAME:
        *stats_reg = &regs->fmbm_rfbc;
        break;
    case E_FMAN_PORT_STATS_CNT_RX_LARGE_FRAME:
        *stats_reg = &regs->fmbm_rlfc;
        break;
    case E_FMAN_PORT_STATS_CNT_RX_OUT_OF_BUF:
        *stats_reg = &regs->fmbm_rodc;
        break;
    case E_FMAN_PORT_STATS_CNT_FILTERED_FRAME:
        *stats_reg = &regs->fmbm_rffc;
        break;
    case E_FMAN_PORT_STATS_CNT_DMA_ERR:
        *stats_reg = &regs->fmbm_rfldec;
        break;
    default:
        *stats_reg = NULL;
    }
}

static void get_tx_stats_reg(struct fman_port *port,
        enum fman_port_stats_counters counter,
        uint32_t **stats_reg)
{
    struct fman_port_tx_bmi_regs *regs = &port->bmi_regs->tx;

    switch (counter) {
    case E_FMAN_PORT_STATS_CNT_FRAME:
        *stats_reg = &regs->fmbm_tfrc;
        break;
    case E_FMAN_PORT_STATS_CNT_DISCARD:
        *stats_reg = &regs->fmbm_tfdc;
        break;
    case E_FMAN_PORT_STATS_CNT_DEALLOC_BUF:
        *stats_reg = &regs->fmbm_tbdc;
        break;
    case E_FMAN_PORT_STATS_CNT_LEN_ERR:
        *stats_reg = &regs->fmbm_tfledc;
        break;
    case E_FMAN_PORT_STATS_CNT_UNSUPPORTED_FORMAT:
        *stats_reg = &regs->fmbm_tfufdc;
        break;
    default:
        *stats_reg = NULL;
    }
}

static void get_oh_stats_reg(struct fman_port *port,
        enum fman_port_stats_counters counter,
        uint32_t **stats_reg)
{
    struct fman_port_oh_bmi_regs *regs = &port->bmi_regs->oh;

    switch (counter) {
    case E_FMAN_PORT_STATS_CNT_FRAME:
        *stats_reg = &regs->fmbm_ofrc;
        break;
    case E_FMAN_PORT_STATS_CNT_DISCARD:
        *stats_reg = &regs->fmbm_ofdc;
        break;
    case E_FMAN_PORT_STATS_CNT_DEALLOC_BUF:
        *stats_reg = &regs->fmbm_obdc;
        break;
    case E_FMAN_PORT_STATS_CNT_FILTERED_FRAME:
        *stats_reg = &regs->fmbm_offc;
        break;
    case E_FMAN_PORT_STATS_CNT_DMA_ERR:
        *stats_reg = &regs->fmbm_ofldec;
        break;
    case E_FMAN_PORT_STATS_CNT_LEN_ERR:
        *stats_reg = &regs->fmbm_ofledc;
        break;
    case E_FMAN_PORT_STATS_CNT_UNSUPPORTED_FORMAT:
        *stats_reg = &regs->fmbm_ofufdc;
        break;
    case E_FMAN_PORT_STATS_CNT_WRED_DISCARD:
        *stats_reg = &regs->fmbm_ofwdc;
        break;
    default:
        *stats_reg = NULL;
    }
}

static void get_rx_perf_reg(struct fman_port *port,
        enum fman_port_perf_counters counter,
        uint32_t **perf_reg)
{
    struct fman_port_rx_bmi_regs *regs = &port->bmi_regs->rx;

    switch (counter) {
    case E_FMAN_PORT_PERF_CNT_CYCLE:
        *perf_reg = &regs->fmbm_rccn;
        break;
    case E_FMAN_PORT_PERF_CNT_TASK_UTIL:
        *perf_reg = &regs->fmbm_rtuc;
        break;
    case E_FMAN_PORT_PERF_CNT_QUEUE_UTIL:
        *perf_reg = &regs->fmbm_rrquc;
        break;
    case E_FMAN_PORT_PERF_CNT_DMA_UTIL:
        *perf_reg = &regs->fmbm_rduc;
        break;
    case E_FMAN_PORT_PERF_CNT_FIFO_UTIL:
        *perf_reg = &regs->fmbm_rfuc;
        break;
    case E_FMAN_PORT_PERF_CNT_RX_PAUSE:
        *perf_reg = &regs->fmbm_rpac;
        break;
    default:
        *perf_reg = NULL;
    }
}

static void get_tx_perf_reg(struct fman_port *port,
        enum fman_port_perf_counters counter,
        uint32_t **perf_reg)
{
    struct fman_port_tx_bmi_regs *regs = &port->bmi_regs->tx;

    switch (counter) {
    case E_FMAN_PORT_PERF_CNT_CYCLE:
        *perf_reg = &regs->fmbm_tccn;
        break;
    case E_FMAN_PORT_PERF_CNT_TASK_UTIL:
        *perf_reg = &regs->fmbm_ttuc;
        break;
    case E_FMAN_PORT_PERF_CNT_QUEUE_UTIL:
        *perf_reg = &regs->fmbm_ttcquc;
        break;
    case E_FMAN_PORT_PERF_CNT_DMA_UTIL:
        *perf_reg = &regs->fmbm_tduc;
        break;
    case E_FMAN_PORT_PERF_CNT_FIFO_UTIL:
        *perf_reg = &regs->fmbm_tfuc;
        break;
    default:
        *perf_reg = NULL;
    }
}

static void get_oh_perf_reg(struct fman_port *port,
        enum fman_port_perf_counters counter,
        uint32_t **perf_reg)
{
    struct fman_port_oh_bmi_regs *regs = &port->bmi_regs->oh;

    switch (counter) {
    case E_FMAN_PORT_PERF_CNT_CYCLE:
        *perf_reg = &regs->fmbm_occn;
        break;
    case E_FMAN_PORT_PERF_CNT_TASK_UTIL:
        *perf_reg = &regs->fmbm_otuc;
        break;
    case E_FMAN_PORT_PERF_CNT_DMA_UTIL:
        *perf_reg = &regs->fmbm_oduc;
        break;
    case E_FMAN_PORT_PERF_CNT_FIFO_UTIL:
        *perf_reg = &regs->fmbm_ofuc;
        break;
    default:
        *perf_reg = NULL;
    }
}

static void get_qmi_counter_reg(struct fman_port *port,
        enum fman_port_qmi_counters  counter,
        uint32_t **queue_reg)
{
    struct fman_port_qmi_regs *regs = port->qmi_regs;

    switch (counter) {
    case E_FMAN_PORT_ENQ_TOTAL:
        *queue_reg = &regs->fmqm_pnetfc;
        break;
    case E_FMAN_PORT_DEQ_TOTAL:
        if ((port->type == E_FMAN_PORT_TYPE_RX) ||
                (port->type == E_FMAN_PORT_TYPE_RX_10G))
            /* Counter not available for Rx ports */
            *queue_reg = NULL;
        else
            *queue_reg = &regs->fmqm_pndtfc;
        break;
    case E_FMAN_PORT_DEQ_FROM_DFLT:
        if ((port->type == E_FMAN_PORT_TYPE_RX) ||
                (port->type == E_FMAN_PORT_TYPE_RX_10G))
            /* Counter not available for Rx ports */
            *queue_reg = NULL;
        else
            *queue_reg = &regs->fmqm_pndfdc;
        break;
    case E_FMAN_PORT_DEQ_CONFIRM:
        if ((port->type == E_FMAN_PORT_TYPE_RX) ||
                (port->type == E_FMAN_PORT_TYPE_RX_10G))
            /* Counter not available for Rx ports */
            *queue_reg = NULL;
        else
            *queue_reg = &regs->fmqm_pndcc;
        break;
    default:
        *queue_reg = NULL;
    }
}

void fman_port_defconfig(struct fman_port_cfg *cfg, enum fman_port_type type)
{
    cfg->dma_swap_data = E_FMAN_PORT_DMA_NO_SWAP;
    cfg->dma_ic_stash_on = FALSE;
    cfg->dma_header_stash_on = FALSE;
    cfg->dma_sg_stash_on = FALSE;
    cfg->dma_write_optimize = TRUE;
    cfg->color = E_FMAN_PORT_COLOR_GREEN;
    cfg->discard_override = FALSE;
    cfg->checksum_bytes_ignore = 0;
    cfg->rx_cut_end_bytes = 4;
    cfg->rx_pri_elevation = ((0x3FF + 1) * FMAN_PORT_BMI_FIFO_UNITS);
    cfg->rx_fifo_thr = ((0x3FF + 1) * FMAN_PORT_BMI_FIFO_UNITS);
    cfg->rx_fd_bits = 0;
    cfg->ic_ext_offset = 0;
    cfg->ic_int_offset = 0;
    cfg->ic_size = 0;
    cfg->int_buf_start_margin = 0;
    cfg->ext_buf_start_margin = 0;
    cfg->ext_buf_end_margin = 0;
    cfg->tx_fifo_min_level  = 0;
    cfg->tx_fifo_low_comf_level = (5 * KILOBYTE);
    cfg->stats_counters_enable = TRUE;
    cfg->perf_counters_enable = TRUE;
    cfg->deq_type = E_FMAN_PORT_DEQ_BY_PRI;

    if (type == E_FMAN_PORT_TYPE_HC) {
        cfg->sync_req = FALSE;
        cfg->deq_prefetch_opt = E_FMAN_PORT_DEQ_NO_PREFETCH;
    } else {
        cfg->sync_req = TRUE;
        cfg->deq_prefetch_opt = E_FMAN_PORT_DEQ_FULL_PREFETCH;
    }

    if (type == E_FMAN_PORT_TYPE_TX_10G) {
        cfg->tx_fifo_deq_pipeline_depth = 4;
        cfg->deq_high_pri = TRUE;
        cfg->deq_byte_cnt = 0x1400;
    } else {
        if ((type == E_FMAN_PORT_TYPE_HC) ||
                (type == E_FMAN_PORT_TYPE_OP))
            cfg->tx_fifo_deq_pipeline_depth = 2;
        else
            cfg->tx_fifo_deq_pipeline_depth = 1;

        cfg->deq_high_pri = FALSE;
        cfg->deq_byte_cnt = 0x400;
    }
    cfg->no_scatter_gather = DEFAULT_FMAN_SP_NO_SCATTER_GATHER;
}

static uint8_t fman_port_find_bpool(struct fman_port *port, uint8_t bpid)
{
    uint32_t *bp_reg, tmp;
    uint8_t i, id;

    /* Find the pool */
    bp_reg = port->bmi_regs->rx.fmbm_ebmpi;
    for (i = 0;
         (i < port->ext_pools_num && (i < FMAN_PORT_MAX_EXT_POOLS_NUM));
         i++) {
        tmp = ioread32be(&bp_reg[i]);
        id = (uint8_t)((tmp & BMI_EXT_BUF_POOL_ID_MASK) >>
                BMI_EXT_BUF_POOL_ID_SHIFT);

        if (id == bpid)
            break;
    }

    return i;
}

int fman_port_init(struct fman_port *port,
        struct fman_port_cfg *cfg,
        struct fman_port_params *params)
{
    int err;

    /* Init BMI registers */
    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        err = init_bmi_rx(port, cfg, params);
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        err = init_bmi_tx(port, cfg, params);
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        err = init_bmi_oh(port, cfg, params);
        break;
    default:
        return -EINVAL;
    }

    if (err)
        return err;

    /* Init QMI registers */
    if (!port->im_en)
    {
        err = init_qmi(port, cfg, params);
        return err;
    }
    return 0;
}

int fman_port_enable(struct fman_port *port)
{
    uint32_t *bmi_cfg_reg, tmp;
    bool rx_port;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        bmi_cfg_reg = &port->bmi_regs->rx.fmbm_rcfg;
        rx_port = TRUE;
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        bmi_cfg_reg = &port->bmi_regs->tx.fmbm_tcfg;
        rx_port = FALSE;
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        bmi_cfg_reg = &port->bmi_regs->oh.fmbm_ocfg;
        rx_port = FALSE;
        break;
    default:
        return -EINVAL;
    }

    /* Enable QMI */
    if (!rx_port) {
        tmp = ioread32be(&port->qmi_regs->fmqm_pnc) | QMI_PORT_CFG_EN;
        iowrite32be(tmp, &port->qmi_regs->fmqm_pnc);
    }

    /* Enable BMI */
    tmp = ioread32be(bmi_cfg_reg) | BMI_PORT_CFG_EN;
    iowrite32be(tmp, bmi_cfg_reg);

    return 0;
}

int fman_port_disable(const struct fman_port *port)
{
    uint32_t *bmi_cfg_reg, *bmi_status_reg, tmp;
    bool rx_port, failure = FALSE;
    int count;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        bmi_cfg_reg = &port->bmi_regs->rx.fmbm_rcfg;
        bmi_status_reg = &port->bmi_regs->rx.fmbm_rst;
        rx_port = TRUE;
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        bmi_cfg_reg = &port->bmi_regs->tx.fmbm_tcfg;
        bmi_status_reg = &port->bmi_regs->tx.fmbm_tst;
        rx_port = FALSE;
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        bmi_cfg_reg = &port->bmi_regs->oh.fmbm_ocfg;
        bmi_status_reg = &port->bmi_regs->oh.fmbm_ost;
        rx_port = FALSE;
        break;
    default:
        return -EINVAL;
    }

    /* Disable QMI */
    if (!rx_port) {
        tmp = ioread32be(&port->qmi_regs->fmqm_pnc) & ~QMI_PORT_CFG_EN;
        iowrite32be(tmp, &port->qmi_regs->fmqm_pnc);

        /* Wait for QMI to finish FD handling */
        count = 100;
        do {
            DELAY(10);
            tmp = ioread32be(&port->qmi_regs->fmqm_pns);
        } while ((tmp & QMI_PORT_STATUS_DEQ_FD_BSY) && --count);

        if (count == 0)
        {
            /* Timeout */
            failure = TRUE;
        }
    }

    /* Disable BMI */
    tmp = ioread32be(bmi_cfg_reg) & ~BMI_PORT_CFG_EN;
    iowrite32be(tmp, bmi_cfg_reg);

    /* Wait for graceful stop end */
    count = 500;
    do {
        DELAY(10);
        tmp = ioread32be(bmi_status_reg);
    } while ((tmp & BMI_PORT_STATUS_BSY) && --count);

    if (count == 0)
    {
        /* Timeout */
        failure = TRUE;
    }

    if (failure)
        return -EBUSY;

    return 0;
}

int fman_port_set_bpools(const struct fman_port *port,
        const struct fman_port_bpools *bp)
{
    uint32_t tmp, *bp_reg, *bp_depl_reg;
    uint8_t i, max_bp_num;
    bool grp_depl_used = FALSE, rx_port;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        max_bp_num = port->ext_pools_num;
        rx_port = TRUE;
        bp_reg = port->bmi_regs->rx.fmbm_ebmpi;
        bp_depl_reg = &port->bmi_regs->rx.fmbm_mpd;
        break;
    case E_FMAN_PORT_TYPE_OP:
        if (port->fm_rev_maj != 4)
            return -EINVAL;
        max_bp_num = FMAN_PORT_OBS_EXT_POOLS_NUM;
        rx_port = FALSE;
        bp_reg = port->bmi_regs->oh.fmbm_oebmpi;
        bp_depl_reg = &port->bmi_regs->oh.fmbm_ompd;
        break;
    default:
        return -EINVAL;
    }

    if (rx_port) {
        /* Check buffers are provided in ascending order */
        for (i = 0;
             (i < (bp->count-1) && (i < FMAN_PORT_MAX_EXT_POOLS_NUM - 1));
             i++) {
            if (bp->bpool[i].size > bp->bpool[i+1].size)
                return -EINVAL;
        }
    }

    /* Set up external buffers pools */
    for (i = 0; i < bp->count; i++) {
        tmp = BMI_EXT_BUF_POOL_VALID;
        tmp |= ((uint32_t)bp->bpool[i].bpid <<
            BMI_EXT_BUF_POOL_ID_SHIFT) & BMI_EXT_BUF_POOL_ID_MASK;

        if (rx_port) {
            if (bp->counters_enable)
                tmp |= BMI_EXT_BUF_POOL_EN_COUNTER;

            if (bp->bpool[i].is_backup)
                tmp |= BMI_EXT_BUF_POOL_BACKUP;

            tmp |= (uint32_t)bp->bpool[i].size;
        }

        iowrite32be(tmp, &bp_reg[i]);
    }

    /* Clear unused pools */
    for (i = bp->count; i < max_bp_num; i++)
        iowrite32be(0, &bp_reg[i]);

    /* Pools depletion */
    tmp = 0;
    for (i = 0; i < FMAN_PORT_MAX_EXT_POOLS_NUM; i++) {
        if (bp->bpool[i].grp_bp_depleted) {
            grp_depl_used = TRUE;
            tmp |= 0x80000000 >> i;
        }

        if (bp->bpool[i].single_bp_depleted)
            tmp |= 0x80 >> i;

        if (bp->bpool[i].pfc_priorities_en)
            tmp |= 0x0100 << i;
    }

    if (grp_depl_used)
        tmp |= ((uint32_t)bp->grp_bp_depleted_num - 1) <<
            BMI_POOL_DEP_NUM_OF_POOLS_SHIFT;

    iowrite32be(tmp, bp_depl_reg);
    return 0;
}

int fman_port_set_rate_limiter(struct fman_port *port,
        struct fman_port_rate_limiter *rate_limiter)
{
    uint32_t *rate_limit_reg, *rate_limit_scale_reg;
    uint32_t granularity, tmp;
    uint8_t usec_bit, factor;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        rate_limit_reg = &port->bmi_regs->tx.fmbm_trlmt;
        rate_limit_scale_reg = &port->bmi_regs->tx.fmbm_trlmts;
        granularity = BMI_RATE_LIMIT_GRAN_TX;
        break;
    case E_FMAN_PORT_TYPE_OP:
        rate_limit_reg = &port->bmi_regs->oh.fmbm_orlmt;
        rate_limit_scale_reg = &port->bmi_regs->oh.fmbm_orlmts;
        granularity = BMI_RATE_LIMIT_GRAN_OP;
        break;
    default:
        return -EINVAL;
    }

    /* Factor is per 1 usec count */
    factor = 1;
    usec_bit = rate_limiter->count_1micro_bit;

    /* If rate limit is too small for an 1usec factor, adjust timestamp
     * scale and multiply the factor */
    while (rate_limiter->rate < (granularity / factor)) {
        if (usec_bit == 31)
            /* Can't configure rate limiter - rate is too small */
            return -EINVAL;

        usec_bit++;
        factor <<= 1;
    }

    /* Figure out register value. The "while" above quarantees that
     * (rate_limiter->rate * factor / granularity) >= 1 */
    tmp = (uint32_t)(rate_limiter->rate * factor / granularity - 1);

    /* Check rate limit isn't too large */
    if (tmp >= BMI_RATE_LIMIT_MAX_RATE_IN_GRAN_UNITS)
        return -EINVAL;

    /* Check burst size is in allowed range */
    if ((rate_limiter->burst_size == 0) ||
            (rate_limiter->burst_size >
                BMI_RATE_LIMIT_MAX_BURST_SIZE))
        return -EINVAL;

    tmp |= (uint32_t)(rate_limiter->burst_size - 1) <<
            BMI_RATE_LIMIT_MAX_BURST_SHIFT;

    if ((port->type == E_FMAN_PORT_TYPE_OP) &&
            (port->fm_rev_maj == 4)) {
        if (rate_limiter->high_burst_size_gran)
            tmp |= BMI_RATE_LIMIT_HIGH_BURST_SIZE_GRAN;
    }

    iowrite32be(tmp, rate_limit_reg);

    /* Set up rate limiter scale register */
    tmp = BMI_RATE_LIMIT_SCALE_EN;
    tmp |= (31 - (uint32_t)usec_bit) << BMI_RATE_LIMIT_SCALE_TSBS_SHIFT;

    if ((port->type == E_FMAN_PORT_TYPE_OP) &&
            (port->fm_rev_maj == 4))
        tmp |= rate_limiter->rate_factor;

    iowrite32be(tmp, rate_limit_scale_reg);

    return 0;
}

int fman_port_delete_rate_limiter(struct fman_port *port)
{
    uint32_t *rate_limit_scale_reg;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        rate_limit_scale_reg = &port->bmi_regs->tx.fmbm_trlmts;
        break;
    case E_FMAN_PORT_TYPE_OP:
        rate_limit_scale_reg = &port->bmi_regs->oh.fmbm_orlmts;
        break;
    default:
        return -EINVAL;
    }

    iowrite32be(0, rate_limit_scale_reg);
    return 0;
}

int fman_port_set_err_mask(struct fman_port *port, uint32_t err_mask)
{
    uint32_t *err_mask_reg;

    /* Obtain register address */
    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        err_mask_reg = &port->bmi_regs->rx.fmbm_rfsem;
        break;
    case E_FMAN_PORT_TYPE_OP:
        err_mask_reg = &port->bmi_regs->oh.fmbm_ofsem;
        break;
    default:
        return -EINVAL;
    }

    iowrite32be(err_mask, err_mask_reg);
    return 0;
}

int fman_port_set_discard_mask(struct fman_port *port, uint32_t discard_mask)
{
    uint32_t *discard_mask_reg;

    /* Obtain register address */
    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        discard_mask_reg = &port->bmi_regs->rx.fmbm_rfsdm;
        break;
    case E_FMAN_PORT_TYPE_OP:
        discard_mask_reg = &port->bmi_regs->oh.fmbm_ofsdm;
        break;
    default:
        return -EINVAL;
    }

    iowrite32be(discard_mask, discard_mask_reg);
    return 0;
}

int fman_port_modify_rx_fd_bits(struct fman_port *port,
        uint8_t rx_fd_bits,
        bool add)
{
    uint32_t    tmp;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        break;
    default:
        return -EINVAL;
    }

    tmp = ioread32be(&port->bmi_regs->rx.fmbm_rfne);

    if (add)
        tmp |= (uint32_t)rx_fd_bits << BMI_NEXT_ENG_FD_BITS_SHIFT;
    else
        tmp &= ~((uint32_t)rx_fd_bits << BMI_NEXT_ENG_FD_BITS_SHIFT);

    iowrite32be(tmp, &port->bmi_regs->rx.fmbm_rfne);
    return 0;
}

int fman_port_set_perf_cnt_params(struct fman_port *port,
        struct fman_port_perf_cnt_params *params)
{
    uint32_t *pcp_reg, tmp;

    /* Obtain register address and check parameters are in range */
    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        pcp_reg = &port->bmi_regs->rx.fmbm_rpcp;
        if ((params->queue_val == 0) ||
            (params->queue_val > MAX_PERFORMANCE_RX_QUEUE_COMP))
            return -EINVAL;
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        pcp_reg = &port->bmi_regs->tx.fmbm_tpcp;
        if ((params->queue_val == 0) ||
            (params->queue_val > MAX_PERFORMANCE_TX_QUEUE_COMP))
            return -EINVAL;
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        pcp_reg = &port->bmi_regs->oh.fmbm_opcp;
        if (params->queue_val != 0)
            return -EINVAL;
        break;
    default:
        return -EINVAL;
    }

    if ((params->task_val == 0) ||
            (params->task_val > MAX_PERFORMANCE_TASK_COMP))
        return -EINVAL;
    if ((params->dma_val == 0) ||
            (params->dma_val > MAX_PERFORMANCE_DMA_COMP))
        return -EINVAL;
    if ((params->fifo_val == 0) ||
            ((params->fifo_val / FMAN_PORT_BMI_FIFO_UNITS) >
                MAX_PERFORMANCE_FIFO_COMP))
        return -EINVAL;
    tmp = (uint32_t)(params->task_val - 1) <<
            BMI_PERFORMANCE_TASK_COMP_SHIFT;
    tmp |= (uint32_t)(params->dma_val - 1) <<
            BMI_PERFORMANCE_DMA_COMP_SHIFT;
    tmp |= (uint32_t)(params->fifo_val / FMAN_PORT_BMI_FIFO_UNITS - 1);

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        tmp |= (uint32_t)(params->queue_val - 1) <<
            BMI_PERFORMANCE_QUEUE_COMP_SHIFT;
        break;
    default:
        break;
    }


    iowrite32be(tmp, pcp_reg);
    return 0;
}

int fman_port_set_stats_cnt_mode(struct fman_port *port, bool enable)
{
    uint32_t *stats_reg, tmp;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        stats_reg = &port->bmi_regs->rx.fmbm_rstc;
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        stats_reg = &port->bmi_regs->tx.fmbm_tstc;
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        stats_reg = &port->bmi_regs->oh.fmbm_ostc;
        break;
    default:
        return -EINVAL;
    }

    tmp = ioread32be(stats_reg);

    if (enable)
        tmp |= BMI_COUNTERS_EN;
    else
        tmp &= ~BMI_COUNTERS_EN;

    iowrite32be(tmp, stats_reg);
    return 0;
}

int fman_port_set_perf_cnt_mode(struct fman_port *port, bool enable)
{
    uint32_t *stats_reg, tmp;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        stats_reg = &port->bmi_regs->rx.fmbm_rpc;
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        stats_reg = &port->bmi_regs->tx.fmbm_tpc;
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        stats_reg = &port->bmi_regs->oh.fmbm_opc;
        break;
    default:
        return -EINVAL;
    }

    tmp = ioread32be(stats_reg);

    if (enable)
        tmp |= BMI_COUNTERS_EN;
    else
        tmp &= ~BMI_COUNTERS_EN;

    iowrite32be(tmp, stats_reg);
    return 0;
}

int fman_port_set_queue_cnt_mode(struct fman_port *port, bool enable)
{
    uint32_t tmp;

    tmp = ioread32be(&port->qmi_regs->fmqm_pnc);

    if (enable)
        tmp |= QMI_PORT_CFG_EN_COUNTERS;
    else
        tmp &= ~QMI_PORT_CFG_EN_COUNTERS;

    iowrite32be(tmp, &port->qmi_regs->fmqm_pnc);
    return 0;
}

int fman_port_set_bpool_cnt_mode(struct fman_port *port,
        uint8_t bpid,
        bool enable)
{
    uint8_t index;
    uint32_t tmp;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        break;
    default:
        return -EINVAL;
    }

    /* Find the pool */
    index = fman_port_find_bpool(port, bpid);
    if (index == port->ext_pools_num || index == FMAN_PORT_MAX_EXT_POOLS_NUM)
        /* Not found */
        return -EINVAL;

    tmp = ioread32be(&port->bmi_regs->rx.fmbm_ebmpi[index]);

    if (enable)
        tmp |= BMI_EXT_BUF_POOL_EN_COUNTER;
    else
        tmp &= ~BMI_EXT_BUF_POOL_EN_COUNTER;

    iowrite32be(tmp, &port->bmi_regs->rx.fmbm_ebmpi[index]);
    return 0;
}

uint32_t fman_port_get_stats_counter(struct fman_port *port,
        enum fman_port_stats_counters  counter)
{
    uint32_t *stats_reg, ret_val;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        get_rx_stats_reg(port, counter, &stats_reg);
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        get_tx_stats_reg(port, counter, &stats_reg);
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        get_oh_stats_reg(port, counter, &stats_reg);
        break;
    default:
        stats_reg = NULL;
    }

    if (stats_reg == NULL)
        return 0;

    ret_val = ioread32be(stats_reg);
    return ret_val;
}

void fman_port_set_stats_counter(struct fman_port *port,
        enum fman_port_stats_counters counter,
        uint32_t value)
{
    uint32_t *stats_reg;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        get_rx_stats_reg(port, counter, &stats_reg);
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        get_tx_stats_reg(port, counter, &stats_reg);
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        get_oh_stats_reg(port, counter, &stats_reg);
        break;
    default:
        stats_reg = NULL;
    }

    if (stats_reg == NULL)
        return;

    iowrite32be(value, stats_reg);
}

uint32_t fman_port_get_perf_counter(struct fman_port *port,
        enum fman_port_perf_counters counter)
{
    uint32_t *perf_reg, ret_val;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        get_rx_perf_reg(port, counter, &perf_reg);
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        get_tx_perf_reg(port, counter, &perf_reg);
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        get_oh_perf_reg(port, counter, &perf_reg);
        break;
    default:
        perf_reg = NULL;
    }

    if (perf_reg == NULL)
        return 0;

    ret_val = ioread32be(perf_reg);
    return ret_val;
}

void fman_port_set_perf_counter(struct fman_port *port,
        enum fman_port_perf_counters counter,
        uint32_t value)
{
    uint32_t *perf_reg;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        get_rx_perf_reg(port, counter, &perf_reg);
        break;
    case E_FMAN_PORT_TYPE_TX:
    case E_FMAN_PORT_TYPE_TX_10G:
        get_tx_perf_reg(port, counter, &perf_reg);
        break;
    case E_FMAN_PORT_TYPE_OP:
    case E_FMAN_PORT_TYPE_HC:
        get_oh_perf_reg(port, counter, &perf_reg);
        break;
    default:
        perf_reg = NULL;
    }

    if (perf_reg == NULL)
        return;

    iowrite32be(value, perf_reg);
}

uint32_t fman_port_get_qmi_counter(struct fman_port *port,
        enum fman_port_qmi_counters  counter)
{
    uint32_t *queue_reg, ret_val;

    get_qmi_counter_reg(port, counter, &queue_reg);

    if (queue_reg == NULL)
        return 0;

    ret_val = ioread32be(queue_reg);
    return ret_val;
}

void fman_port_set_qmi_counter(struct fman_port *port,
        enum fman_port_qmi_counters counter,
        uint32_t value)
{
    uint32_t *queue_reg;

    get_qmi_counter_reg(port, counter, &queue_reg);

    if (queue_reg == NULL)
        return;

    iowrite32be(value, queue_reg);
}

uint32_t fman_port_get_bpool_counter(struct fman_port *port, uint8_t bpid)
{
    uint8_t index;
    uint32_t ret_val;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        break;
    default:
        return 0;
    }

    /* Find the pool */
    index = fman_port_find_bpool(port, bpid);
    if (index == port->ext_pools_num || index == FMAN_PORT_MAX_EXT_POOLS_NUM)
        /* Not found */
        return 0;

    ret_val = ioread32be(&port->bmi_regs->rx.fmbm_acnt[index]);
    return ret_val;
}

void fman_port_set_bpool_counter(struct fman_port *port,
        uint8_t bpid,
        uint32_t value)
{
    uint8_t index;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        break;
    default:
        return;
    }

    /* Find the pool */
    index = fman_port_find_bpool(port, bpid);
    if (index == port->ext_pools_num || index == FMAN_PORT_MAX_EXT_POOLS_NUM)
        /* Not found */
        return;

    iowrite32be(value, &port->bmi_regs->rx.fmbm_acnt[index]);
}

int fman_port_add_congestion_grps(struct fman_port *port,
        uint32_t grps_map[FMAN_PORT_CG_MAP_NUM])
{
    int i;
    uint32_t tmp, *grp_map_reg;
    uint8_t max_grp_map_num;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        if (port->fm_rev_maj == 4)
            max_grp_map_num = 1;
        else
            max_grp_map_num = FMAN_PORT_CG_MAP_NUM;
        grp_map_reg = port->bmi_regs->rx.fmbm_rcgm;
        break;
    case E_FMAN_PORT_TYPE_OP:
        max_grp_map_num = 1;
        if (port->fm_rev_maj != 4)
            return -EINVAL;
        grp_map_reg = port->bmi_regs->oh.fmbm_ocgm;
        break;
    default:
        return -EINVAL;
    }

    for (i = (max_grp_map_num - 1); i >= 0; i--) {
        if (grps_map[i] == 0)
            continue;
        tmp = ioread32be(&grp_map_reg[i]);
        tmp |= grps_map[i];
        iowrite32be(tmp, &grp_map_reg[i]);
    }

    return 0;
}

int fman_port_remove_congestion_grps(struct fman_port *port,
        uint32_t grps_map[FMAN_PORT_CG_MAP_NUM])
{
    int i;
    uint32_t tmp, *grp_map_reg;
    uint8_t max_grp_map_num;

    switch (port->type) {
    case E_FMAN_PORT_TYPE_RX:
    case E_FMAN_PORT_TYPE_RX_10G:
        if (port->fm_rev_maj == 4)
            max_grp_map_num = 1;
        else
            max_grp_map_num = FMAN_PORT_CG_MAP_NUM;
        grp_map_reg = port->bmi_regs->rx.fmbm_rcgm;
        break;
    case E_FMAN_PORT_TYPE_OP:
        max_grp_map_num = 1;
        if (port->fm_rev_maj != 4)
            return -EINVAL;
        grp_map_reg = port->bmi_regs->oh.fmbm_ocgm;
        break;
    default:
        return -EINVAL;
    }

    for (i = (max_grp_map_num - 1); i >= 0; i--) {
        if (grps_map[i] == 0)
            continue;
        tmp = ioread32be(&grp_map_reg[i]);
        tmp &= ~grps_map[i];
        iowrite32be(tmp, &grp_map_reg[i]);
    }
    return 0;
}

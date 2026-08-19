/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_geni_se

#if !defined(_TRACE_QCOM_GENI_SE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_GENI_SE_H

#include <linux/io.h>
#include <linux/tracepoint.h>
#include <linux/soc/qcom/geni-se.h>

TRACE_EVENT(geni_se_regs,
	    TP_PROTO(struct geni_se *se),

	    TP_ARGS(se),

	    TP_STRUCT__entry(__string(geni_se_name,		dev_name(se->dev))
		__field(u32,	geni_se_m_cmd0)
		__field(u32,	geni_se_m_irq_status)
		__field(u32,	geni_se_s_cmd0)
		__field(u32,	geni_se_s_irq_status)
		__field(u32,	geni_se_status)
		__field(u32,	geni_se_ios)
		__field(u32,	geni_se_m_cmd_ctrl)
		__field(u32,	geni_se_m_cmd_err)
		__field(u32,	geni_se_m_fw_err)
		__field(u32,	geni_se_tx_fifo_status)
		__field(u32,	geni_se_rx_fifo_status)
		__field(u32,	geni_se_tx_watermark)
		__field(u32,	geni_se_rx_watermark)
		__field(u32,	geni_se_rx_watermark_rfr)
		__field(u32,	geni_se_m_gp_length)
		__field(u32,	geni_se_s_gp_length)
		__field(u32,	geni_se_dma_tx_irq)
		__field(u32,	geni_se_dma_rx_irq)
		__field(u32,	geni_se_dma_tx_irq_en)
		__field(u32,	geni_se_dma_rx_irq_en)
		__field(u32,	geni_se_dma_rx_len)
		__field(u32,	geni_se_dma_rx_len_in)
		__field(u32,	geni_se_dma_tx_len)
		__field(u32,	geni_se_dma_tx_len_in)
		__field(u32,	geni_se_dma_tx_ptr_l)
		__field(u32,	geni_se_dma_tx_ptr_h)
		__field(u32,	geni_se_dma_rx_ptr_l)
		__field(u32,	geni_se_dma_rx_ptr_h)
		__field(u32,	geni_se_dma_tx_attr)
		__field(u32,	geni_se_dma_tx_max_burst)
		__field(u32,	geni_se_dma_rx_attr)
		__field(u32,	geni_se_dma_rx_max_burst)
		__field(u32,	geni_se_dma_if_en)
		__field(u32,	geni_se_dma_if_en_ro)
		__field(u32,	geni_se_dma_general_cfg)
		__field(u32,	geni_se_dma_qsb_trans_cfg)
		__field(u32,	geni_se_dma_dbg)
		__field(u32,	geni_se_m_irq_en)
		__field(u32,	geni_se_s_irq_en)
		__field(u32,	geni_se_gsi_event_en)
		__field(u32,	geni_se_irq_en)
		__field(u32,	geni_se_ser_m_clk_cfg)
		__field(u32,	geni_se_ser_s_clk_cfg)
		__field(u32,	geni_se_general_cfg)
		__field(u32,	geni_se_output_ctrl)
		__field(u32,	geni_se_clk_ctrl_ro)
		__field(u32,	geni_se_fifo_if_disable)
		__field(u32,	geni_se_fw_multilock_msa)
		__field(u32,	geni_se_clk_sel)
	    ),

	    TP_fast_assign(__assign_str(geni_se_name);
		__entry->geni_se_m_cmd0		  = readl(se->base + SE_GENI_M_CMD0);
		__entry->geni_se_m_irq_status	  = readl(se->base + SE_GENI_M_IRQ_STATUS);
		__entry->geni_se_s_cmd0		  = readl(se->base + SE_GENI_S_CMD0);
		__entry->geni_se_s_irq_status	  = readl(se->base + SE_GENI_S_IRQ_STATUS);
		__entry->geni_se_status		  = readl(se->base + SE_GENI_STATUS);
		__entry->geni_se_ios		  = readl(se->base + SE_GENI_IOS);
		__entry->geni_se_m_cmd_ctrl	  = readl(se->base + SE_GENI_M_CMD_CTRL_REG);
		__entry->geni_se_m_cmd_err	  = readl(se->base + M_CMD_ERR_STATUS);
		__entry->geni_se_m_fw_err	  = readl(se->base + M_FW_ERR_STATUS);
		__entry->geni_se_tx_fifo_status	  = readl(se->base + SE_GENI_TX_FIFO_STATUS);
		__entry->geni_se_rx_fifo_status	  = readl(se->base + SE_GENI_RX_FIFO_STATUS);
		__entry->geni_se_tx_watermark	  = readl(se->base + SE_GENI_TX_WATERMARK_REG);
		__entry->geni_se_rx_watermark	  = readl(se->base + SE_GENI_RX_WATERMARK_REG);
		__entry->geni_se_rx_watermark_rfr = readl(se->base + SE_GENI_RX_RFR_WATERMARK_REG);
		__entry->geni_se_m_gp_length	  = readl(se->base + SE_GENI_M_GP_LENGTH);
		__entry->geni_se_s_gp_length	  = readl(se->base + SE_GENI_S_GP_LENGTH);
		__entry->geni_se_dma_tx_irq	  = readl(se->base + SE_DMA_TX_IRQ_STAT);
		__entry->geni_se_dma_rx_irq	  = readl(se->base + SE_DMA_RX_IRQ_STAT);
		__entry->geni_se_dma_tx_irq_en	  = readl(se->base + SE_DMA_TX_IRQ_EN);
		__entry->geni_se_dma_rx_irq_en	  = readl(se->base + SE_DMA_RX_IRQ_EN);
		__entry->geni_se_dma_rx_len	  = readl(se->base + SE_DMA_RX_LEN);
		__entry->geni_se_dma_rx_len_in	  = readl(se->base + SE_DMA_RX_LEN_IN);
		__entry->geni_se_dma_tx_len	  = readl(se->base + SE_DMA_TX_LEN);
		__entry->geni_se_dma_tx_len_in	  = readl(se->base + SE_DMA_TX_LEN_IN);
		__entry->geni_se_dma_tx_ptr_l	  = readl(se->base + SE_DMA_TX_PTR_L);
		__entry->geni_se_dma_tx_ptr_h	  = readl(se->base + SE_DMA_TX_PTR_H);
		__entry->geni_se_dma_rx_ptr_l	  = readl(se->base + SE_DMA_RX_PTR_L);
		__entry->geni_se_dma_rx_ptr_h	  = readl(se->base + SE_DMA_RX_PTR_H);
		__entry->geni_se_dma_tx_attr	  = readl(se->base + SE_DMA_TX_ATTR);
		__entry->geni_se_dma_tx_max_burst = readl(se->base + SE_DMA_TX_MAX_BURST);
		__entry->geni_se_dma_rx_attr	  = readl(se->base + SE_DMA_RX_ATTR);
		__entry->geni_se_dma_rx_max_burst = readl(se->base + SE_DMA_RX_MAX_BURST);
		__entry->geni_se_dma_if_en	  = readl(se->base + SE_DMA_IF_EN);
		__entry->geni_se_dma_if_en_ro	  = readl(se->base + DMA_IF_EN_RO);
		__entry->geni_se_dma_general_cfg  = readl(se->base + DMA_GENERAL_CFG);
		__entry->geni_se_dma_qsb_trans_cfg = readl(se->base + SE_DMA_QSB_TRANS_CFG);
		__entry->geni_se_dma_dbg	  = readl(se->base + SE_DMA_DEBUG_REG0);
		__entry->geni_se_m_irq_en	  = readl(se->base + SE_GENI_M_IRQ_EN);
		__entry->geni_se_s_irq_en	  = readl(se->base + SE_GENI_S_IRQ_EN);
		__entry->geni_se_gsi_event_en	  = readl(se->base + SE_GSI_EVENT_EN);
		__entry->geni_se_irq_en		  = readl(se->base + SE_IRQ_EN);
		__entry->geni_se_ser_m_clk_cfg	  = readl(se->base + GENI_SER_M_CLK_CFG);
		__entry->geni_se_ser_s_clk_cfg	  = readl(se->base + GENI_SER_S_CLK_CFG);
		__entry->geni_se_general_cfg	  = readl(se->base + GENI_GENERAL_CFG);
		__entry->geni_se_output_ctrl	  = readl(se->base + GENI_OUTPUT_CTRL);
		__entry->geni_se_clk_ctrl_ro	  = readl(se->base + GENI_CLK_CTRL_RO);
		__entry->geni_se_fifo_if_disable  = readl(se->base + GENI_IF_DISABLE_RO);
		__entry->geni_se_fw_multilock_msa = readl(se->base + GENI_FW_MULTILOCK_MSA_RO);
		__entry->geni_se_clk_sel	  = readl(se->base + SE_GENI_CLK_SEL);
	    ),

	    TP_printk("%s: m_cmd0=0x%08x m_irq_status=0x%08x s_cmd0=0x%08x s_irq_status=0x%08x geni_status=0x%08x geni_ios=0x%08x m_cmd_ctrl=0x%08x m_cmd_err=0x%08x m_fw_err=0x%08x tx_fifo_sts=0x%08x rx_fifo_sts=0x%08x tx_watermark=0x%08x rx_watermark=0x%08x rx_watermark_rfr=0x%08x m_gp_length=0x%08x s_gp_length=0x%08x dma_tx_irq=0x%08x dma_rx_irq=0x%08x dma_tx_irq_en=0x%08x dma_rx_irq_en=0x%08x dma_rx_len=0x%08x dma_rx_len_in=0x%08x dma_tx_len=0x%08x dma_tx_len_in=0x%08x dma_tx_ptr_l=0x%08x dma_tx_ptr_h=0x%08x dma_rx_ptr_l=0x%08x dma_rx_ptr_h=0x%08x dma_tx_attr=0x%08x dma_tx_max_burst=0x%08x dma_rx_attr=0x%08x dma_rx_max_burst=0x%08x dma_if_en=0x%08x dma_if_en_ro=0x%08x dma_general_cfg=0x%08x dma_qsb_trans_cfg=0x%08x dma_dbg=0x%08x m_irq_en=0x%08x s_irq_en=0x%08x gsi_event_en=0x%08x se_irq_en=0x%08x ser_m_clk_cfg=0x%08x ser_s_clk_cfg=0x%08x general_cfg=0x%08x output_ctrl=0x%08x clk_ctrl_ro=0x%08x fifo_if_dis=0x%08x fw_multilock_msa=0x%08x clk_sel=0x%08x",
		      __get_str(geni_se_name),
		      __entry->geni_se_m_cmd0, __entry->geni_se_m_irq_status,
		      __entry->geni_se_s_cmd0, __entry->geni_se_s_irq_status,
		      __entry->geni_se_status, __entry->geni_se_ios,
		      __entry->geni_se_m_cmd_ctrl,
		      __entry->geni_se_m_cmd_err, __entry->geni_se_m_fw_err,
		      __entry->geni_se_tx_fifo_status, __entry->geni_se_rx_fifo_status,
		      __entry->geni_se_tx_watermark, __entry->geni_se_rx_watermark,
		      __entry->geni_se_rx_watermark_rfr,
		      __entry->geni_se_m_gp_length, __entry->geni_se_s_gp_length,
		      __entry->geni_se_dma_tx_irq, __entry->geni_se_dma_rx_irq,
		      __entry->geni_se_dma_tx_irq_en, __entry->geni_se_dma_rx_irq_en,
		      __entry->geni_se_dma_rx_len, __entry->geni_se_dma_rx_len_in,
		      __entry->geni_se_dma_tx_len, __entry->geni_se_dma_tx_len_in,
		      __entry->geni_se_dma_tx_ptr_l, __entry->geni_se_dma_tx_ptr_h,
		      __entry->geni_se_dma_rx_ptr_l, __entry->geni_se_dma_rx_ptr_h,
		      __entry->geni_se_dma_tx_attr, __entry->geni_se_dma_tx_max_burst,
		      __entry->geni_se_dma_rx_attr, __entry->geni_se_dma_rx_max_burst,
		      __entry->geni_se_dma_if_en, __entry->geni_se_dma_if_en_ro,
		      __entry->geni_se_dma_general_cfg, __entry->geni_se_dma_qsb_trans_cfg,
		      __entry->geni_se_dma_dbg,
		      __entry->geni_se_m_irq_en, __entry->geni_se_s_irq_en,
		      __entry->geni_se_gsi_event_en, __entry->geni_se_irq_en,
		      __entry->geni_se_ser_m_clk_cfg, __entry->geni_se_ser_s_clk_cfg,
		      __entry->geni_se_general_cfg, __entry->geni_se_output_ctrl,
		      __entry->geni_se_clk_ctrl_ro, __entry->geni_se_fifo_if_disable,
		      __entry->geni_se_fw_multilock_msa, __entry->geni_se_clk_sel)
);

#endif /* _TRACE_QCOM_GENI_SE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

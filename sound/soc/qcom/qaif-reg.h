/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-reg.h -- QAIF ALSA SoC CPU-Platform DAI driver register header
 */
#ifndef SND_SOC_QCOM_QAIF_REG_H
#define SND_SOC_QCOM_QAIF_REG_H

#include "qaif.h"

#define QAIF_SUMMARY_IRQSTAT_REG(v)			(0x19188 + (0x1000 * ((v)->ee)))

/* Core HW info */
#define QAIF_HW_VERSION_REG				(0x0000)
#define QAIF_HW_INFO_REG				(0x0004)
#define QAIF_HW_INFO2_REG				(0x0008)

/* Interface lane and channel info */
#define QAIF_AUD_INTF_LANE_INFO_REG			(0x0020)
#define QAIF_AUD_INTF_LANE_INFO2_REG			(0x0024)
#define QAIF_CDC_TX_INTF_CH_INFO_REG(n)			(0x0028 + (0x4 * (n)))
#define QAIF_CDC_RX_INTF_CH_INFO_REG(n)			(0x0068 + (0x4 * (n)))
#define QAIF_QXM1_SHRAM_LENGTH_INFO_REG			(0x0088)
#define QAIF_QXM0_SHRAM_LENGTH_INFO_REG			(0x008C)
#define QAIF_NUM_AUD_INTF_TO_RAIL_INFO_REG		(0x0090)

/* Debug/control and status */
#define QAIF_DEBUG_CTL_REG				(0x0200)
#define QAIF_WRDMA_LOOPBACK_EN_REG			(0x0204)
#define QAIF_WRDMA_LOOPBACK_SEL_REG			(0x0208)
#define QAIF_SHRAM_DYNAMIC_CLK_GATING_EN_REG		(0x0300)
#define QAIF_AXI_STATUS_REG				(0x0304)
#define QAIF_QSB_DYNAMIC_CLK_GATING_EN_REG		(0x0308)
#define QAIF_START_STOP_CTRL_BYPASS_EN_REG		(0x030C)
#define QAIF_QXM0_AXI_ATTR_CFG_REG			(0x040C)

/* QXM request/grant debug */
#define QAIF_QXM0_AUD_WR_REQ_GNT_DBG_STAT_REG		(0x0500)
#define QAIF_QXM1_AUD_WR_REQ_GNT_DBG_STAT_REG		(0x0504)
#define QAIF_QXM0_CODEC_RX_WR_REQ_DBG_STAT_REG		(0x0508)
#define QAIF_QXM0_CODEC_RX_WR_GNT_DBG_STAT_REG		(0x050C)
#define QAIF_QXM1_CODEC_RX_WR_REQ_DBG_STAT_REG		(0x0510)
#define QAIF_QXM1_CODEC_RX_WR_GNT_DBG_STAT_REG		(0x0514)
#define QAIF_QXM0_AUD_RD_REQ_GNT_DBG_STAT_REG		(0x0518)
#define QAIF_QXM1_AUD_RD_REQ_GNT_DBG_STAT_REG		(0x051C)
#define QAIF_QXM0_CODEC_TX_RD_REQ_DBG_STAT_REG		(0x0520)
#define QAIF_QXM0_CODEC_TX_RD_GNT_DBG_STAT_REG		(0x0524)
#define QAIF_QXM1_CODEC_TX_RD_REQ_DBG_STAT_REG		(0x0528)
#define QAIF_QXM1_CODEC_TX_RD_GNT_DBG_STAT_REG		(0x052C)
#define QAIF_QXM0_EXT_RDDMA_RD_REQ_GNT_DBG_STAT_REG	(0x0530)
#define QAIF_QXM1_EXT_RDDMA_RD_REQ_GNT_DBG_STAT_REG	(0x0534)

/* QSB transaction debug */
#define QAIF_QSB_AUD_WR_TXN_DBG_STAT_REG		(0x0538)
#define QAIF_QSB_CODEC_RX_WR_TXN_ERR_DBG_STAT_REG	(0x053C)
#define QAIF_QSB_CODEC_RX_WR_TXN_OKAY_DBG_STAT_REG	(0x0540)
#define QAIF_QSB_AUD_ADDR_SENT_DBG_STAT_REG		(0x0544)
#define QAIF_QSB_CODEC_TX_RD_ADDR_SENT_DBG_STAT_REG	(0x0548)
#define QAIF_QSB_EXT_RDDMA_RD_ADDR_SENT_DBG_STAT_REG	(0x054C)
#define QAIF_QSB_CODEC_RX_WR_ADDR_SENT_DBG_STAT_REG	(0x0550)
#define QAIF_QSB_AUD_RD_TXN_DBG_STAT_REG		(0x0554)
#define QAIF_QSB_CODEC_TX_RD_TXN_ERR_DBG_STAT_REG	(0x0558)
#define QAIF_QSB_CODEC_TX_RD_TXN_RCVD_DBG_STAT_REG	(0x055C)
#define QAIF_QSB_EXT_RDDMA_RD_TXN_DBG_STAT_REG		(0x0560)
#define QAIF_QSB_MISC_DBG_STATUS_REG			(0x0564)

/* Global spare and HWE */
#define QAIF_GLOBAL_SPARE_IN_REG			(0x0B00)
#define QAIF_GLOBAL_SPARE_OUT_REG			(0x0B04)
#define QAIF_HWE_CFG_REG				(0x0B08)

/* SID maps */
#define QAIF_WRDMA_SID_MAP_REG				(0x1B00)
#define QAIF_CDC_WRDMA_SID_MAP_REG			(0x1B40)
#define QAIF_RDDMA_SID_MAP_REG				(0x1C00)
#define QAIF_CDC_RDDMA_SID_MAP_REG			(0x1C40)

/* EE overlap interrupts */
#define QAIF_EE_OVERLAP_IRQ_EN_REG			(0x1D00)
#define QAIF_EE_OVERLAP_IRQ_RAW_STATUS_REG		(0x1D04)
#define QAIF_EE_OVERLAP_IRQ_CLEAR_REG			(0x1D08)
#define QAIF_EE_OVERLAP_IRQ_FORCE_REG			(0x1D0C)

/*
 * EE (Execution Engine) assignment and map registers.
 *
 * The EE offset (v->ee) is an address/index offset relative to the
 * platform-defined QAIF/EE base and lets the hardware route the AIF
 * operation to the correct EE context. It is hardware/platform-specific and
 * must match the mapping defined for the selected QAIF AIF/lane in the
 * hardware programming guide. It is not a runtime data offset and must not
 * be changed based on the stream configuration.
 */
#define QAIF_EE_RDDMA_ASSIGNMENT_REG(v)			(0x19148 + (0x1000 * ((v)->ee)))
#define QAIF_EE_WRDMA_ASSIGNMENT_REG(v)			(0x19150 + (0x1000 * ((v)->ee)))
#define QAIF_EE_INTF_ASSIGNMENT_REG(v)			(0x19158 + (0x1000 * ((v)->ee)))
#define QAIF_EE_CODEC_RDDMA_ASSIGN_REG(v)		(0x19308 + (0x1000 * ((v)->ee)))
#define QAIF_EE_CODEC_WRDMA_ASSIGN_REG(v)		(0x19318 + (0x1000 * ((v)->ee)))
#define QAIF_EE_RDDMA_MAP_REG(v)			(0x1920 + (0x1000 * ((v)->ee)))
#define QAIF_EE_WRDMA_MAP_REG(v)			(0x1940 + (0x1000 * ((v)->ee)))
#define QAIF_EE_INTF_MAP_REG(v)				(0x1960 + (0x1000 * ((v)->ee)))
#define QAIF_EE_CODEC_RDDMA_MAP_REG(v)			(0x1980 + (0x1000 * ((v)->ee)))
#define QAIF_EE_CODEC_WRDMA_MAP_REG(v)			(0x1A00 + (0x1000 * ((v)->ee)))

/* EE rate-detection and VFR interrupts */
#define QAIF_EE_RATE_DET_IRQ_EN_REG(v)			(0x190F0 + (0x1000 * ((v)->ee)))
#define QAIF_EE_RATE_DET_IRQ_STAT_REG(v)		(0x190F4 + (0x1000 * ((v)->ee)))
#define QAIF_EE_RATE_DET_IRQ_RAW_STAT_REG(v)		(0x190F8 + (0x1000 * ((v)->ee)))
#define QAIF_EE_RATE_DET_IRQ_CLEAR_REG(v)		(0x190FC + (0x1000 * ((v)->ee)))
#define QAIF_EE_RATE_DET_IRQ_FORCE_REG(v)		(0x19100 + (0x1000 * ((v)->ee)))

#define QAIF_EE_VFR_IRQ_EN_REG(v)			(0x19104 + (0x1000 * ((v)->ee)))
#define QAIF_EE_VFR_IRQ_STATUS_REG(v)			(0x19108 + (0x1000 * ((v)->ee)))
#define QAIF_EE_VFR_IRQ_RAW_STATUS_REG(v)		(0x1910C + (0x1000 * ((v)->ee)))
#define QAIF_EE_VFR_IRQ_CLEAR_REG(v)			(0x19110 + (0x1000 * ((v)->ee)))
#define QAIF_EE_VFR_IRQ_FORCE_REG(v)			(0x19114 + (0x1000 * ((v)->ee)))

/* EE AUD_INTF underflow/overflow interrupts */
#define QAIF_EE_AUD_INTF_UF_IRQ_EN_REG(v)		(0x19160 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_UF_IRQ_STAT_REG(v)		(0x19164 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_UF_IRQ_RAW_STAT_REG(v)		(0x19168 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_UF_IRQ_CLEAR_REG(v)		(0x1916C + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_UF_IRQ_FORCE_REG(v)		(0x19170 + (0x1000 * ((v)->ee)))

#define QAIF_EE_AUD_INTF_OF_IRQ_EN_REG(v)		(0x19174 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_OF_IRQ_STAT_REG(v)		(0x19178 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_OF_IRQ_RAW_STAT_REG(v)		(0x1917C + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_OF_IRQ_CLEAR_REG(v)		(0x19180 + (0x1000 * ((v)->ee)))
#define QAIF_EE_AUD_INTF_OF_IRQ_FORCE_REG(v)		(0x19184 + (0x1000 * ((v)->ee)))

/* EE L2 Period IRQ mux selection */
#define QAIF_EE_L2_PERIOD_IRQ_0_3_MUX_SEL_REG(v)	(0x19F00 + (0x1000 * ((v)->ee)))
#define QAIF_EE_L2_PERIOD_IRQ_4_7_MUX_SEL_REG(v)	(0x19F04 + (0x1000 * ((v)->ee)))

/* AUD_INTF block (per interface, stride 0x1000 starting at 0x4000) */
#define QAIF_AUD_INTF_REG_ADDR(offset, i)		(0x4000 + (offset) + (0x1000 * (i)))

#define QAIF_AUD_INTF_CTL_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0000, (i))
#define QAIF_AUD_INTF_SYNC_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0004, (i))
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0008, (i))
#define QAIF_AUD_INTF_FRAME_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x000C, (i))
#define QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0010, (i))
#define QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0030, (i))
#define QAIF_AUD_INTF_LANE_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0050, (i))
#define QAIF_AUD_INTF_MI2S_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0054, (i))
#define QAIF_AUD_INTF_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0058, (i))
#define QAIF_AUD_INTF_CHAR_CTL_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x005C, (i))
#define QAIF_AUD_INTF_CHAR_CFG_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0060, (i))
#define QAIF_AUD_INTF_CHAR_DATA_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x0064, (i))
#define QAIF_AUD_INTF_CHAR_DATA_EXT_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0068, (i))
#define QAIF_AUD_INTF_CHAR_SYNC_REG(i)			QAIF_AUD_INTF_REG_ADDR(0x006C, (i))
#define QAIF_AUD_INTF_INIT_DBG_STATUS_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0FF0, (i))
#define QAIF_AUD_INTF_TX_DBG_STATUS_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0FF4, (i))
#define QAIF_AUD_INTF_RX_DBG_STATUS_REG(i)		QAIF_AUD_INTF_REG_ADDR(0x0FF8, (i))

/* RATE_DET block (per detector, stride 0x1000 starting at 0x1E000) */
#define QAIF_RATE_DET_REG_ADDR(offset, det)		(0x1E000 + (offset) + (0x1000 * (det)))

#define QAIF_RATE_DET_CONFIG_REG(det)			QAIF_RATE_DET_REG_ADDR(0x0000, (det))
#define QAIF_RATE_DET_TARGET1_CONF_REG(det)		QAIF_RATE_DET_REG_ADDR(0x0004, (det))
#define QAIF_RATE_DET_TARGET2_CONF_REG(det)		QAIF_RATE_DET_REG_ADDR(0x0008, (det))
#define QAIF_RATE_DET_BIN_REG(det)			QAIF_RATE_DET_REG_ADDR(0x000C, (det))
#define QAIF_RATE_DET_STC_DIFF_REG(det)			QAIF_RATE_DET_REG_ADDR(0x0010, (det))
#define QAIF_RATE_DET_SEL_REG(det)			QAIF_RATE_DET_REG_ADDR(0x0014, (det))
#define QAIF_RATE_DET_TIMEOUT_CFG_REG(det)		QAIF_RATE_DET_REG_ADDR(0x0018, (det))

#define QAIF_WRDMA_MAP_QXM				(0x1000)
#define QAIF_CDC_WRDMA_MAP_QXM				(0x1004)
#define QAIF_RDDMA_MAP_QXM				(0x1010)
#define QAIF_CDC_RDDMA_MAP_QXM				(0x1014)
#define QAIF_RDDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1100 + (0x4 * (i)))
#define QAIF_CDC_RDDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1140 + (0x4 * (i)))
#define QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1200 + (0x4 * (i)))
#define QAIF_CDC_RDDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1240 + (0x4 * (i)))
#define QAIF_RDDMA_QXM1_SHRAM_LEN(i)			(0x1300 + (0x4 * (i)))
#define QAIF_CDC_RDDMA_QXM1_SHRAM_LEN(i)		(0x1340 + (0x4 * (i)))
#define QAIF_RDDMA_QXM0_SHRAM_LEN(i)			(0x1400 + (0x4 * (i)))
#define QAIF_CDC_RDDMA_QXM0_SHRAM_LEN(i)		(0x1440 + (0x4 * (i)))
#define QAIF_WRDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1500 + (0x4 * (i)))
#define QAIF_CDC_WRDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1540 + (0x4 * (i)))
#define QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1600 + (0x4 * (i)))
#define QAIF_CDC_WRDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1640 + (0x4 * (i)))
#define QAIF_WRDMA_QXM1_SHRAM_LEN(i)			(0x1700 + (0x4 * (i)))
#define QAIF_CDC_WRDMA_QXM1_SHRAM_LEN(i)		(0x1740 + (0x4 * (i)))
#define QAIF_WRDMA_QXM0_SHRAM_LEN(i)			(0x1800 + (0x4 * (i)))
#define QAIF_CDC_WRDMA_QXM0_SHRAM_LEN(i)		(0x1840 + (0x4 * (i)))

/* RDDMA */
static inline u32 qaif_rddma_reg_addr(const struct qaif_variant *v,
				      u32 offset, u32 i)
{
	return v->rddma_reg_base + offset + v->rddma_stride * i;
}

#define QAIF_RDDMA_CTL_REG(v, i)			qaif_rddma_reg_addr(v, 0x00, (i))
#define QAIF_RDDMA_CFG_REG(v, i)			qaif_rddma_reg_addr(v, 0x04, (i))
#define QAIF_RDDMA_BASE_ADDR_REG(v, i)			qaif_rddma_reg_addr(v, 0x08, (i))
#define QAIF_RDDMA_BUFF_LEN_REG(v, i)			qaif_rddma_reg_addr(v, 0x10, (i))
#define QAIF_RDDMA_CURR_ADDR_REG(v, i)			qaif_rddma_reg_addr(v, 0x14, (i))
#define QAIF_RDDMA_PERIOD_LEN_REG(v, i)			qaif_rddma_reg_addr(v, 0x1C, (i))
#define QAIF_RDDMA_PERIOD_CNT_REG(v, i)			qaif_rddma_reg_addr(v, 0x20, (i))
#define QAIF_RDDMA_SHRAM_WORDCNT_REG(v, i)		qaif_rddma_reg_addr(v, 0x24, (i))
#define QAIF_RDDMA_FRAME_STATUS_REG(v, i)		qaif_rddma_reg_addr(v, 0x28, (i))
#define QAIF_RDDMA_FRAME_STATUS_EXTN_REG(v, i)		qaif_rddma_reg_addr(v, 0x2C, (i))
#define QAIF_RDDMA_FRAME_STATUS_CLR_REG(v, i)		qaif_rddma_reg_addr(v, 0x30, (i))
#define QAIF_RDDMA_SET_BUFF_CNT_REG(v, i)		qaif_rddma_reg_addr(v, 0x34, (i))
#define QAIF_RDDMA_SET_PERIOD_CNT_REG(v, i)		qaif_rddma_reg_addr(v, 0x38, (i))
#define QAIF_RDDMA_STC_LSB_REG(v, i)			qaif_rddma_reg_addr(v, 0x3C, (i))
#define QAIF_RDDMA_STC_MSB_REG(v, i)			qaif_rddma_reg_addr(v, 0x40, (i))
#define QAIF_RDDMA_PERIOD_DET_STAT_REG(v, i)		qaif_rddma_reg_addr(v, 0x44, (i))
#define QAIF_RDDMA_PERIOD_DET_CLR_REG(v, i)		qaif_rddma_reg_addr(v, 0x48, (i))
#define QAIF_RDDMA_FORMAT_ERR_REG(v, i)			qaif_rddma_reg_addr(v, 0x4C, (i))
#define QAIF_RDDMA_AHB_BYPASS_REG(v, i)			qaif_rddma_reg_addr(v, 0x50, (i))
#define QAIF_RDDMA_SHUTDOWN_STAT_REG(v, i)		qaif_rddma_reg_addr(v, 0x54, (i))
#define QAIF_RDDMA_PADDING_CFG_REG(v, i)		qaif_rddma_reg_addr(v, 0x58, (i))
#define QAIF_RDDMA_STATUS_REG(v, i)			qaif_rddma_reg_addr(v, 0x60, (i))
#define QAIF_RDDMA_DBG_STATUS_REG(v, i)			qaif_rddma_reg_addr(v, 0xFF0, (i))

static inline u32 qaif_codec_rddma_reg_addr(const struct qaif_variant *v,
					    u32 offset, u32 i)
{
	return v->codec_rddma_reg_base + offset + v->codec_rddma_stride * i;
}

#define QAIF_CDC_RDDMA_CTL_REG(v, i)			qaif_codec_rddma_reg_addr(v, 0x00, (i))
#define QAIF_CDC_RDDMA_CFG_REG(v, i)			qaif_codec_rddma_reg_addr(v, 0x04, (i))
#define QAIF_CDC_RDDMA_BASE_ADDR_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x08, (i))
#define QAIF_CDC_RDDMA_BUFF_LEN_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x10, (i))
#define QAIF_CDC_RDDMA_CURR_ADDR_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x14, (i))
#define QAIF_CDC_RDDMA_PERIOD_LEN_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x1C, (i))
#define QAIF_CDC_RDDMA_PERIOD_CNT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x20, (i))
#define QAIF_CDC_RDDMA_SHRAM_WORDCNT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x24, (i))
#define QAIF_CDC_RDDMA_FRAME_STAT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x28, (i))
#define QAIF_CDC_RDDMA_FRAME_STAT_EXTN_REG(v, i)	qaif_codec_rddma_reg_addr(v, 0x2C, (i))
#define QAIF_CDC_RDDMA_FRAME_STAT_CLR_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x30, (i))
#define QAIF_CDC_RDDMA_SET_BUFF_CNT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x34, (i))
#define QAIF_CDC_RDDMA_SET_PERIOD_CNT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x38, (i))
#define QAIF_CDC_RDDMA_STC_LSB_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x3C, (i))
#define QAIF_CDC_RDDMA_STC_MSB_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x40, (i))
#define QAIF_CDC_RDDMA_PERIOD_DET_STAT_REG(v, i)	qaif_codec_rddma_reg_addr(v, 0x44, (i))
#define QAIF_CDC_RDDMA_PERIOD_DET_CLR_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x48, (i))
#define QAIF_CDC_RDDMA_FORMAT_ERR_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x4C, (i))
#define QAIF_CDC_RDDMA_AHB_BYPASS_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x50, (i))
#define QAIF_CDC_RDDMA_SHUTDOWN_STAT_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x54, (i))
#define QAIF_CDC_RDDMA_PADDING_CFG_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x58, (i))
#define QAIF_CDC_RDDMA_INTF_CFG_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0x5C, (i))
#define QAIF_CDC_RDDMA_STATUS_REG(v, i)			qaif_codec_rddma_reg_addr(v, 0x60, (i))
#define QAIF_CDC_RDDMA_DBG_STATUS_REG(v, i)		qaif_codec_rddma_reg_addr(v, 0xFF0, (i))
#define QAIF_CDC_RDDMA_INTF_DBG_STATUS_REG(v, i)	qaif_codec_rddma_reg_addr(v, 0xFF4, (i))

/* WRDMA */
static inline u32 qaif_wrdma_reg_addr(const struct qaif_variant *v,
				      u32 offset, u32 i)
{
	return v->wrdma_reg_base + offset + v->wrdma_stride * i;
}

#define QAIF_WRDMA_CTL_REG(v, i)			qaif_wrdma_reg_addr(v, 0x00, (i))
#define QAIF_WRDMA_CFG_REG(v, i)			qaif_wrdma_reg_addr(v, 0x04, (i))
#define QAIF_WRDMA_BASE_ADDR_REG(v, i)			qaif_wrdma_reg_addr(v, 0x08, (i))
#define QAIF_WRDMA_BUFF_LEN_REG(v, i)			qaif_wrdma_reg_addr(v, 0x10, (i))
#define QAIF_WRDMA_CURR_ADDR_REG(v, i)			qaif_wrdma_reg_addr(v, 0x14, (i))
#define QAIF_WRDMA_PERIOD_LEN_REG(v, i)			qaif_wrdma_reg_addr(v, 0x1C, (i))
#define QAIF_WRDMA_PERIOD_CNT_REG(v, i)			qaif_wrdma_reg_addr(v, 0x20, (i))
#define QAIF_WRDMA_SHRAM_WORDCNT_REG(v, i)		qaif_wrdma_reg_addr(v, 0x24, (i))
#define QAIF_WRDMA_FRAME_STATUS_REG(v, i)		qaif_wrdma_reg_addr(v, 0x28, (i))
#define QAIF_WRDMA_FRAME_STATUS_EXTN_REG(v, i)		qaif_wrdma_reg_addr(v, 0x2C, (i))
#define QAIF_WRDMA_FRAME_STATUS_CLR_REG(v, i)		qaif_wrdma_reg_addr(v, 0x30, (i))
#define QAIF_WRDMA_SET_BUFF_CNT_REG(v, i)		qaif_wrdma_reg_addr(v, 0x34, (i))
#define QAIF_WRDMA_SET_PERIOD_CNT_REG(v, i)		qaif_wrdma_reg_addr(v, 0x38, (i))
#define QAIF_WRDMA_STC_LSB_REG(v, i)			qaif_wrdma_reg_addr(v, 0x3C, (i))
#define QAIF_WRDMA_STC_MSB_REG(v, i)			qaif_wrdma_reg_addr(v, 0x40, (i))
#define QAIF_WRDMA_PERIOD_DET_STAT_REG(v, i)		qaif_wrdma_reg_addr(v, 0x44, (i))
#define QAIF_WRDMA_PERIOD_DET_CLR_REG(v, i)		qaif_wrdma_reg_addr(v, 0x48, (i))
#define QAIF_WRDMA_FORMAT_ERR_REG(v, i)			qaif_wrdma_reg_addr(v, 0x4C, (i))
#define QAIF_WRDMA_AHB_BYPASS_REG(v, i)			qaif_wrdma_reg_addr(v, 0x50, (i))
#define QAIF_WRDMA_SHUTDOWN_STAT_REG(v, i)		qaif_wrdma_reg_addr(v, 0x54, (i))
#define QAIF_WRDMA_DBG_STATUS_REG(v, i)			qaif_wrdma_reg_addr(v, 0xFF0, (i))

static inline u32 qaif_codec_wrdma_reg_addr(const struct qaif_variant *v,
					    u32 offset, u32 i)
{
	return v->codec_wrdma_reg_base + offset + v->codec_wrdma_stride * i;
}

#define QAIF_CDC_WRDMA_CTL_REG(v, i)			qaif_codec_wrdma_reg_addr(v, 0x00, (i))
#define QAIF_CDC_WRDMA_CFG_REG(v, i)			qaif_codec_wrdma_reg_addr(v, 0x04, (i))
#define QAIF_CDC_WRDMA_BASE_ADDR_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x08, (i))
#define QAIF_CDC_WRDMA_BUFF_LEN_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x10, (i))
#define QAIF_CDC_WRDMA_CURR_ADDR_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x14, (i))
#define QAIF_CDC_WRDMA_PERIOD_LEN_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x1C, (i))
#define QAIF_CDC_WRDMA_PERIOD_CNT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x20, (i))
#define QAIF_CDC_WRDMA_SHRAM_WORDCNT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x24, (i))
#define QAIF_CDC_WRDMA_FRAME_STAT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x28, (i))
#define QAIF_CDC_WRDMA_FRAME_STAT_EXTN_REG(v, i)	qaif_codec_wrdma_reg_addr(v, 0x2C, (i))
#define QAIF_CDC_WRDMA_FRAME_STAT_CLR_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x30, (i))
#define QAIF_CDC_WRDMA_SET_BUFF_CNT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x34, (i))
#define QAIF_CDC_WRDMA_SET_PERIOD_CNT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x38, (i))
#define QAIF_CDC_WRDMA_STC_LSB_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x3C, (i))
#define QAIF_CDC_WRDMA_STC_MSB_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x40, (i))
#define QAIF_CDC_WRDMA_PERIOD_DET_STAT_REG(v, i)	qaif_codec_wrdma_reg_addr(v, 0x44, (i))
#define QAIF_CDC_WRDMA_PERIOD_DET_CLR_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x48, (i))
#define QAIF_CDC_WRDMA_FORMAT_ERR_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x4C, (i))
#define QAIF_CDC_WRDMA_AHB_BYPASS_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x50, (i))
#define QAIF_CDC_WRDMA_SHUTDOWN_STAT_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x54, (i))
#define QAIF_CDC_WRDMA_INTF_CFG_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0x58, (i))
#define QAIF_CDC_WRDMA_DBG_STATUS_REG(v, i)		qaif_codec_wrdma_reg_addr(v, 0xFF0, (i))
#define QAIF_CDC_WRDMA_INTF_DBG_STATUS_REG(v, i)	qaif_codec_wrdma_reg_addr(v, 0xFF4, (i))

static inline u32 qaif_rddma_irq_reg_addr(const struct qaif_variant *v,
					  enum qaif_irq_type dma_type,
					  u32 offset)
{
	if (dma_type == QAIF_AIF_IRQ)
		return v->rddma_irq_reg_base + offset +
		       v->rddma_irq_stride * v->ee;
	return v->codec_rddma_irq_reg_base + offset +
	       v->codec_rddma_irq_stride * v->ee;
}

/* RDDMA Period Interrupts */
#define QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x00)
#define QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x08)
#define QAIF_EE_RDDMA_PERIOD_IRQ_RAW_STAT_REG(v, i)	qaif_rddma_irq_reg_addr(v, i, 0x10)
#define QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x18)
#define QAIF_EE_RDDMA_PERIOD_IRQ_FORCE_REG(v, i)	qaif_rddma_irq_reg_addr(v, i, 0x20)
/* RDDMA Underflow Interrupts */
#define QAIF_EE_RDDMA_UF_IRQ_EN_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x28)
#define QAIF_EE_RDDMA_UF_IRQ_STAT_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x30)
#define QAIF_EE_RDDMA_UF_IRQ_RAW_STAT_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x38)
#define QAIF_EE_RDDMA_UF_IRQ_CLR_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x40)
#define QAIF_EE_RDDMA_UF_IRQ_FORCE_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x48)
/* RDDMA Error Response Interrupts */
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x50)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, i)	qaif_rddma_irq_reg_addr(v, i, 0x58)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_RAW_STAT(v, i)	qaif_rddma_irq_reg_addr(v, i, 0x60)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, i)		qaif_rddma_irq_reg_addr(v, i, 0x68)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_FORCE_REG(v, i)	qaif_rddma_irq_reg_addr(v, i, 0x70)

static inline u32 qaif_wrdma_irq_reg_addr(const struct qaif_variant *v,
					  enum qaif_irq_type dma_type,
					  u32 offset)
{
	if (dma_type == QAIF_AIF_IRQ)
		return v->wrdma_irq_reg_base + offset +
		       v->wrdma_irq_stride * v->ee;
	return v->codec_wrdma_irq_reg_base + offset +
	       v->codec_wrdma_irq_stride * v->ee;
}

/* WRDMA Period Interrupts */
#define QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x00)
#define QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x08)
#define QAIF_EE_WRDMA_PERIOD_IRQ_RAW_STAT_REG(v, i)	qaif_wrdma_irq_reg_addr(v, i, 0x10)
#define QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x18)
#define QAIF_EE_WRDMA_PERIOD_IRQ_FORCE_REG(v, i)	qaif_wrdma_irq_reg_addr(v, i, 0x20)
/* WRDMA Overflow Interrupts */
#define QAIF_EE_WRDMA_OF_IRQ_EN_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x28)
#define QAIF_EE_WRDMA_OF_IRQ_STAT_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x30)
#define QAIF_EE_WRDMA_OF_IRQ_RAW_STAT_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x38)
#define QAIF_EE_WRDMA_OF_IRQ_CLR_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x40)
#define QAIF_EE_WRDMA_OF_IRQ_FORCE_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x48)
/* WRDMA Error Response Interrupts */
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x50)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, i)	qaif_wrdma_irq_reg_addr(v, i, 0x58)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_RAW_STAT(v, i)	qaif_wrdma_irq_reg_addr(v, i, 0x60)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, i)		qaif_wrdma_irq_reg_addr(v, i, 0x68)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_FORCE_REG(v, i)	qaif_wrdma_irq_reg_addr(v, i, 0x70)

static inline u32 qaif_dmacfg_reg(const struct qaif_variant *v,
				  u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_CFG_REG(v, i) :
			QAIF_CDC_WRDMA_CFG_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_CFG_REG(v, i) : QAIF_WRDMA_CFG_REG(v, i);
}

static inline u32 qaif_dmactl_reg(const struct qaif_variant *v,
				  u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_CTL_REG(v, i) :
			QAIF_CDC_WRDMA_CTL_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_CTL_REG(v, i) : QAIF_WRDMA_CTL_REG(v, i);
}

static inline u32 qaif_dma_shutdown_stat_reg(const struct qaif_variant *v,
					     u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_SHUTDOWN_STAT_REG(v, i) :
			QAIF_CDC_WRDMA_SHUTDOWN_STAT_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_SHUTDOWN_STAT_REG(v, i) :
		QAIF_WRDMA_SHUTDOWN_STAT_REG(v, i);
}

static inline u32 qaif_dmabuff_reg(const struct qaif_variant *v,
				   u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_BUFF_LEN_REG(v, i) :
			QAIF_CDC_WRDMA_BUFF_LEN_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_BUFF_LEN_REG(v, i) : QAIF_WRDMA_BUFF_LEN_REG(v, i);
}

static inline u32 qaif_dmacurr_reg(const struct qaif_variant *v,
				   u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_CURR_ADDR_REG(v, i) :
			QAIF_CDC_WRDMA_CURR_ADDR_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_CURR_ADDR_REG(v, i) : QAIF_WRDMA_CURR_ADDR_REG(v, i);
}

static inline u32 qaif_dmaper_cnt_reg(const struct qaif_variant *v,
				      u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_PERIOD_CNT_REG(v, i) :
			QAIF_CDC_WRDMA_PERIOD_CNT_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_PERIOD_CNT_REG(v, i) : QAIF_WRDMA_PERIOD_CNT_REG(v, i);
}

static inline u32 qaif_dmaper_len_reg(const struct qaif_variant *v,
				      u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_PERIOD_LEN_REG(v, i) :
			QAIF_CDC_WRDMA_PERIOD_LEN_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_PERIOD_LEN_REG(v, i) : QAIF_WRDMA_PERIOD_LEN_REG(v, i);
}

static inline u32 qaif_dmabase_reg(const struct qaif_variant *v,
				   u32 i, int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_BASE_ADDR_REG(v, i) :
			QAIF_CDC_WRDMA_BASE_ADDR_REG(v, i);
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_BASE_ADDR_REG(v, i) : QAIF_WRDMA_BASE_ADDR_REG(v, i);
}

static inline u32 qaif_sid_map_reg(int dir, unsigned int dai_id)
{
	if (qaif_is_cif_port(dai_id))
		return dir == SNDRV_PCM_STREAM_PLAYBACK ?
			QAIF_CDC_RDDMA_SID_MAP_REG :
			QAIF_CDC_WRDMA_SID_MAP_REG;
	return dir == SNDRV_PCM_STREAM_PLAYBACK ?
		QAIF_RDDMA_SID_MAP_REG : QAIF_WRDMA_SID_MAP_REG;
}

#endif /* SND_SOC_QCOM_QAIF_REG_H */

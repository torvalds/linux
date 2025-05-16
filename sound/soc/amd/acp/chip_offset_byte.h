/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */

#ifndef _ACP_IP_OFFSET_HEADER
#define _ACP_IP_OFFSET_HEADER

#define ACPAXI2AXI_ATU_CTRL                           0xC40
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1                0xC00
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_1                0xC04
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_2                0xC08
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_2                0xC0C
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_5                0xC20
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_5                0xC24

#define GRP1_OFFSET	0x0
#define GRP2_OFFSET	0x4000

#define ACP_PGFSM_CONTROL			0x141C
#define ACP_PGFSM_STATUS                        0x1420
#define ACP_SOFT_RESET                          0x1000
#define ACP_CONTROL                             0x1004
#define ACP_PIN_CONFIG				0x1440
#define ACP3X_PIN_CONFIG			0x1400

#define ACP_EXTERNAL_INTR_REG_ADDR(chip, offset, ctrl) \
	(chip->base + chip->rsrc->irq_reg_offset + offset + (ctrl * 0x04))

#define ACP_EXTERNAL_INTR_ENB(chip) ACP_EXTERNAL_INTR_REG_ADDR(chip, 0x0, 0x0)
#define ACP_EXTERNAL_INTR_CNTL(chip, ctrl) ACP_EXTERNAL_INTR_REG_ADDR(chip, 0x4, ctrl)
#define ACP_EXTERNAL_INTR_STAT(chip, ctrl) ACP_EXTERNAL_INTR_REG_ADDR(chip, \
	(0x4 + (chip->rsrc->no_of_ctrls * 0x04)), ctrl)

/* Registers from ACP_AUDIO_BUFFERS block */

#define ACP_I2S_REG_ADDR(acp_adata, addr) \
			 ((addr) + (acp_adata->rsrc->irqp_used * \
			 acp_adata->rsrc->irq_reg_offset))

#define ACP_I2S_RX_RINGBUFADDR(adata)               ACP_I2S_REG_ADDR(adata, 0x2000)
#define ACP_I2S_RX_RINGBUFSIZE(adata)               ACP_I2S_REG_ADDR(adata, 0x2004)
#define ACP_I2S_RX_LINKPOSITIONCNTR(adata)          ACP_I2S_REG_ADDR(adata, 0x2008)
#define ACP_I2S_RX_FIFOADDR(adata)                  ACP_I2S_REG_ADDR(adata, 0x200C)
#define ACP_I2S_RX_FIFOSIZE(adata)                  ACP_I2S_REG_ADDR(adata, 0x2010)
#define ACP_I2S_RX_DMA_SIZE(adata)                  ACP_I2S_REG_ADDR(adata, 0x2014)
#define ACP_I2S_RX_LINEARPOSITIONCNTR_HIGH(adata)   ACP_I2S_REG_ADDR(adata, 0x2018)
#define ACP_I2S_RX_LINEARPOSITIONCNTR_LOW(adata)    ACP_I2S_REG_ADDR(adata, 0x201C)
#define ACP_I2S_RX_INTR_WATERMARK_SIZE(adata)       ACP_I2S_REG_ADDR(adata, 0x2020)
#define ACP_I2S_TX_RINGBUFADDR(adata)               ACP_I2S_REG_ADDR(adata, 0x2024)
#define ACP_I2S_TX_RINGBUFSIZE(adata)               ACP_I2S_REG_ADDR(adata, 0x2028)
#define ACP_I2S_TX_LINKPOSITIONCNTR(adata)          ACP_I2S_REG_ADDR(adata, 0x202C)
#define ACP_I2S_TX_FIFOADDR(adata)                  ACP_I2S_REG_ADDR(adata, 0x2030)
#define ACP_I2S_TX_FIFOSIZE(adata)                  ACP_I2S_REG_ADDR(adata, 0x2034)
#define ACP_I2S_TX_DMA_SIZE(adata)                  ACP_I2S_REG_ADDR(adata, 0x2038)
#define ACP_I2S_TX_LINEARPOSITIONCNTR_HIGH(adata)   ACP_I2S_REG_ADDR(adata, 0x203C)
#define ACP_I2S_TX_LINEARPOSITIONCNTR_LOW(adata)    ACP_I2S_REG_ADDR(adata, 0x2040)
#define ACP_I2S_TX_INTR_WATERMARK_SIZE(adata)       ACP_I2S_REG_ADDR(adata, 0x2044)
#define ACP_BT_RX_RINGBUFADDR(adata)                ACP_I2S_REG_ADDR(adata, 0x2048)
#define ACP_BT_RX_RINGBUFSIZE(adata)                ACP_I2S_REG_ADDR(adata, 0x204C)
#define ACP_BT_RX_LINKPOSITIONCNTR(adata)           ACP_I2S_REG_ADDR(adata, 0x2050)
#define ACP_BT_RX_FIFOADDR(adata)                   ACP_I2S_REG_ADDR(adata, 0x2054)
#define ACP_BT_RX_FIFOSIZE(adata)                   ACP_I2S_REG_ADDR(adata, 0x2058)
#define ACP_BT_RX_DMA_SIZE(adata)                   ACP_I2S_REG_ADDR(adata, 0x205C)
#define ACP_BT_RX_LINEARPOSITIONCNTR_HIGH(adata)    ACP_I2S_REG_ADDR(adata, 0x2060)
#define ACP_BT_RX_LINEARPOSITIONCNTR_LOW(adata)     ACP_I2S_REG_ADDR(adata, 0x2064)
#define ACP_BT_RX_INTR_WATERMARK_SIZE(adata)        ACP_I2S_REG_ADDR(adata, 0x2068)
#define ACP_BT_TX_RINGBUFADDR(adata)                ACP_I2S_REG_ADDR(adata, 0x206C)
#define ACP_BT_TX_RINGBUFSIZE(adata)                ACP_I2S_REG_ADDR(adata, 0x2070)
#define ACP_BT_TX_LINKPOSITIONCNTR(adata)           ACP_I2S_REG_ADDR(adata, 0x2074)
#define ACP_BT_TX_FIFOADDR(adata)                   ACP_I2S_REG_ADDR(adata, 0x2078)
#define ACP_BT_TX_FIFOSIZE(adata)                   ACP_I2S_REG_ADDR(adata, 0x207C)
#define ACP_BT_TX_DMA_SIZE(adata)                   ACP_I2S_REG_ADDR(adata, 0x2080)
#define ACP_BT_TX_LINEARPOSITIONCNTR_HIGH(adata)    ACP_I2S_REG_ADDR(adata, 0x2084)
#define ACP_BT_TX_LINEARPOSITIONCNTR_LOW(adata)     ACP_I2S_REG_ADDR(adata, 0x2088)
#define ACP_BT_TX_INTR_WATERMARK_SIZE(adata)        ACP_I2S_REG_ADDR(adata, 0x208C)

#define ACP_HS_RX_RINGBUFADDR			      0x3A90
#define ACP_HS_RX_RINGBUFSIZE			      0x3A94
#define ACP_HS_RX_LINKPOSITIONCNTR		      0x3A98
#define ACP_HS_RX_FIFOADDR			      0x3A9C
#define ACP_HS_RX_FIFOSIZE			      0x3AA0
#define ACP_HS_RX_DMA_SIZE			      0x3AA4
#define ACP_HS_RX_LINEARPOSITIONCNTR_HIGH	      0x3AA8
#define ACP_HS_RX_LINEARPOSITIONCNTR_LOW	      0x3AAC
#define ACP_HS_RX_INTR_WATERMARK_SIZE		      0x3AB0
#define ACP_HS_TX_RINGBUFADDR			      0x3AB4
#define ACP_HS_TX_RINGBUFSIZE			      0x3AB8
#define ACP_HS_TX_LINKPOSITIONCNTR		      0x3ABC
#define ACP_HS_TX_FIFOADDR			      0x3AC0
#define ACP_HS_TX_FIFOSIZE			      0x3AC4
#define ACP_HS_TX_DMA_SIZE			      0x3AC8
#define ACP_HS_TX_LINEARPOSITIONCNTR_HIGH	      0x3ACC
#define ACP_HS_TX_LINEARPOSITIONCNTR_LOW	      0x3AD0
#define ACP_HS_TX_INTR_WATERMARK_SIZE		      0x3AD4

#define ACP_I2STDM_IER                                0x2400
#define ACP_I2STDM_IRER                               0x2404
#define ACP_I2STDM_RXFRMT                             0x2408
#define ACP_I2STDM_ITER                               0x240C
#define ACP_I2STDM_TXFRMT                             0x2410

/* Registers from ACP_BT_TDM block */

#define ACP_BTTDM_IER                                 0x2800
#define ACP_BTTDM_IRER                                0x2804
#define ACP_BTTDM_RXFRMT                              0x2808
#define ACP_BTTDM_ITER                                0x280C
#define ACP_BTTDM_TXFRMT                              0x2810

/* Registers from ACP_HS_TDM block */
#define ACP_HSTDM_IER                                 0x2814
#define ACP_HSTDM_IRER                                0x2818
#define ACP_HSTDM_RXFRMT                              0x281C
#define ACP_HSTDM_ITER                                0x2820
#define ACP_HSTDM_TXFRMT                              0x2824

/* Registers from ACP_WOV_PDM block */

#define ACP_WOV_PDM_ENABLE                            0x2C04
#define ACP_WOV_PDM_DMA_ENABLE                        0x2C08
#define ACP_WOV_RX_RINGBUFADDR                        0x2C0C
#define ACP_WOV_RX_RINGBUFSIZE                        0x2C10
#define ACP_WOV_RX_LINKPOSITIONCNTR                   0x2C14
#define ACP_WOV_RX_LINEARPOSITIONCNTR_HIGH            0x2C18
#define ACP_WOV_RX_LINEARPOSITIONCNTR_LOW             0x2C1C
#define ACP_WOV_RX_INTR_WATERMARK_SIZE                0x2C20
#define ACP_WOV_PDM_FIFO_FLUSH                        0x2C24
#define ACP_WOV_PDM_NO_OF_CHANNELS                    0x2C28
#define ACP_WOV_PDM_DECIMATION_FACTOR                 0x2C2C
#define ACP_WOV_PDM_VAD_CTRL                          0x2C30
#define ACP_WOV_BUFFER_STATUS                         0x2C58
#define ACP_WOV_MISC_CTRL                             0x2C5C
#define ACP_WOV_CLK_CTRL                              0x2C60
#define ACP_PDM_VAD_DYNAMIC_CLK_GATING_EN             0x2C64
#define ACP_WOV_ERROR_STATUS_REGISTER                 0x2C68

#define ACP_I2STDM0_MSTRCLKGEN			      0x2414
#define ACP_I2STDM1_MSTRCLKGEN			      0x2418
#define ACP_I2STDM2_MSTRCLKGEN			      0x241C
#endif

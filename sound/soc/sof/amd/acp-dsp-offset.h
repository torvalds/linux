/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */

#ifndef _ACP_DSP_IP_OFFSET_H
#define _ACP_DSP_IP_OFFSET_H

/* Registers from ACP_DMA_0 block */
#define ACP_DMA_CNTL_0				0x00
#define ACP_DMA_DSCR_STRT_IDX_0			0x20
#define ACP_DMA_DSCR_CNT_0			0x40
#define ACP_DMA_PRIO_0				0x60
#define ACP_DMA_CUR_DSCR_0			0x80
#define ACP_DMA_ERR_STS_0			0xC0
#define ACP_DMA_DESC_BASE_ADDR			0xE0
#define ACP_DMA_DESC_MAX_NUM_DSCR		0xE4
#define ACP_DMA_CH_STS				0xE8
#define ACP_DMA_CH_GROUP			0xEC
#define ACP_DMA_CH_RST_STS			0xF0

/* Registers from ACP_DSP_0 block */
#define ACP_DSP0_RUNSTALL			0x414

/* Registers from ACP_AXI2AXIATU block */
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1		0xC00
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_1		0xC04
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_2		0xC08
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_2		0xC0C
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_3		0xC10
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_3		0xC14
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_4		0xC18
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_4		0xC1C
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_5		0xC20
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_5		0xC24
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_6		0xC28
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_6		0xC2C
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_7		0xC30
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_7		0xC34
#define ACPAXI2AXI_ATU_PAGE_SIZE_GRP_8		0xC38
#define ACPAXI2AXI_ATU_BASE_ADDR_GRP_8		0xC3C
#define ACPAXI2AXI_ATU_CTRL			0xC40
#define ACP_SOFT_RESET				0x1000
#define ACP_CONTROL				0x1004

#define ACP3X_I2S_PIN_CONFIG			0x1400
#define ACP6X_I2S_PIN_CONFIG			0x1440

/* Registers offsets from ACP_PGFSM block */
#define ACP3X_PGFSM_BASE			0x141C
#define ACP6X_PGFSM_BASE                        0x1024
#define PGFSM_CONTROL_OFFSET			0x0
#define PGFSM_STATUS_OFFSET			0x4
#define ACP3X_CLKMUX_SEL			0x1424
#define ACP6X_CLKMUX_SEL			0x102C

/* Registers from ACP_INTR block */
#define ACP3X_EXT_INTR_STAT			0x1808
#define ACP6X_EXT_INTR_STAT                     0x1A0C

#define ACP3X_DSP_SW_INTR_BASE			0x1814
#define ACP6X_DSP_SW_INTR_BASE                  0x1808
#define DSP_SW_INTR_CNTL_OFFSET			0x0
#define DSP_SW_INTR_STAT_OFFSET			0x4
#define DSP_SW_INTR_TRIG_OFFSET			0x8
#define ACP_ERROR_STATUS			0x18C4
#define ACP3X_AXI2DAGB_SEM_0			0x1880
#define ACP6X_AXI2DAGB_SEM_0			0x1874

/* Registers from ACP_SHA block */
#define ACP_SHA_DSP_FW_QUALIFIER		0x1C70
#define ACP_SHA_DMA_CMD				0x1CB0
#define ACP_SHA_MSG_LENGTH			0x1CB4
#define ACP_SHA_DMA_STRT_ADDR			0x1CB8
#define ACP_SHA_DMA_DESTINATION_ADDR		0x1CBC
#define ACP_SHA_DMA_CMD_STS			0x1CC0
#define ACP_SHA_DMA_ERR_STATUS			0x1CC4
#define ACP_SHA_TRANSFER_BYTE_CNT		0x1CC8
#define ACP_SHA_PSP_ACK                         0x1C74

#define ACP_SCRATCH_REG_0			0x10000
#define ACP6X_DSP_FUSION_RUNSTALL		0x0644
#endif

/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

/*
 * Copyright (c) 2022 MediaTek Corporation. All rights reserved.
 *
 *  Header file for the mt8186 DSP register definition
 */

#ifndef __MT8186_H
#define __MT8186_H

struct mtk_adsp_chip_info;
struct snd_sof_dev;

#define DSP_REG_BAR			4
#define DSP_SECREG_BAR			5
#define DSP_BUSREG_BAR			6

/*****************************************************************************
 *                  R E G I S T E R       TABLE
 *****************************************************************************/
/* dsp cfg */
#define ADSP_CFGREG_SW_RSTN		0x0000
#define SW_DBG_RSTN_C0			BIT(0)
#define SW_RSTN_C0			BIT(4)
#define ADSP_HIFI_IO_CONFIG		0x000C
#define TRACEMEMREADY			BIT(15)
#define RUNSTALL			BIT(31)
#define ADSP_IRQ_MASK			0x0030
#define ADSP_DVFSRC_REQ			0x0040
#define ADSP_DDREN_REQ_0		0x0044
#define ADSP_SEMAPHORE			0x0064
#define ADSP_WDT_CON_C0			0x007C
#define ADSP_MBOX_IRQ_EN		0x009C
#define DSP_MBOX0_IRQ_EN		BIT(0)
#define DSP_MBOX1_IRQ_EN		BIT(1)
#define DSP_MBOX2_IRQ_EN		BIT(2)
#define DSP_MBOX3_IRQ_EN		BIT(3)
#define DSP_MBOX4_IRQ_EN		BIT(4)
#define DSP_PDEBUGPC			0x013C
#define ADSP_CK_EN			0x1000
#define CORE_CLK_EN			BIT(0)
#define COREDBG_EN			BIT(1)
#define TIMER_EN			BIT(3)
#define DMA_EN				BIT(4)
#define UART_EN				BIT(5)
#define ADSP_UART_CTRL			0x1010
#define UART_BCLK_CG			BIT(0)
#define UART_RSTN			BIT(3)

/* dsp sec */
#define ADSP_PRID			0x0
#define ADSP_ALTVEC_C0			0x04
#define ADSP_ALTVECSEL			0x0C
#define ADSP_ALTVECSEL_C0		BIT(1)

/* dsp bus */
#define ADSP_SRAM_POOL_CON		0x190
#define DSP_SRAM_POOL_PD_MASK		0xF00F /* [0:3] and [12:15] */
#define DSP_C0_EMI_MAP_ADDR		0xA00  /* ADSP Core0 To EMI Address Remap */
#define DSP_C0_DMAEMI_MAP_ADDR		0xA08  /* DMA0 To EMI Address Remap */

/* DSP memories */
#define MBOX_OFFSET			0x500000 /* DRAM */
#define MBOX_SIZE			0x1000   /* consistent with which in memory.h of sof fw */
#define DSP_DRAM_SIZE			0xA00000 /* 16M */

/*remap dram between AP and DSP view, 4KB aligned*/
#define SRAM_PHYS_BASE_FROM_DSP_VIEW	0x4E100000 /* MT8186 DSP view */
#define DRAM_PHYS_BASE_FROM_DSP_VIEW	0x60000000 /* MT8186 DSP view */
#define DRAM_REMAP_SHIFT		12
#define DRAM_REMAP_MASK			0xFFF

#define SIZE_SHARED_DRAM_DL			0x40000 /*Shared buffer for Downlink*/
#define SIZE_SHARED_DRAM_UL			0x40000 /*Shared buffer for Uplink*/
#define TOTAL_SIZE_SHARED_DRAM_FROM_TAIL	(SIZE_SHARED_DRAM_DL + SIZE_SHARED_DRAM_UL)

void mt8186_sof_hifixdsp_boot_sequence(struct snd_sof_dev *sdev, u32 boot_addr);
void mt8186_sof_hifixdsp_shutdown(struct snd_sof_dev *sdev);
#endif

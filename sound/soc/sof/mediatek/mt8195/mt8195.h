/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2021 MediaTek Corporation. All rights reserved.
 *
 *  Header file for the mt8195 DSP register definition
 */

#ifndef __MT8195_H
#define __MT8195_H

struct mtk_adsp_chip_info;
struct snd_sof_dev;

#define DSP_REG_BASE			0x10803000
#define SCP_CFGREG_BASE			0x10724000
#define DSP_SYSAO_BASE			0x1080C000

/*****************************************************************************
 *                  R E G I S T E R       TABLE
 *****************************************************************************/
#define DSP_JTAGMUX			0x0000
#define DSP_ALTRESETVEC			0x0004
#define DSP_PDEBUGDATA			0x0008
#define DSP_PDEBUGBUS0			0x000c
#define PDEBUG_ENABLE			BIT(0)
#define DSP_PDEBUGBUS1			0x0010
#define DSP_PDEBUGINST			0x0014
#define DSP_PDEBUGLS0STAT		0x0018
#define DSP_PDEBUGLS1STAT		0x001c
#define DSP_PDEBUGPC			0x0020
#define DSP_RESET_SW			0x0024 /*reset sw*/
#define ADSP_BRESET_SW			BIT(0)
#define ADSP_DRESET_SW			BIT(1)
#define ADSP_RUNSTALL			BIT(3)
#define STATVECTOR_SEL			BIT(4)
#define DSP_PFAULTBUS			0x0028
#define DSP_PFAULTINFO			0x002c
#define DSP_GPR00			0x0030
#define DSP_GPR01			0x0034
#define DSP_GPR02			0x0038
#define DSP_GPR03			0x003c
#define DSP_GPR04			0x0040
#define DSP_GPR05			0x0044
#define DSP_GPR06			0x0048
#define DSP_GPR07			0x004c
#define DSP_GPR08			0x0050
#define DSP_GPR09			0x0054
#define DSP_GPR0A			0x0058
#define DSP_GPR0B			0x005c
#define DSP_GPR0C			0x0060
#define DSP_GPR0D			0x0064
#define DSP_GPR0E			0x0068
#define DSP_GPR0F			0x006c
#define DSP_GPR10			0x0070
#define DSP_GPR11			0x0074
#define DSP_GPR12			0x0078
#define DSP_GPR13			0x007c
#define DSP_GPR14			0x0080
#define DSP_GPR15			0x0084
#define DSP_GPR16			0x0088
#define DSP_GPR17			0x008c
#define DSP_GPR18			0x0090
#define DSP_GPR19			0x0094
#define DSP_GPR1A			0x0098
#define DSP_GPR1B			0x009c
#define DSP_GPR1C			0x00a0
#define DSP_GPR1D			0x00a4
#define DSP_GPR1E			0x00a8
#define DSP_GPR1F			0x00ac
#define DSP_TCM_OFFSET			0x00b0    /* not used */
#define DSP_DDR_OFFSET			0x00b4    /* not used */
#define DSP_INTFDSP			0x00d0
#define DSP_INTFDSP_CLR			0x00d4
#define DSP_SRAM_PD_SW1			0x00d8
#define DSP_SRAM_PD_SW2			0x00dc
#define DSP_OCD				0x00e0
#define DSP_RG_DSP_IRQ_POL		0x00f0    /* not used */
#define DSP_DSP_IRQ_EN			0x00f4    /* not used */
#define DSP_DSP_IRQ_LEVEL		0x00f8    /* not used */
#define DSP_DSP_IRQ_STATUS		0x00fc    /* not used */
#define DSP_RG_INT2CIRQ			0x0114
#define DSP_RG_INT_POL_CTL0		0x0120
#define DSP_RG_INT_EN_CTL0		0x0130
#define DSP_RG_INT_LV_CTL0		0x0140
#define DSP_RG_INT_STATUS0		0x0150
#define DSP_PDEBUGSTATUS0		0x0200
#define DSP_PDEBUGSTATUS1		0x0204
#define DSP_PDEBUGSTATUS2		0x0208
#define DSP_PDEBUGSTATUS3		0x020c
#define DSP_PDEBUGSTATUS4		0x0210
#define DSP_PDEBUGSTATUS5		0x0214
#define DSP_PDEBUGSTATUS6		0x0218
#define DSP_PDEBUGSTATUS7		0x021c
#define DSP_DSP2PSRAM_PRIORITY		0x0220  /* not used */
#define DSP_AUDIO_DSP2SPM_INT		0x0224
#define DSP_AUDIO_DSP2SPM_INT_ACK	0x0228
#define DSP_AUDIO_DSP_DEBUG_SEL		0x022C
#define DSP_AUDIO_DSP_EMI_BASE_ADDR	0x02E0  /* not used */
#define DSP_AUDIO_DSP_SHARED_IRAM	0x02E4
#define DSP_AUDIO_DSP_CKCTRL_P2P_CK_CON	0x02F0
#define DSP_RG_SEMAPHORE00		0x0300
#define DSP_RG_SEMAPHORE01		0x0304
#define DSP_RG_SEMAPHORE02		0x0308
#define DSP_RG_SEMAPHORE03		0x030C
#define DSP_RG_SEMAPHORE04		0x0310
#define DSP_RG_SEMAPHORE05		0x0314
#define DSP_RG_SEMAPHORE06		0x0318
#define DSP_RG_SEMAPHORE07		0x031C
#define DSP_RESERVED_0			0x03F0
#define DSP_RESERVED_1			0x03F4

/* dsp wdt */
#define DSP_WDT_MODE			0x0400

/* dsp mbox */
#define DSP_MBOX_IN_CMD			0x00
#define DSP_MBOX_IN_CMD_CLR		0x04
#define DSP_MBOX_OUT_CMD		0x1c
#define DSP_MBOX_OUT_CMD_CLR		0x20
#define DSP_MBOX_IN_MSG0		0x08
#define DSP_MBOX_IN_MSG1		0x0C
#define DSP_MBOX_OUT_MSG0		0x24
#define DSP_MBOX_OUT_MSG1		0x28

/*dsp sys ao*/
#define ADSP_SRAM_POOL_CON		(DSP_SYSAO_BASE + 0x30)
#define DSP_SRAM_POOL_PD_MASK		0xf
#define DSP_EMI_MAP_ADDR		(DSP_SYSAO_BASE + 0x81c)

/* DSP memories */
#define MBOX_OFFSET	0x800000 /* DRAM */
#define MBOX_SIZE	0x1000 /* consistent with which in memory.h of sof fw */
#define DSP_DRAM_SIZE	0x1000000 /* 16M */

#define DSP_REG_BAR	4
#define DSP_MBOX0_BAR	5
#define DSP_MBOX1_BAR	6
#define DSP_MBOX2_BAR	7

#define TOTAL_SIZE_SHARED_SRAM_FROM_TAIL  0x0

#define SIZE_SHARED_DRAM_DL 0x40000 /*Shared buffer for Downlink*/
#define SIZE_SHARED_DRAM_UL 0x40000 /*Shared buffer for Uplink*/

#define TOTAL_SIZE_SHARED_DRAM_FROM_TAIL  \
	(SIZE_SHARED_DRAM_DL + SIZE_SHARED_DRAM_UL)

#define SRAM_PHYS_BASE_FROM_DSP_VIEW	0x40000000 /* MT8195 DSP view */
#define DRAM_PHYS_BASE_FROM_DSP_VIEW	0x60000000 /* MT8195 DSP view */

/*remap dram between AP and DSP view, 4KB aligned*/
#define DRAM_REMAP_SHIFT	12
#define DRAM_REMAP_MASK		(BIT(DRAM_REMAP_SHIFT) - 1)

void sof_hifixdsp_boot_sequence(struct snd_sof_dev *sdev, u32 boot_addr);
void sof_hifixdsp_shutdown(struct snd_sof_dev *sdev);
#endif

/*
 * mt2701-reg.h  --  Mediatek 2701 audio driver reg definition
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MT2701_REG_H_
#define _MT2701_REG_H_

#define AUDIO_TOP_CON0 0x0000
#define AUDIO_TOP_CON4 0x0010
#define AUDIO_TOP_CON5 0x0014
#define AFE_DAIBT_CON0 0x001c
#define AFE_MRGIF_CON 0x003c
#define ASMI_TIMING_CON1 0x0100
#define ASMO_TIMING_CON1 0x0104
#define PWR1_ASM_CON1 0x0108
#define ASYS_TOP_CON 0x0600
#define ASYS_I2SIN1_CON 0x0604
#define ASYS_I2SIN2_CON 0x0608
#define ASYS_I2SIN3_CON 0x060c
#define ASYS_I2SIN4_CON 0x0610
#define ASYS_I2SIN5_CON 0x0614
#define ASYS_I2SO1_CON 0x061C
#define ASYS_I2SO2_CON 0x0620
#define ASYS_I2SO3_CON 0x0624
#define ASYS_I2SO4_CON 0x0628
#define ASYS_I2SO5_CON 0x062c
#define PWR2_TOP_CON 0x0634
#define AFE_CONN0 0x06c0
#define AFE_CONN1 0x06c4
#define AFE_CONN2 0x06c8
#define AFE_CONN3 0x06cc
#define AFE_CONN14 0x06f8
#define AFE_CONN15 0x06fc
#define AFE_CONN16 0x0700
#define AFE_CONN17 0x0704
#define AFE_CONN18 0x0708
#define AFE_CONN19 0x070c
#define AFE_CONN20 0x0710
#define AFE_CONN21 0x0714
#define AFE_CONN22 0x0718
#define AFE_CONN23 0x071c
#define AFE_CONN24 0x0720
#define AFE_CONN41 0x0764
#define ASYS_IRQ1_CON 0x0780
#define ASYS_IRQ2_CON 0x0784
#define ASYS_IRQ3_CON 0x0788
#define ASYS_IRQ_CLR 0x07c0
#define ASYS_IRQ_STATUS 0x07c4
#define PWR2_ASM_CON1 0x1070
#define AFE_DAC_CON0 0x1200
#define AFE_DAC_CON1 0x1204
#define AFE_DAC_CON2 0x1208
#define AFE_DAC_CON3 0x120c
#define AFE_DAC_CON4 0x1210
#define AFE_MEMIF_HD_CON1 0x121c
#define AFE_MEMIF_PBUF_SIZE 0x1238
#define AFE_MEMIF_HD_CON0 0x123c
#define AFE_DL1_BASE 0x1240
#define AFE_DL1_CUR 0x1244
#define AFE_DL2_BASE 0x1250
#define AFE_DL2_CUR 0x1254
#define AFE_DL3_BASE 0x1260
#define AFE_DL3_CUR 0x1264
#define AFE_DL4_BASE 0x1270
#define AFE_DL4_CUR 0x1274
#define AFE_DL5_BASE 0x1280
#define AFE_DL5_CUR 0x1284
#define AFE_DLMCH_BASE 0x12a0
#define AFE_DLMCH_CUR 0x12a4
#define AFE_ARB1_BASE 0x12b0
#define AFE_ARB1_CUR 0x12b4
#define AFE_VUL_BASE 0x1300
#define AFE_VUL_CUR 0x130c
#define AFE_UL2_BASE 0x1310
#define AFE_UL2_END 0x1318
#define AFE_UL2_CUR 0x131c
#define AFE_UL3_BASE 0x1320
#define AFE_UL3_END 0x1328
#define AFE_UL3_CUR 0x132c
#define AFE_UL4_BASE 0x1330
#define AFE_UL4_END 0x1338
#define AFE_UL4_CUR 0x133c
#define AFE_UL5_BASE 0x1340
#define AFE_UL5_END 0x1348
#define AFE_UL5_CUR 0x134c
#define AFE_DAI_BASE 0x1370
#define AFE_DAI_CUR 0x137c

/* AFE_DAIBT_CON0 (0x001c) */
#define AFE_DAIBT_CON0_DAIBT_EN		(0x1 << 0)
#define AFE_DAIBT_CON0_BT_FUNC_EN	(0x1 << 1)
#define AFE_DAIBT_CON0_BT_FUNC_RDY	(0x1 << 3)
#define AFE_DAIBT_CON0_BT_WIDE_MODE_EN	(0x1 << 9)
#define AFE_DAIBT_CON0_MRG_USE		(0x1 << 12)

/* PWR1_ASM_CON1 (0x0108) */
#define PWR1_ASM_CON1_INIT_VAL		(0x492)

/* AFE_MRGIF_CON (0x003c) */
#define AFE_MRGIF_CON_MRG_EN		(0x1 << 0)
#define AFE_MRGIF_CON_MRG_I2S_EN	(0x1 << 16)
#define AFE_MRGIF_CON_I2S_MODE_MASK	(0xf << 20)
#define AFE_MRGIF_CON_I2S_MODE_32K	(0x4 << 20)

/* ASYS_TOP_CON (0x0600) */
#define ASYS_TOP_CON_ASYS_TIMING_ON		(0x3 << 0)

/* PWR2_ASM_CON1 (0x1070) */
#define PWR2_ASM_CON1_INIT_VAL		(0x492492)

/* AFE_DAC_CON0 (0x1200) */
#define AFE_DAC_CON0_AFE_ON		(0x1 << 0)

/* AFE_MEMIF_PBUF_SIZE (0x1238) */
#define AFE_MEMIF_PBUF_SIZE_DLM_MASK		(0x1 << 29)
#define AFE_MEMIF_PBUF_SIZE_PAIR_INTERLEAVE	(0x0 << 29)
#define AFE_MEMIF_PBUF_SIZE_FULL_INTERLEAVE	(0x1 << 29)
#define DLMCH_BIT_WIDTH_MASK			(0x1 << 28)
#define AFE_MEMIF_PBUF_SIZE_DLM_CH_MASK		(0xf << 24)
#define AFE_MEMIF_PBUF_SIZE_DLM_CH(x)		((x) << 24)
#define AFE_MEMIF_PBUF_SIZE_DLM_BYTE_MASK	(0x3 << 12)
#define AFE_MEMIF_PBUF_SIZE_DLM_32BYTES		(0x1 << 12)

/* I2S in/out register bit control */
#define ASYS_I2S_CON_FS			(0x1f << 8)
#define ASYS_I2S_CON_FS_SET(x)		((x) << 8)
#define ASYS_I2S_CON_RESET		(0x1 << 30)
#define ASYS_I2S_CON_I2S_EN		(0x1 << 0)
#define ASYS_I2S_CON_I2S_COUPLE_MODE	(0x1 << 17)
/* 0:EIAJ 1:I2S */
#define ASYS_I2S_CON_I2S_MODE		(0x1 << 3)
#define ASYS_I2S_CON_WIDE_MODE		(0x1 << 1)
#define ASYS_I2S_CON_WIDE_MODE_SET(x)	((x) << 1)
#define ASYS_I2S_IN_PHASE_FIX		(0x1 << 31)

#endif

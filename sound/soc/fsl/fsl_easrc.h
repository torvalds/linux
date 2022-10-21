/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 NXP
 */

#ifndef _FSL_EASRC_H
#define _FSL_EASRC_H

#include <sound/asound.h>
#include <linux/dma/imx-dma.h>

#include "fsl_asrc_common.h"

/* EASRC Register Map */

/* ASRC Input Write FIFO */
#define REG_EASRC_WRFIFO(ctx)		(0x000 + 4 * (ctx))
/* ASRC Output Read FIFO */
#define REG_EASRC_RDFIFO(ctx)		(0x010 + 4 * (ctx))
/* ASRC Context Control */
#define REG_EASRC_CC(ctx)		(0x020 + 4 * (ctx))
/* ASRC Context Control Extended 1 */
#define REG_EASRC_CCE1(ctx)		(0x030 + 4 * (ctx))
/* ASRC Context Control Extended 2 */
#define REG_EASRC_CCE2(ctx)		(0x040 + 4 * (ctx))
/* ASRC Control Input Access */
#define REG_EASRC_CIA(ctx)		(0x050 + 4 * (ctx))
/* ASRC Datapath Processor Control Slot0 */
#define REG_EASRC_DPCS0R0(ctx)		(0x060 + 4 * (ctx))
#define REG_EASRC_DPCS0R1(ctx)		(0x070 + 4 * (ctx))
#define REG_EASRC_DPCS0R2(ctx)		(0x080 + 4 * (ctx))
#define REG_EASRC_DPCS0R3(ctx)		(0x090 + 4 * (ctx))
/* ASRC Datapath Processor Control Slot1 */
#define REG_EASRC_DPCS1R0(ctx)		(0x0A0 + 4 * (ctx))
#define REG_EASRC_DPCS1R1(ctx)		(0x0B0 + 4 * (ctx))
#define REG_EASRC_DPCS1R2(ctx)		(0x0C0 + 4 * (ctx))
#define REG_EASRC_DPCS1R3(ctx)		(0x0D0 + 4 * (ctx))
/* ASRC Context Output Control */
#define REG_EASRC_COC(ctx)		(0x0E0 + 4 * (ctx))
/* ASRC Control Output Access */
#define REG_EASRC_COA(ctx)		(0x0F0 + 4 * (ctx))
/* ASRC Sample FIFO Status */
#define REG_EASRC_SFS(ctx)		(0x100 + 4 * (ctx))
/* ASRC Resampling Ratio Low */
#define REG_EASRC_RRL(ctx)		(0x110 + 8 * (ctx))
/* ASRC Resampling Ratio High */
#define REG_EASRC_RRH(ctx)		(0x114 + 8 * (ctx))
/* ASRC Resampling Ratio Update Control */
#define REG_EASRC_RUC(ctx)		(0x130 + 4 * (ctx))
/* ASRC Resampling Ratio Update Rate */
#define REG_EASRC_RUR(ctx)		(0x140 + 4 * (ctx))
/* ASRC Resampling Center Tap Coefficient Low */
#define REG_EASRC_RCTCL			(0x150)
/* ASRC Resampling Center Tap Coefficient High */
#define REG_EASRC_RCTCH			(0x154)
/* ASRC Prefilter Coefficient FIFO */
#define REG_EASRC_PCF(ctx)		(0x160 + 4 * (ctx))
/* ASRC Context Resampling Coefficient Memory */
#define REG_EASRC_CRCM			0x170
/* ASRC Context Resampling Coefficient Control*/
#define REG_EASRC_CRCC			0x174
/* ASRC Interrupt Control */
#define REG_EASRC_IRQC			0x178
/* ASRC Interrupt Status Flags */
#define REG_EASRC_IRQF			0x17C
/* ASRC Channel Status 0 */
#define REG_EASRC_CS0(ctx)		(0x180 + 4 * (ctx))
/* ASRC Channel Status 1 */
#define REG_EASRC_CS1(ctx)		(0x190 + 4 * (ctx))
/* ASRC Channel Status 2 */
#define REG_EASRC_CS2(ctx)		(0x1A0 + 4 * (ctx))
/* ASRC Channel Status 3 */
#define REG_EASRC_CS3(ctx)		(0x1B0 + 4 * (ctx))
/* ASRC Channel Status 4 */
#define REG_EASRC_CS4(ctx)		(0x1C0 + 4 * (ctx))
/* ASRC Channel Status 5 */
#define REG_EASRC_CS5(ctx)		(0x1D0 + 4 * (ctx))
/* ASRC Debug Control Register */
#define REG_EASRC_DBGC			0x1E0
/* ASRC Debug Status Register */
#define REG_EASRC_DBGS			0x1E4

#define REG_EASRC_FIFO(x, ctx)		(x == IN ? REG_EASRC_WRFIFO(ctx) \
						: REG_EASRC_RDFIFO(ctx))

/* ASRC Context Control (CC) */
#define EASRC_CC_EN_SHIFT		31
#define EASRC_CC_EN_MASK		BIT(EASRC_CC_EN_SHIFT)
#define EASRC_CC_EN			BIT(EASRC_CC_EN_SHIFT)
#define EASRC_CC_STOP_SHIFT		29
#define EASRC_CC_STOP_MASK		BIT(EASRC_CC_STOP_SHIFT)
#define EASRC_CC_STOP			BIT(EASRC_CC_STOP_SHIFT)
#define EASRC_CC_FWMDE_SHIFT		28
#define EASRC_CC_FWMDE_MASK		BIT(EASRC_CC_FWMDE_SHIFT)
#define EASRC_CC_FWMDE			BIT(EASRC_CC_FWMDE_SHIFT)
#define EASRC_CC_FIFO_WTMK_SHIFT	16
#define EASRC_CC_FIFO_WTMK_WIDTH	7
#define EASRC_CC_FIFO_WTMK_MASK		((BIT(EASRC_CC_FIFO_WTMK_WIDTH) - 1) \
					 << EASRC_CC_FIFO_WTMK_SHIFT)
#define EASRC_CC_FIFO_WTMK(v)		(((v) << EASRC_CC_FIFO_WTMK_SHIFT) \
					 & EASRC_CC_FIFO_WTMK_MASK)
#define EASRC_CC_SAMPLE_POS_SHIFT	11
#define EASRC_CC_SAMPLE_POS_WIDTH	5
#define EASRC_CC_SAMPLE_POS_MASK	((BIT(EASRC_CC_SAMPLE_POS_WIDTH) - 1) \
					 << EASRC_CC_SAMPLE_POS_SHIFT)
#define EASRC_CC_SAMPLE_POS(v)		(((v) << EASRC_CC_SAMPLE_POS_SHIFT) \
					 & EASRC_CC_SAMPLE_POS_MASK)
#define EASRC_CC_ENDIANNESS_SHIFT	10
#define EASRC_CC_ENDIANNESS_MASK	BIT(EASRC_CC_ENDIANNESS_SHIFT)
#define EASRC_CC_ENDIANNESS		BIT(EASRC_CC_ENDIANNESS_SHIFT)
#define EASRC_CC_BPS_SHIFT		8
#define EASRC_CC_BPS_WIDTH		2
#define EASRC_CC_BPS_MASK		((BIT(EASRC_CC_BPS_WIDTH) - 1) \
					 << EASRC_CC_BPS_SHIFT)
#define EASRC_CC_BPS(v)			(((v) << EASRC_CC_BPS_SHIFT) \
					 & EASRC_CC_BPS_MASK)
#define EASRC_CC_FMT_SHIFT		7
#define EASRC_CC_FMT_MASK		BIT(EASRC_CC_FMT_SHIFT)
#define EASRC_CC_FMT			BIT(EASRC_CC_FMT_SHIFT)
#define EASRC_CC_INSIGN_SHIFT		6
#define EASRC_CC_INSIGN_MASK		BIT(EASRC_CC_INSIGN_SHIFT)
#define EASRC_CC_INSIGN			BIT(EASRC_CC_INSIGN_SHIFT)
#define EASRC_CC_CHEN_SHIFT		0
#define EASRC_CC_CHEN_WIDTH		5
#define EASRC_CC_CHEN_MASK		((BIT(EASRC_CC_CHEN_WIDTH) - 1) \
					 << EASRC_CC_CHEN_SHIFT)
#define EASRC_CC_CHEN(v)		(((v) << EASRC_CC_CHEN_SHIFT) \
					 & EASRC_CC_CHEN_MASK)

/* ASRC Context Control Extended 1 (CCE1) */
#define EASRC_CCE1_COEF_WS_SHIFT	25
#define EASRC_CCE1_COEF_WS_MASK		BIT(EASRC_CCE1_COEF_WS_SHIFT)
#define EASRC_CCE1_COEF_WS		BIT(EASRC_CCE1_COEF_WS_SHIFT)
#define EASRC_CCE1_COEF_MEM_RST_SHIFT	24
#define EASRC_CCE1_COEF_MEM_RST_MASK	BIT(EASRC_CCE1_COEF_MEM_RST_SHIFT)
#define EASRC_CCE1_COEF_MEM_RST		BIT(EASRC_CCE1_COEF_MEM_RST_SHIFT)
#define EASRC_CCE1_PF_EXP_SHIFT		16
#define EASRC_CCE1_PF_EXP_WIDTH		8
#define EASRC_CCE1_PF_EXP_MASK		((BIT(EASRC_CCE1_PF_EXP_WIDTH) - 1) \
					 << EASRC_CCE1_PF_EXP_SHIFT)
#define EASRC_CCE1_PF_EXP(v)		(((v) << EASRC_CCE1_PF_EXP_SHIFT) \
					 & EASRC_CCE1_PF_EXP_MASK)
#define EASRC_CCE1_PF_ST1_WBFP_SHIFT	9
#define EASRC_CCE1_PF_ST1_WBFP_MASK	BIT(EASRC_CCE1_PF_ST1_WBFP_SHIFT)
#define EASRC_CCE1_PF_ST1_WBFP		BIT(EASRC_CCE1_PF_ST1_WBFP_SHIFT)
#define EASRC_CCE1_PF_TSEN_SHIFT	8
#define EASRC_CCE1_PF_TSEN_MASK		BIT(EASRC_CCE1_PF_TSEN_SHIFT)
#define EASRC_CCE1_PF_TSEN		BIT(EASRC_CCE1_PF_TSEN_SHIFT)
#define EASRC_CCE1_RS_BYPASS_SHIFT	7
#define EASRC_CCE1_RS_BYPASS_MASK	BIT(EASRC_CCE1_RS_BYPASS_SHIFT)
#define EASRC_CCE1_RS_BYPASS		BIT(EASRC_CCE1_RS_BYPASS_SHIFT)
#define EASRC_CCE1_PF_BYPASS_SHIFT	6
#define EASRC_CCE1_PF_BYPASS_MASK	BIT(EASRC_CCE1_PF_BYPASS_SHIFT)
#define EASRC_CCE1_PF_BYPASS		BIT(EASRC_CCE1_PF_BYPASS_SHIFT)
#define EASRC_CCE1_RS_STOP_SHIFT	5
#define EASRC_CCE1_RS_STOP_MASK		BIT(EASRC_CCE1_RS_STOP_SHIFT)
#define EASRC_CCE1_RS_STOP		BIT(EASRC_CCE1_RS_STOP_SHIFT)
#define EASRC_CCE1_PF_STOP_SHIFT	4
#define EASRC_CCE1_PF_STOP_MASK		BIT(EASRC_CCE1_PF_STOP_SHIFT)
#define EASRC_CCE1_PF_STOP		BIT(EASRC_CCE1_PF_STOP_SHIFT)
#define EASRC_CCE1_RS_INIT_SHIFT	2
#define EASRC_CCE1_RS_INIT_WIDTH	2
#define EASRC_CCE1_RS_INIT_MASK		((BIT(EASRC_CCE1_RS_INIT_WIDTH) - 1) \
					 << EASRC_CCE1_RS_INIT_SHIFT)
#define EASRC_CCE1_RS_INIT(v)		(((v) << EASRC_CCE1_RS_INIT_SHIFT) \
					 & EASRC_CCE1_RS_INIT_MASK)
#define EASRC_CCE1_PF_INIT_SHIFT	0
#define EASRC_CCE1_PF_INIT_WIDTH	2
#define EASRC_CCE1_PF_INIT_MASK		((BIT(EASRC_CCE1_PF_INIT_WIDTH) - 1) \
					 << EASRC_CCE1_PF_INIT_SHIFT)
#define EASRC_CCE1_PF_INIT(v)		(((v) << EASRC_CCE1_PF_INIT_SHIFT) \
					 & EASRC_CCE1_PF_INIT_MASK)

/* ASRC Context Control Extended 2 (CCE2) */
#define EASRC_CCE2_ST2_TAPS_SHIFT	16
#define EASRC_CCE2_ST2_TAPS_WIDTH	9
#define EASRC_CCE2_ST2_TAPS_MASK	((BIT(EASRC_CCE2_ST2_TAPS_WIDTH) - 1) \
					 << EASRC_CCE2_ST2_TAPS_SHIFT)
#define EASRC_CCE2_ST2_TAPS(v)		(((v) << EASRC_CCE2_ST2_TAPS_SHIFT) \
					 & EASRC_CCE2_ST2_TAPS_MASK)
#define EASRC_CCE2_ST1_TAPS_SHIFT	0
#define EASRC_CCE2_ST1_TAPS_WIDTH	9
#define EASRC_CCE2_ST1_TAPS_MASK	((BIT(EASRC_CCE2_ST1_TAPS_WIDTH) - 1) \
					 << EASRC_CCE2_ST1_TAPS_SHIFT)
#define EASRC_CCE2_ST1_TAPS(v)		(((v) << EASRC_CCE2_ST1_TAPS_SHIFT) \
					 & EASRC_CCE2_ST1_TAPS_MASK)

/* ASRC Control Input Access (CIA) */
#define EASRC_CIA_ITER_SHIFT		16
#define EASRC_CIA_ITER_WIDTH		6
#define EASRC_CIA_ITER_MASK		((BIT(EASRC_CIA_ITER_WIDTH) - 1) \
					 << EASRC_CIA_ITER_SHIFT)
#define EASRC_CIA_ITER(v)		(((v) << EASRC_CIA_ITER_SHIFT) \
					 & EASRC_CIA_ITER_MASK)
#define EASRC_CIA_GRLEN_SHIFT		8
#define EASRC_CIA_GRLEN_WIDTH		6
#define EASRC_CIA_GRLEN_MASK		((BIT(EASRC_CIA_GRLEN_WIDTH) - 1) \
					 << EASRC_CIA_GRLEN_SHIFT)
#define EASRC_CIA_GRLEN(v)		(((v) << EASRC_CIA_GRLEN_SHIFT) \
					 & EASRC_CIA_GRLEN_MASK)
#define EASRC_CIA_ACCLEN_SHIFT		0
#define EASRC_CIA_ACCLEN_WIDTH		6
#define EASRC_CIA_ACCLEN_MASK		((BIT(EASRC_CIA_ACCLEN_WIDTH) - 1) \
					 << EASRC_CIA_ACCLEN_SHIFT)
#define EASRC_CIA_ACCLEN(v)		(((v) << EASRC_CIA_ACCLEN_SHIFT) \
					 & EASRC_CIA_ACCLEN_MASK)

/* ASRC Datapath Processor Control Slot0 Register0 (DPCS0R0) */
#define EASRC_DPCS0R0_MAXCH_SHIFT	24
#define EASRC_DPCS0R0_MAXCH_WIDTH	5
#define EASRC_DPCS0R0_MAXCH_MASK	((BIT(EASRC_DPCS0R0_MAXCH_WIDTH) - 1) \
					 << EASRC_DPCS0R0_MAXCH_SHIFT)
#define EASRC_DPCS0R0_MAXCH(v)		(((v) << EASRC_DPCS0R0_MAXCH_SHIFT) \
					 & EASRC_DPCS0R0_MAXCH_MASK)
#define EASRC_DPCS0R0_MINCH_SHIFT	16
#define EASRC_DPCS0R0_MINCH_WIDTH	5
#define EASRC_DPCS0R0_MINCH_MASK	((BIT(EASRC_DPCS0R0_MINCH_WIDTH) - 1) \
					 << EASRC_DPCS0R0_MINCH_SHIFT)
#define EASRC_DPCS0R0_MINCH(v)		(((v) << EASRC_DPCS0R0_MINCH_SHIFT) \
					 & EASRC_DPCS0R0_MINCH_MASK)
#define EASRC_DPCS0R0_NUMCH_SHIFT	8
#define EASRC_DPCS0R0_NUMCH_WIDTH	5
#define EASRC_DPCS0R0_NUMCH_MASK	((BIT(EASRC_DPCS0R0_NUMCH_WIDTH) - 1) \
					 << EASRC_DPCS0R0_NUMCH_SHIFT)
#define EASRC_DPCS0R0_NUMCH(v)		(((v) << EASRC_DPCS0R0_NUMCH_SHIFT) \
					 & EASRC_DPCS0R0_NUMCH_MASK)
#define EASRC_DPCS0R0_CTXNUM_SHIFT	1
#define EASRC_DPCS0R0_CTXNUM_WIDTH	2
#define EASRC_DPCS0R0_CTXNUM_MASK	((BIT(EASRC_DPCS0R0_CTXNUM_WIDTH) - 1) \
					 << EASRC_DPCS0R0_CTXNUM_SHIFT)
#define EASRC_DPCS0R0_CTXNUM(v)		(((v) << EASRC_DPCS0R0_CTXNUM_SHIFT) \
					 & EASRC_DPCS0R0_CTXNUM_MASK)
#define EASRC_DPCS0R0_EN_SHIFT		0
#define EASRC_DPCS0R0_EN_MASK		BIT(EASRC_DPCS0R0_EN_SHIFT)
#define EASRC_DPCS0R0_EN		BIT(EASRC_DPCS0R0_EN_SHIFT)

/* ASRC Datapath Processor Control Slot0 Register1 (DPCS0R1) */
#define EASRC_DPCS0R1_ST1_EXP_SHIFT	0
#define EASRC_DPCS0R1_ST1_EXP_WIDTH	13
#define EASRC_DPCS0R1_ST1_EXP_MASK	((BIT(EASRC_DPCS0R1_ST1_EXP_WIDTH) - 1) \
					 << EASRC_DPCS0R1_ST1_EXP_SHIFT)
#define EASRC_DPCS0R1_ST1_EXP(v)	(((v) << EASRC_DPCS0R1_ST1_EXP_SHIFT) \
					 & EASRC_DPCS0R1_ST1_EXP_MASK)

/* ASRC Datapath Processor Control Slot0 Register2 (DPCS0R2) */
#define EASRC_DPCS0R2_ST1_MA_SHIFT	16
#define EASRC_DPCS0R2_ST1_MA_WIDTH	13
#define EASRC_DPCS0R2_ST1_MA_MASK	((BIT(EASRC_DPCS0R2_ST1_MA_WIDTH) - 1) \
					 << EASRC_DPCS0R2_ST1_MA_SHIFT)
#define EASRC_DPCS0R2_ST1_MA(v)		(((v) << EASRC_DPCS0R2_ST1_MA_SHIFT) \
					 & EASRC_DPCS0R2_ST1_MA_MASK)
#define EASRC_DPCS0R2_ST1_SA_SHIFT	0
#define EASRC_DPCS0R2_ST1_SA_WIDTH	13
#define EASRC_DPCS0R2_ST1_SA_MASK	((BIT(EASRC_DPCS0R2_ST1_SA_WIDTH) - 1) \
					 << EASRC_DPCS0R2_ST1_SA_SHIFT)
#define EASRC_DPCS0R2_ST1_SA(v)		(((v) << EASRC_DPCS0R2_ST1_SA_SHIFT) \
					 & EASRC_DPCS0R2_ST1_SA_MASK)

/* ASRC Datapath Processor Control Slot0 Register3 (DPCS0R3) */
#define EASRC_DPCS0R3_ST2_MA_SHIFT	16
#define EASRC_DPCS0R3_ST2_MA_WIDTH	13
#define EASRC_DPCS0R3_ST2_MA_MASK	((BIT(EASRC_DPCS0R3_ST2_MA_WIDTH) - 1) \
					 << EASRC_DPCS0R3_ST2_MA_SHIFT)
#define EASRC_DPCS0R3_ST2_MA(v)		(((v) << EASRC_DPCS0R3_ST2_MA_SHIFT) \
					 & EASRC_DPCS0R3_ST2_MA_MASK)
#define EASRC_DPCS0R3_ST2_SA_SHIFT	0
#define EASRC_DPCS0R3_ST2_SA_WIDTH	13
#define EASRC_DPCS0R3_ST2_SA_MASK	((BIT(EASRC_DPCS0R3_ST2_SA_WIDTH) - 1) \
					 << EASRC_DPCS0R3_ST2_SA_SHIFT)
#define EASRC_DPCS0R3_ST2_SA(v)		(((v) << EASRC_DPCS0R3_ST2_SA_SHIFT) \
						 & EASRC_DPCS0R3_ST2_SA_MASK)

/* ASRC Context Output Control (COC) */
#define EASRC_COC_FWMDE_SHIFT		28
#define EASRC_COC_FWMDE_MASK		BIT(EASRC_COC_FWMDE_SHIFT)
#define EASRC_COC_FWMDE			BIT(EASRC_COC_FWMDE_SHIFT)
#define EASRC_COC_FIFO_WTMK_SHIFT	16
#define EASRC_COC_FIFO_WTMK_WIDTH	7
#define EASRC_COC_FIFO_WTMK_MASK	((BIT(EASRC_COC_FIFO_WTMK_WIDTH) - 1) \
					 << EASRC_COC_FIFO_WTMK_SHIFT)
#define EASRC_COC_FIFO_WTMK(v)		(((v) << EASRC_COC_FIFO_WTMK_SHIFT) \
					 & EASRC_COC_FIFO_WTMK_MASK)
#define EASRC_COC_SAMPLE_POS_SHIFT	11
#define EASRC_COC_SAMPLE_POS_WIDTH	5
#define EASRC_COC_SAMPLE_POS_MASK	((BIT(EASRC_COC_SAMPLE_POS_WIDTH) - 1) \
					 << EASRC_COC_SAMPLE_POS_SHIFT)
#define EASRC_COC_SAMPLE_POS(v)		(((v) << EASRC_COC_SAMPLE_POS_SHIFT) \
					 & EASRC_COC_SAMPLE_POS_MASK)
#define EASRC_COC_ENDIANNESS_SHIFT	10
#define EASRC_COC_ENDIANNESS_MASK	BIT(EASRC_COC_ENDIANNESS_SHIFT)
#define EASRC_COC_ENDIANNESS		BIT(EASRC_COC_ENDIANNESS_SHIFT)
#define EASRC_COC_BPS_SHIFT		8
#define EASRC_COC_BPS_WIDTH		2
#define EASRC_COC_BPS_MASK		((BIT(EASRC_COC_BPS_WIDTH) - 1) \
					 << EASRC_COC_BPS_SHIFT)
#define EASRC_COC_BPS(v)		(((v) << EASRC_COC_BPS_SHIFT) \
					 & EASRC_COC_BPS_MASK)
#define EASRC_COC_FMT_SHIFT		7
#define EASRC_COC_FMT_MASK		BIT(EASRC_COC_FMT_SHIFT)
#define EASRC_COC_FMT			BIT(EASRC_COC_FMT_SHIFT)
#define EASRC_COC_OUTSIGN_SHIFT		6
#define EASRC_COC_OUTSIGN_MASK		BIT(EASRC_COC_OUTSIGN_SHIFT)
#define EASRC_COC_OUTSIGN_OUT		BIT(EASRC_COC_OUTSIGN_SHIFT)
#define EASRC_COC_IEC_VDATA_SHIFT	2
#define EASRC_COC_IEC_VDATA_MASK	BIT(EASRC_COC_IEC_VDATA_SHIFT)
#define EASRC_COC_IEC_VDATA		BIT(EASRC_COC_IEC_VDATA_SHIFT)
#define EASRC_COC_IEC_EN_SHIFT		1
#define EASRC_COC_IEC_EN_MASK		BIT(EASRC_COC_IEC_EN_SHIFT)
#define EASRC_COC_IEC_EN		BIT(EASRC_COC_IEC_EN_SHIFT)
#define EASRC_COC_DITHER_EN_SHIFT	0
#define EASRC_COC_DITHER_EN_MASK	BIT(EASRC_COC_DITHER_EN_SHIFT)
#define EASRC_COC_DITHER_EN		BIT(EASRC_COC_DITHER_EN_SHIFT)

/* ASRC Control Output Access (COA) */
#define EASRC_COA_ITER_SHIFT		16
#define EASRC_COA_ITER_WIDTH		6
#define EASRC_COA_ITER_MASK		((BIT(EASRC_COA_ITER_WIDTH) - 1) \
					 << EASRC_COA_ITER_SHIFT)
#define EASRC_COA_ITER(v)		(((v) << EASRC_COA_ITER_SHIFT) \
					 & EASRC_COA_ITER_MASK)
#define EASRC_COA_GRLEN_SHIFT		8
#define EASRC_COA_GRLEN_WIDTH		6
#define EASRC_COA_GRLEN_MASK		((BIT(EASRC_COA_GRLEN_WIDTH) - 1) \
					 << EASRC_COA_GRLEN_SHIFT)
#define EASRC_COA_GRLEN(v)		(((v) << EASRC_COA_GRLEN_SHIFT) \
					 & EASRC_COA_GRLEN_MASK)
#define EASRC_COA_ACCLEN_SHIFT		0
#define EASRC_COA_ACCLEN_WIDTH		6
#define EASRC_COA_ACCLEN_MASK		((BIT(EASRC_COA_ACCLEN_WIDTH) - 1) \
					 << EASRC_COA_ACCLEN_SHIFT)
#define EASRC_COA_ACCLEN(v)		(((v) << EASRC_COA_ACCLEN_SHIFT) \
					 & EASRC_COA_ACCLEN_MASK)

/* ASRC Sample FIFO Status (SFS) */
#define EASRC_SFS_IWTMK_SHIFT		23
#define EASRC_SFS_IWTMK_MASK		BIT(EASRC_SFS_IWTMK_SHIFT)
#define EASRC_SFS_IWTMK			BIT(EASRC_SFS_IWTMK_SHIFT)
#define EASRC_SFS_NSGI_SHIFT		16
#define EASRC_SFS_NSGI_WIDTH		7
#define EASRC_SFS_NSGI_MASK		((BIT(EASRC_SFS_NSGI_WIDTH) - 1) \
					 << EASRC_SFS_NSGI_SHIFT)
#define EASRC_SFS_NSGI(v)		(((v) << EASRC_SFS_NSGI_SHIFT) \
					 & EASRC_SFS_NSGI_MASK)
#define EASRC_SFS_OWTMK_SHIFT		7
#define EASRC_SFS_OWTMK_MASK		BIT(EASRC_SFS_OWTMK_SHIFT)
#define EASRC_SFS_OWTMK			BIT(EASRC_SFS_OWTMK_SHIFT)
#define EASRC_SFS_NSGO_SHIFT		0
#define EASRC_SFS_NSGO_WIDTH		7
#define EASRC_SFS_NSGO_MASK		((BIT(EASRC_SFS_NSGO_WIDTH) - 1) \
					 << EASRC_SFS_NSGO_SHIFT)
#define EASRC_SFS_NSGO(v)		(((v) << EASRC_SFS_NSGO_SHIFT) \
					 & EASRC_SFS_NSGO_MASK)

/* ASRC Resampling Ratio Low (RRL) */
#define EASRC_RRL_RS_RL_SHIFT		0
#define EASRC_RRL_RS_RL_WIDTH		32
#define EASRC_RRL_RS_RL(v)		((v) << EASRC_RRL_RS_RL_SHIFT)

/* ASRC Resampling Ratio High (RRH) */
#define EASRC_RRH_RS_VLD_SHIFT		31
#define EASRC_RRH_RS_VLD_MASK		BIT(EASRC_RRH_RS_VLD_SHIFT)
#define EASRC_RRH_RS_VLD		BIT(EASRC_RRH_RS_VLD_SHIFT)
#define EASRC_RRH_RS_RH_SHIFT		0
#define EASRC_RRH_RS_RH_WIDTH		12
#define EASRC_RRH_RS_RH_MASK		((BIT(EASRC_RRH_RS_RH_WIDTH) - 1) \
					 << EASRC_RRH_RS_RH_SHIFT)
#define EASRC_RRH_RS_RH(v)		(((v) << EASRC_RRH_RS_RH_SHIFT) \
					 & EASRC_RRH_RS_RH_MASK)

/* ASRC Resampling Ratio Update Control (RSUC) */
#define EASRC_RSUC_RS_RM_SHIFT		0
#define EASRC_RSUC_RS_RM_WIDTH		32
#define EASRC_RSUC_RS_RM(v)		((v) << EASRC_RSUC_RS_RM_SHIFT)

/* ASRC Resampling Ratio Update Rate (RRUR) */
#define EASRC_RRUR_RRR_SHIFT		0
#define EASRC_RRUR_RRR_WIDTH		31
#define EASRC_RRUR_RRR_MASK		((BIT(EASRC_RRUR_RRR_WIDTH) - 1) \
					 << EASRC_RRUR_RRR_SHIFT)
#define EASRC_RRUR_RRR(v)		(((v) << EASRC_RRUR_RRR_SHIFT) \
					 & EASRC_RRUR_RRR_MASK)

/* ASRC Resampling Center Tap Coefficient Low (RCTCL) */
#define EASRC_RCTCL_RS_CL_SHIFT		0
#define EASRC_RCTCL_RS_CL_WIDTH		32
#define EASRC_RCTCL_RS_CL(v)		((v) << EASRC_RCTCL_RS_CL_SHIFT)

/* ASRC Resampling Center Tap Coefficient High (RCTCH) */
#define EASRC_RCTCH_RS_CH_SHIFT		0
#define EASRC_RCTCH_RS_CH_WIDTH		32
#define EASRC_RCTCH_RS_CH(v)		((v) << EASRC_RCTCH_RS_CH_SHIFT)

/* ASRC Prefilter Coefficient FIFO (PCF) */
#define EASRC_PCF_CD_SHIFT		0
#define EASRC_PCF_CD_WIDTH		32
#define EASRC_PCF_CD(v)			((v) << EASRC_PCF_CD_SHIFT)

/* ASRC Context Resampling Coefficient Memory (CRCM) */
#define EASRC_CRCM_RS_CWD_SHIFT		0
#define EASRC_CRCM_RS_CWD_WIDTH		32
#define EASRC_CRCM_RS_CWD(v)		((v) << EASRC_CRCM_RS_CWD_SHIFT)

/* ASRC Context Resampling Coefficient Control (CRCC) */
#define EASRC_CRCC_RS_CA_SHIFT		16
#define EASRC_CRCC_RS_CA_WIDTH		11
#define EASRC_CRCC_RS_CA_MASK		((BIT(EASRC_CRCC_RS_CA_WIDTH) - 1) \
					 << EASRC_CRCC_RS_CA_SHIFT)
#define EASRC_CRCC_RS_CA(v)		(((v) << EASRC_CRCC_RS_CA_SHIFT) \
					 & EASRC_CRCC_RS_CA_MASK)
#define EASRC_CRCC_RS_TAPS_SHIFT	1
#define EASRC_CRCC_RS_TAPS_WIDTH	2
#define EASRC_CRCC_RS_TAPS_MASK		((BIT(EASRC_CRCC_RS_TAPS_WIDTH) - 1) \
					 << EASRC_CRCC_RS_TAPS_SHIFT)
#define EASRC_CRCC_RS_TAPS(v)		(((v) << EASRC_CRCC_RS_TAPS_SHIFT) \
					 & EASRC_CRCC_RS_TAPS_MASK)
#define EASRC_CRCC_RS_CPR_SHIFT		0
#define EASRC_CRCC_RS_CPR_MASK		BIT(EASRC_CRCC_RS_CPR_SHIFT)
#define EASRC_CRCC_RS_CPR		BIT(EASRC_CRCC_RS_CPR_SHIFT)

/* ASRC Interrupt_Control (IC) */
#define EASRC_IRQC_RSDM_SHIFT		8
#define EASRC_IRQC_RSDM_WIDTH		4
#define EASRC_IRQC_RSDM_MASK		((BIT(EASRC_IRQC_RSDM_WIDTH) - 1) \
					 << EASRC_IRQC_RSDM_SHIFT)
#define EASRC_IRQC_RSDM(v)		(((v) << EASRC_IRQC_RSDM_SHIFT) \
					 & EASRC_IRQC_RSDM_MASK)
#define EASRC_IRQC_OERM_SHIFT		4
#define EASRC_IRQC_OERM_WIDTH		4
#define EASRC_IRQC_OERM_MASK		((BIT(EASRC_IRQC_OERM_WIDTH) - 1) \
					 << EASRC_IRQC_OERM_SHIFT)
#define EASRC_IRQC_OERM(v)		(((v) << EASRC_IRQC_OERM_SHIFT) \
					 & EASRC_IEQC_OERM_MASK)
#define EASRC_IRQC_IOM_SHIFT		0
#define EASRC_IRQC_IOM_WIDTH		4
#define EASRC_IRQC_IOM_MASK		((BIT(EASRC_IRQC_IOM_WIDTH) - 1) \
					 << EASRC_IRQC_IOM_SHIFT)
#define EASRC_IRQC_IOM(v)		(((v) << EASRC_IRQC_IOM_SHIFT) \
					 & EASRC_IRQC_IOM_MASK)

/* ASRC Interrupt Status Flags (ISF) */
#define EASRC_IRQF_RSD_SHIFT		8
#define EASRC_IRQF_RSD_WIDTH		4
#define EASRC_IRQF_RSD_MASK		((BIT(EASRC_IRQF_RSD_WIDTH) - 1) \
					 << EASRC_IRQF_RSD_SHIFT)
#define EASRC_IRQF_RSD(v)		(((v) << EASRC_IRQF_RSD_SHIFT) \
					 & EASRC_IRQF_RSD_MASK)
#define EASRC_IRQF_OER_SHIFT		4
#define EASRC_IRQF_OER_WIDTH		4
#define EASRC_IRQF_OER_MASK		((BIT(EASRC_IRQF_OER_WIDTH) - 1) \
					 << EASRC_IRQF_OER_SHIFT)
#define EASRC_IRQF_OER(v)		(((v) << EASRC_IRQF_OER_SHIFT) \
					 & EASRC_IRQF_OER_MASK)
#define EASRC_IRQF_IFO_SHIFT		0
#define EASRC_IRQF_IFO_WIDTH		4
#define EASRC_IRQF_IFO_MASK		((BIT(EASRC_IRQF_IFO_WIDTH) - 1) \
					 << EASRC_IRQF_IFO_SHIFT)
#define EASRC_IRQF_IFO(v)		(((v) << EASRC_IRQF_IFO_SHIFT) \
					 & EASRC_IRQF_IFO_MASK)

/* ASRC Context Channel STAT */
#define EASRC_CSx_CSx_SHIFT		0
#define EASRC_CSx_CSx_WIDTH		32
#define EASRC_CSx_CSx(v)		((v) << EASRC_CSx_CSx_SHIFT)

/* ASRC Debug Control Register */
#define EASRC_DBGC_DMS_SHIFT		0
#define EASRC_DBGC_DMS_WIDTH		6
#define EASRC_DBGC_DMS_MASK		((BIT(EASRC_DBGC_DMS_WIDTH) - 1) \
					 << EASRC_DBGC_DMS_SHIFT)
#define EASRC_DBGC_DMS(v)		(((v) << EASRC_DBGC_DMS_SHIFT) \
					 & EASRC_DBGC_DMS_MASK)

/* ASRC Debug Status Register */
#define EASRC_DBGS_DS_SHIFT		0
#define EASRC_DBGS_DS_WIDTH		32
#define EASRC_DBGS_DS(v)		((v) << EASRC_DBGS_DS_SHIFT)

/* General Constants */
#define EASRC_CTX_MAX_NUM		4
#define EASRC_RS_COEFF_MEM		0
#define EASRC_PF_COEFF_MEM		1

/* Prefilter constants */
#define EASRC_PF_ST1_ONLY		0
#define EASRC_PF_TWO_STAGE_MODE		1
#define EASRC_PF_ST1_COEFF_WR		0
#define EASRC_PF_ST2_COEFF_WR		1
#define EASRC_MAX_PF_TAPS		384

/* Resampling constants */
#define EASRC_RS_32_TAPS		0
#define EASRC_RS_64_TAPS		1
#define EASRC_RS_128_TAPS		2

/* Initialization mode */
#define EASRC_INIT_MODE_SW_CONTROL	0
#define EASRC_INIT_MODE_REPLICATE	1
#define EASRC_INIT_MODE_ZERO_FILL	2

/* FIFO watermarks */
#define FSL_EASRC_INPUTFIFO_WML		0x4
#define FSL_EASRC_OUTPUTFIFO_WML	0x1

#define EASRC_INPUTFIFO_THRESHOLD_MIN	0
#define EASRC_INPUTFIFO_THRESHOLD_MAX	127
#define EASRC_OUTPUTFIFO_THRESHOLD_MIN	0
#define EASRC_OUTPUTFIFO_THRESHOLD_MAX	63

#define EASRC_DMA_BUFFER_SIZE		(1024 * 48 * 9)
#define EASRC_MAX_BUFFER_SIZE		(1024 * 48)

#define FIRMWARE_MAGIC			0xDEAD
#define FIRMWARE_VERSION		1

#define PREFILTER_MEM_LEN		0x1800

enum easrc_word_width {
	EASRC_WIDTH_16_BIT = 0,
	EASRC_WIDTH_20_BIT = 1,
	EASRC_WIDTH_24_BIT = 2,
	EASRC_WIDTH_32_BIT = 3,
};

struct __attribute__((__packed__))  asrc_firmware_hdr {
	u32 magic;
	u32 interp_scen;
	u32 prefil_scen;
	u32 firmware_version;
};

struct __attribute__((__packed__)) interp_params {
	u32 magic;
	u32 num_taps;
	u32 num_phases;
	u64 center_tap;
	u64 coeff[8192];
};

struct __attribute__((__packed__)) prefil_params {
	u32 magic;
	u32 insr;
	u32 outsr;
	u32 st1_taps;
	u32 st2_taps;
	u32 st1_exp;
	u64 coeff[256];
};

struct dma_block {
	void *dma_vaddr;
	unsigned int length;
	unsigned int max_buf_size;
};

struct fsl_easrc_data_fmt {
	unsigned int width : 2;
	unsigned int endianness : 1;
	unsigned int unsign : 1;
	unsigned int floating_point : 1;
	unsigned int iec958: 1;
	unsigned int sample_pos: 5;
	unsigned int addexp;
};

struct fsl_easrc_io_params {
	struct fsl_easrc_data_fmt fmt;
	unsigned int group_len;
	unsigned int iterations;
	unsigned int access_len;
	unsigned int fifo_wtmk;
	unsigned int sample_rate;
	snd_pcm_format_t sample_format;
	unsigned int norm_rate;
};

struct fsl_easrc_slot {
	bool busy;
	int ctx_index;
	int slot_index;
	int num_channel;  /* maximum is 8 */
	int min_channel;
	int max_channel;
	int pf_mem_used;
};

/**
 * fsl_easrc_ctx_priv: EASRC context private data
 *
 * @in_params: input parameter
 * @out_params:  output parameter
 * @st1_num_taps: tap number of stage 1
 * @st2_num_taps: tap number of stage 2
 * @st1_num_exp: exponent number of stage 1
 * @pf_init_mode: prefilter init mode
 * @rs_init_mode:  resample filter init mode
 * @ctx_streams: stream flag of ctx
 * @rs_ratio: resampler ratio
 * @st1_coeff: pointer of stage 1 coeff
 * @st2_coeff: pointer of stage 2 coeff
 * @in_filled_sample: input filled sample
 * @out_missed_sample: sample missed in output
 * @st1_addexp: exponent added for stage1
 * @st2_addexp: exponent added for stage2
 */
struct fsl_easrc_ctx_priv {
	struct fsl_easrc_io_params in_params;
	struct fsl_easrc_io_params out_params;
	unsigned int st1_num_taps;
	unsigned int st2_num_taps;
	unsigned int st1_num_exp;
	unsigned int pf_init_mode;
	unsigned int rs_init_mode;
	unsigned int ctx_streams;
	u64 rs_ratio;
	u64 *st1_coeff;
	u64 *st2_coeff;
	int in_filled_sample;
	int out_missed_sample;
	int st1_addexp;
	int st2_addexp;
};

/**
 * fsl_easrc_priv: EASRC private data
 *
 * @slot: slot setting
 * @firmware_hdr:  the header of firmware
 * @interp: pointer to interpolation filter coeff
 * @prefil: pointer to prefilter coeff
 * @fw: firmware of coeff table
 * @fw_name: firmware name
 * @rs_num_taps:  resample filter taps, 32, 64, or 128
 * @bps_iec958: bits per sample of iec958
 * @rs_coeff: resampler coefficient
 * @const_coeff: one tap prefilter coefficient
 * @firmware_loaded: firmware is loaded
 */
struct fsl_easrc_priv {
	struct fsl_easrc_slot slot[EASRC_CTX_MAX_NUM][2];
	struct asrc_firmware_hdr *firmware_hdr;
	struct interp_params *interp;
	struct prefil_params *prefil;
	const struct firmware *fw;
	const char *fw_name;
	unsigned int rs_num_taps;
	unsigned int bps_iec958[EASRC_CTX_MAX_NUM];
	u64 *rs_coeff;
	u64 const_coeff;
	int firmware_loaded;
};
#endif /* _FSL_EASRC_H */

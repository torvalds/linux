/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_HAWI_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_HAWI_H

/* VIDEO_CC clocks */
#define VIDEO_CC_AHB_CLK					0
#define VIDEO_CC_AHB_CLK_SRC					1
#define VIDEO_CC_CX_AXI0_CLK					2
#define VIDEO_CC_CX_DBGCH_XO_CLK				3
#define VIDEO_CC_CX_XO_CLK					4
#define VIDEO_CC_DBGCH_XO_CLK					5
#define VIDEO_CC_MVS0_CLK					6
#define VIDEO_CC_MVS0_CLK_SRC					7
#define VIDEO_CC_MVS0_SHIFT_CLK					8
#define VIDEO_CC_MVS0_VPP0_CLK					9
#define VIDEO_CC_MVS0_VPP0_VPP1_GATING_CLK			10
#define VIDEO_CC_MVS0_VPP1_CLK					11
#define VIDEO_CC_MVS0A_CLK					12
#define VIDEO_CC_MVS0A_CLK_SRC					13
#define VIDEO_CC_MVS0B_CLK					14
#define VIDEO_CC_MVS0B_CLK_SRC					15
#define VIDEO_CC_MVS0C_CLK					16
#define VIDEO_CC_MVS0C_CLK_SRC					17
#define VIDEO_CC_MVS0C_CTL_FREERUN_CLK				18
#define VIDEO_CC_MVS0C_DEBUG_CLK				19
#define VIDEO_CC_MVS0C_FREERUN_CLK				20
#define VIDEO_CC_MVS0C_SHIFT_CLK				21
#define VIDEO_CC_PLL0						22
#define VIDEO_CC_PLL0_OUT_EVEN					23
#define VIDEO_CC_PLL1						24
#define VIDEO_CC_PLL2						25
#define VIDEO_CC_PLL3						26
#define VIDEO_CC_SLEEP_CLK					27
#define VIDEO_CC_XO_CLK						28
#define VIDEO_CC_XO_CLK_SRC					29

/* VIDEO_CC power domains */
#define VIDEO_CC_AXI0_CX_INT_GDSC				0
#define VIDEO_CC_MM_INT_GDSC					1
#define VIDEO_CC_MVS0_GDSC					2
#define VIDEO_CC_MVS0_VPP0_GDSC					3
#define VIDEO_CC_MVS0_VPP1_GDSC					4
#define VIDEO_CC_MVS0A_GDSC					5
#define VIDEO_CC_MVS0C_GDSC					6

/* VIDEO_CC resets */
#define VIDEO_CC_AXI0_CX_INT_BCR				0
#define VIDEO_CC_INTERFACE_BCR					1
#define VIDEO_CC_MM_INT_BCR					2
#define VIDEO_CC_MVS0_BCR					3
#define VIDEO_CC_MVS0_VPP0_BCR					4
#define VIDEO_CC_MVS0_VPP1_BCR					5
#define VIDEO_CC_MVS0A_BCR					6
#define VIDEO_CC_MVS0C_CLK_ARES					7
#define VIDEO_CC_MVS0C_BCR					8
#define VIDEO_CC_MVS0C_CTL_FREERUN_CLK_ARES			9
#define VIDEO_CC_MVS0C_FREERUN_CLK_ARES				10
#define VIDEO_CC_XO_CLK_ARES					11

#endif

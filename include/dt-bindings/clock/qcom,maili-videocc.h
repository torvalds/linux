/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_MAILI_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_MAILI_H

/* VIDEO_CC clocks */
#define VIDEO_CC_AHB_CLK					0
#define VIDEO_CC_AHB_CLK_SRC					1
#define VIDEO_CC_MVS0_CLK					2
#define VIDEO_CC_MVS0_CLK_SRC					3
#define VIDEO_CC_MVS0_FREERUN_CLK				4
#define VIDEO_CC_MVS0_SHIFT_CLK					5
#define VIDEO_CC_MVS0_VPP0_CLK					6
#define VIDEO_CC_MVS0_VPP0_FREERUN_CLK				7
#define VIDEO_CC_MVS0B_CLK					8
#define VIDEO_CC_MVS0B_CLK_SRC					9
#define VIDEO_CC_MVS0B_FREERUN_CLK				10
#define VIDEO_CC_MVS0C_CLK					11
#define VIDEO_CC_MVS0C_CLK_SRC					12
#define VIDEO_CC_MVS0C_DEBUG_CLK				13
#define VIDEO_CC_MVS0C_FREERUN_CLK				14
#define VIDEO_CC_MVS0C_SHIFT_CLK				15
#define VIDEO_CC_PLL0						16
#define VIDEO_CC_PLL1						17
#define VIDEO_CC_PLL2						18
#define VIDEO_CC_SLEEP_CLK					19
#define VIDEO_CC_TS_XO_CLK					20
#define VIDEO_CC_XO_CLK						21
#define VIDEO_CC_XO_CLK_SRC					22

/* VIDEO_CC power domains */
#define VIDEO_CC_MVS0_GDSC					0
#define VIDEO_CC_MVS0_VPP0_GDSC					1
#define VIDEO_CC_MVS0C_GDSC					2

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_MVS0_BCR					1
#define VIDEO_CC_MVS0_CLK_ARES					2
#define VIDEO_CC_MVS0_FREERUN_CLK_ARES				3
#define VIDEO_CC_MVS0_VPP0_BCR					4
#define VIDEO_CC_MVS0C_BCR					5
#define VIDEO_CC_MVS0C_CLK_ARES					6
#define VIDEO_CC_MVS0C_FREERUN_CLK_ARES				7
#define VIDEO_CC_XO_CLK_ARES					8

#endif

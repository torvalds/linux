/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SERAPH_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SERAPH_H

/* VIDEO_CC clocks */
#define VIDEO_CC_PLL0						0
#define VIDEO_CC_PLL1						1
#define VIDEO_CC_PLL2						2
#define VIDEO_CC_AHB_CLK					3
#define VIDEO_CC_AHB_CLK_SRC					4
#define VIDEO_CC_MVS0_CLK					5
#define VIDEO_CC_MVS0_CLK_SRC					6
#define VIDEO_CC_MVS0_FREERUN_CLK				7
#define VIDEO_CC_MVS0_SHIFT_CLK					8
#define VIDEO_CC_MVS0_VPP0_CLK					9
#define VIDEO_CC_MVS0_VPP0_FREERUN_CLK				10
#define VIDEO_CC_MVS0_VPP1_CLK					11
#define VIDEO_CC_MVS0_VPP1_FREERUN_CLK				12
#define VIDEO_CC_MVS0B_CLK					13
#define VIDEO_CC_MVS0B_CLK_SRC					14
#define VIDEO_CC_MVS0B_FREERUN_CLK				15
#define VIDEO_CC_MVS0C_CLK					16
#define VIDEO_CC_MVS0C_CLK_SRC					17
#define VIDEO_CC_MVS0C_FREERUN_CLK				18
#define VIDEO_CC_MVS0C_SHIFT_CLK				19
#define VIDEO_CC_SLEEP_CLK					20
#define VIDEO_CC_TS_XO_CLK					21
#define VIDEO_CC_XO_CLK						22
#define VIDEO_CC_XO_CLK_SRC					23

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_MVS0_BCR					1
#define VIDEO_CC_MVS0_VPP0_BCR					2
#define VIDEO_CC_MVS0_VPP1_BCR					3
#define VIDEO_CC_MVS0C_BCR					4
#define VIDEO_CC_MVS0C_CLK_ARES					5
#define VIDEO_CC_XO_CLK_ARES					6
#define VIDEO_CC_MVS0_FREERUN_CLK_ARES				7
#define VIDEO_CC_MVS0C_FREERUN_CLK_ARES				8

#endif

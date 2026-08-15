/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_X1P42100_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_X1P42100_H

/* VIDEO_CC clocks */
#define VIDEO_CC_MVS0_CLK					0
#define VIDEO_CC_MVS0_CLK_SRC					1
#define VIDEO_CC_MVS0_DIV_CLK_SRC				2
#define VIDEO_CC_MVS0C_CLK					3
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC				4
#define VIDEO_CC_MVS1_CLK					5
#define VIDEO_CC_MVS1_CLK_SRC					6
#define VIDEO_CC_MVS1_DIV_CLK_SRC				7
#define VIDEO_CC_MVS1C_CLK					8
#define VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC				9
#define VIDEO_CC_PLL0						10
#define VIDEO_CC_PLL1						11
#define VIDEO_CC_MVS0_SHIFT_CLK					12
#define VIDEO_CC_MVS0C_SHIFT_CLK				13
#define VIDEO_CC_MVS1_SHIFT_CLK					14
#define VIDEO_CC_MVS1C_SHIFT_CLK				15
#define VIDEO_CC_XO_CLK_SRC					16
#define VIDEO_CC_MVS0_BSE_CLK					17
#define VIDEO_CC_MVS0_BSE_CLK_SRC				18
#define VIDEO_CC_MVS0_BSE_DIV4_DIV_CLK_SRC			19

/* VIDEO_CC power domains */
#define VIDEO_CC_MVS0C_GDSC					0
#define VIDEO_CC_MVS0_GDSC					1
#define VIDEO_CC_MVS1C_GDSC					2
#define VIDEO_CC_MVS1_GDSC					3

/* VIDEO_CC resets */
#define CVP_VIDEO_CC_INTERFACE_BCR				0
#define CVP_VIDEO_CC_MVS0_BCR					1
#define CVP_VIDEO_CC_MVS0C_BCR					2
#define CVP_VIDEO_CC_MVS1_BCR					3
#define CVP_VIDEO_CC_MVS1C_BCR					4
#define VIDEO_CC_MVS0C_CLK_ARES					5
#define VIDEO_CC_MVS1C_CLK_ARES					6
#define VIDEO_CC_XO_CLK_ARES					7
#define VIDEO_CC_MVS0_BSE_BCR					8

#endif

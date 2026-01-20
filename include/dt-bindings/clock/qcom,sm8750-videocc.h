/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8750_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8750_H

/* VIDEO_CC clocks */
#define VIDEO_CC_AHB_CLK					0
#define VIDEO_CC_AHB_CLK_SRC					1
#define VIDEO_CC_MVS0_CLK					2
#define VIDEO_CC_MVS0_CLK_SRC					3
#define VIDEO_CC_MVS0_DIV_CLK_SRC				4
#define VIDEO_CC_MVS0_FREERUN_CLK				5
#define VIDEO_CC_MVS0_SHIFT_CLK					6
#define VIDEO_CC_MVS0C_CLK					7
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC				8
#define VIDEO_CC_MVS0C_FREERUN_CLK				9
#define VIDEO_CC_MVS0C_SHIFT_CLK				10
#define VIDEO_CC_PLL0						11
#define VIDEO_CC_SLEEP_CLK					12
#define VIDEO_CC_SLEEP_CLK_SRC					13
#define VIDEO_CC_XO_CLK						14
#define VIDEO_CC_XO_CLK_SRC					15

/* VIDEO_CC power domains */
#define VIDEO_CC_MVS0_GDSC					0
#define VIDEO_CC_MVS0C_GDSC					1

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_MVS0_BCR					1
#define VIDEO_CC_MVS0C_CLK_ARES					2
#define VIDEO_CC_MVS0C_BCR					3
#define VIDEO_CC_MVS0_FREERUN_CLK_ARES				4
#define VIDEO_CC_MVS0C_FREERUN_CLK_ARES				5
#define VIDEO_CC_XO_CLK_ARES					6

#endif

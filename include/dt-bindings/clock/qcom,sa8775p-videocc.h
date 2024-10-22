/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_SA8775P_VIDEO_CC_H
#define _DT_BINDINGS_CLK_QCOM_SA8775P_VIDEO_CC_H

/* VIDEO_CC clocks */
#define VIDEO_CC_AHB_CLK					0
#define VIDEO_CC_AHB_CLK_SRC					1
#define VIDEO_CC_MVS0_CLK					2
#define VIDEO_CC_MVS0_CLK_SRC					3
#define VIDEO_CC_MVS0_DIV_CLK_SRC				4
#define VIDEO_CC_MVS0C_CLK					5
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC				6
#define VIDEO_CC_MVS1_CLK					7
#define VIDEO_CC_MVS1_CLK_SRC					8
#define VIDEO_CC_MVS1_DIV_CLK_SRC				9
#define VIDEO_CC_MVS1C_CLK					10
#define VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC				11
#define VIDEO_CC_PLL_LOCK_MONITOR_CLK				12
#define VIDEO_CC_SLEEP_CLK					13
#define VIDEO_CC_SLEEP_CLK_SRC					14
#define VIDEO_CC_SM_DIV_CLK_SRC					15
#define VIDEO_CC_SM_OBS_CLK					16
#define VIDEO_CC_XO_CLK						17
#define VIDEO_CC_XO_CLK_SRC					18
#define VIDEO_PLL0						19
#define VIDEO_PLL1						20

/* VIDEO_CC power domains */
#define VIDEO_CC_MVS0C_GDSC					0
#define VIDEO_CC_MVS0_GDSC					1
#define VIDEO_CC_MVS1C_GDSC					2
#define VIDEO_CC_MVS1_GDSC					3

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_MVS0_BCR					1
#define VIDEO_CC_MVS0C_CLK_ARES					2
#define VIDEO_CC_MVS0C_BCR					3
#define VIDEO_CC_MVS1_BCR					4
#define VIDEO_CC_MVS1C_CLK_ARES					5
#define VIDEO_CC_MVS1C_BCR					6

#endif

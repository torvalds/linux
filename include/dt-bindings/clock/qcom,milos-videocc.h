/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025, Luca Weiss <luca.weiss@fairphone.com>
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_MILOS_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_MILOS_H

/* VIDEO_CC clocks */
#define VIDEO_CC_PLL0						0
#define VIDEO_CC_AHB_CLK					1
#define VIDEO_CC_AHB_CLK_SRC					2
#define VIDEO_CC_MVS0_CLK					3
#define VIDEO_CC_MVS0_CLK_SRC					4
#define VIDEO_CC_MVS0_DIV_CLK_SRC				5
#define VIDEO_CC_MVS0_SHIFT_CLK					6
#define VIDEO_CC_MVS0C_CLK					7
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC				8
#define VIDEO_CC_MVS0C_SHIFT_CLK				9
#define VIDEO_CC_SLEEP_CLK					10
#define VIDEO_CC_SLEEP_CLK_SRC					11
#define VIDEO_CC_XO_CLK						12
#define VIDEO_CC_XO_CLK_SRC					13

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_MVS0_BCR					1
#define VIDEO_CC_MVS0C_CLK_ARES					2
#define VIDEO_CC_MVS0C_BCR					3

/* VIDEO_CC power domains */
#define VIDEO_CC_MVS0_GDSC					0
#define VIDEO_CC_MVS0C_GDSC					1

#endif

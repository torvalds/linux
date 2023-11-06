/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8350_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8350_H

/* Clocks */
#define VIDEO_CC_AHB_CLK_SRC					0
#define VIDEO_CC_MVS0_CLK					1
#define VIDEO_CC_MVS0_CLK_SRC					2
#define VIDEO_CC_MVS0_DIV_CLK_SRC				3
#define VIDEO_CC_MVS0C_CLK					4
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC				5
#define VIDEO_CC_MVS1_CLK					6
#define VIDEO_CC_MVS1_CLK_SRC					7
#define VIDEO_CC_MVS1_DIV2_CLK					8
#define VIDEO_CC_MVS1_DIV_CLK_SRC				9
#define VIDEO_CC_MVS1C_CLK					10
#define VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC				11
#define VIDEO_CC_SLEEP_CLK					12
#define VIDEO_CC_SLEEP_CLK_SRC					13
#define VIDEO_CC_XO_CLK_SRC					14
#define VIDEO_PLL0						15
#define VIDEO_PLL1						16

/* GDSCs */
#define MVS0C_GDSC						0
#define MVS1C_GDSC						1
#define MVS0_GDSC						2
#define MVS1_GDSC						3

#endif

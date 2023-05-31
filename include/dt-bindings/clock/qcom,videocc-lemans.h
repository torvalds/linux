/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_LEMANS_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_LEMANS_H

/* VIDEO_CC clocks */
#define VIDEO_PLL0					0
#define VIDEO_PLL1					1
#define VIDEO_CC_AHB_CLK				2
#define VIDEO_CC_AHB_CLK_SRC				3
#define VIDEO_CC_MVS0_CLK				4
#define VIDEO_CC_MVS0_CLK_SRC				5
#define VIDEO_CC_MVS0_DIV_CLK_SRC			6
#define VIDEO_CC_MVS0C_CLK				7
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC			8
#define VIDEO_CC_MVS1_CLK				9
#define VIDEO_CC_MVS1_CLK_SRC				10
#define VIDEO_CC_MVS1_DIV_CLK_SRC			11
#define VIDEO_CC_MVS1C_CLK				12
#define VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC			13
#define VIDEO_CC_SLEEP_CLK				14
#define VIDEO_CC_SLEEP_CLK_SRC				15
#define VIDEO_CC_XO_CLK					16
#define VIDEO_CC_XO_CLK_SRC				17

/* VIDEO_CC resets */
#define CVP_VIDEO_CC_INTERFACE_BCR			0
#define CVP_VIDEO_CC_MVS0_BCR				1
#define CVP_VIDEO_CC_MVS0C_BCR				2
#define CVP_VIDEO_CC_MVS1_BCR				3
#define CVP_VIDEO_CC_MVS1C_BCR				4
#define VIDEO_CC_MVS0C_CLK_ARES				5
#define VIDEO_CC_MVS1C_CLK_ARES				6

#endif

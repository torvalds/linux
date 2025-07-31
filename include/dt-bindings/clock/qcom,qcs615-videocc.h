/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_QCS615_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_QCS615_H

/* VIDEO_CC clocks */
#define VIDEO_CC_SLEEP_CLK					0
#define VIDEO_CC_SLEEP_CLK_SRC					1
#define VIDEO_CC_VCODEC0_AXI_CLK				2
#define VIDEO_CC_VCODEC0_CORE_CLK				3
#define VIDEO_CC_VENUS_AHB_CLK					4
#define VIDEO_CC_VENUS_CLK_SRC					5
#define VIDEO_CC_VENUS_CTL_AXI_CLK				6
#define VIDEO_CC_VENUS_CTL_CORE_CLK				7
#define VIDEO_CC_XO_CLK						8
#define VIDEO_PLL0						9

/* VIDEO_CC power domains */
#define VCODEC0_GDSC						0
#define VENUS_GDSC						1

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR					0
#define VIDEO_CC_VCODEC0_BCR					1
#define VIDEO_CC_VENUS_BCR					2

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8250_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8250_H

/* VIDEO_CC clocks */
#define VIDEO_CC_MVS0_CLK_SRC		0
#define VIDEO_CC_MVS0C_CLK		1
#define VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC	2
#define VIDEO_CC_MVS1_CLK_SRC		3
#define VIDEO_CC_MVS1_DIV2_CLK		4
#define VIDEO_CC_MVS1C_CLK		5
#define VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC	6
#define VIDEO_CC_PLL0			7
#define VIDEO_CC_PLL1			8
#define VIDEO_CC_MVS0_DIV_CLK_SRC	9
#define VIDEO_CC_MVS0_CLK		10

/* VIDEO_CC resets */
#define VIDEO_CC_CVP_INTERFACE_BCR	0
#define VIDEO_CC_CVP_MVS0_BCR		1
#define VIDEO_CC_MVS0C_CLK_ARES		2
#define VIDEO_CC_CVP_MVS0C_BCR		3
#define VIDEO_CC_CVP_MVS1_BCR		4
#define VIDEO_CC_MVS1C_CLK_ARES		5
#define VIDEO_CC_CVP_MVS1C_BCR		6

#define MVS0C_GDSC			0
#define MVS1C_GDSC			1
#define MVS0_GDSC			2
#define MVS1_GDSC			3

#endif

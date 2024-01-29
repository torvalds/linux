/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8150_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8150_H

/* VIDEO_CC clocks */
#define VIDEO_CC_IRIS_AHB_CLK		0
#define VIDEO_CC_IRIS_CLK_SRC		1
#define VIDEO_CC_MVS0_CORE_CLK		2
#define VIDEO_CC_MVS1_CORE_CLK		3
#define VIDEO_CC_MVSC_CORE_CLK		4
#define VIDEO_CC_PLL0			5

/* VIDEO_CC Resets */
#define VIDEO_CC_MVSC_CORE_CLK_BCR	0
#define VIDEO_CC_INTERFACE_BCR		1
#define VIDEO_CC_MVS0_BCR		2
#define VIDEO_CC_MVS1_BCR		3
#define VIDEO_CC_MVSC_BCR		4

/* VIDEO_CC GDSCRs */
#define VENUS_GDSC			0
#define VCODEC0_GDSC			1
#define VCODEC1_GDSC			2

#endif

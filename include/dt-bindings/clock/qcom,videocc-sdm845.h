/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_SDM_VIDEO_CC_SDM845_H
#define _DT_BINDINGS_CLK_SDM_VIDEO_CC_SDM845_H

/* VIDEO_CC clock registers */
#define VIDEO_CC_APB_CLK		0
#define VIDEO_CC_AT_CLK			1
#define VIDEO_CC_QDSS_TRIG_CLK		2
#define VIDEO_CC_QDSS_TSCTR_DIV8_CLK	3
#define VIDEO_CC_VCODEC0_AXI_CLK	4
#define VIDEO_CC_VCODEC0_CORE_CLK	5
#define VIDEO_CC_VCODEC1_AXI_CLK	6
#define VIDEO_CC_VCODEC1_CORE_CLK	7
#define VIDEO_CC_VENUS_AHB_CLK		8
#define VIDEO_CC_VENUS_CLK_SRC		9
#define VIDEO_CC_VENUS_CTL_AXI_CLK	10
#define VIDEO_CC_VENUS_CTL_CORE_CLK	11
#define VIDEO_PLL0			12

/* VIDEO_CC Resets */
#define VIDEO_CC_VENUS_BCR		0
#define VIDEO_CC_VCODEC0_BCR		1
#define VIDEO_CC_VCODEC1_BCR		2
#define VIDEO_CC_INTERFACE_BCR		3

/* VIDEO_CC GDSCRs */
#define VENUS_GDSC			0
#define VCODEC0_GDSC			1
#define VCODEC1_GDSC			2

#endif

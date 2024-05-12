/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_DISP_CC_SM6375_H
#define _DT_BINDINGS_CLK_QCOM_DISP_CC_SM6375_H

/* Clocks */
#define DISP_CC_PLL0					0
#define DISP_CC_MDSS_AHB_CLK				1
#define DISP_CC_MDSS_AHB_CLK_SRC			2
#define DISP_CC_MDSS_BYTE0_CLK				3
#define DISP_CC_MDSS_BYTE0_CLK_SRC			4
#define DISP_CC_MDSS_BYTE0_DIV_CLK_SRC			5
#define DISP_CC_MDSS_BYTE0_INTF_CLK			6
#define DISP_CC_MDSS_ESC0_CLK				7
#define DISP_CC_MDSS_ESC0_CLK_SRC			8
#define DISP_CC_MDSS_MDP_CLK				9
#define DISP_CC_MDSS_MDP_CLK_SRC			10
#define DISP_CC_MDSS_MDP_LUT_CLK			11
#define DISP_CC_MDSS_NON_GDSC_AHB_CLK			12
#define DISP_CC_MDSS_PCLK0_CLK				13
#define DISP_CC_MDSS_PCLK0_CLK_SRC			14
#define DISP_CC_MDSS_ROT_CLK				15
#define DISP_CC_MDSS_ROT_CLK_SRC			16
#define DISP_CC_MDSS_RSCC_AHB_CLK			17
#define DISP_CC_MDSS_RSCC_VSYNC_CLK			18
#define DISP_CC_MDSS_VSYNC_CLK				19
#define DISP_CC_MDSS_VSYNC_CLK_SRC			20
#define DISP_CC_SLEEP_CLK				21
#define DISP_CC_XO_CLK					22

/* Resets */
#define DISP_CC_MDSS_CORE_BCR				0
#define DISP_CC_MDSS_RSCC_BCR				1

/* GDSCs */
#define MDSS_GDSC					0

#endif

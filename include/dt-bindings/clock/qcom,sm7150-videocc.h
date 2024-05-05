/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Danila Tikhonov <danila@jiaxyga.com>
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEOCC_SM7150_H
#define _DT_BINDINGS_CLK_QCOM_VIDEOCC_SM7150_H

#define VIDEOCC_PLL0			0
#define VIDEOCC_IRIS_AHB_CLK		1
#define VIDEOCC_IRIS_CLK_SRC		2
#define VIDEOCC_MVS0_AXI_CLK		3
#define VIDEOCC_MVS0_CORE_CLK		4
#define VIDEOCC_MVS1_AXI_CLK		5
#define VIDEOCC_MVS1_CORE_CLK		6
#define VIDEOCC_MVSC_CORE_CLK		7
#define VIDEOCC_MVSC_CTL_AXI_CLK	8
#define VIDEOCC_VENUS_AHB_CLK		9
#define VIDEOCC_XO_CLK			10
#define VIDEOCC_XO_CLK_SRC		11

/* VIDEOCC GDSCRs */
#define VENUS_GDSC			0
#define VCODEC0_GDSC			1
#define VCODEC1_GDSC			2

#endif

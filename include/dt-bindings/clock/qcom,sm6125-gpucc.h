/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6125_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6125_H

/* Clocks */
#define GPU_CC_PLL0_OUT_AUX2			0
#define GPU_CC_PLL1_OUT_AUX2			1
#define GPU_CC_CRC_AHB_CLK			2
#define GPU_CC_CX_APB_CLK			3
#define GPU_CC_CX_GFX3D_CLK			4
#define GPU_CC_CX_GMU_CLK			5
#define GPU_CC_CX_SNOC_DVM_CLK			6
#define GPU_CC_CXO_AON_CLK			7
#define GPU_CC_CXO_CLK				8
#define GPU_CC_GMU_CLK_SRC			9
#define GPU_CC_SLEEP_CLK			10
#define GPU_CC_GX_GFX3D_CLK			11
#define GPU_CC_GX_GFX3D_CLK_SRC			12
#define GPU_CC_AHB_CLK				13
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK		14

/* GDSCs */
#define GPU_CX_GDSC				0
#define GPU_GX_GDSC				1

#endif

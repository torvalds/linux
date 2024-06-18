/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6115_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6115_H

/* GPU_CC clocks */
#define GPU_CC_PLL0			0
#define GPU_CC_PLL0_OUT_AUX2		1
#define GPU_CC_PLL1			2
#define GPU_CC_PLL1_OUT_AUX		3
#define GPU_CC_AHB_CLK			4
#define GPU_CC_CRC_AHB_CLK		5
#define GPU_CC_CX_GFX3D_CLK		6
#define GPU_CC_CX_GMU_CLK		7
#define GPU_CC_CX_SNOC_DVM_CLK		8
#define GPU_CC_CXO_AON_CLK		9
#define GPU_CC_CXO_CLK			10
#define GPU_CC_GMU_CLK_SRC		11
#define GPU_CC_GX_CXO_CLK		12
#define GPU_CC_GX_GFX3D_CLK		13
#define GPU_CC_GX_GFX3D_CLK_SRC		14
#define GPU_CC_SLEEP_CLK		15
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK	16

/* Resets */
#define GPU_GX_BCR			0

/* GDSCs */
#define GPU_CX_GDSC			0
#define GPU_GX_GDSC			1

#endif

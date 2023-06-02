/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6150_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6150_H

/* Hardware clocks */
#define CRC_DIV_PLL0				0
#define CRC_DIV_PLL1				1

/* GPU_CC clocks */
#define GPU_CC_PLL0						2
#define GPU_CC_PLL1						3
#define GPU_CC_AHB_CLK						4
#define GPU_CC_CRC_AHB_CLK					5
#define GPU_CC_CX_GFX3D_CLK					6
#define GPU_CC_CX_GFX3D_SLV_CLK					7
#define GPU_CC_CX_GMU_CLK					8
#define GPU_CC_CX_SNOC_DVM_CLK					9
#define GPU_CC_CXO_AON_CLK					10
#define GPU_CC_CXO_CLK						11
#define GPU_CC_GMU_CLK_SRC					12
#define GPU_CC_GX_GFX3D_CLK					13
#define GPU_CC_GX_GFX3D_CLK_SRC					14
#define GPU_CC_GX_GMU_CLK					15
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK				16
#define GPU_CC_SLEEP_CLK					17

#endif

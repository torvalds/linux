/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_QCS615_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_QCS615_H

/* GPU_CC clocks */
#define CRC_DIV_PLL0						0
#define CRC_DIV_PLL1						1
#define GPU_CC_PLL0						2
#define GPU_CC_PLL1						3
#define GPU_CC_CRC_AHB_CLK					4
#define GPU_CC_CX_GFX3D_CLK					5
#define GPU_CC_CX_GFX3D_SLV_CLK					6
#define GPU_CC_CX_GMU_CLK					7
#define GPU_CC_CX_SNOC_DVM_CLK					8
#define GPU_CC_CXO_AON_CLK					9
#define GPU_CC_CXO_CLK						10
#define GPU_CC_GMU_CLK_SRC					11
#define GPU_CC_GX_GFX3D_CLK					12
#define GPU_CC_GX_GFX3D_CLK_SRC					13
#define GPU_CC_GX_GMU_CLK					14
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK				15
#define GPU_CC_SLEEP_CLK					16

/* GPU_CC power domains */
#define CX_GDSC							0
#define GX_GDSC							1

/* GPU_CC resets */
#define GPU_CC_CX_BCR						0
#define GPU_CC_GFX3D_AON_BCR					1
#define GPU_CC_GMU_BCR						2
#define GPU_CC_GX_BCR						3
#define GPU_CC_XO_BCR						4

#endif

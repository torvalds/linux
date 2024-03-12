/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_PITTI_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_PITTI_H

/* GPU_CC clocks */
#define GPU_CC_PLL0						0
#define GPU_CC_PLL0_OUT_EVEN					1
#define GPU_CC_PLL1						2
#define GPU_CC_AHB_CLK						3
#define GPU_CC_CRC_AHB_CLK					4
#define GPU_CC_CX_ACCU_SHIFT_CLK				5
#define GPU_CC_CX_GFX3D_CLK					6
#define GPU_CC_CX_GFX3D_SLV_CLK					7
#define GPU_CC_CX_GMU_CLK					8
#define GPU_CC_CXO_AON_CLK					9
#define GPU_CC_CXO_CLK						10
#define GPU_CC_DEMET_CLK					11
#define GPU_CC_DEMET_DIV_CLK_SRC				12
#define GPU_CC_GMU_CLK_SRC					13
#define GPU_CC_GX_ACCU_SHIFT_CLK				14
#define GPU_CC_GX_CXO_CLK					15
#define GPU_CC_GX_GFX3D_CLK					16
#define GPU_CC_GX_GFX3D_CLK_SRC					17
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK				18
#define GPU_CC_MEMNOC_GFX_CLK					19
#define GPU_CC_SLEEP_CLK					20
#define GPU_CC_XO_CLK_SRC					21

/* GPU_CC resets */
#define GPUCC_GPU_CC_CX_BCR					0
#define GPUCC_GPU_CC_GFX3D_AON_BCR				1
#define GPUCC_GPU_CC_GMU_BCR					2
#define GPUCC_GPU_CC_GX_BCR					3
#define GPUCC_GPU_CC_XO_BCR					4
#define GPU_CC_FREQUENCY_LIMITER_IRQ_CLEAR			5

#endif

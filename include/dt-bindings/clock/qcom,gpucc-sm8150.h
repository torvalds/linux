/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8150_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8150_H

/* GPU_CC clocks */
#define GPU_CC_PLL1				0
#define GPU_CC_AHB_CLK				1
#define GPU_CC_CRC_AHB_CLK			2
#define GPU_CC_CX_GMU_CLK			3
#define GPU_CC_CX_SNOC_DVM_CLK			4
#define GPU_CC_CXO_AON_CLK			5
#define GPU_CC_CXO_CLK				6
#define GPU_CC_GMU_CLK_SRC			7
#define GPU_CC_GX_GMU_CLK			8
#define GPU_CC_SLEEP_CLK			9

/* GPU_CC resets */
#define GPUCC_GPU_CC_ACD_BCR			0
#define GPUCC_GPU_CC_CX_BCR			1
#define GPUCC_GPU_CC_GFX3D_AON_BCR		2
#define GPUCC_GPU_CC_GMU_BCR			3
#define GPUCC_GPU_CC_GX_BCR			4
#define GPUCC_GPU_CC_SPDM_BCR			5
#define GPUCC_GPU_CC_XO_BCR			6


/* GPU_CC GDSCRs */
#define GPU_CX_GDSC				0
#define GPU_GX_GDSC				1

#endif

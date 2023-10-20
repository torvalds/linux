/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_HOLI_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_HOLI_H

/* GPU_CC clocks */
#define GPU_CC_PLL0						0
#define GPU_CC_PLL1						1
#define GPU_CC_AHB_CLK						2
#define GPU_CC_CX_GFX3D_CLK					3
#define GPU_CC_CX_GFX3D_SLV_CLK					4
#define GPU_CC_CX_GMU_CLK					5
#define GPU_CC_CX_SNOC_DVM_CLK					6
#define GPU_CC_CXO_AON_CLK					7
#define GPU_CC_CXO_CLK						8
#define GPU_CC_GMU_CLK_SRC					9
#define GPU_CC_GX_CXO_CLK					10
#define GPU_CC_GX_GFX3D_CLK					11
#define GPU_CC_GX_GFX3D_CLK_SRC					12
#define GPU_CC_GX_GMU_CLK					13
#define GPU_CC_SLEEP_CLK					14

/* GPU_CC resets */

#define GPUCC_GPU_CC_FREQUENCY_LIMITER_IRQ_CLEAR		0

#endif

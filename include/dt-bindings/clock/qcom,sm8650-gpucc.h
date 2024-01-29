/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8650_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8650_H

/* GPU_CC clocks */
#define GPU_CC_AHB_CLK				0
#define GPU_CC_CRC_AHB_CLK			1
#define GPU_CC_CX_ACCU_SHIFT_CLK		2
#define GPU_CC_CX_FF_CLK			3
#define GPU_CC_CX_GMU_CLK			4
#define GPU_CC_CXO_AON_CLK			5
#define GPU_CC_CXO_CLK				6
#define GPU_CC_DEMET_CLK			7
#define GPU_CC_DPM_CLK				8
#define GPU_CC_FF_CLK_SRC			9
#define GPU_CC_FREQ_MEASURE_CLK			10
#define GPU_CC_GMU_CLK_SRC			11
#define GPU_CC_GX_ACCU_SHIFT_CLK		12
#define GPU_CC_GX_FF_CLK			13
#define GPU_CC_GX_GFX3D_CLK			14
#define GPU_CC_GX_GFX3D_RDVM_CLK		15
#define GPU_CC_GX_GMU_CLK			16
#define GPU_CC_GX_VSENSE_CLK			17
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK		18
#define GPU_CC_HUB_AON_CLK			19
#define GPU_CC_HUB_CLK_SRC			20
#define GPU_CC_HUB_CX_INT_CLK			21
#define GPU_CC_HUB_DIV_CLK_SRC			22
#define GPU_CC_MEMNOC_GFX_CLK			23
#define GPU_CC_PLL0				24
#define GPU_CC_PLL1				25
#define GPU_CC_SLEEP_CLK			26

/* GDSCs */
#define GPU_GX_GDSC				0
#define GPU_CX_GDSC				1

#endif

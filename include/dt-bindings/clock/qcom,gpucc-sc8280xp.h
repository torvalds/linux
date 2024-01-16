/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SC8280XP_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SC8280XP_H

/* GPU_CC clocks */
#define GPU_CC_PLL0						0
#define GPU_CC_PLL1						1
#define GPU_CC_AHB_CLK						2
#define GPU_CC_CB_CLK						3
#define GPU_CC_CRC_AHB_CLK					4
#define GPU_CC_CX_GMU_CLK					5
#define GPU_CC_CX_SNOC_DVM_CLK					6
#define GPU_CC_CXO_AON_CLK					7
#define GPU_CC_CXO_CLK						8
#define GPU_CC_FREQ_MEASURE_CLK					9
#define GPU_CC_GMU_CLK_SRC					10
#define GPU_CC_GX_GMU_CLK					11
#define GPU_CC_GX_VSENSE_CLK					12
#define GPU_CC_HUB_AHB_DIV_CLK_SRC				13
#define GPU_CC_HUB_AON_CLK					14
#define GPU_CC_HUB_CLK_SRC					15
#define GPU_CC_HUB_CX_INT_CLK					16
#define GPU_CC_HUB_CX_INT_DIV_CLK_SRC				17
#define GPU_CC_SLEEP_CLK					18
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK				19

/* GPU_CC power domains */
#define GPU_CC_CX_GDSC				0
#define GPU_CC_GX_GDSC				1

#endif

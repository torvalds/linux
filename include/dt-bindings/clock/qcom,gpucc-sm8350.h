/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8350_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM8350_H

/* GPU_CC clocks */
#define GPU_CC_AHB_CLK			0
#define GPU_CC_CB_CLK			1
#define GPU_CC_CRC_AHB_CLK		2
#define GPU_CC_CX_APB_CLK		3
#define GPU_CC_CX_GMU_CLK		4
#define GPU_CC_CX_QDSS_AT_CLK		5
#define GPU_CC_CX_QDSS_TRIG_CLK		6
#define GPU_CC_CX_QDSS_TSCTR_CLK	7
#define GPU_CC_CX_SNOC_DVM_CLK		8
#define GPU_CC_CXO_AON_CLK		9
#define GPU_CC_CXO_CLK			10
#define GPU_CC_FREQ_MEASURE_CLK		11
#define GPU_CC_GMU_CLK_SRC		12
#define GPU_CC_GX_GMU_CLK		13
#define GPU_CC_GX_QDSS_TSCTR_CLK	14
#define GPU_CC_GX_VSENSE_CLK		15
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK	16
#define GPU_CC_HUB_AHB_DIV_CLK_SRC	17
#define GPU_CC_HUB_AON_CLK		18
#define GPU_CC_HUB_CLK_SRC		19
#define GPU_CC_HUB_CX_INT_CLK		20
#define GPU_CC_HUB_CX_INT_DIV_CLK_SRC	21
#define GPU_CC_MND1X_0_GFX3D_CLK	22
#define GPU_CC_MND1X_1_GFX3D_CLK	23
#define GPU_CC_PLL0			24
#define GPU_CC_PLL1			25
#define GPU_CC_SLEEP_CLK		26

/* GPU_CC resets */
#define GPUCC_GPU_CC_ACD_BCR		0
#define GPUCC_GPU_CC_CB_BCR		1
#define GPUCC_GPU_CC_CX_BCR		2
#define GPUCC_GPU_CC_FAST_HUB_BCR	3
#define GPUCC_GPU_CC_GFX3D_AON_BCR	4
#define GPUCC_GPU_CC_GMU_BCR		5
#define GPUCC_GPU_CC_GX_BCR		6
#define GPUCC_GPU_CC_XO_BCR		7

/* GPU_CC GDSCRs */
#define GPU_CX_GDSC			0
#define GPU_GX_GDSC			1

#endif

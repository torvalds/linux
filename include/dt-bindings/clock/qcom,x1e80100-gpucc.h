/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_X1E80100_GPU_CC_H
#define _DT_BINDINGS_CLK_QCOM_X1E80100_GPU_CC_H

/* GPU_CC clocks */
#define GPU_CC_AHB_CLK						0
#define GPU_CC_CB_CLK						1
#define GPU_CC_CRC_AHB_CLK					2
#define GPU_CC_CX_FF_CLK					3
#define GPU_CC_CX_GMU_CLK					4
#define GPU_CC_CXO_AON_CLK					5
#define GPU_CC_CXO_CLK						6
#define GPU_CC_DEMET_CLK					7
#define GPU_CC_DEMET_DIV_CLK_SRC				8
#define GPU_CC_FF_CLK_SRC					9
#define GPU_CC_FREQ_MEASURE_CLK					10
#define GPU_CC_GMU_CLK_SRC					11
#define GPU_CC_GX_GMU_CLK					12
#define GPU_CC_GX_VSENSE_CLK					13
#define GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK				14
#define GPU_CC_HUB_AON_CLK					15
#define GPU_CC_HUB_CLK_SRC					16
#define GPU_CC_HUB_CX_INT_CLK					17
#define GPU_CC_MEMNOC_GFX_CLK					18
#define GPU_CC_MND1X_0_GFX3D_CLK				19
#define GPU_CC_MND1X_1_GFX3D_CLK				20
#define GPU_CC_PLL0						21
#define GPU_CC_PLL1						22
#define GPU_CC_SLEEP_CLK					23
#define GPU_CC_XO_CLK_SRC					24
#define GPU_CC_XO_DIV_CLK_SRC					25
#define GPU_CC_CX_ACCU_SHIFT_CLK				26
#define GPU_CC_GX_ACCU_SHIFT_CLK				27

/* GDSCs */
#define GPU_CX_GDSC						0
#define GPU_GX_GDSC						1

/* GPU_CC resets */
#define GPU_CC_ACD_BCR                                          0
#define GPU_CC_CB_BCR                                           1
#define GPU_CC_CX_BCR                                           2
#define GPU_CC_FAST_HUB_BCR                                     3
#define GPU_CC_FF_BCR                                           4
#define GPU_CC_GFX3D_AON_BCR                                    5
#define GPU_CC_GMU_BCR                                          6
#define GPU_CC_GX_BCR                                           7
#define GPU_CC_XO_BCR                                           8

#endif

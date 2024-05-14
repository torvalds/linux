/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6350_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6350_H

/* GPU_CC clocks */
#define GPU_CC_PLL0						0
#define GPU_CC_PLL1						1
#define GPU_CC_ACD_AHB_CLK					2
#define GPU_CC_ACD_CXO_CLK					3
#define GPU_CC_AHB_CLK						4
#define GPU_CC_CRC_AHB_CLK					5
#define GPU_CC_CX_GFX3D_CLK					6
#define GPU_CC_CX_GFX3D_SLV_CLK					7
#define GPU_CC_CX_GMU_CLK					8
#define GPU_CC_CX_SNOC_DVM_CLK					9
#define GPU_CC_CXO_AON_CLK					10
#define GPU_CC_CXO_CLK						11
#define GPU_CC_GMU_CLK_SRC					12
#define GPU_CC_GX_CXO_CLK					13
#define GPU_CC_GX_GFX3D_CLK					14
#define GPU_CC_GX_GFX3D_CLK_SRC					15
#define GPU_CC_GX_GMU_CLK					16
#define GPU_CC_GX_VSENSE_CLK					17

/* CLK_HW */
#define GPU_CC_CRC_DIV						0

/* GDSCs */
#define GPU_CX_GDSC						0
#define GPU_GX_GDSC						1

#endif

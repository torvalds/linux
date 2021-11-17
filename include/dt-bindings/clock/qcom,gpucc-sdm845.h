/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_SDM_GPU_CC_SDM845_H
#define _DT_BINDINGS_CLK_SDM_GPU_CC_SDM845_H

/* GPU_CC clock registers */
#define GPU_CC_CX_GMU_CLK			0
#define GPU_CC_CXO_CLK				1
#define GPU_CC_GMU_CLK_SRC			2
#define GPU_CC_PLL1				3

/* GPU_CC Resets */
#define GPUCC_GPU_CC_CX_BCR			0
#define GPUCC_GPU_CC_GMU_BCR			1
#define GPUCC_GPU_CC_XO_BCR			2

/* GPU_CC GDSCRs */
#define GPU_CX_GDSC				0
#define GPU_GX_GDSC				1

#endif

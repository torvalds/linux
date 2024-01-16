/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, Jeffrey Hugo
 */

#ifndef _DT_BINDINGS_CLK_MSM_GPUCC_8998_H
#define _DT_BINDINGS_CLK_MSM_GPUCC_8998_H

#define GPUPLL0						0
#define GPUPLL0_OUT_EVEN				1
#define RBCPR_CLK_SRC					2
#define GFX3D_CLK_SRC					3
#define RBBMTIMER_CLK_SRC				4
#define GFX3D_ISENSE_CLK_SRC				5
#define RBCPR_CLK					6
#define GFX3D_CLK					7
#define RBBMTIMER_CLK					8
#define GFX3D_ISENSE_CLK				9
#define GPUCC_CXO_CLK					10

#define GPU_CX_BCR					0
#define RBCPR_BCR					1
#define GPU_GX_BCR					2
#define GPU_ISENSE_BCR					3

#define GPU_CX_GDSC					1
#define GPU_GX_GDSC					2

#endif

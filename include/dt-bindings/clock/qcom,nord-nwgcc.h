/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_NW_GCC_NORD_H
#define _DT_BINDINGS_CLK_QCOM_NW_GCC_NORD_H

/* NW_GCC clocks */
#define NW_GCC_ACMU_MUX_CLK					0
#define NW_GCC_CAMERA_AHB_CLK					1
#define NW_GCC_CAMERA_HF_AXI_CLK				2
#define NW_GCC_CAMERA_SF_AXI_CLK				3
#define NW_GCC_CAMERA_TRIG_CLK					4
#define NW_GCC_CAMERA_XO_CLK					5
#define NW_GCC_DISP_0_AHB_CLK					6
#define NW_GCC_DISP_0_HF_AXI_CLK				7
#define NW_GCC_DISP_0_TRIG_CLK					8
#define NW_GCC_DISP_1_AHB_CLK					9
#define NW_GCC_DISP_1_HF_AXI_CLK				10
#define NW_GCC_DISP_1_TRIG_CLK					11
#define NW_GCC_DPRX0_AXI_HF_CLK					12
#define NW_GCC_DPRX0_CFG_AHB_CLK				13
#define NW_GCC_DPRX1_AXI_HF_CLK					14
#define NW_GCC_DPRX1_CFG_AHB_CLK				15
#define NW_GCC_EVA_AHB_CLK					16
#define NW_GCC_EVA_AXI0_CLK					17
#define NW_GCC_EVA_AXI0C_CLK					18
#define NW_GCC_EVA_TRIG_CLK					19
#define NW_GCC_EVA_XO_CLK					20
#define NW_GCC_FRQ_MEASURE_REF_CLK				21
#define NW_GCC_GP1_CLK						22
#define NW_GCC_GP1_CLK_SRC					23
#define NW_GCC_GP2_CLK						24
#define NW_GCC_GP2_CLK_SRC					25
#define NW_GCC_GPLL0						26
#define NW_GCC_GPLL0_OUT_EVEN					27
#define NW_GCC_GPU_2_CFG_AHB_CLK				28
#define NW_GCC_GPU_2_GPLL0_CLK_SRC				29
#define NW_GCC_GPU_2_GPLL0_DIV_CLK_SRC				30
#define NW_GCC_GPU_2_HSCNOC_GFX_CLK				31
#define NW_GCC_GPU_CFG_AHB_CLK					32
#define NW_GCC_GPU_GPLL0_CLK_SRC				33
#define NW_GCC_GPU_GPLL0_DIV_CLK_SRC				34
#define NW_GCC_GPU_HSCNOC_GFX_CLK				35
#define NW_GCC_GPU_SMMU_VOTE_CLK				36
#define NW_GCC_HSCNOC_GPU_2_AXI_CLK				37
#define NW_GCC_HSCNOC_GPU_AXI_CLK				38
#define NW_GCC_MMU_1_TCU_VOTE_CLK				39
#define NW_GCC_VIDEO_AHB_CLK					40
#define NW_GCC_VIDEO_AXI0_CLK					41
#define NW_GCC_VIDEO_AXI0C_CLK					42
#define NW_GCC_VIDEO_AXI1_CLK					43
#define NW_GCC_VIDEO_XO_CLK					44

/* NW_GCC power domains */

/* NW_GCC resets */
#define NW_GCC_CAMERA_BCR					0
#define NW_GCC_DISPLAY_0_BCR					1
#define NW_GCC_DISPLAY_1_BCR					2
#define NW_GCC_DPRX0_BCR					3
#define NW_GCC_DPRX1_BCR					4
#define NW_GCC_EVA_BCR						5
#define NW_GCC_GPU_2_BCR					6
#define NW_GCC_GPU_BCR						7
#define NW_GCC_VIDEO_BCR					8
#define NW_GCC_VIDEO_AXI0_CLK_ARES				9
#define NW_GCC_VIDEO_AXI0C_CLK_ARES				10
#define NW_GCC_VIDEO_AXI1_CLK_ARES				11

#endif

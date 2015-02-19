/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_CLK_APQ_MMCC_8084_H
#define _DT_BINDINGS_CLK_APQ_MMCC_8084_H

#define MMSS_AHB_CLK_SRC		0
#define MMSS_AXI_CLK_SRC		1
#define MMPLL0				2
#define MMPLL0_VOTE			3
#define MMPLL1				4
#define MMPLL1_VOTE			5
#define MMPLL2				6
#define MMPLL3				7
#define MMPLL4				8
#define CSI0_CLK_SRC			9
#define CSI1_CLK_SRC			10
#define CSI2_CLK_SRC			11
#define CSI3_CLK_SRC			12
#define VCODEC0_CLK_SRC			13
#define VFE0_CLK_SRC			14
#define VFE1_CLK_SRC			15
#define MDP_CLK_SRC			16
#define PCLK0_CLK_SRC			17
#define PCLK1_CLK_SRC			18
#define OCMEMNOC_CLK_SRC		19
#define GFX3D_CLK_SRC			20
#define JPEG0_CLK_SRC			21
#define JPEG1_CLK_SRC			22
#define JPEG2_CLK_SRC			23
#define EDPPIXEL_CLK_SRC		24
#define EXTPCLK_CLK_SRC			25
#define VP_CLK_SRC			26
#define CCI_CLK_SRC			27
#define CAMSS_GP0_CLK_SRC		28
#define CAMSS_GP1_CLK_SRC		29
#define MCLK0_CLK_SRC			30
#define MCLK1_CLK_SRC			31
#define MCLK2_CLK_SRC			32
#define MCLK3_CLK_SRC			33
#define CSI0PHYTIMER_CLK_SRC		34
#define CSI1PHYTIMER_CLK_SRC		35
#define CSI2PHYTIMER_CLK_SRC		36
#define CPP_CLK_SRC			37
#define BYTE0_CLK_SRC			38
#define BYTE1_CLK_SRC			39
#define EDPAUX_CLK_SRC			40
#define EDPLINK_CLK_SRC			41
#define ESC0_CLK_SRC			42
#define ESC1_CLK_SRC			43
#define HDMI_CLK_SRC			44
#define VSYNC_CLK_SRC			45
#define MMSS_RBCPR_CLK_SRC		46
#define RBBMTIMER_CLK_SRC		47
#define MAPLE_CLK_SRC			48
#define VDP_CLK_SRC			49
#define VPU_BUS_CLK_SRC			50
#define MMSS_CXO_CLK			51
#define MMSS_SLEEPCLK_CLK		52
#define AVSYNC_AHB_CLK			53
#define AVSYNC_EDPPIXEL_CLK		54
#define AVSYNC_EXTPCLK_CLK		55
#define AVSYNC_PCLK0_CLK		56
#define AVSYNC_PCLK1_CLK		57
#define AVSYNC_VP_CLK			58
#define CAMSS_AHB_CLK			59
#define CAMSS_CCI_CCI_AHB_CLK		60
#define CAMSS_CCI_CCI_CLK		61
#define CAMSS_CSI0_AHB_CLK		62
#define CAMSS_CSI0_CLK			63
#define CAMSS_CSI0PHY_CLK		64
#define CAMSS_CSI0PIX_CLK		65
#define CAMSS_CSI0RDI_CLK		66
#define CAMSS_CSI1_AHB_CLK		67
#define CAMSS_CSI1_CLK			68
#define CAMSS_CSI1PHY_CLK		69
#define CAMSS_CSI1PIX_CLK		70
#define CAMSS_CSI1RDI_CLK		71
#define CAMSS_CSI2_AHB_CLK		72
#define CAMSS_CSI2_CLK			73
#define CAMSS_CSI2PHY_CLK		74
#define CAMSS_CSI2PIX_CLK		75
#define CAMSS_CSI2RDI_CLK		76
#define CAMSS_CSI3_AHB_CLK		77
#define CAMSS_CSI3_CLK			78
#define CAMSS_CSI3PHY_CLK		79
#define CAMSS_CSI3PIX_CLK		80
#define CAMSS_CSI3RDI_CLK		81
#define CAMSS_CSI_VFE0_CLK		82
#define CAMSS_CSI_VFE1_CLK		83
#define CAMSS_GP0_CLK			84
#define CAMSS_GP1_CLK			85
#define CAMSS_ISPIF_AHB_CLK		86
#define CAMSS_JPEG_JPEG0_CLK		87
#define CAMSS_JPEG_JPEG1_CLK		88
#define CAMSS_JPEG_JPEG2_CLK		89
#define CAMSS_JPEG_JPEG_AHB_CLK		90
#define CAMSS_JPEG_JPEG_AXI_CLK		91
#define CAMSS_MCLK0_CLK			92
#define CAMSS_MCLK1_CLK			93
#define CAMSS_MCLK2_CLK			94
#define CAMSS_MCLK3_CLK			95
#define CAMSS_MICRO_AHB_CLK		96
#define CAMSS_PHY0_CSI0PHYTIMER_CLK	97
#define CAMSS_PHY1_CSI1PHYTIMER_CLK	98
#define CAMSS_PHY2_CSI2PHYTIMER_CLK	99
#define CAMSS_TOP_AHB_CLK		100
#define CAMSS_VFE_CPP_AHB_CLK		101
#define CAMSS_VFE_CPP_CLK		102
#define CAMSS_VFE_VFE0_CLK		103
#define CAMSS_VFE_VFE1_CLK		104
#define CAMSS_VFE_VFE_AHB_CLK		105
#define CAMSS_VFE_VFE_AXI_CLK		106
#define MDSS_AHB_CLK			107
#define MDSS_AXI_CLK			108
#define MDSS_BYTE0_CLK			109
#define MDSS_BYTE1_CLK			110
#define MDSS_EDPAUX_CLK			111
#define MDSS_EDPLINK_CLK		112
#define MDSS_EDPPIXEL_CLK		113
#define MDSS_ESC0_CLK			114
#define MDSS_ESC1_CLK			115
#define MDSS_EXTPCLK_CLK		116
#define MDSS_HDMI_AHB_CLK		117
#define MDSS_HDMI_CLK			118
#define MDSS_MDP_CLK			119
#define MDSS_MDP_LUT_CLK		120
#define MDSS_PCLK0_CLK			121
#define MDSS_PCLK1_CLK			122
#define MDSS_VSYNC_CLK			123
#define MMSS_RBCPR_AHB_CLK		124
#define MMSS_RBCPR_CLK			125
#define MMSS_SPDM_AHB_CLK		126
#define MMSS_SPDM_AXI_CLK		127
#define MMSS_SPDM_CSI0_CLK		128
#define MMSS_SPDM_GFX3D_CLK		129
#define MMSS_SPDM_JPEG0_CLK		130
#define MMSS_SPDM_JPEG1_CLK		131
#define MMSS_SPDM_JPEG2_CLK		132
#define MMSS_SPDM_MDP_CLK		133
#define MMSS_SPDM_PCLK0_CLK		134
#define MMSS_SPDM_PCLK1_CLK		135
#define MMSS_SPDM_VCODEC0_CLK		136
#define MMSS_SPDM_VFE0_CLK		137
#define MMSS_SPDM_VFE1_CLK		138
#define MMSS_SPDM_RM_AXI_CLK		139
#define MMSS_SPDM_RM_OCMEMNOC_CLK	140
#define MMSS_MISC_AHB_CLK		141
#define MMSS_MMSSNOC_AHB_CLK		142
#define MMSS_MMSSNOC_BTO_AHB_CLK	143
#define MMSS_MMSSNOC_AXI_CLK		144
#define MMSS_S0_AXI_CLK			145
#define OCMEMCX_AHB_CLK			146
#define OCMEMCX_OCMEMNOC_CLK		147
#define OXILI_OCMEMGX_CLK		148
#define OXILI_GFX3D_CLK			149
#define OXILI_RBBMTIMER_CLK		150
#define OXILICX_AHB_CLK			151
#define VENUS0_AHB_CLK			152
#define VENUS0_AXI_CLK			153
#define VENUS0_CORE0_VCODEC_CLK		154
#define VENUS0_CORE1_VCODEC_CLK		155
#define VENUS0_OCMEMNOC_CLK		156
#define VENUS0_VCODEC0_CLK		157
#define VPU_AHB_CLK			158
#define VPU_AXI_CLK			159
#define VPU_BUS_CLK			160
#define VPU_CXO_CLK			161
#define VPU_MAPLE_CLK			162
#define VPU_SLEEP_CLK			163
#define VPU_VDP_CLK			164

#endif

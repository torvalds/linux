/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

#ifndef _DT_BINDINGS_CLK_MSM_MMCC_8974_H
#define _DT_BINDINGS_CLK_MSM_MMCC_8974_H

#define MMSS_AHB_CLK_SRC				0
#define MMSS_AXI_CLK_SRC				1
#define MMPLL0						2
#define MMPLL0_VOTE					3
#define MMPLL1						4
#define MMPLL1_VOTE					5
#define MMPLL2						6
#define MMPLL3						7
#define CSI0_CLK_SRC					8
#define CSI1_CLK_SRC					9
#define CSI2_CLK_SRC					10
#define CSI3_CLK_SRC					11
#define VFE0_CLK_SRC					12
#define VFE1_CLK_SRC					13
#define MDP_CLK_SRC					14
#define GFX3D_CLK_SRC					15
#define JPEG0_CLK_SRC					16
#define JPEG1_CLK_SRC					17
#define JPEG2_CLK_SRC					18
#define PCLK0_CLK_SRC					19
#define PCLK1_CLK_SRC					20
#define VCODEC0_CLK_SRC					21
#define CCI_CLK_SRC					22
#define CAMSS_GP0_CLK_SRC				23
#define CAMSS_GP1_CLK_SRC				24
#define MCLK0_CLK_SRC					25
#define MCLK1_CLK_SRC					26
#define MCLK2_CLK_SRC					27
#define MCLK3_CLK_SRC					28
#define CSI0PHYTIMER_CLK_SRC				29
#define CSI1PHYTIMER_CLK_SRC				30
#define CSI2PHYTIMER_CLK_SRC				31
#define CPP_CLK_SRC					32
#define BYTE0_CLK_SRC					33
#define BYTE1_CLK_SRC					34
#define EDPAUX_CLK_SRC					35
#define EDPLINK_CLK_SRC					36
#define EDPPIXEL_CLK_SRC				37
#define ESC0_CLK_SRC					38
#define ESC1_CLK_SRC					39
#define EXTPCLK_CLK_SRC					40
#define HDMI_CLK_SRC					41
#define VSYNC_CLK_SRC					42
#define MMSS_RBCPR_CLK_SRC				43
#define CAMSS_CCI_CCI_AHB_CLK				44
#define CAMSS_CCI_CCI_CLK				45
#define CAMSS_CSI0_AHB_CLK				46
#define CAMSS_CSI0_CLK					47
#define CAMSS_CSI0PHY_CLK				48
#define CAMSS_CSI0PIX_CLK				49
#define CAMSS_CSI0RDI_CLK				50
#define CAMSS_CSI1_AHB_CLK				51
#define CAMSS_CSI1_CLK					52
#define CAMSS_CSI1PHY_CLK				53
#define CAMSS_CSI1PIX_CLK				54
#define CAMSS_CSI1RDI_CLK				55
#define CAMSS_CSI2_AHB_CLK				56
#define CAMSS_CSI2_CLK					57
#define CAMSS_CSI2PHY_CLK				58
#define CAMSS_CSI2PIX_CLK				59
#define CAMSS_CSI2RDI_CLK				60
#define CAMSS_CSI3_AHB_CLK				61
#define CAMSS_CSI3_CLK					62
#define CAMSS_CSI3PHY_CLK				63
#define CAMSS_CSI3PIX_CLK				64
#define CAMSS_CSI3RDI_CLK				65
#define CAMSS_CSI_VFE0_CLK				66
#define CAMSS_CSI_VFE1_CLK				67
#define CAMSS_GP0_CLK					68
#define CAMSS_GP1_CLK					69
#define CAMSS_ISPIF_AHB_CLK				70
#define CAMSS_JPEG_JPEG0_CLK				71
#define CAMSS_JPEG_JPEG1_CLK				72
#define CAMSS_JPEG_JPEG2_CLK				73
#define CAMSS_JPEG_JPEG_AHB_CLK				74
#define CAMSS_JPEG_JPEG_AXI_CLK				75
#define CAMSS_JPEG_JPEG_OCMEMNOC_CLK			76
#define CAMSS_MCLK0_CLK					77
#define CAMSS_MCLK1_CLK					78
#define CAMSS_MCLK2_CLK					79
#define CAMSS_MCLK3_CLK					80
#define CAMSS_MICRO_AHB_CLK				81
#define CAMSS_PHY0_CSI0PHYTIMER_CLK			82
#define CAMSS_PHY1_CSI1PHYTIMER_CLK			83
#define CAMSS_PHY2_CSI2PHYTIMER_CLK			84
#define CAMSS_TOP_AHB_CLK				85
#define CAMSS_VFE_CPP_AHB_CLK				86
#define CAMSS_VFE_CPP_CLK				87
#define CAMSS_VFE_VFE0_CLK				88
#define CAMSS_VFE_VFE1_CLK				89
#define CAMSS_VFE_VFE_AHB_CLK				90
#define CAMSS_VFE_VFE_AXI_CLK				91
#define CAMSS_VFE_VFE_OCMEMNOC_CLK			92
#define MDSS_AHB_CLK					93
#define MDSS_AXI_CLK					94
#define MDSS_BYTE0_CLK					95
#define MDSS_BYTE1_CLK					96
#define MDSS_EDPAUX_CLK					97
#define MDSS_EDPLINK_CLK				98
#define MDSS_EDPPIXEL_CLK				99
#define MDSS_ESC0_CLK					100
#define MDSS_ESC1_CLK					101
#define MDSS_EXTPCLK_CLK				102
#define MDSS_HDMI_AHB_CLK				103
#define MDSS_HDMI_CLK					104
#define MDSS_MDP_CLK					105
#define MDSS_MDP_LUT_CLK				106
#define MDSS_PCLK0_CLK					107
#define MDSS_PCLK1_CLK					108
#define MDSS_VSYNC_CLK					109
#define MMSS_MISC_AHB_CLK				110
#define MMSS_MMSSNOC_AHB_CLK				111
#define MMSS_MMSSNOC_BTO_AHB_CLK			112
#define MMSS_MMSSNOC_AXI_CLK				113
#define MMSS_S0_AXI_CLK					114
#define OCMEMCX_AHB_CLK					115
#define OCMEMCX_OCMEMNOC_CLK				116
#define OXILI_OCMEMGX_CLK				117
#define OCMEMNOC_CLK					118
#define OXILI_GFX3D_CLK					119
#define OXILICX_AHB_CLK					120
#define OXILICX_AXI_CLK					121
#define VENUS0_AHB_CLK					122
#define VENUS0_AXI_CLK					123
#define VENUS0_OCMEMNOC_CLK				124
#define VENUS0_VCODEC0_CLK				125
#define OCMEMNOC_CLK_SRC				126
#define SPDM_JPEG0					127
#define SPDM_JPEG1					128
#define SPDM_MDP					129
#define SPDM_AXI					130
#define SPDM_VCODEC0					131
#define SPDM_VFE0					132
#define SPDM_VFE1					133
#define SPDM_JPEG2					134
#define SPDM_PCLK1					135
#define SPDM_GFX3D					136
#define SPDM_AHB					137
#define SPDM_PCLK0					138
#define SPDM_OCMEMNOC					139
#define SPDM_CSI0					140
#define SPDM_RM_AXI					141
#define SPDM_RM_OCMEMNOC				142

#endif

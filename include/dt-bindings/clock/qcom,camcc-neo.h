/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2021-2022, 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_NEO_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_NEO_H

/* CAM_CC clocks */
#define CAM_CC_PLL0						0
#define CAM_CC_PLL0_OUT_EVEN					1
#define CAM_CC_PLL0_OUT_ODD					2
#define CAM_CC_PLL1						3
#define CAM_CC_PLL1_OUT_EVEN					4
#define CAM_CC_PLL2						5
#define CAM_CC_PLL2_OUT_EVEN					6
#define CAM_CC_PLL3						7
#define CAM_CC_PLL3_OUT_EVEN					8
#define CAM_CC_PLL4						9
#define CAM_CC_PLL4_OUT_EVEN					10
#define CAM_CC_PLL5						11
#define CAM_CC_PLL5_OUT_EVEN					12
#define CAM_CC_PLL6						13
#define CAM_CC_PLL6_OUT_EVEN					14
#define CAM_CC_PLL6_OUT_ODD					15
#define CAM_CC_BPS_AHB_CLK					16
#define CAM_CC_BPS_CLK						17
#define CAM_CC_BPS_CLK_SRC					18
#define CAM_CC_BPS_FAST_AHB_CLK					19
#define CAM_CC_CAMNOC_AHB_CLK					20
#define CAM_CC_CAMNOC_AXI_CLK					21
#define CAM_CC_CAMNOC_AXI_CLK_SRC				22
#define CAM_CC_CAMNOC_DCD_XO_CLK				23
#define CAM_CC_CAMNOC_XO_CLK					24
#define CAM_CC_CCI_0_CLK					25
#define CAM_CC_CCI_0_CLK_SRC					26
#define CAM_CC_CCI_1_CLK					27
#define CAM_CC_CCI_1_CLK_SRC					28
#define CAM_CC_CCI_2_CLK					29
#define CAM_CC_CCI_2_CLK_SRC					30
#define CAM_CC_CCI_3_CLK					31
#define CAM_CC_CCI_3_CLK_SRC					32
#define CAM_CC_CORE_AHB_CLK					33
#define CAM_CC_CPAS_AHB_CLK					34
#define CAM_CC_CPAS_BPS_CLK					35
#define CAM_CC_CPAS_FAST_AHB_CLK				36
#define CAM_CC_CPAS_IFE_0_CLK					37
#define CAM_CC_CPAS_IFE_1_CLK					38
#define CAM_CC_CPAS_IFE_LITE_CLK				39
#define CAM_CC_CPAS_IPE_NPS_CLK					40
#define CAM_CC_CPHY_RX_CLK_SRC					41
#define CAM_CC_CSI0PHYTIMER_CLK					42
#define CAM_CC_CSI0PHYTIMER_CLK_SRC				43
#define CAM_CC_CSI1PHYTIMER_CLK					44
#define CAM_CC_CSI1PHYTIMER_CLK_SRC				45
#define CAM_CC_CSI2PHYTIMER_CLK					46
#define CAM_CC_CSI2PHYTIMER_CLK_SRC				47
#define CAM_CC_CSI3PHYTIMER_CLK					48
#define CAM_CC_CSI3PHYTIMER_CLK_SRC				49
#define CAM_CC_CSID_CLK						50
#define CAM_CC_CSID_CLK_SRC					51
#define CAM_CC_CSID_CSIPHY_RX_CLK				52
#define CAM_CC_CSIPHY0_CLK					53
#define CAM_CC_CSIPHY1_CLK					54
#define CAM_CC_CSIPHY2_CLK					55
#define CAM_CC_CSIPHY3_CLK					56
#define CAM_CC_DRV_AHB_CLK					57
#define CAM_CC_DRV_XO_CLK					58
#define CAM_CC_FAST_AHB_CLK_SRC					59
#define CAM_CC_GDSC_CLK						60
#define CAM_CC_ICP_AHB_CLK					61
#define CAM_CC_ICP_CLK						62
#define CAM_CC_ICP_CLK_SRC					63
#define CAM_CC_IFE_0_CLK					64
#define CAM_CC_IFE_0_CLK_SRC					65
#define CAM_CC_IFE_0_DSP_CLK					66
#define CAM_CC_IFE_0_FAST_AHB_CLK				67
#define CAM_CC_IFE_1_CLK					68
#define CAM_CC_IFE_1_CLK_SRC					69
#define CAM_CC_IFE_1_DSP_CLK					70
#define CAM_CC_IFE_1_FAST_AHB_CLK				71
#define CAM_CC_IFE_LITE_AHB_CLK					72
#define CAM_CC_IFE_LITE_CLK					73
#define CAM_CC_IFE_LITE_CLK_SRC					74
#define CAM_CC_IFE_LITE_CPHY_RX_CLK				75
#define CAM_CC_IFE_LITE_CSID_CLK				76
#define CAM_CC_IFE_LITE_CSID_CLK_SRC				77
#define CAM_CC_IPE_NPS_AHB_CLK					78
#define CAM_CC_IPE_NPS_CLK					79
#define CAM_CC_IPE_NPS_CLK_SRC					80
#define CAM_CC_IPE_NPS_FAST_AHB_CLK				81
#define CAM_CC_IPE_PPS_CLK					82
#define CAM_CC_IPE_PPS_FAST_AHB_CLK				83
#define CAM_CC_JPEG_1_CLK					84
#define CAM_CC_JPEG_2_CLK					85
#define CAM_CC_JPEG_CLK						86
#define CAM_CC_JPEG_CLK_SRC					87
#define CAM_CC_MCLK0_CLK					88
#define CAM_CC_MCLK0_CLK_SRC					89
#define CAM_CC_MCLK1_CLK					90
#define CAM_CC_MCLK1_CLK_SRC					91
#define CAM_CC_MCLK2_CLK					92
#define CAM_CC_MCLK2_CLK_SRC					93
#define CAM_CC_MCLK3_CLK					94
#define CAM_CC_MCLK3_CLK_SRC					95
#define CAM_CC_MCLK4_CLK					96
#define CAM_CC_MCLK4_CLK_SRC					97
#define CAM_CC_MCLK5_CLK					98
#define CAM_CC_MCLK5_CLK_SRC					99
#define CAM_CC_MCLK6_CLK					100
#define CAM_CC_MCLK6_CLK_SRC					101
#define CAM_CC_MCLK7_CLK					102
#define CAM_CC_MCLK7_CLK_SRC					103
#define CAM_CC_SLEEP_CLK					107
#define CAM_CC_SLEEP_CLK_SRC					108
#define CAM_CC_SLOW_AHB_CLK_SRC					109
#define CAM_CC_XO_CLK_SRC					110
#define CAM_CC_QDSS_DEBUG_CLK					111
#define CAM_CC_QDSS_DEBUG_CLK_SRC				112
#define CAM_CC_QDSS_DEBUG_XO_CLK				113

/* CAM_CC resets */
#define CAM_CC_BPS_BCR						0
#define CAM_CC_DRV_BCR						1
#define CAM_CC_ICP_BCR						2
#define CAM_CC_IFE_0_BCR					3
#define CAM_CC_IFE_1_BCR					4
#define CAM_CC_IPE_0_BCR					5
#define CAM_CC_QDSS_DEBUG_BCR					6

#endif

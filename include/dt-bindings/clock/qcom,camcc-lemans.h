/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_LEMANS_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_LEMANS_H

/* CAM_CC clocks */
#define CAM_CC_PLL0					0
#define CAM_CC_PLL0_OUT_EVEN				1
#define CAM_CC_PLL0_OUT_ODD				2
#define CAM_CC_PLL2					3
#define CAM_CC_PLL3					4
#define CAM_CC_PLL3_OUT_EVEN				5
#define CAM_CC_PLL4					6
#define CAM_CC_PLL4_OUT_EVEN				7
#define CAM_CC_PLL5					8
#define CAM_CC_PLL5_OUT_EVEN				9
#define CAM_CC_CAMNOC_AXI_CLK				10
#define CAM_CC_CAMNOC_AXI_CLK_SRC			11
#define CAM_CC_CAMNOC_DCD_XO_CLK			12
#define CAM_CC_CAMNOC_XO_CLK				13
#define CAM_CC_CCI_0_CLK				14
#define CAM_CC_CCI_0_CLK_SRC				15
#define CAM_CC_CCI_1_CLK				16
#define CAM_CC_CCI_1_CLK_SRC				17
#define CAM_CC_CCI_2_CLK				18
#define CAM_CC_CCI_2_CLK_SRC				19
#define CAM_CC_CCI_3_CLK				20
#define CAM_CC_CCI_3_CLK_SRC				21
#define CAM_CC_CORE_AHB_CLK				22
#define CAM_CC_CPAS_AHB_CLK				23
#define CAM_CC_CPAS_FAST_AHB_CLK			24
#define CAM_CC_CPAS_IFE_0_CLK				25
#define CAM_CC_CPAS_IFE_1_CLK				26
#define CAM_CC_CPAS_IFE_LITE_CLK			27
#define CAM_CC_CPAS_IPE_CLK				28
#define CAM_CC_CPAS_SFE_LITE_0_CLK			29
#define CAM_CC_CPAS_SFE_LITE_1_CLK			30
#define CAM_CC_CPHY_RX_CLK_SRC				31
#define CAM_CC_CSI0PHYTIMER_CLK				32
#define CAM_CC_CSI0PHYTIMER_CLK_SRC			33
#define CAM_CC_CSI1PHYTIMER_CLK				34
#define CAM_CC_CSI1PHYTIMER_CLK_SRC			35
#define CAM_CC_CSI2PHYTIMER_CLK				36
#define CAM_CC_CSI2PHYTIMER_CLK_SRC			37
#define CAM_CC_CSI3PHYTIMER_CLK				38
#define CAM_CC_CSI3PHYTIMER_CLK_SRC			39
#define CAM_CC_CSID_CLK					40
#define CAM_CC_CSID_CLK_SRC				41
#define CAM_CC_CSID_CSIPHY_RX_CLK			42
#define CAM_CC_CSIPHY0_CLK				43
#define CAM_CC_CSIPHY1_CLK				44
#define CAM_CC_CSIPHY2_CLK				45
#define CAM_CC_CSIPHY3_CLK				46
#define CAM_CC_FAST_AHB_CLK_SRC				47
#define CAM_CC_GDSC_CLK					48
#define CAM_CC_ICP_AHB_CLK				49
#define CAM_CC_ICP_CLK					50
#define CAM_CC_ICP_CLK_SRC				51
#define CAM_CC_IFE_0_CLK				52
#define CAM_CC_IFE_0_CLK_SRC				53
#define CAM_CC_IFE_0_FAST_AHB_CLK			54
#define CAM_CC_IFE_1_CLK				55
#define CAM_CC_IFE_1_CLK_SRC				56
#define CAM_CC_IFE_1_FAST_AHB_CLK			57
#define CAM_CC_IFE_LITE_AHB_CLK				58
#define CAM_CC_IFE_LITE_CLK				59
#define CAM_CC_IFE_LITE_CLK_SRC				60
#define CAM_CC_IFE_LITE_CPHY_RX_CLK			61
#define CAM_CC_IFE_LITE_CSID_CLK			62
#define CAM_CC_IFE_LITE_CSID_CLK_SRC			63
#define CAM_CC_IPE_AHB_CLK				64
#define CAM_CC_IPE_CLK					65
#define CAM_CC_IPE_CLK_SRC				66
#define CAM_CC_IPE_FAST_AHB_CLK				67
#define CAM_CC_MCLK0_CLK				68
#define CAM_CC_MCLK0_CLK_SRC				69
#define CAM_CC_MCLK1_CLK				70
#define CAM_CC_MCLK1_CLK_SRC				71
#define CAM_CC_MCLK2_CLK				72
#define CAM_CC_MCLK2_CLK_SRC				73
#define CAM_CC_MCLK3_CLK				74
#define CAM_CC_MCLK3_CLK_SRC				75
#define CAM_CC_SFE_LITE_0_CLK				76
#define CAM_CC_SFE_LITE_0_FAST_AHB_CLK			77
#define CAM_CC_SFE_LITE_1_CLK				78
#define CAM_CC_SFE_LITE_1_FAST_AHB_CLK			79
#define CAM_CC_SLEEP_CLK				80
#define CAM_CC_SLEEP_CLK_SRC				81
#define CAM_CC_SLOW_AHB_CLK_SRC				82
#define CAM_CC_XO_CLK_SRC				83
#define CAM_CC_TITAN_TOP_ACCU_SHIFT_CLK			84

/* CAM_CC resets */
#define CAM_CC_ICP_BCR					0
#define CAM_CC_IFE_0_BCR				1
#define CAM_CC_IFE_1_BCR				2
#define CAM_CC_IPE_0_BCR				3
#define CAM_CC_SFE_LITE_0_BCR				4
#define CAM_CC_SFE_LITE_1_BCR				5

#endif

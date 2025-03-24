/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_SM8150_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_SM8150_H

/* CAM_CC clocks */
#define CAM_CC_PLL0					0
#define CAM_CC_PLL0_OUT_EVEN				1
#define CAM_CC_PLL0_OUT_ODD				2
#define CAM_CC_PLL1					3
#define CAM_CC_PLL1_OUT_EVEN				4
#define CAM_CC_PLL2					5
#define CAM_CC_PLL2_OUT_MAIN				6
#define CAM_CC_PLL3					7
#define CAM_CC_PLL3_OUT_EVEN				8
#define CAM_CC_PLL4					9
#define CAM_CC_PLL4_OUT_EVEN				10
#define CAM_CC_BPS_AHB_CLK				11
#define CAM_CC_BPS_AREG_CLK				12
#define CAM_CC_BPS_AXI_CLK				13
#define CAM_CC_BPS_CLK					14
#define CAM_CC_BPS_CLK_SRC				15
#define CAM_CC_CAMNOC_AXI_CLK				16
#define CAM_CC_CAMNOC_AXI_CLK_SRC			17
#define CAM_CC_CAMNOC_DCD_XO_CLK			18
#define CAM_CC_CCI_0_CLK				19
#define CAM_CC_CCI_0_CLK_SRC				20
#define CAM_CC_CCI_1_CLK				21
#define CAM_CC_CCI_1_CLK_SRC				22
#define CAM_CC_CORE_AHB_CLK				23
#define CAM_CC_CPAS_AHB_CLK				24
#define CAM_CC_CPHY_RX_CLK_SRC				25
#define CAM_CC_CSI0PHYTIMER_CLK				26
#define CAM_CC_CSI0PHYTIMER_CLK_SRC			27
#define CAM_CC_CSI1PHYTIMER_CLK				28
#define CAM_CC_CSI1PHYTIMER_CLK_SRC			29
#define CAM_CC_CSI2PHYTIMER_CLK				30
#define CAM_CC_CSI2PHYTIMER_CLK_SRC			31
#define CAM_CC_CSI3PHYTIMER_CLK				32
#define CAM_CC_CSI3PHYTIMER_CLK_SRC			33
#define CAM_CC_CSIPHY0_CLK				34
#define CAM_CC_CSIPHY1_CLK				35
#define CAM_CC_CSIPHY2_CLK				36
#define CAM_CC_CSIPHY3_CLK				37
#define CAM_CC_FAST_AHB_CLK_SRC				38
#define CAM_CC_FD_CORE_CLK				39
#define CAM_CC_FD_CORE_CLK_SRC				40
#define CAM_CC_FD_CORE_UAR_CLK				41
#define CAM_CC_GDSC_CLK					42
#define CAM_CC_ICP_AHB_CLK				43
#define CAM_CC_ICP_CLK					44
#define CAM_CC_ICP_CLK_SRC				45
#define CAM_CC_IFE_0_AXI_CLK				46
#define CAM_CC_IFE_0_CLK				47
#define CAM_CC_IFE_0_CLK_SRC				48
#define CAM_CC_IFE_0_CPHY_RX_CLK			49
#define CAM_CC_IFE_0_CSID_CLK				50
#define CAM_CC_IFE_0_CSID_CLK_SRC			51
#define CAM_CC_IFE_0_DSP_CLK				52
#define CAM_CC_IFE_1_AXI_CLK				53
#define CAM_CC_IFE_1_CLK				54
#define CAM_CC_IFE_1_CLK_SRC				55
#define CAM_CC_IFE_1_CPHY_RX_CLK			56
#define CAM_CC_IFE_1_CSID_CLK				57
#define CAM_CC_IFE_1_CSID_CLK_SRC			58
#define CAM_CC_IFE_1_DSP_CLK				59
#define CAM_CC_IFE_LITE_0_CLK				60
#define CAM_CC_IFE_LITE_0_CLK_SRC			61
#define CAM_CC_IFE_LITE_0_CPHY_RX_CLK			62
#define CAM_CC_IFE_LITE_0_CSID_CLK			63
#define CAM_CC_IFE_LITE_0_CSID_CLK_SRC			64
#define CAM_CC_IFE_LITE_1_CLK				65
#define CAM_CC_IFE_LITE_1_CLK_SRC			66
#define CAM_CC_IFE_LITE_1_CPHY_RX_CLK			67
#define CAM_CC_IFE_LITE_1_CSID_CLK			68
#define CAM_CC_IFE_LITE_1_CSID_CLK_SRC			69
#define CAM_CC_IPE_0_AHB_CLK				70
#define CAM_CC_IPE_0_AREG_CLK				71
#define CAM_CC_IPE_0_AXI_CLK				72
#define CAM_CC_IPE_0_CLK				73
#define CAM_CC_IPE_0_CLK_SRC				74
#define CAM_CC_IPE_1_AHB_CLK				75
#define CAM_CC_IPE_1_AREG_CLK				76
#define CAM_CC_IPE_1_AXI_CLK				77
#define CAM_CC_IPE_1_CLK				78
#define CAM_CC_JPEG_CLK					79
#define CAM_CC_JPEG_CLK_SRC				80
#define CAM_CC_LRME_CLK					81
#define CAM_CC_LRME_CLK_SRC				82
#define CAM_CC_MCLK0_CLK				83
#define CAM_CC_MCLK0_CLK_SRC				84
#define CAM_CC_MCLK1_CLK				85
#define CAM_CC_MCLK1_CLK_SRC				86
#define CAM_CC_MCLK2_CLK				87
#define CAM_CC_MCLK2_CLK_SRC				88
#define CAM_CC_MCLK3_CLK				89
#define CAM_CC_MCLK3_CLK_SRC				90
#define CAM_CC_SLOW_AHB_CLK_SRC				91

/* CAM_CC power domains */
#define TITAN_TOP_GDSC					0
#define BPS_GDSC					1
#define IFE_0_GDSC					2
#define IFE_1_GDSC					3
#define IPE_0_GDSC					4
#define IPE_1_GDSC					5

/* CAM_CC resets */
#define CAM_CC_BPS_BCR					0
#define CAM_CC_CAMNOC_BCR				1
#define CAM_CC_CCI_BCR					2
#define CAM_CC_CPAS_BCR					3
#define CAM_CC_CSI0PHY_BCR				4
#define CAM_CC_CSI1PHY_BCR				5
#define CAM_CC_CSI2PHY_BCR				6
#define CAM_CC_CSI3PHY_BCR				7
#define CAM_CC_FD_BCR					8
#define CAM_CC_ICP_BCR					9
#define CAM_CC_IFE_0_BCR				10
#define CAM_CC_IFE_1_BCR				11
#define CAM_CC_IFE_LITE_0_BCR				12
#define CAM_CC_IFE_LITE_1_BCR				13
#define CAM_CC_IPE_0_BCR				14
#define CAM_CC_IPE_1_BCR				15
#define CAM_CC_JPEG_BCR					16
#define CAM_CC_LRME_BCR					17
#define CAM_CC_MCLK0_BCR				18
#define CAM_CC_MCLK1_BCR				19
#define CAM_CC_MCLK2_BCR				20
#define CAM_CC_MCLK3_BCR				21

#endif

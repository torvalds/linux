// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_SDM_CAM_CC_SDM845_H
#define _DT_BINDINGS_CLK_SDM_CAM_CC_SDM845_H

/* CAM_CC clock registers */
#define CAM_CC_BPS_AHB_CLK				0
#define CAM_CC_BPS_AREG_CLK				1
#define CAM_CC_BPS_AXI_CLK				2
#define CAM_CC_BPS_CLK					3
#define CAM_CC_BPS_CLK_SRC				4
#define CAM_CC_CAMNOC_ATB_CLK				5
#define CAM_CC_CAMNOC_AXI_CLK				6
#define CAM_CC_CCI_CLK					7
#define CAM_CC_CCI_CLK_SRC				8
#define CAM_CC_CPAS_AHB_CLK				9
#define CAM_CC_CPHY_RX_CLK_SRC				10
#define CAM_CC_CSI0PHYTIMER_CLK				11
#define CAM_CC_CSI0PHYTIMER_CLK_SRC			12
#define CAM_CC_CSI1PHYTIMER_CLK				13
#define CAM_CC_CSI1PHYTIMER_CLK_SRC			14
#define CAM_CC_CSI2PHYTIMER_CLK				15
#define CAM_CC_CSI2PHYTIMER_CLK_SRC			16
#define CAM_CC_CSI3PHYTIMER_CLK				17
#define CAM_CC_CSI3PHYTIMER_CLK_SRC			18
#define CAM_CC_CSIPHY0_CLK				19
#define CAM_CC_CSIPHY1_CLK				20
#define CAM_CC_CSIPHY2_CLK				21
#define CAM_CC_CSIPHY3_CLK				22
#define CAM_CC_FAST_AHB_CLK_SRC				23
#define CAM_CC_FD_CORE_CLK				24
#define CAM_CC_FD_CORE_CLK_SRC				25
#define CAM_CC_FD_CORE_UAR_CLK				26
#define CAM_CC_ICP_APB_CLK				27
#define CAM_CC_ICP_ATB_CLK				28
#define CAM_CC_ICP_CLK					29
#define CAM_CC_ICP_CLK_SRC				30
#define CAM_CC_ICP_CTI_CLK				31
#define CAM_CC_ICP_TS_CLK				32
#define CAM_CC_IFE_0_AXI_CLK				33
#define CAM_CC_IFE_0_CLK				34
#define CAM_CC_IFE_0_CLK_SRC				35
#define CAM_CC_IFE_0_CPHY_RX_CLK			36
#define CAM_CC_IFE_0_CSID_CLK				37
#define CAM_CC_IFE_0_CSID_CLK_SRC			38
#define CAM_CC_IFE_0_DSP_CLK				39
#define CAM_CC_IFE_1_AXI_CLK				40
#define CAM_CC_IFE_1_CLK				41
#define CAM_CC_IFE_1_CLK_SRC				42
#define CAM_CC_IFE_1_CPHY_RX_CLK			43
#define CAM_CC_IFE_1_CSID_CLK				44
#define CAM_CC_IFE_1_CSID_CLK_SRC			45
#define CAM_CC_IFE_1_DSP_CLK				46
#define CAM_CC_IFE_LITE_CLK				47
#define CAM_CC_IFE_LITE_CLK_SRC				48
#define CAM_CC_IFE_LITE_CPHY_RX_CLK			49
#define CAM_CC_IFE_LITE_CSID_CLK			50
#define CAM_CC_IFE_LITE_CSID_CLK_SRC			51
#define CAM_CC_IPE_0_AHB_CLK				52
#define CAM_CC_IPE_0_AREG_CLK				53
#define CAM_CC_IPE_0_AXI_CLK				54
#define CAM_CC_IPE_0_CLK				55
#define CAM_CC_IPE_0_CLK_SRC				56
#define CAM_CC_IPE_1_AHB_CLK				57
#define CAM_CC_IPE_1_AREG_CLK				58
#define CAM_CC_IPE_1_AXI_CLK				59
#define CAM_CC_IPE_1_CLK				60
#define CAM_CC_IPE_1_CLK_SRC				61
#define CAM_CC_JPEG_CLK					62
#define CAM_CC_JPEG_CLK_SRC				63
#define CAM_CC_LRME_CLK					64
#define CAM_CC_LRME_CLK_SRC				65
#define CAM_CC_MCLK0_CLK				66
#define CAM_CC_MCLK0_CLK_SRC				67
#define CAM_CC_MCLK1_CLK				68
#define CAM_CC_MCLK1_CLK_SRC				69
#define CAM_CC_MCLK2_CLK				70
#define CAM_CC_MCLK2_CLK_SRC				71
#define CAM_CC_MCLK3_CLK				72
#define CAM_CC_MCLK3_CLK_SRC				73
#define CAM_CC_PLL0					74
#define CAM_CC_PLL0_OUT_EVEN				75
#define CAM_CC_PLL1					76
#define CAM_CC_PLL1_OUT_EVEN				77
#define CAM_CC_PLL2					78
#define CAM_CC_PLL2_OUT_EVEN				79
#define CAM_CC_PLL3					80
#define CAM_CC_PLL3_OUT_EVEN				81
#define CAM_CC_SLOW_AHB_CLK_SRC				82
#define CAM_CC_SOC_AHB_CLK				83
#define CAM_CC_SYS_TMR_CLK				84

/* CAM_CC Resets */
#define TITAN_CAM_CC_CCI_BCR				0
#define TITAN_CAM_CC_CPAS_BCR				1
#define TITAN_CAM_CC_CSI0PHY_BCR			2
#define TITAN_CAM_CC_CSI1PHY_BCR			3
#define TITAN_CAM_CC_CSI2PHY_BCR			4
#define TITAN_CAM_CC_MCLK0_BCR				5
#define TITAN_CAM_CC_MCLK1_BCR				6
#define TITAN_CAM_CC_MCLK2_BCR				7
#define TITAN_CAM_CC_MCLK3_BCR				8
#define TITAN_CAM_CC_TITAN_TOP_BCR			9

/* CAM_CC GDSCRs */
#define BPS_GDSC					0
#define IPE_0_GDSC					1
#define IPE_1_GDSC					2
#define IFE_0_GDSC					3
#define IFE_1_GDSC					4
#define TITAN_TOP_GDSC					5

#endif

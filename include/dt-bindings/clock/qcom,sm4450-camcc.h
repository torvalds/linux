/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_SM4450_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_SM4450_H

/* CAM_CC clocks */
#define CAM_CC_BPS_AHB_CLK					0
#define CAM_CC_BPS_AREG_CLK					1
#define CAM_CC_BPS_CLK						2
#define CAM_CC_BPS_CLK_SRC					3
#define CAM_CC_CAMNOC_ATB_CLK					4
#define CAM_CC_CAMNOC_AXI_CLK					5
#define CAM_CC_CAMNOC_AXI_CLK_SRC				6
#define CAM_CC_CAMNOC_AXI_HF_CLK				7
#define CAM_CC_CAMNOC_AXI_SF_CLK				8
#define CAM_CC_CCI_0_CLK					9
#define CAM_CC_CCI_0_CLK_SRC					10
#define CAM_CC_CCI_1_CLK					11
#define CAM_CC_CCI_1_CLK_SRC					12
#define CAM_CC_CORE_AHB_CLK					13
#define CAM_CC_CPAS_AHB_CLK					14
#define CAM_CC_CPHY_RX_CLK_SRC					15
#define CAM_CC_CRE_AHB_CLK					16
#define CAM_CC_CRE_CLK						17
#define CAM_CC_CRE_CLK_SRC					18
#define CAM_CC_CSI0PHYTIMER_CLK					19
#define CAM_CC_CSI0PHYTIMER_CLK_SRC				20
#define CAM_CC_CSI1PHYTIMER_CLK					21
#define CAM_CC_CSI1PHYTIMER_CLK_SRC				22
#define CAM_CC_CSI2PHYTIMER_CLK					23
#define CAM_CC_CSI2PHYTIMER_CLK_SRC				24
#define CAM_CC_CSIPHY0_CLK					25
#define CAM_CC_CSIPHY1_CLK					26
#define CAM_CC_CSIPHY2_CLK					27
#define CAM_CC_FAST_AHB_CLK_SRC					28
#define CAM_CC_ICP_ATB_CLK					29
#define CAM_CC_ICP_CLK						30
#define CAM_CC_ICP_CLK_SRC					31
#define CAM_CC_ICP_CTI_CLK					32
#define CAM_CC_ICP_TS_CLK					33
#define CAM_CC_MCLK0_CLK					34
#define CAM_CC_MCLK0_CLK_SRC					35
#define CAM_CC_MCLK1_CLK					36
#define CAM_CC_MCLK1_CLK_SRC					37
#define CAM_CC_MCLK2_CLK					38
#define CAM_CC_MCLK2_CLK_SRC					39
#define CAM_CC_MCLK3_CLK					40
#define CAM_CC_MCLK3_CLK_SRC					41
#define CAM_CC_OPE_0_AHB_CLK					42
#define CAM_CC_OPE_0_AREG_CLK					43
#define CAM_CC_OPE_0_CLK					44
#define CAM_CC_OPE_0_CLK_SRC					45
#define CAM_CC_PLL0						46
#define CAM_CC_PLL0_OUT_EVEN					47
#define CAM_CC_PLL0_OUT_ODD					48
#define CAM_CC_PLL1						49
#define CAM_CC_PLL1_OUT_EVEN					50
#define CAM_CC_PLL2						51
#define CAM_CC_PLL2_OUT_EVEN					52
#define CAM_CC_PLL3						53
#define CAM_CC_PLL3_OUT_EVEN					54
#define CAM_CC_PLL4						55
#define CAM_CC_PLL4_OUT_EVEN					56
#define CAM_CC_SLOW_AHB_CLK_SRC					57
#define CAM_CC_SOC_AHB_CLK					58
#define CAM_CC_SYS_TMR_CLK					59
#define CAM_CC_TFE_0_AHB_CLK					60
#define CAM_CC_TFE_0_CLK					61
#define CAM_CC_TFE_0_CLK_SRC					62
#define CAM_CC_TFE_0_CPHY_RX_CLK				63
#define CAM_CC_TFE_0_CSID_CLK					64
#define CAM_CC_TFE_0_CSID_CLK_SRC				65
#define CAM_CC_TFE_1_AHB_CLK					66
#define CAM_CC_TFE_1_CLK					67
#define CAM_CC_TFE_1_CLK_SRC					68
#define CAM_CC_TFE_1_CPHY_RX_CLK				69
#define CAM_CC_TFE_1_CSID_CLK					70
#define CAM_CC_TFE_1_CSID_CLK_SRC				71

/* CAM_CC power domains */
#define CAM_CC_CAMSS_TOP_GDSC					0

/* CAM_CC resets */
#define CAM_CC_BPS_BCR						0
#define CAM_CC_CAMNOC_BCR					1
#define CAM_CC_CAMSS_TOP_BCR					2
#define CAM_CC_CCI_0_BCR					3
#define CAM_CC_CCI_1_BCR					4
#define CAM_CC_CPAS_BCR						5
#define CAM_CC_CRE_BCR						6
#define CAM_CC_CSI0PHY_BCR					7
#define CAM_CC_CSI1PHY_BCR					8
#define CAM_CC_CSI2PHY_BCR					9
#define CAM_CC_ICP_BCR						10
#define CAM_CC_MCLK0_BCR					11
#define CAM_CC_MCLK1_BCR					12
#define CAM_CC_MCLK2_BCR					13
#define CAM_CC_MCLK3_BCR					14
#define CAM_CC_OPE_0_BCR					15
#define CAM_CC_TFE_0_BCR					16
#define CAM_CC_TFE_1_BCR					17

#endif

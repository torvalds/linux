/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_X1E80100_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_X1E80100_H

/* CAM_CC clocks */
#define CAM_CC_BPS_AHB_CLK					0
#define CAM_CC_BPS_CLK						1
#define CAM_CC_BPS_CLK_SRC					2
#define CAM_CC_BPS_FAST_AHB_CLK					3
#define CAM_CC_CAMNOC_AXI_NRT_CLK				4
#define CAM_CC_CAMNOC_AXI_RT_CLK				5
#define CAM_CC_CAMNOC_AXI_RT_CLK_SRC				6
#define CAM_CC_CAMNOC_DCD_XO_CLK				7
#define CAM_CC_CAMNOC_XO_CLK					8
#define CAM_CC_CCI_0_CLK					9
#define CAM_CC_CCI_0_CLK_SRC					10
#define CAM_CC_CCI_1_CLK					11
#define CAM_CC_CCI_1_CLK_SRC					12
#define CAM_CC_CORE_AHB_CLK					13
#define CAM_CC_CPAS_AHB_CLK					14
#define CAM_CC_CPAS_BPS_CLK					15
#define CAM_CC_CPAS_FAST_AHB_CLK				16
#define CAM_CC_CPAS_IFE_0_CLK					17
#define CAM_CC_CPAS_IFE_1_CLK					18
#define CAM_CC_CPAS_IFE_LITE_CLK				19
#define CAM_CC_CPAS_IPE_NPS_CLK					20
#define CAM_CC_CPAS_SFE_0_CLK					21
#define CAM_CC_CPHY_RX_CLK_SRC					22
#define CAM_CC_CSI0PHYTIMER_CLK					23
#define CAM_CC_CSI0PHYTIMER_CLK_SRC				24
#define CAM_CC_CSI1PHYTIMER_CLK					25
#define CAM_CC_CSI1PHYTIMER_CLK_SRC				26
#define CAM_CC_CSI2PHYTIMER_CLK					27
#define CAM_CC_CSI2PHYTIMER_CLK_SRC				28
#define CAM_CC_CSI3PHYTIMER_CLK					29
#define CAM_CC_CSI3PHYTIMER_CLK_SRC				30
#define CAM_CC_CSI4PHYTIMER_CLK					31
#define CAM_CC_CSI4PHYTIMER_CLK_SRC				32
#define CAM_CC_CSI5PHYTIMER_CLK					33
#define CAM_CC_CSI5PHYTIMER_CLK_SRC				34
#define CAM_CC_CSID_CLK						35
#define CAM_CC_CSID_CLK_SRC					36
#define CAM_CC_CSID_CSIPHY_RX_CLK				37
#define CAM_CC_CSIPHY0_CLK					38
#define CAM_CC_CSIPHY1_CLK					39
#define CAM_CC_CSIPHY2_CLK					40
#define CAM_CC_CSIPHY3_CLK					41
#define CAM_CC_CSIPHY4_CLK					42
#define CAM_CC_CSIPHY5_CLK					43
#define CAM_CC_FAST_AHB_CLK_SRC					44
#define CAM_CC_GDSC_CLK						45
#define CAM_CC_ICP_AHB_CLK					46
#define CAM_CC_ICP_CLK						47
#define CAM_CC_ICP_CLK_SRC					48
#define CAM_CC_IFE_0_CLK					49
#define CAM_CC_IFE_0_CLK_SRC					50
#define CAM_CC_IFE_0_DSP_CLK					51
#define CAM_CC_IFE_0_FAST_AHB_CLK				52
#define CAM_CC_IFE_1_CLK					53
#define CAM_CC_IFE_1_CLK_SRC					54
#define CAM_CC_IFE_1_DSP_CLK					55
#define CAM_CC_IFE_1_FAST_AHB_CLK				56
#define CAM_CC_IFE_LITE_AHB_CLK					57
#define CAM_CC_IFE_LITE_CLK					58
#define CAM_CC_IFE_LITE_CLK_SRC					59
#define CAM_CC_IFE_LITE_CPHY_RX_CLK				60
#define CAM_CC_IFE_LITE_CSID_CLK				61
#define CAM_CC_IFE_LITE_CSID_CLK_SRC				62
#define CAM_CC_IPE_NPS_AHB_CLK					63
#define CAM_CC_IPE_NPS_CLK					64
#define CAM_CC_IPE_NPS_CLK_SRC					65
#define CAM_CC_IPE_NPS_FAST_AHB_CLK				66
#define CAM_CC_IPE_PPS_CLK					67
#define CAM_CC_IPE_PPS_FAST_AHB_CLK				68
#define CAM_CC_JPEG_CLK						69
#define CAM_CC_JPEG_CLK_SRC					70
#define CAM_CC_MCLK0_CLK					71
#define CAM_CC_MCLK0_CLK_SRC					72
#define CAM_CC_MCLK1_CLK					73
#define CAM_CC_MCLK1_CLK_SRC					74
#define CAM_CC_MCLK2_CLK					75
#define CAM_CC_MCLK2_CLK_SRC					76
#define CAM_CC_MCLK3_CLK					77
#define CAM_CC_MCLK3_CLK_SRC					78
#define CAM_CC_MCLK4_CLK					79
#define CAM_CC_MCLK4_CLK_SRC					80
#define CAM_CC_MCLK5_CLK					81
#define CAM_CC_MCLK5_CLK_SRC					82
#define CAM_CC_MCLK6_CLK					83
#define CAM_CC_MCLK6_CLK_SRC					84
#define CAM_CC_MCLK7_CLK					85
#define CAM_CC_MCLK7_CLK_SRC					86
#define CAM_CC_PLL0						87
#define CAM_CC_PLL0_OUT_EVEN					88
#define CAM_CC_PLL0_OUT_ODD					89
#define CAM_CC_PLL1						90
#define CAM_CC_PLL1_OUT_EVEN					91
#define CAM_CC_PLL2						92
#define CAM_CC_PLL3						93
#define CAM_CC_PLL3_OUT_EVEN					94
#define CAM_CC_PLL4						95
#define CAM_CC_PLL4_OUT_EVEN					96
#define CAM_CC_PLL6						97
#define CAM_CC_PLL6_OUT_EVEN					98
#define CAM_CC_PLL8						99
#define CAM_CC_PLL8_OUT_EVEN					100
#define CAM_CC_SFE_0_CLK					101
#define CAM_CC_SFE_0_CLK_SRC					102
#define CAM_CC_SFE_0_FAST_AHB_CLK				103
#define CAM_CC_SLEEP_CLK					104
#define CAM_CC_SLEEP_CLK_SRC					105
#define CAM_CC_SLOW_AHB_CLK_SRC					106
#define CAM_CC_XO_CLK_SRC					107

/* CAM_CC power domains */
#define CAM_CC_BPS_GDSC						0
#define CAM_CC_IFE_0_GDSC					1
#define CAM_CC_IFE_1_GDSC					2
#define CAM_CC_IPE_0_GDSC					3
#define CAM_CC_SFE_0_GDSC					4
#define CAM_CC_TITAN_TOP_GDSC					5

/* CAM_CC resets */
#define CAM_CC_BPS_BCR						0
#define CAM_CC_ICP_BCR						1
#define CAM_CC_IFE_0_BCR					2
#define CAM_CC_IFE_1_BCR					3
#define CAM_CC_IPE_0_BCR					4
#define CAM_CC_SFE_0_BCR					5

#endif
